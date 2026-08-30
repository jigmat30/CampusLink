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
#include <cstdlib>
#include <cctype>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

// ---------- Data model ----------

struct Message {
    std::string id;
    long timestamp;
    std::string category;
    std::string name;
    std::string clientId;
    std::string parentId;
    std::string text;
};

struct Poll {
    std::string id;
    long timestamp;
    std::string creatorName;
    std::string question;
    std::vector<std::string> options;
};

struct Vote {
    std::string pollId;
    std::string clientId;
    int optionIndex;
    std::string name;
};

struct Reaction {
    std::string messageId;
    std::string emoji;
    std::string clientId;
};

std::mutex fileMutex;
const std::string DATA_FILE = "messages.log";
const std::string POLLS_FILE = "polls.log";
const std::string VOTES_FILE = "votes.log";
const std::string REACTIONS_FILE = "reactions.log";

// ---------- Small helper functions ----------

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

std::string jsEscape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\'' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

std::string escapeForStorage(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '|') out += "\\p";
        else if (c == ';') out += "\\s";
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
            else if (next == 's') { out += ';'; i++; }
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

std::string generateId() {
    static bool seeded = false;
    if (!seeded) { srand(static_cast<unsigned>(time(nullptr)) ^ getpid()); seeded = true; }
    return std::to_string(std::time(nullptr)) + std::to_string(rand() % 1000000);
}

std::string getCookieValue(const std::string& cookieHeader, const std::string& key) {
    size_t pos = cookieHeader.find(key + "=");
    if (pos == std::string::npos) return "";
    size_t start = pos + key.size() + 1;
    size_t end = cookieHeader.find(';', start);
    size_t len = (end == std::string::npos) ? std::string::npos : end - start;
    return cookieHeader.substr(start, len);
}

std::string avatarColor(const std::string& name) {
    static const std::vector<std::string> palette = {
        "#ef4444", "#f97316", "#eab308", "#22c55e", "#06b6d4", "#3b82f6", "#8b5cf6", "#ec4899"
    };
    unsigned sum = 0;
    for (char c : name) sum += static_cast<unsigned char>(c);
    return palette[sum % palette.size()];
}

std::string initials(const std::string& name) {
    if (name.empty()) return "?";
    std::string result;
    result += static_cast<char>(toupper(name[0]));
    return result;
}

static const std::vector<std::string> REACTION_EMOJIS = {
    "\xF0\x9F\x91\x8D", "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x98\x82",
    "\xF0\x9F\x98\xAE", "\xF0\x9F\x98\xA2", "\xF0\x9F\x99\x8F", "\xF0\x9F\x94\xA5"
};

// ---------- Message storage ----------

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
        if (parts.size() != 7) continue;

        Message m;
        m.id = parts[0];
        try { m.timestamp = std::stol(parts[1]); } catch (...) { continue; }
        m.category = parts[2];
        m.name = unescapeFromStorage(parts[3]);
        m.clientId = parts[4];
        m.parentId = parts[5];
        m.text = unescapeFromStorage(parts[6]);
        messages.push_back(m);
    }
    return messages;
}

void saveMessage(const Message& m) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream file(DATA_FILE, std::ios::app);
    file << m.id << "|" << m.timestamp << "|" << m.category << "|"
         << escapeForStorage(m.name) << "|" << m.clientId << "|" << m.parentId << "|"
         << escapeForStorage(m.text) << "\n";
}

// Deletes a message, and cascades to delete any replies that pointed to it,
// so replies never become orphaned (invisible, but still on disk).
void deleteMessage(const std::string& id, const std::string& requesterClientId) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream in(DATA_FILE);
    std::vector<std::string> keepLines;
    std::string line;
    bool deleted = false;

    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::string current;
        for (char c : line) {
            if (c == '|') { parts.push_back(current); current.clear(); }
            else current += c;
        }
        parts.push_back(current);
        if (parts.size() != 7) { keepLines.push_back(line); continue; }

        bool isTarget = (parts[0] == id && parts[4] == requesterClientId);
        if (isTarget) { deleted = true; continue; }
        keepLines.push_back(line);
    }
    in.close();

    if (deleted) {
        std::vector<std::string> finalLines;
        for (auto& l : keepLines) {
            std::vector<std::string> parts;
            std::string current;
            for (char c : l) {
                if (c == '|') { parts.push_back(current); current.clear(); }
                else current += c;
            }
            parts.push_back(current);
            if (parts.size() == 7 && parts[5] == id) continue; // reply to the deleted message
            finalLines.push_back(l);
        }
        keepLines = finalLines;
    }

    std::ofstream out(DATA_FILE, std::ios::trunc);
    for (auto& l : keepLines) out << l << "\n";
}

// ---------- Poll storage ----------

