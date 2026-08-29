#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <mutex>
#include <ctime>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// ---------- Data model ----------

struct Message {
    long timestamp;
    std::string category;
    std::string name;
    std::string text;
};

std::mutex fileMutex;
const std::string DATA_FILE = "messages.log";

// ---------- Small helper functions ----------

// Converts "%20" -> " " and "+" -> " " (standard URL/form encoding rules)
std::string urlDecode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int value;
            std::istringstream iss(s.substr(i + 1, 2));
            if (iss >> std::hex >> value) {
                result += static_cast<char>(value);
                i += 2;
            } else {
                result += s[i];
            }
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

// Prevents posted messages from injecting raw HTML/scripts into the page
std::string htmlEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Our storage format uses "|" as a separator, so we escape any "|" or
// newline characters inside the actual message text before saving.
std::string escapeForStorage(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\p";
        else if (c == '\n') out += "\\n";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
}

std::string unescapeFromStorage(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == 'p') { out += '|'; i++; }
            else if (next == 'n') { out += '\n'; i++; }
            else if (next == '\\') { out += '\\'; i++; }
            else out += s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string timeAgo(long timestamp) {
    long now = std::time(nullptr);
    long diff = now - timestamp;
    if (diff < 60) return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    return std::to_string(diff / 86400) + "d ago";
}

// ---------- Storage layer ----------

std::vector<Message> loadMessages() {
    std::vector<Message> messages;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream file(DATA_FILE);
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::string current;
        for (char c : line) {
            if (c == '|') { parts.push_back(current); current.clear(); }
            else current += c;
        }
        parts.push_back(current);
        if (parts.size() != 4) continue; // skip malformed/corrupted lines

        Message m;
        try { m.timestamp = std::stol(parts[0]); } catch (...) { continue; }
        m.category = parts[1];
        m.name = unescapeFromStorage(parts[2]);
        m.text = unescapeFromStorage(parts[3]);
        messages.push_back(m);
    }
    return messages;
}

void saveMessage(const Message& m) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream file(DATA_FILE, std::ios::app);
    file << m.timestamp << "|" << m.category << "|"
         << escapeForStorage(m.name) << "|" << escapeForStorage(m.text) << "\n";
}

// ---------- HTTP request parsing ----------

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::string readRawRequest(int clientSocket) {
    std::string raw;
    char buffer[4096];
    size_t headerEnd;

    // Keep reading until we've received the full header block
    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (n <= 0) return raw;
        raw.append(buffer, n);
        if (raw.size() > (1 << 20)) break; // safety cap
    }

    // Check for Content-Length to know if/how much body to expect
    size_t contentLength = 0;
    size_t clPos = raw.find("Content-Length:");
    if (clPos != std::string::npos && clPos < headerEnd) {
        size_t start = clPos + 16;
        size_t end = raw.find("\r\n", start);
        try { contentLength = std::stoul(raw.substr(start, end - start)); }
        catch (...) { contentLength = 0; }
    }

    size_t bodyStart = headerEnd + 4;
    size_t bodySoFar = raw.size() - bodyStart;
    while (bodySoFar < contentLength) {
        ssize_t n = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        raw.append(buffer, n);
        bodySoFar += n;
    }
    return raw;
}

HttpRequest parseRequest(const std::string& raw) {
    HttpRequest req;
    size_t headerEnd = raw.find("\r\n\r\n");
    std::string headerSection = (headerEnd == std::string::npos) ? raw : raw.substr(0, headerEnd);
    req.body = (headerEnd == std::string::npos) ? "" : raw.substr(headerEnd + 4);

    std::istringstream stream(headerSection);
    std::string line;

    if (std::getline(stream, line)) {
        std::istringstream lineStream(line);
        std::string version;
        lineStream >> req.method >> req.path >> version;
    }

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        req.headers[key] = value;
    }
    return req;
}

