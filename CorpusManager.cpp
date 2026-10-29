#include "CorpusManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <dirent.h>
#include <cstdio>
#include <memory>
#include <array>
#include <algorithm>

using namespace std;

vector<string> CorpusManager::tokenize(const string& text) {
    vector<string> tokens;
    string cleanText = "";
    
    for (char c : text) {
        if (isalnum(c)) cleanText += tolower(c);
        else cleanText += ' ';
    }
    
    stringstream ss(cleanText);
    string word;
    while (ss >> word) tokens.push_back(word);
    
    return tokens;
}

void CorpusManager::parseAndStoreText(const string& text) {
    stringstream ss(text);
    string line;
    string currentParagraph = "";

    while (getline(ss, line)) {
        if (line.empty()) {
            if (!currentParagraph.empty()) {
                Document doc;
                doc.id = docIdCounter++;
                doc.originalText = currentParagraph;
                doc.tokens = tokenize(currentParagraph);
                documents.push_back(doc);
                currentParagraph = ""; 
            }
        } else {
            currentParagraph += line + "\n";
        }
    }

    if (!currentParagraph.empty()) {
        Document doc;
        doc.id = docIdCounter++;
        doc.originalText = currentParagraph;
        doc.tokens = tokenize(currentParagraph);
        documents.push_back(doc);
    }
}

string CorpusManager::extractTextFromPDF(const string& filePath) {
    // Requires pdftotext to be installed and in the system PATH
    string command = "pdftotext \"" + filePath + "\" -";
    array<char, 1024> buffer;
    string result;
    
#ifdef _WIN32
    unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
    unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif

    if (!pipe) {
        cerr << "Error: Failed to run pdftotext. Make sure Xpdf tools are installed." << endl;
        return "";
    }
    
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    
    return result;
}

void CorpusManager::loadFile(const string& filePath) {
    string ext = filePath.length() >= 4 ? filePath.substr(filePath.length() - 4) : "";
    transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".txt") {
        ifstream file(filePath);
        if (file.is_open()) {
            stringstream buffer;
            buffer << file.rdbuf();
            parseAndStoreText(buffer.str());
            file.close();
            cout << "Successfully loaded TXT: " << filePath << endl;
        } else {
            cout << "Could not open file: " << filePath << endl;
        }
    } else if (ext == ".pdf") {
        string extractedText = extractTextFromPDF(filePath);
        if (!extractedText.empty()) {
            parseAndStoreText(extractedText);
            cout << "Successfully loaded PDF: " << filePath << endl;
        } else {
            cout << "Failed to extract text from PDF: " << filePath << endl;
        }
    } else {
        cout << "Unsupported file format: " << filePath << ". Only .txt and .pdf are supported." << endl;
    }
}

void CorpusManager::loadDirectory(const string& directoryPath) {
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(directoryPath.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string fileName = ent->d_name;
            if (fileName == "." || fileName == "..") continue;
            
            string filePath = directoryPath + "/" + fileName;
            string ext = fileName.length() >= 4 ? fileName.substr(fileName.length() - 4) : "";
            transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            if (ext == ".txt" || ext == ".pdf") {
                loadFile(filePath);
            }
        }
        closedir(dir);
    } else {
        cout << "Could not open directory: " << directoryPath << endl;
    }
    cout << "Total paragraphs loaded into corpus: " << documents.size() << endl;
}

vector<Document> CorpusManager::getDocuments() const {
    return documents;
}