std::vector<Poll> loadPolls() {
    std::vector<Poll> polls;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream file(POLLS_FILE);
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
        if (parts.size() != 5) continue;

        Poll p;
        p.id = parts[0];
        try { p.timestamp = std::stol(parts[1]); } catch (...) { continue; }
        p.creatorName = unescapeFromStorage(parts[2]);
        p.question = unescapeFromStorage(parts[3]);

        std::string optsRaw = parts[4];
        std::string cur;
        for (char c : optsRaw) {
            if (c == ';') { p.options.push_back(unescapeFromStorage(cur)); cur.clear(); }
            else cur += c;
        }
        p.options.push_back(unescapeFromStorage(cur));
        polls.push_back(p);
    }
    return polls;
}

void savePoll(const Poll& p) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream file(POLLS_FILE, std::ios::app);
    file << p.id << "|" << p.timestamp << "|" << escapeForStorage(p.creatorName) << "|"
         << escapeForStorage(p.question) << "|";
    for (size_t i = 0; i < p.options.size(); i++) {
        file << escapeForStorage(p.options[i]);
        if (i + 1 < p.options.size()) file << ";";
    }
    file << "\n";
}

std::vector<Vote> loadVotes() {
    std::vector<Vote> votes;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream file(VOTES_FILE);
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
        if (parts.size() != 4) continue;

        Vote v;
        v.pollId = parts[0];
        v.clientId = parts[1];
        try { v.optionIndex = std::stoi(parts[2]); } catch (...) { continue; }
        v.name = unescapeFromStorage(parts[3]);
        votes.push_back(v);
    }
    return votes;
}

// Atomically checks "has this person already voted?" and saves the vote if not,
// all under one lock -- avoids a race where two quick clicks both slip through.
bool tryAddVote(const std::string& pollId, const std::string& clientId, int optionIndex, const std::string& name) {
    std::lock_guard<std::mutex> lock(fileMutex);

    std::ifstream in(VOTES_FILE);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::string current;
        for (char c : line) {
            if (c == '|') { parts.push_back(current); current.clear(); }
            else current += c;
        }
        parts.push_back(current);
        if (parts.size() == 4 && parts[0] == pollId && parts[1] == clientId) {
            return false; // already voted
        }
    }
    in.close();

    std::ofstream out(VOTES_FILE, std::ios::app);
    out << pollId << "|" << clientId << "|" << optionIndex << "|" << escapeForStorage(name) << "\n";
    return true;
}

// ---------- Reaction storage ----------

std::vector<Reaction> loadReactions() {
    std::vector<Reaction> reactions;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream file(REACTIONS_FILE);
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
        if (parts.size() != 3) continue;
        Reaction r;
        r.messageId = parts[0];
        r.emoji = parts[1];
        r.clientId = parts[2];
        reactions.push_back(r);
    }
    return reactions;
}

void toggleReaction(const std::string& messageId, const std::string& emoji, const std::string& clientId) {
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ifstream in(REACTIONS_FILE);
    std::vector<std::string> keepLines;
    bool found = false;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::vector<std::string> parts;
        std::string current;
        for (char c : line) {
            if (c == '|') { parts.push_back(current); current.clear(); }
            else current += c;
        }
        parts.push_back(current);
        if (parts.size() == 3 && parts[0] == messageId && parts[1] == emoji && parts[2] == clientId) {
            found = true;
            continue;
        }
        keepLines.push_back(line);
    }
    in.close();

    std::ofstream out(REACTIONS_FILE, std::ios::trunc);
    for (auto& l : keepLines) out << l << "\n";
    if (!found) {
        out << messageId << "|" << emoji << "|" << clientId << "\n";
    }
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

    while ((headerEnd = raw.find("\r\n\r\n")) == std::string::npos) {
        ssize_t n = recv(clientSocket, buffer, sizeof(buffer), 0);
        if (n <= 0) return raw;
        raw.append(buffer, n);
        if (raw.size() > (1 << 20)) break;
    }

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

