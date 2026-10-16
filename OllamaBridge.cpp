#include "OllamaBridge.h"
#include <iostream>
#include <string>
#include <fstream>
#include <cstdio>
#include <array>
#include <memory>

using namespace std;

string OllamaBridge::escapeForJson(const string& input) {
    string escaped;
    for (char c : input) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\n') escaped += "\\n";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
    }
    return escaped;
}

string OllamaBridge::askOllama(const string& context, const string& question) {
    string fullPrompt = "Use the following context to answer the question.\n\nContext:\n" + context + "\n\nQuestion: " + question;
    string safePrompt = escapeForJson(fullPrompt);
    
    string jsonPayload = "{\"model\": \"llama3\", \"prompt\": \"" + safePrompt + "\", \"stream\": false}";

    // Write payload to temp file to avoid Windows escaping issues
    ofstream outFile("temp_ollama_payload.json");
    outFile << jsonPayload;
    outFile.close();

    string command = "curl -s -d \"@temp_ollama_payload.json\" http://localhost:11434/api/generate";
    
    array<char, 1024> buffer;
    string result;
    
#ifdef _WIN32
    unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif

    if (!pipe) return "Error: Failed to run command prompt.";
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    remove("temp_ollama_payload.json");

    string searchKey = "\"response\":\"";
    size_t startPos = result.find(searchKey);
    if (startPos != string::npos) {
        startPos += searchKey.length();
        size_t endPos = result.find("\",\"done\"", startPos);
        
        if (endPos != string::npos) {
            string finalAnswer = result.substr(startPos, endPos - startPos);
            
            size_t pos = 0;
            while ((pos = finalAnswer.find("\\n", pos)) != string::npos) {
                finalAnswer.replace(pos, 2, "\n");
                pos += 1;
            }
            pos = 0;
            while ((pos = finalAnswer.find("\\\"", pos)) != string::npos) {
                finalAnswer.replace(pos, 2, "\"");
                pos += 1;
            }
            return finalAnswer;
        }
    }
    return "Error: Could not parse response from Ollama.";
}