#include "InvertedIndex.h"

using namespace std;

void InvertedIndex::buildIndex(const vector<Document>& documents) {
    totalDocuments = documents.size();
    
    for (const Document& doc : documents) {
        unordered_map<string, int> wordCounts;
        
        for (const string& word : doc.tokens) {
            wordCounts[word]++;
        }
        
        for (const auto& pair : wordCounts) {
            DocumentNode node;
            node.docId = doc.id;
            node.termFrequency = pair.second;
            index[pair.first].push_back(node);
        }
    }
}

vector<DocumentNode> InvertedIndex::getDocumentsForWord(const string& word) const {
    auto it = index.find(word);
    if (it != index.end()) {
        return it->second; 
    }
    return vector<DocumentNode>();
}

int InvertedIndex::getTotalDocuments() const {
    return totalDocuments;
}