std::string renderMessageBubble(const Message& m, const std::string& requestClientId,
                                 bool isReply, const std::vector<Reaction>& allReactions) {
    bool mine = (m.clientId == requestClientId);
    std::string name = m.name.empty() ? "Guest" : htmlEscape(m.name);

    std::map<std::string, std::vector<std::string>> reactionMap;
    for (auto& r : allReactions) {
        if (r.messageId == m.id) reactionMap[r.emoji].push_back(r.clientId);
    }

    std::ostringstream html;
    html << "<div class='bubble-row " << (mine ? "mine" : "theirs") << (isReply ? " reply" : "") << "'>";
    if (!mine) {
        html << "<div class='avatar' style='background:" << avatarColor(m.name) << "'>" << initials(m.name) << "</div>";
    }
    html << "<div class='bubble'>"
         << "<div class='bubble-header'><strong>" << name << "</strong> "
         << "<span class='tag'>" << htmlEscape(m.category) << "</span> "
         << "<span class='time'>" << timeAgo(m.timestamp) << "</span></div>"
         << "<div class='bubble-text'>" << htmlEscape(m.text) << "</div>";

    html << "<div class='bubble-actions'>";
    if (!isReply) {
        html << "<span class='action-link' onclick=\"replyTo('" << m.id << "','"
             << htmlEscape(jsEscape(m.name.empty() ? "Guest" : m.name)) << "')\">Reply</span>";
    }
    if (mine) {
        html << " <form method='POST' action='/delete' style='display:inline' "
             << "onsubmit=\"return confirm('Delete this message?')\">"
             << "<input type='hidden' name='id' value='" << m.id << "'>"
             << "<button type='submit' class='del-btn'>Delete</button></form>";
    }
    html << "</div>";

    html << "<div class='reactions-row'>";
    for (auto& kv : reactionMap) {
        if (kv.second.empty()) continue;
        bool mineReacted = std::find(kv.second.begin(), kv.second.end(), requestClientId) != kv.second.end();
        html << "<span class='reaction-pill" << (mineReacted ? " mine" : "") << "' onclick=\"react('"
             << m.id << "','" << kv.first << "')\">" << kv.first << " " << kv.second.size() << "</span>";
    }
    html << "<span class='add-reaction' onclick=\"toggleEmojiPicker('" << m.id << "')\">+</span>";
    html << "<span class='emoji-picker' id='picker-" << m.id << "'>";
    for (auto& e : REACTION_EMOJIS) {
        html << "<span class='emoji-choice' onclick=\"react('" << m.id << "','" << e << "');toggleEmojiPicker('"
             << m.id << "')\">" << e << "</span>";
    }
    html << "</span></div>";

    html << "</div>";

    if (mine) {
        html << "<div class='avatar' style='background:" << avatarColor(m.name) << "'>" << initials(m.name) << "</div>";
    }
    html << "</div>";
    return html.str();
}

std::string renderPoll(const Poll& p, const std::vector<Vote>& allVotes, const std::string& requestClientId) {
    std::vector<int> counts(p.options.size(), 0);
    std::vector<std::vector<std::string>> voterNames(p.options.size());
    int total = 0;
    int myVote = -1;

    for (auto& v : allVotes) {
        if (v.pollId == p.id && v.optionIndex >= 0 && (size_t)v.optionIndex < p.options.size()) {
            counts[v.optionIndex]++;
            voterNames[v.optionIndex].push_back(v.name.empty() ? "Guest" : v.name);
            total++;
            if (v.clientId == requestClientId) myVote = v.optionIndex;
        }
    }

    std::ostringstream html;
    html << "<div class='poll-card'>"
         << "<div class='poll-question'>\xF0\x9F\x93\x8A " << htmlEscape(p.question) << "</div>"
         << "<div class='poll-meta'>by <strong>" << htmlEscape(p.creatorName.empty() ? "Guest" : p.creatorName) << "</strong></div>";

    for (size_t i = 0; i < p.options.size(); i++) {
        int pct = (total > 0) ? (counts[i] * 100 / total) : 0;
        bool isMine = ((int)i == myVote);

        html << "<div class='poll-result'>"
             << "<div class='poll-result-label'><span>" << htmlEscape(p.options[i])
             << (isMine ? " <span class='you-voted'>&#10003; your vote</span>" : "")
             << "</span><span class='poll-pct'>" << pct << "%</span></div>"
             << "<div class='poll-bar-bg'><div class='poll-bar-fill" << (isMine ? " mine" : "")
             << "' style='width:" << pct << "%'></div></div>";

        if (myVote == -1) {
            html << "<form method='POST' action='/vote' class='poll-vote-form' onsubmit='return handleVoteSubmit(this)'>"
                 << "<input type='hidden' name='pollId' value='" << p.id << "'>"
                 << "<input type='hidden' name='optionIndex' value='" << i << "'>"
                 << "<input type='hidden' name='name' class='voteNameInput' value=''>"
                 << "<button type='submit' class='poll-vote-btn'>Vote</button></form>";
        }

        if (!voterNames[i].empty()) {
            std::string pickId = p.id + "_" + std::to_string(i);
            html << "<div class='voters-toggle' onclick=\"toggleVoters('" << pickId << "')\">View voters (" << counts[i] << ")</div>";
            html << "<div class='voters-list' id='voters-" << pickId << "' style='display:none;'>";
            for (auto& vn : voterNames[i]) html << "<span class='voter-chip'>" << htmlEscape(vn) << "</span>";
            html << "</div>";
        }
        html << "</div>";
    }
    html << "<div class='poll-total'>" << total << " vote" << (total == 1 ? "" : "s") << "</div></div>";
    return html.str();
}

