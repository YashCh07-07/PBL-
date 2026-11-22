#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "CorpusManager.h"
#include "InvertedIndex.h"
#include "BM25Ranker.h"
#include "OllamaBridge.h"

using namespace std;

// RAII wrapper around Win32 CRITICAL_SECTION (works on MinGW 6.3.0 without std::mutex)
struct ScopedLock {
    CRITICAL_SECTION* cs;
    explicit ScopedLock(CRITICAL_SECTION* c) : cs(c) { EnterCriticalSection(cs); }
    ~ScopedLock() { LeaveCriticalSection(cs); }
};

struct RagState {
    CRITICAL_SECTION cs;
    unique_ptr<CorpusManager> corpusManager;
    unique_ptr<InvertedIndex> invertedIndex;
    unique_ptr<BM25Ranker> ranker;
    OllamaBridge ollama;
    vector<Document> allDocs;

    bool isLoaded = false;
    string activeFilename;
    size_t totalTokens = 0;

    RagState() { InitializeCriticalSection(&cs); }
    ~RagState() { DeleteCriticalSection(&cs); }

    void reset() {
        corpusManager.reset();
        invertedIndex.reset();
        ranker.reset();
        allDocs.clear();
        isLoaded = false;
        activeFilename.clear();
        totalTokens = 0;
    }
};

static RagState g_state;

vector<string> tokenizeQuery(const string& query) {
    vector<string> tokens;
    string cleanQuery;
    for (unsigned char c : query) {
        if (isalnum(c)) cleanQuery += static_cast<char>(tolower(c));
        else cleanQuery += ' ';
    }
    stringstream ss(cleanQuery);
    string word;
    while (ss >> word) tokens.push_back(word);
    return tokens;
}

string escapeJson(const string& input) {
    string out;
    out.reserve(input.size() + 32);
    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) out += ' ';
                else out += static_cast<char>(c);
        }
    }
    return out;
}

string urlDecode(const string& str) {
    string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            string hex = str.substr(i + 1, 2);
            char decoded = static_cast<char>(strtol(hex.c_str(), nullptr, 16));
            out += decoded;
            i += 2;
        } else {
            out += str[i];
        }
    }
    return out;
}

string getQueryParam(const string& query, const string& key) {
    string prefix = key + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        string pair = (amp == string::npos) ? query.substr(pos) : query.substr(pos, amp - pos);
        if (pair.compare(0, prefix.size(), prefix) == 0) {
            return urlDecode(pair.substr(prefix.size()));
        }
        if (amp == string::npos) break;
        pos = amp + 1;
    }
    return "";
}

