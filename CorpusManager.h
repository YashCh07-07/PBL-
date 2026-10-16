#ifndef CORPUSMANAGER_H
#define CORPUSMANAGER_H

#include <string>
#include <vector>

struct Document {
    int id;
    std::string originalText;
    std::vector<std::string> tokens;
};

class CorpusManager {
private:
    std::vector<Document> documents;
    std::vector<std::string> tokenize(const std::string& text);

public:
    void loadDirectory(const std::string& directoryPath);
    std::vector<Document> getDocuments() const;
};

#endif