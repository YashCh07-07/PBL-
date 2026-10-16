#include "CorpusManager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cctype>
#include <dirent.h>// Built-in Windows directory reader

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

void CorpusManager::loadDirectory(const string& directoryPath) {
    int docIdCounter = 0;
    DIR *dir;
    struct dirent *ent;

    if ((dir = opendir(directoryPath.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            string fileName = ent->d_name;
            
            // Only process .txt files
            if (fileName.length() >= 4 && fileName.substr(fileName.length() - 4) == ".txt") {
                string filePath = directoryPath + "/" + fileName;
                ifstream file(filePath);
                
                if (!file.is_open()) continue;

                string line;
                string currentParagraph = "";

                while (getline(file, line)) {
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
                file.close();
            }
        }
        closedir(dir);
    } else {
        cout << "Could not open directory: " << directoryPath << endl;
    }
    cout << "Successfully loaded " << documents.size() << " paragraphs." << endl;
}

vector<Document> CorpusManager::getDocuments() const {
    return documents;
}