std::unordered_map<std::string, std::string> parseFormBody(const std::string& body) {
    std::unordered_map<std::string, std::string> fields;
    std::istringstream iss(body);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        size_t eq = pair.find('=');
        if (eq == std::string::npos) continue;
        fields[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    return fields;
}

// ---------- Response building ----------

void sendResponse(int clientSocket, int statusCode, const std::string& statusText,
                   const std::string& contentType, const std::string& body,
                   const std::string& extraHeaders = "") {
    std::string response =
        "HTTP/1.1 " + std::to_string(statusCode) + " " + statusText + "\r\n" +
        "Content-Type: " + contentType + "\r\n" +
        "Content-Length: " + std::to_string(body.size()) + "\r\n" +
        extraHeaders +
        "\r\n" + body;
    send(clientSocket, response.c_str(), response.size(), 0);
}

std::string buildHtmlPage() {
    std::vector<Message> messages = loadMessages();
    std::sort(messages.begin(), messages.end(), [](const Message& a, const Message& b) {
        return a.timestamp > b.timestamp;
    });

    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
         << "<meta http-equiv='refresh' content='15'>"
         << "<title>CampusLink</title><style>"
         << "body{font-family:sans-serif;max-width:600px;margin:40px auto;padding:0 16px;background:#f7f7f7;}"
         << "h1{color:#222;} form{background:#fff;padding:16px;border-radius:8px;margin-bottom:24px;box-shadow:0 1px 3px rgba(0,0,0,.1);}"
         << "input,select,textarea{width:100%;padding:8px;margin:6px 0;box-sizing:border-box;}"
         << "button{background:#2563eb;color:#fff;border:none;padding:10px 16px;border-radius:6px;cursor:pointer;}"
         << ".msg{background:#fff;padding:12px;border-radius:8px;margin-bottom:10px;box-shadow:0 1px 3px rgba(0,0,0,.1);}"
         << ".tag{display:inline-block;font-size:12px;padding:2px 8px;border-radius:10px;background:#e5e7eb;margin-right:8px;}"
         << ".time{color:#888;font-size:12px;}"
         << "</style></head><body><h1>CampusLink</h1>"
         << "<form method='POST' action='/post'>"
         << "<input type='text' name='name' placeholder='Your name (optional)' maxlength='40'>"
         << "<select name='category'>"
         << "<option value='general'>General</option>"
         << "<option value='notice'>Notice</option>"
         << "<option value='doubt'>Doubt</option>"
         << "</select>"
         << "<textarea name='message' placeholder='Type your message...' maxlength='500' rows='3' required></textarea>"
         << "<button type='submit'>Post</button></form>";

    if (messages.empty()) {
        html << "<p>No messages yet. Be the first to post!</p>";
    }
    for (const Message& m : messages) {
        std::string name = m.name.empty() ? "Anonymous" : htmlEscape(m.name);
        html << "<div class='msg'><span class='tag'>" << htmlEscape(m.category) << "</span>"
             << "<strong>" << name << "</strong> "
             << "<span class='time'>" << timeAgo(m.timestamp) << "</span>"
             << "<p>" << htmlEscape(m.text) << "</p></div>";
    }
    html << "</body></html>";
    return html.str();
}

// ---------- Request handling / routing ----------

void handleClient(int clientSocket) {
    std::string raw = readRawRequest(clientSocket);
    if (raw.empty()) { close(clientSocket); return; }

    HttpRequest req = parseRequest(raw);

    if (req.method == "GET" && req.path == "/") {
        sendResponse(clientSocket, 200, "OK", "text/html", buildHtmlPage());
    }
    else if (req.method == "POST" && req.path == "/post") {
        auto fields = parseFormBody(req.body);
        std::string name = fields.count("name") ? fields["name"] : "";
        std::string category = fields.count("category") ? fields["category"] : "general";
        std::string text = fields.count("message") ? fields["message"] : "";

        if (text.size() > 500) text = text.substr(0, 500);
        if (!text.empty()) {
            Message m{ std::time(nullptr), category, name, text };
            saveMessage(m);
        }
        sendResponse(clientSocket, 303, "See Other", "text/plain", "", "Location: /\r\n");
    }
    else {
        sendResponse(clientSocket, 404, "Not Found", "text/html", "<h1>404 Not Found</h1>");
    }
    close(clientSocket);
}

// ---------- Server startup ----------

int main() {
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) { std::cerr << "Failed to create socket\n"; return 1; }

    int opt = 1;
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed\n"; return 1;
    }
    if (listen(serverSocket, 10) < 0) {
        std::cerr << "Listen failed\n"; return 1;
    }

    std::cout << "CampusLink running at http://localhost:8080\n";

    while (true) {
        int clientSocket = accept(serverSocket, nullptr, nullptr);
        if (clientSocket < 0) { std::cerr << "Accept failed\n"; continue; }
        std::thread(handleClient, clientSocket).detach();
    }

    close(serverSocket);
    return 0;
}