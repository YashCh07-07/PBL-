#ifndef INVERTEDINDEX_H
#define INVERTEDINDEX_H

#include <string>
#include <vector>
#include <unordered_map>
#include "CorpusManager.h"

struct DocumentNode {
    int docId;
    int termFrequency;
};

class InvertedIndex {
private:
    std::unordered_map<std::string, std::vector<DocumentNode>> index;
    int totalDocuments = 0;

public:
    void buildIndex(const std::vector<Document>& documents);
    std::vector<DocumentNode> getDocumentsForWord(const std::string& word) const;
    int getTotalDocuments() const;
};

#endif
