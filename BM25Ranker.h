#ifndef BM25RANKER_H
#define BM25RANKER_H

#include <vector>
#include <string>
#include <unordered_map>
#include "CorpusManager.h"
#include "InvertedIndex.h"

struct DocScore {
    int docId;
    double score;

    bool operator>(const DocScore& other) const {
        return score > other.score;
    }
};

class BM25Ranker {
private:
    const double k1 = 1.5; 
    const double b = 0.75;
    double averageDocumentLength = 0.0;
    std::unordered_map<int, int> documentLengths;

public:
    void initialize(const std::vector<Document>& documents);
    std::vector<int> getTopKDocuments(const std::vector<std::string>& queryTokens, const InvertedIndex& index, int K);
};

#endif