std::string renderPollsAndFeed(const std::string& requestClientId) {
    std::vector<Poll> polls = loadPolls();
    std::vector<Vote> votes = loadVotes();
    std::vector<Message> messages = loadMessages();
    std::vector<Reaction> reactions = loadReactions();

    struct TimelineItem {
        long timestamp;
        std::string html;
    };
    std::vector<TimelineItem> items;

    for (auto& p : polls) {
        items.push_back({ p.timestamp, renderPoll(p, votes, requestClientId) });
    }

    for (auto& m : messages) {
        if (!m.parentId.empty()) continue;

        std::string bubbleHtml = renderMessageBubble(m, requestClientId, false, reactions);

        std::vector<Message> replies;
        for (auto& r : messages) {
            if (r.parentId == m.id) replies.push_back(r);
        }
        std::sort(replies.begin(), replies.end(), [](const Message& a, const Message& b) {
            return a.timestamp < b.timestamp;
        });
        for (auto& r : replies) {
            bubbleHtml += renderMessageBubble(r, requestClientId, true, reactions);
        }

        items.push_back({ m.timestamp, bubbleHtml });
    }

    std::sort(items.begin(), items.end(), [](const TimelineItem& a, const TimelineItem& b) {
        return a.timestamp < b.timestamp;
    });

    std::ostringstream html;
    html << "<div id='feed-inner'>";
    if (items.empty()) {
        html << "<p class='empty'>No messages yet. Be the first to post!</p>";
    } else {
        for (auto& it : items) html << it.html;
    }
    html << "</div>";
    return html.str();
}

