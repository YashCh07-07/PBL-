#ifndef OLLAMABRIDGE_H
#define OLLAMABRIDGE_H

#include <string>

class OllamaBridge {
private:
    std::string escapeForJson(const std::string& input);

public:
    std::string askOllama(const std::string& context, const std::string& question);
};

#endif