bool sendAll(SOCKET sock, const string& data) {
    size_t totalSent = 0;
    while (totalSent < data.size()) {
        int sent = send(sock, data.data() + totalSent, static_cast<int>(data.size() - totalSent), 0);
        if (sent <= 0) return false;
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

void sendResponse(SOCKET sock, int code, const string& statusText, const string& contentType, const string& body) {
    stringstream hdr;
    hdr << "HTTP/1.1 " << code << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n";
    if (sendAll(sock, hdr.str())) {
        sendAll(sock, body);
    }
}

void handleRequest(SOCKET sock) {
    DWORD timeoutMs = 15000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    // 1. Read HTTP headers
    string raw;
    char buf[8192];
    size_t headerEnd = string::npos;

    while (headerEnd == string::npos) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) return;
        raw.append(buf, n);
        headerEnd = raw.find("\r\n\r\n");
        if (raw.size() > 65536 && headerEnd == string::npos) {
            sendResponse(sock, 431, "Request Header Fields Too Large", "text/plain", "Headers too large");
            return;
        }
    }

    string headerPart = raw.substr(0, headerEnd);
    string body = raw.substr(headerEnd + 4);

    // Parse request line: METHOD TARGET HTTP/1.1
    size_t firstLineEnd = headerPart.find("\r\n");
    string reqLine = (firstLineEnd == string::npos) ? headerPart : headerPart.substr(0, firstLineEnd);
    stringstream reqSS(reqLine);
    string method, target;
    reqSS >> method >> target;

    string path = target;
    string query;
    size_t qPos = target.find('?');
    if (qPos != string::npos) {
        path = target.substr(0, qPos);
        query = target.substr(qPos + 1);
    }

    // Parse Content-Length & Expect: 100-continue
    string lowerHeaders = headerPart;
    transform(lowerHeaders.begin(), lowerHeaders.end(), lowerHeaders.begin(), ::tolower);

    size_t contentLength = 0;
    size_t clPos = lowerHeaders.find("content-length:");
    if (clPos != string::npos) {
        size_t lineEnd = lowerHeaders.find("\r\n", clPos);
        string val = lowerHeaders.substr(clPos + 15, (lineEnd == string::npos ? lowerHeaders.size() : lineEnd) - (clPos + 15));
        contentLength = static_cast<size_t>(strtoull(val.c_str(), nullptr, 10));
    }

    const size_t MAX_BODY = 50 * 1024 * 1024; // 50 MB
    if (contentLength > MAX_BODY) {
        sendResponse(sock, 413, "Payload Too Large", "application/json", "{\"error\":\"File exceeds 50 MB limit.\"}");
        return;
    }

    if (lowerHeaders.find("expect: 100-continue") != string::npos) {
        sendAll(sock, "HTTP/1.1 100 Continue\r\n\r\n");
    }

    // 2. Read remaining request body
    body.reserve(contentLength);
    while (body.size() < contentLength) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;
        body.append(buf, n);
    }
    if (body.size() > contentLength && contentLength > 0) {
        body.resize(contentLength);
    }

    // 3. Route handling
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        ifstream file("frontend/index.html", ios::binary);
        if (!file) {
            sendResponse(sock, 500, "Internal Server Error", "text/plain",
                         "Error: frontend/index.html not found in project folder.");
            return;
        }
        stringstream ss;
        ss << file.rdbuf();
        sendResponse(sock, 200, "OK", "text/html; charset=utf-8", ss.str());
        return;
    }

    if (method == "GET" && path == "/favicon.ico") {
        sendResponse(sock, 204, "No Content", "image/x-icon", "");
        return;
    }

    if (method == "GET" && path == "/api/status") {
        ScopedLock lock(&g_state.cs);
        stringstream json;
        json << "{"
             << "\"loaded\":" << (g_state.isLoaded ? "true" : "false") << ","
             << "\"filename\":\"" << escapeJson(g_state.activeFilename) << "\","
             << "\"chunks\":" << g_state.allDocs.size() << ","
             << "\"totalTokens\":" << g_state.totalTokens
             << "}";
        sendResponse(sock, 200, "OK", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/reset") {
        ScopedLock lock(&g_state.cs);
        g_state.reset();
        cout << "[Reset] Corpus cleared.\n";
        sendResponse(sock, 200, "OK", "application/json", "{\"loaded\":false}");
        return;
    }

    if (method == "POST" && path == "/api/upload") {
        string filename = getQueryParam(query, "filename");
        if (filename.empty() || body.empty()) {
            sendResponse(sock, 400, "Bad Request", "application/json", "{\"error\":\"Missing filename or empty file body.\"}");
            return;
        }

        string ext = filename.length() >= 4 ? filename.substr(filename.length() - 4) : "";
        transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext != ".txt" && ext != ".pdf") {
            sendResponse(sock, 400, "Bad Request", "application/json",
                         "{\"error\":\"Unsupported format. Only .txt and .pdf files are allowed.\"}");
            return;
        }

        ScopedLock lock(&g_state.cs);

        string tempPath = "temp_uploaded_corpus" + ext;
        {
            ofstream outFile(tempPath, ios::binary);
            if (!outFile) {
                sendResponse(sock, 500, "Internal Server Error", "application/json",
                             "{\"error\":\"Server failed to write temporary file.\"}");
                return;
            }
            outFile.write(body.data(), static_cast<streamsize>(body.size()));
        }

        unique_ptr<CorpusManager> newCorpus(new CorpusManager());
        newCorpus->loadFile(tempPath);
        remove(tempPath.c_str());

        vector<Document> docs = newCorpus->getDocuments();
        if (docs.empty()) {
            sendResponse(sock, 400, "Bad Request", "application/json",
                         "{\"error\":\"No readable text extracted. If uploading a PDF, verify pdftotext is installed and in PATH.\"}");
            return;
        }

        unique_ptr<InvertedIndex> newIndex(new InvertedIndex());
        newIndex->buildIndex(docs);

        unique_ptr<BM25Ranker> newRanker(new BM25Ranker());
        newRanker->initialize(docs);

        size_t tokens = 0;
        for (const auto& doc : docs) tokens += doc.tokens.size();

        g_state.corpusManager = move(newCorpus);
        g_state.invertedIndex = move(newIndex);
        g_state.ranker = move(newRanker);
        g_state.allDocs = move(docs);
        g_state.activeFilename = filename;
        g_state.totalTokens = tokens;
        g_state.isLoaded = true;

        cout << "[Indexed] " << filename << " (" << g_state.allDocs.size() << " chunks, " << tokens << " tokens)\n";

        stringstream json;
        json << "{"
             << "\"loaded\":true,"
             << "\"filename\":\"" << escapeJson(g_state.activeFilename) << "\","
             << "\"chunks\":" << g_state.allDocs.size() << ","
             << "\"totalTokens\":" << g_state.totalTokens
             << "}";
        sendResponse(sock, 200, "OK", "application/json", json.str());
        return;
    }

    if (method == "POST" && path == "/api/ask") {
        if (body.empty()) {
            sendResponse(sock, 400, "Bad Request", "application/json", "{\"error\":\"Question cannot be empty.\"}");
            return;
        }

        int topK = 3;
        string topKStr = getQueryParam(query, "topK");
        if (!topKStr.empty()) {
            int parsed = atoi(topKStr.c_str());
            if (parsed > 0) topK = max(1, min(10, parsed));
        }

        ScopedLock lock(&g_state.cs);
        if (!g_state.isLoaded) {
            sendResponse(sock, 400, "Bad Request", "application/json",
                         "{\"error\":\"No corpus loaded. Please upload a file first.\"}");
            return;
        }

        auto startTime = chrono::steady_clock::now();

        vector<string> queryWords = tokenizeQuery(body);
        vector<int> topDocIds = g_state.ranker->getTopKDocuments(queryWords, *g_state.invertedIndex, topK);

        string contextText;
        vector<Document> matchedDocs;
        for (int id : topDocIds) {
            for (const Document& doc : g_state.allDocs) {
                if (doc.id == id) {
                    matchedDocs.push_back(doc);
                    contextText += doc.originalText + "\n\n";
                    break;
                }
            }
        }

        if (matchedDocs.empty()) {
            sendResponse(sock, 200, "OK", "application/json",
                         "{\"answer\":\"No matching keywords were found in the indexed corpus for that query.\",\"sources\":[],\"latencyMs\":0}");
            return;
        }

        string answer = g_state.ollama.askOllama(contextText, body);
        auto endTime = chrono::steady_clock::now();
        long long latencyMs = chrono::duration_cast<chrono::milliseconds>(endTime - startTime).count();

        stringstream json;
        json << "{\"answer\":\"" << escapeJson(answer) << "\",\"sources\":[";
        for (size_t i = 0; i < matchedDocs.size(); ++i) {
            if (i > 0) json << ",";
            json << "{\"id\":" << matchedDocs[i].id
                 << ",\"text\":\"" << escapeJson(matchedDocs[i].originalText) << "\"}";
        }
        json << "],\"latencyMs\":" << latencyMs << "}";

        sendResponse(sock, 200, "OK", "application/json", json.str());
        return;
    }

    sendResponse(sock, 404, "Not Found", "application/json", "{\"error\":\"Endpoint not found.\"}");
}

