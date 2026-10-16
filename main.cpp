#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cctype>

#include "CorpusManager.h"
#include "InvertedIndex.h"
#include "BM25Ranker.h"
#include "OllamaBridge.h"

using namespace std;

vector<string> tokenizeQuery(const string& query) {
    vector<string> tokens;
    string cleanQuery = "";

    for (char c : query) {
        if (isalnum(c)) cleanQuery += tolower(c);
        else cleanQuery += ' ';
    }
    
    stringstream ss(cleanQuery);
    string word;
    while (ss >> word) tokens.push_back(word);
    
    return tokens;
}

int main() {
    cout << "========================================\n";
    cout << "   Local C++ RAG Tool (BM25 + Ollama)   \n";
    cout << "========================================\n\n";

    CorpusManager corpusManager;
    InvertedIndex invertedIndex;
    BM25Ranker ranker;
    OllamaBridge ollama;

    string corpusFolder = "C:\\Users\\yashc\\OneDrive\\Desktop\\pbl"; 
    
    cout << "Loading documents from folder: " << corpusFolder << "...\n";
    
    corpusManager.loadDirectory(corpusFolder);
    vector<Document> allDocs = corpusManager.getDocuments();
    
    if (allDocs.empty()) {
        cout << "Error: No documents found.\n";
        return 1;
    }

    cout << "Building Inverted Index...\n";
    invertedIndex.buildIndex(allDocs);
    
    cout << "Initializing BM25 Ranker...\n";
    ranker.initialize(allDocs);
    
    cout << "System ready!\n\n";

    string userQuestion;
    int topK = 3; 

    while (true) {
        cout << "\nAsk a question (or type 'exit' to quit): ";
        
        if (!getline(cin, userQuestion)) {
            break;
        }
        if (userQuestion == "exit" || userQuestion == "quit") {
            cout << "Goodbye!\n";
            break;
        }
        if (userQuestion.empty()) continue;

        cout << "\nSearching for the best context...\n";
        vector<string> queryWords = tokenizeQuery(userQuestion);
        vector<int> topDocIds = ranker.getTopKDocuments(queryWords, invertedIndex, topK);

        if (topDocIds.empty()) {
            cout << "Couldn't find any relevant keywords in the corpus to answer that.\n";
            continue;
        }

        string contextText = "";
        for (int id : topDocIds) {
            for (Document& doc : allDocs) {
                if (doc.id == id) {
                    contextText += doc.originalText + "\n\n";
                    break;
                }
            }
        }

        cout << "Asking local AI...\n";
        string answer = ollama.askOllama(contextText, userQuestion);

        cout << "\n--- AI Answer ---\n";
        cout << answer << "\n";
        cout << "-----------------\n";
    }

    return 0;
}