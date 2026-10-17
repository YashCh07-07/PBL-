#include "BM25Ranker.h"
#include <cmath>
#include <queue>
#include <algorithm>

using namespace std;

void BM25Ranker::initialize(const vector<Document>& documents) {
    int totalWords = 0;
    for (const Document& doc : documents) {
        int docLength = doc.tokens.size();
        documentLengths[doc.id] = docLength;
        totalWords += docLength;
    }
    if (!documents.empty()) {
        averageDocumentLength = static_cast<double>(totalWords) / documents.size();
    }
}

vector<int> BM25Ranker::getTopKDocuments(const vector<string>& queryTokens, const InvertedIndex& index, int K) {
    unordered_map<int, double> documentScores;
    int totalDocs = index.getTotalDocuments();

    for (const string& word : queryTokens) {
        vector<DocumentNode> docsWithWord = index.getDocumentsForWord(word);
        if (docsWithWord.empty()) continue;
        
        double docFreq = docsWithWord.size();
        double idf = log(((totalDocs - docFreq + 0.5) / (docFreq + 0.5)) + 1.0);
        
        for (const DocumentNode& node : docsWithWord) {
            int tf = node.termFrequency;
            int docLength = documentLengths[node.docId];
            
            double lengthNormalization = 1.0 - b + b * (static_cast<double>(docLength) / averageDocumentLength);
            double termScore = idf * ((tf * (k1 + 1.0)) / (tf + k1 * lengthNormalization));
            
            documentScores[node.docId] += termScore;
        }
    }

    priority_queue<DocScore, vector<DocScore>, greater<DocScore>> minHeap;

    for (const auto& pair : documentScores) {
        DocScore ds;
        ds.docId = pair.first;
        ds.score = pair.second;
        minHeap.push(ds);
        if (minHeap.size() > K) minHeap.pop(); 
    }

    vector<int> topK_IDs;
    while (!minHeap.empty()) {
        topK_IDs.push_back(minHeap.top().docId);
        minHeap.pop();
    }
    
    reverse(topK_IDs.begin(), topK_IDs.end());
    return topK_IDs;
}