unsigned __stdcall clientWorker(void* arg) {
    SOCKET sock = *static_cast<SOCKET*>(arg);
    delete static_cast<SOCKET*>(arg);
    handleRequest(sock);
    closesocket(sock);
    return 0;
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET serverSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSock == INVALID_SOCKET) {
        cerr << "Failed to create socket.\n";
        WSACleanup();
        return 1;
    }

    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port = htons(8080);

    if (bind(serverSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        cerr << "Bind failed on port 8080. Make sure nothing else is using port 8080.\n";
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }

    if (listen(serverSock, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed.\n";
        closesocket(serverSock);
        WSACleanup();
        return 1;
    }

    cout << "==========================================\n";
    cout << "  CORPUS RAG SERVER (C++ BM25 + Ollama)   \n";
    cout << "  Open: http://localhost:8080             \n";
    cout << "==========================================\n";

    while (true) {
        SOCKET clientSock = accept(serverSock, nullptr, nullptr);
        if (clientSock == INVALID_SOCKET) continue;

        SOCKET* sockPtr = new SOCKET(clientSock);
        HANDLE hThread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, clientWorker, sockPtr, 0, nullptr));
        if (hThread) {
            CloseHandle(hThread);
        } else {
            delete sockPtr;
            closesocket(clientSock);
        }
    }

    closesocket(serverSock);
    WSACleanup();
    return 0;
}
