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
    int docIdCounter = 0;
    std::vector<std::string> tokenize(const std::string& text);
    std::string extractTextFromPDF(const std::string& filePath);
    void parseAndStoreText(const std::string& text);

public:
    void loadDirectory(const std::string& directoryPath);
    void loadFile(const std::string& filePath);
    std::vector<Document> getDocuments() const;
};

#endif