std::string buildHtmlPage(const std::string& requestClientId) {
    std::ostringstream html;
    html << "<!DOCTYPE html><html><head><meta charset='utf-8'>"
         << "<meta name='viewport' content='width=device-width, initial-scale=1'>"
         << "<title>CampusLink</title><style>"
         << "*{box-sizing:border-box;margin:0;padding:0;}"
         << "html,body{height:100%;}"
         << "body{font-family:-apple-system,'Segoe UI',sans-serif;background:#eef1f8;"
         << "display:flex;flex-direction:column;height:100vh;overflow:hidden;}"
         << "header{background:linear-gradient(135deg,#6366f1,#8b5cf6);padding:16px 20px;"
         << "box-shadow:0 4px 20px rgba(99,102,241,.35);color:#fff;flex-shrink:0;}"
         << ".header-inner{max-width:640px;margin:0 auto;display:flex;align-items:center;gap:12px;}"
         << ".logo{width:40px;height:40px;background:rgba(255,255,255,.18);border-radius:12px;"
         << "display:flex;align-items:center;justify-content:center;flex-shrink:0;}"
         << ".title-block h1{font-size:19px;font-weight:800;letter-spacing:-.5px;}"
         << ".title-block p{font-size:11.5px;opacity:.9;margin-top:1px;}"
         << ".live-pill{margin-left:auto;background:rgba(255,255,255,.2);padding:5px 12px;border-radius:20px;"
         << "font-size:11px;font-weight:600;display:flex;align-items:center;gap:6px;white-space:nowrap;}"
         << ".live-dot{width:7px;height:7px;background:#4ade80;border-radius:50%;animation:pulse 1.8s infinite;}"
         << "@keyframes pulse{0%{box-shadow:0 0 0 0 rgba(74,222,128,.6);}70%{box-shadow:0 0 0 8px rgba(74,222,128,0);}100%{box-shadow:0 0 0 0 rgba(74,222,128,0);}}"
         << ".scroll-area{flex:1;overflow-y:auto;padding:16px;}"
         << ".scroll-inner{max-width:640px;margin:0 auto;}"
         << ".polls-section{margin-bottom:14px;}"
         << ".poll-card{background:#fff;border-radius:16px;padding:16px;margin-bottom:14px;"
         << "box-shadow:0 2px 10px rgba(30,41,59,.07);border:1px solid #eef0f5;}"
         << ".poll-question{font-weight:700;font-size:15px;margin-bottom:4px;color:#1f2937;}"
         << ".poll-meta{font-size:11px;color:#9ca3af;margin-bottom:8px;}"
         << ".poll-result{margin-bottom:10px;}"
         << ".poll-result-label{font-size:13px;font-weight:600;color:#374151;margin-bottom:4px;"
         << "display:flex;justify-content:space-between;}"
         << ".you-voted{color:#8b5cf6;font-size:11px;}"
         << ".poll-pct{color:#6b7280;font-weight:700;}"
         << ".poll-bar-bg{background:#f3f4f6;border-radius:8px;height:10px;overflow:hidden;}"
         << ".poll-bar-fill{background:linear-gradient(90deg,#a5b4fc,#8b5cf6);height:100%;border-radius:8px;}"
         << ".poll-bar-fill.mine{background:linear-gradient(90deg,#6366f1,#4f46e5);}"
         << ".poll-total{font-size:11.5px;color:#9ca3af;margin-top:4px;}"
         << ".poll-vote-form{display:inline-block;margin-top:4px;}"
         << ".poll-vote-btn{background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;border:none;"
         << "padding:4px 14px;border-radius:20px;font-size:11.5px;font-weight:700;cursor:pointer;}"
         << ".voters-toggle{font-size:11px;color:#8b5cf6;cursor:pointer;margin-top:4px;text-decoration:underline;}"
         << ".voters-list{margin-top:6px;display:flex;flex-wrap:wrap;gap:5px;}"
         << ".voter-chip{background:#f5f3ff;color:#4c1d95;padding:2px 9px;border-radius:10px;font-size:11.5px;font-weight:600;}"
         << ".bubble-row{display:flex;align-items:flex-end;margin-bottom:12px;gap:8px;}"
         << ".bubble-row.mine{justify-content:flex-end;}"
         << ".bubble-row.reply{margin-left:38px;opacity:.96;}"
         << ".avatar{width:34px;height:34px;border-radius:11px;color:#fff;display:flex;align-items:center;"
         << "justify-content:center;font-size:14px;font-weight:800;flex-shrink:0;box-shadow:0 2px 6px rgba(0,0,0,.15);}"
         << ".bubble{max-width:75%;background:#fff;padding:11px 15px;border-radius:16px;box-shadow:0 2px 8px rgba(30,41,59,.07);}"
         << ".mine .bubble{background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;}"
         << ".bubble-header{font-size:12px;margin-bottom:4px;opacity:.75;display:flex;gap:6px;align-items:center;}"
         << ".mine .bubble-header{color:#e0e7ff;}"
         << ".tag{background:rgba(99,102,241,.12);color:#6366f1;padding:2px 9px;border-radius:8px;"
         << "font-size:10.5px;font-weight:700;text-transform:uppercase;}"
         << ".mine .tag{background:rgba(255,255,255,.25);color:#fff;}"
         << ".bubble-text{font-size:14.5px;white-space:pre-wrap;word-break:break-word;line-height:1.4;}"
         << ".bubble-actions{margin-top:6px;font-size:11px;}"
         << ".action-link{color:#6366f1;cursor:pointer;font-weight:600;margin-right:10px;}"
         << ".mine .action-link{color:#dbeafe;}"
         << ".del-btn{background:none;border:none;color:#dc2626;font-size:11px;font-weight:600;cursor:pointer;padding:0;}"
         << ".mine .del-btn{color:#fecaca;}"
         << ".reactions-row{margin-top:6px;display:flex;flex-wrap:wrap;gap:4px;align-items:center;}"
         << ".reaction-pill{background:#f3f4f6;border-radius:12px;padding:2px 8px;font-size:12px;cursor:pointer;border:1px solid transparent;}"
         << ".reaction-pill.mine{background:#ede9fe;border-color:#8b5cf6;}"
         << ".mine .reaction-pill{background:rgba(255,255,255,.25);color:#fff;}"
         << ".mine .reaction-pill.mine{background:rgba(255,255,255,.4);border-color:#fff;}"
         << ".add-reaction{cursor:pointer;font-size:13px;color:#9ca3af;padding:1px 6px;border-radius:10px;background:#f3f4f6;}"
         << ".mine .add-reaction{background:rgba(255,255,255,.2);color:#e0e7ff;}"
         << ".emoji-picker{display:none;gap:4px;background:#fff;border:1px solid #e5e7eb;border-radius:10px;"
         << "padding:4px 6px;box-shadow:0 2px 8px rgba(0,0,0,.1);}"
         << ".emoji-choice{cursor:pointer;font-size:16px;padding:2px;}"
         << ".empty{text-align:center;color:#9ca3af;padding:30px 0;font-size:14px;}"
         << "footer{text-align:center;color:#9ca3af;font-size:11px;margin-top:16px;padding-bottom:8px;}"
         << ".composer-bar{flex-shrink:0;background:#fff;border-top:1px solid #e5e7eb;"
         << "box-shadow:0 -2px 12px rgba(30,41,59,.06);padding:10px 16px 12px;}"
         << ".composer-inner{max-width:640px;margin:0 auto;}"
         << "#nameDisplay{font-size:12px;color:#6366f1;font-weight:600;margin-bottom:6px;}"
         << ".change-link{color:#8b5cf6;cursor:pointer;text-decoration:underline;margin-left:6px;font-weight:600;}"
         << "#nameRow label{font-size:11px;font-weight:700;color:#6366f1;text-transform:uppercase;"
         << "letter-spacing:.5px;display:block;margin-bottom:4px;}"
         << "#nameRow input{width:100%;padding:8px 10px;margin-bottom:8px;border:1.5px solid #e5e7eb;"
         << "border-radius:9px;font-family:inherit;font-size:13.5px;}"
         << "#replyingTo{display:none;background:#eef2ff;padding:6px 10px;border-radius:9px;margin-bottom:6px;"
         << "font-size:12px;color:#4338ca;font-weight:600;}"
         << "#replyingTo .cancel{color:#dc2626;cursor:pointer;margin-left:8px;font-weight:700;}"
         << ".poll-composer{background:#f8f7ff;padding:12px;border-radius:12px;margin-bottom:10px;border:1.5px dashed #c7d2fe;}"
         << ".poll-composer label{font-size:11px;font-weight:700;color:#6366f1;text-transform:uppercase;letter-spacing:.5px;}"
         << ".poll-composer input{width:100%;padding:8px 10px;margin:4px 0 8px;border:1.5px solid #e5e7eb;border-radius:9px;font-family:inherit;font-size:13.5px;}"
         << ".poll-composer button{background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;border:none;"
         << "padding:9px 18px;border-radius:9px;cursor:pointer;font-weight:700;font-size:13px;}"
         << ".msg-row{display:flex;gap:8px;align-items:flex-end;}"
         << ".plus-wrap{position:relative;}"
         << ".plus-btn{width:38px;height:38px;border-radius:50%;background:#f3f4f6;color:#6366f1;"
         << "border:none;font-size:20px;cursor:pointer;flex-shrink:0;display:flex;align-items:center;justify-content:center;}"
         << ".plus-menu{position:absolute;bottom:46px;left:0;background:#fff;border:1px solid #e5e7eb;"
         << "border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.12);padding:6px;min-width:120px;z-index:20;}"
         << ".plus-menu-item{padding:8px 12px;font-size:13.5px;font-weight:600;color:#374151;cursor:pointer;border-radius:8px;}"
         << ".plus-menu-item:hover{background:#f3f4f6;}"
         << ".msg-row select{width:auto;flex-shrink:0;padding:9px 8px;border:1.5px solid #e5e7eb;"
         << "border-radius:10px;font-size:13px;font-family:inherit;}"
         << ".msg-row textarea{flex:1;resize:none;padding:9px 12px;border:1.5px solid #e5e7eb;"
         << "border-radius:14px;font-family:inherit;font-size:14px;max-height:80px;}"
         << ".msg-row textarea:focus,.msg-row select:focus{outline:none;border-color:#8b5cf6;}"
         << ".send-btn{background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;border:none;"
         << "width:42px;height:42px;border-radius:50%;cursor:pointer;flex-shrink:0;font-size:16px;"
         << "display:flex;align-items:center;justify-content:center;}"
         << ".send-btn:active{transform:scale(.93);}"
         << "</style></head><body>"

         << "<header><div class='header-inner'>"
         << "<div class='logo'><svg width='22' height='22' viewBox='0 0 24 24' fill='none' stroke='white' "
         << "stroke-width='2' stroke-linecap='round'><path d='M2 8.5a16 16 0 0 1 20 0'/>"
         << "<path d='M5 12.5a11 11 0 0 1 14 0'/><path d='M8.5 16.5a6 6 0 0 1 7 0'/>"
         << "<circle cx='12' cy='20' r='1' fill='white'/></svg></div>"
         << "<div class='title-block'><h1>CampusLink</h1><p>Local chat &middot; zero internet needed</p></div>"
         << "<div class='live-pill'><span class='live-dot'></span>Live</div>"
         << "</div></header>"

         << "<div class='scroll-area' id='scrollArea'><div class='scroll-inner'>"
         << "<div id='feed-container'>" << renderPollsAndFeed(requestClientId) << "</div>"
         << "<footer>Built with raw sockets &middot; no frameworks &middot; works over LAN with zero internet</footer>"
         << "</div></div>"

         << "<div class='composer-bar'><div class='composer-inner'>"
         << "<div id='replyingTo'></div>"
         << "<div id='nameDisplay' style='display:none;'></div>"
         << "<form class='poll-composer' method='POST' action='/poll' id='pollComposer' style='display:none;' onsubmit='return handleVoteSubmit(this)'>"
         << "<input type='hidden' name='name' class='voteNameInput' value=''>"
         << "<label>Poll Question</label>"
         << "<input type='text' name='question' placeholder='e.g. Mess menu for Sunday?' maxlength='120' required>"
         << "<label>Options</label>"
         << "<input type='text' name='option1' placeholder='Option 1' maxlength='60' required>"
         << "<input type='text' name='option2' placeholder='Option 2' maxlength='60' required>"
         << "<input type='text' name='option3' placeholder='Option 3 (optional)' maxlength='60'>"
         << "<input type='text' name='option4' placeholder='Option 4 (optional)' maxlength='60'>"
         << "<button type='submit'>Create Poll</button>"
         << "</form>"
         << "<form method='POST' action='/post' id='composer'>"
         << "<input type='hidden' name='parentId' id='parentId' value=''>"
         << "<div id='nameRow'>"
         << "<label>Your name</label>"
         << "<input type='text' name='name' id='nameInput' placeholder='e.g. Rahul' maxlength='40' required>"
         << "</div>"
         << "<div class='msg-row'>"
         << "<div class='plus-wrap'>"
         << "<button type='button' class='plus-btn' onclick='togglePlusMenu(event)'>+</button>"
         << "<div class='plus-menu' id='plusMenu' style='display:none;'>"
         << "<div class='plus-menu-item' onclick=\"togglePollForm();closePlusMenu();\">\xF0\x9F\x93\x8A Poll</div>"
         << "</div></div>"
         << "<select name='category'>"
         << "<option value='general'>\xF0\x9F\x92\xAC</option>"
         << "<option value='notice'>\xF0\x9F\x93\xA2</option>"
         << "<option value='doubt'>\xE2\x9D\x93</option>"
         << "</select>"
         << "<textarea name='message' id='messageBox' placeholder='Type a message...' maxlength='500' rows='1' required></textarea>"
         << "<button type='submit' class='send-btn'>\xE2\x9E\xA4</button>"
         << "</div></form>"
         << "</div></div>"

         << "<script>"
         << "function replyTo(id,name){"
         << "document.getElementById('parentId').value=id;"
         << "var r=document.getElementById('replyingTo');"
         << "r.innerHTML='Replying to '+name+' <span class=\"cancel\" onclick=\"cancelReply()\">cancel</span>';"
         << "r.style.display='block';"
         << "document.getElementById('messageBox').focus();"
         << "}"
         << "function cancelReply(){"
         << "document.getElementById('parentId').value='';"
         << "document.getElementById('replyingTo').style.display='none';"
         << "}"
         << "function togglePollForm(){"
         << "var f=document.getElementById('pollComposer');"
         << "f.style.display=(f.style.display==='none')?'block':'none';"
         << "}"
         << "function togglePlusMenu(e){"
         << "if(e) e.stopPropagation();"
         << "var m=document.getElementById('plusMenu');"
         << "m.style.display=(m.style.display==='none')?'block':'none';"
         << "}"
         << "function closePlusMenu(){"
         << "document.getElementById('plusMenu').style.display='none';"
         << "}"
         << "document.addEventListener('click', function(e){"
         << "var menu=document.getElementById('plusMenu');"
         << "if(menu && menu.style.display==='block' && !menu.contains(e.target)){ menu.style.display='none'; }"
         << "});"
         << "function toggleVoters(id){"
         << "var el=document.getElementById('voters-'+id);"
         << "el.style.display=(el.style.display==='none')?'block':'none';"
         << "}"
         << "function isNearBottom(){"
         << "var a=document.getElementById('scrollArea');"
         << "return (a.scrollHeight - a.scrollTop - a.clientHeight) < 120;"
         << "}"
         << "function scrollToBottom(){"
         << "var a=document.getElementById('scrollArea');"
         << "a.scrollTop = a.scrollHeight;"
         << "}"
         << "function fillVoteNames(){"
         << "var stored = localStorage.getItem('campuslink_name');"
         << "document.querySelectorAll('.voteNameInput').forEach(function(el){el.value=stored||'';});"
         << "}"
         << "function refreshFeed(){"
         << "var wasNearBottom = isNearBottom();"
         << "fetch('/messages').then(function(r){return r.text();}).then(function(html){"
         << "document.getElementById('feed-container').innerHTML = html;"
         << "fillVoteNames();"
         << "if(wasNearBottom) scrollToBottom();"
         << "});"
         << "}"
         << "function react(id, emoji){"
         << "var body = 'messageId='+encodeURIComponent(id)+'&emoji='+encodeURIComponent(emoji);"
         << "fetch('/react', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:body})"
         << ".then(function(){ refreshFeed(); });"
         << "}"
         << "function toggleEmojiPicker(id){"
         << "var p=document.getElementById('picker-'+id);"
         << "p.style.display=(p.style.display==='inline-flex')?'none':'inline-flex';"
         << "}"
         << "function loadStoredName(){"
         << "var stored = localStorage.getItem('campuslink_name');"
         << "var input = document.getElementById('nameInput');"
         << "var display = document.getElementById('nameDisplay');"
         << "var row = document.getElementById('nameRow');"
         << "if(stored){"
         << "input.value = stored;"
         << "row.style.display='none';"
         << "display.innerHTML = 'Posting as <strong>'+stored+'</strong> <span class=\"change-link\" onclick=\"changeName()\">change</span>';"
         << "display.style.display='block';"
         << "}"
         << "fillVoteNames();"
         << "}"
         << "function changeName(){"
         << "document.getElementById('nameRow').style.display='block';"
         << "document.getElementById('nameDisplay').style.display='none';"
         << "document.getElementById('nameInput').focus();"
         << "}"
         << "function handleVoteSubmit(form){"
         << "var stored = localStorage.getItem('campuslink_name');"
         << "if(!stored){"
         << "var name = prompt('Enter your name to vote/create polls:');"
         << "if(!name || !name.trim()){ return false; }"
         << "stored = name.trim();"
         << "localStorage.setItem('campuslink_name', stored);"
         << "loadStoredName();"
         << "}"
         << "form.querySelector('.voteNameInput').value = stored;"
         << "return true;"
         << "}"
         << "document.getElementById('composer').addEventListener('submit', function(){"
         << "var val = document.getElementById('nameInput').value.trim();"
         << "if(val) localStorage.setItem('campuslink_name', val);"
         << "});"
         << "loadStoredName();"
         << "scrollToBottom();"
         << "setInterval(refreshFeed, 4000);"
         << "</script>"
         << "</body></html>";
    return html.str();
}

