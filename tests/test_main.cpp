#include <iostream>
#include <string>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int passCount = 0;
int failCount = 0;

void check(bool condition, const std::string& testName) {
    if (condition) {
        std::cout << "PASS: " << testName << "\n";
        passCount++;
    } else {
        std::cout << "FAIL: " << testName << "\n";
        failCount++;
    }
}

// Connects to the running server, sends a raw HTTP request, returns the raw response
std::string sendRequest(const std::string& request) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return "";
    }

    send(sock, request.c_str(), request.size(), 0);

    std::string response;
    char buffer[4096];
    ssize_t n;
    while ((n = recv(sock, buffer, sizeof(buffer), 0)) > 0) {
        response.append(buffer, n);
    }
    close(sock);
    return response;
}

int main() {
    std::cout << "Running CampusLink tests against http://localhost:8080\n";
    std::cout << "(Make sure the server is running in another terminal first.)\n\n";

    // Test 1: homepage loads correctly
    {
        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string res = sendRequest(req);
        check(res.find("200 OK") != std::string::npos, "GET / returns 200 OK");
        check(res.find("CampusLink") != std::string::npos, "GET / contains 'CampusLink'");
    }

    // Test 2: posting a message works and redirects
    {
        std::string body = "name=TestBot&category=general&message=Automated+test+message+12345";
        std::ostringstream req;
        req << "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            << "Content-Type: application/x-www-form-urlencoded\r\n"
            << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
        std::string res = sendRequest(req.str());
        check(res.find("303") != std::string::npos, "POST /post returns 303 redirect");
    }

    // Test 3: the posted message actually shows up afterward
    {
        std::string req = "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string res = sendRequest(req);
        check(res.find("Automated test message 12345") != std::string::npos,
              "Posted message appears on homepage");
    }

    // Test 4: empty message doesn't break anything
    {
        std::string body = "name=TestBot&category=general&message=";
        std::ostringstream req;
        req << "POST /post HTTP/1.1\r\nHost: localhost\r\n"
            << "Content-Type: application/x-www-form-urlencoded\r\n"
            << "Content-Length: " << body.size() << "\r\nConnection: close\r\n\r\n" << body;
        std::string res = sendRequest(req.str());
        check(res.find("303") != std::string::npos, "POST /post with empty message doesn't crash");
    }

    // Test 5: unknown routes return 404 instead of crashing
    {
        std::string req = "GET /doesnotexist HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        std::string res = sendRequest(req);
        check(res.find("404") != std::string::npos, "GET /doesnotexist returns 404");
    }

    std::cout << "\n" << passCount << " passed, " << failCount << " failed\n";
    return failCount == 0 ? 0 : 1;
}