// ---------- Request handling / routing ----------

void handleClient(int clientSocket) {
    std::string raw = readRawRequest(clientSocket);
    if (raw.empty()) { close(clientSocket); return; }

    HttpRequest req = parseRequest(raw);

    std::string cookieHeader = req.headers.count("Cookie") ? req.headers["Cookie"] : "";
    std::string clientId = getCookieValue(cookieHeader, "clid");
    std::string setCookieHeader;
    if (clientId.empty()) {
        clientId = generateId();
        setCookieHeader = "Set-Cookie: clid=" + clientId + "; Path=/\r\n";
    }

    if (req.method == "GET" && req.path == "/") {
        sendResponse(clientSocket, 200, "OK", "text/html", buildHtmlPage(clientId), setCookieHeader);
    }
    else if (req.method == "GET" && req.path == "/messages") {
        sendResponse(clientSocket, 200, "OK", "text/html", renderPollsAndFeed(clientId), setCookieHeader);
    }
    else if (req.method == "POST" && req.path == "/post") {
        auto fields = parseFormBody(req.body);
        std::string name = fields.count("name") ? fields["name"] : "";
        std::string category = fields.count("category") ? fields["category"] : "general";
        std::string text = fields.count("message") ? fields["message"] : "";
        std::string parentId = fields.count("parentId") ? fields["parentId"] : "";

        if (name.empty()) name = "Guest";
        if (name.size() > 40) name = name.substr(0, 40);
        if (text.size() > 500) text = text.substr(0, 500);

        if (!text.empty()) {
            Message m;
            m.id = generateId();
            m.timestamp = std::time(nullptr);
            m.category = category;
            m.name = name;
            m.clientId = clientId;
            m.parentId = parentId;
            m.text = text;
            saveMessage(m);
        }
        sendResponse(clientSocket, 303, "See Other", "text/plain", "", "Location: /\r\n" + setCookieHeader);
    }
    else if (req.method == "POST" && req.path == "/delete") {
        auto fields = parseFormBody(req.body);
        std::string id = fields.count("id") ? fields["id"] : "";
        if (!id.empty()) deleteMessage(id, clientId);
        sendResponse(clientSocket, 303, "See Other", "text/plain", "", "Location: /\r\n" + setCookieHeader);
    }
    else if (req.method == "POST" && req.path == "/poll") {
        auto fields = parseFormBody(req.body);
        std::string question = fields.count("question") ? fields["question"] : "";
        std::string creatorName = fields.count("name") ? fields["name"] : "";
        if (creatorName.empty()) creatorName = "Guest";
        std::vector<std::string> options;
        for (int i = 1; i <= 4; i++) {
            std::string key = "option" + std::to_string(i);
            if (fields.count(key) && !fields[key].empty()) options.push_back(fields[key]);
        }
        if (!question.empty() && options.size() >= 2) {
            Poll p;
            p.id = generateId();
            p.timestamp = std::time(nullptr);
            p.creatorName = creatorName;
            p.question = question;
            p.options = options;
            savePoll(p);
        }
        sendResponse(clientSocket, 303, "See Other", "text/plain", "", "Location: /\r\n" + setCookieHeader);
    }
    else if (req.method == "POST" && req.path == "/vote") {
        auto fields = parseFormBody(req.body);
        std::string pollId = fields.count("pollId") ? fields["pollId"] : "";
        std::string voterName = fields.count("name") ? fields["name"] : "";
        if (voterName.empty()) voterName = "Guest";
        int optionIndex = -1;
        if (fields.count("optionIndex")) {
            try { optionIndex = std::stoi(fields["optionIndex"]); } catch (...) {}
        }
        if (!pollId.empty() && optionIndex >= 0) {
            tryAddVote(pollId, clientId, optionIndex, voterName);
        }
        sendResponse(clientSocket, 303, "See Other", "text/plain", "", "Location: /\r\n" + setCookieHeader);
    }
    else if (req.method == "POST" && req.path == "/react") {
        auto fields = parseFormBody(req.body);
        std::string messageId = fields.count("messageId") ? fields["messageId"] : "";
        std::string emoji = fields.count("emoji") ? fields["emoji"] : "";
        if (!messageId.empty() && !emoji.empty()) {
            toggleReaction(messageId, emoji, clientId);
        }
        sendResponse(clientSocket, 200, "OK", "text/plain", "ok", setCookieHeader);
    }
    else {
        sendResponse(clientSocket, 404, "Not Found", "text/html", "<h1>404 Not Found</h1>", setCookieHeader);
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