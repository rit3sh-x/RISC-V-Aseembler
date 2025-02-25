#ifndef LEXER_HPP
#define LEXER_HPP
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <cctype>
#include <algorithm>
#include "types.hpp"

using namespace riscv;

struct Token {
    TokenType type;
    std::string value;
    int lineNumber;
    Token(TokenType t, const std::string& v, int ln) : type(t), value(v), lineNumber(ln) {}
};

class Lexer {
public:
    static std::vector<std::vector<Token>> Tokenizer(const std::string& filename);
    static std::string getTokenTypeName(TokenType type);

private:
    static std::vector<Token> tokenizeLine(const std::string& line, int lineNumber);
    static bool isRegister(const std::string& token);
    static bool isImmediate(const std::string& token);
    static bool isDirective(const std::string& token);
    static bool isLabel(const std::string& token);
    static bool isMemory(const std::string& token, std::string& offset, std::string& reg);
    static std::string tokenTypeToString(TokenType type);
    static Token classifyToken(const std::string& token, int lineNumber);
    static std::string trim(const std::string& str);
};

std::string Lexer::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

std::string Lexer::tokenTypeToString(TokenType type) {
    switch (type) {
        case TokenType::OPCODE: return "OPCODE";
        case TokenType::REGISTER: return "REGISTER";
        case TokenType::IMMEDIATE: return "IMMEDIATE";
        case TokenType::MEMORY: return "MEMORY";
        case TokenType::LABEL: return "LABEL";
        case TokenType::DIRECTIVE: return "DIRECTIVE";
        case TokenType::STRING: return "STRING";
        case TokenType::ERROR: return "ERROR";
        case TokenType::STANDALONE: return "STANDALONE";
        case TokenType::UNKNOWN: return "UNKNOWN";
        default: return "INVALID";
    }
}

std::string Lexer::getTokenTypeName(TokenType type) {
    return tokenTypeToString(type);
}

bool Lexer::isRegister(const std::string& token) {
    std::string lowerToken = token;
    std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(), ::tolower);
    if (validRegisters.find(lowerToken) != validRegisters.end()) {
        return true;
    }
    if (token.length() > 1 && token[0] == 'x') {
        std::string numStr = token.substr(1);
        if (numStr.empty() || !std::all_of(numStr.begin(), numStr.end(), ::isdigit)) {
            return false;
        }
        try {
            int regNum = std::stoi(numStr);
            return regNum >= 0 && regNum <= 31;
        } catch (const std::exception&) {
            return false;
        }
    }
    return false;
}

bool Lexer::isImmediate(const std::string& token) {
    if (token.empty()) return false;
    size_t pos = 0;
    bool isHex = false;
    if (token[0] == '-') pos = 1;
    if (token.length() > pos + 2 && token[pos] == '0' && ::tolower(token[pos + 1]) == 'x') {
        isHex = true;
        pos += 2;
    }
    if (pos >= token.length()) return false;
    return isHex ? std::all_of(token.begin() + pos, token.end(), ::isxdigit) : std::all_of(token.begin() + pos, token.end(), ::isdigit);
}

bool Lexer::isDirective(const std::string& token) {
    std::string lowerToken = token;
    std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(), ::tolower);
    return directives.count(lowerToken) > 0;
}

bool Lexer::isLabel(const std::string& token) {
    return !token.empty() && token.back() == ':' && std::all_of(token.begin(), token.end() - 1, [](char c) { return std::isalnum(c) || c == '_' || c == '.'; });
}

bool Lexer::isMemory(const std::string& token, std::string& offset, std::string& reg) {
    size_t open = token.find('(');
    size_t close = token.find(')');
    if (open == std::string::npos || close == std::string::npos || close < open) return false;
    offset = trim(token.substr(0, open));
    reg = trim(token.substr(open + 1, close - open - 1));
    return (!offset.empty() && isImmediate(offset)) && !reg.empty() && isRegister(reg);
}

Token Lexer::classifyToken(const std::string& token, int lineNumber) {
    std::string trimmed = trim(token);
    if (trimmed.empty()) return {TokenType::UNKNOWN, "", lineNumber};

    std::string offset, reg;
    if (isMemory(trimmed, offset, reg)) {
        return {TokenType::MEMORY, trimmed, lineNumber};
    }
    if (standaloneOpcodes.count(trimmed)) {
        return {TokenType::STANDALONE, trimmed, lineNumber};
    }
    if (opcodes.count(trimmed)) {
        return {TokenType::OPCODE, trimmed, lineNumber};
    }
    if (isDirective(trimmed)) {
        return {TokenType::DIRECTIVE, trimmed, lineNumber};
    }
    if (isRegister(trimmed)) {
        return {TokenType::REGISTER, trimmed, lineNumber};
    }
    if (isImmediate(trimmed)) {
        return {TokenType::IMMEDIATE, trimmed, lineNumber};
    }
    if (isLabel(trimmed)) {
        std::string labelName = trimmed.substr(0, trimmed.length() - 1);
        definedLabels.insert(labelName);
        return {TokenType::LABEL, labelName, lineNumber};
    }
    if (definedLabels.count(trimmed)) {
        return {TokenType::LABEL, trimmed, lineNumber};
    }
    return {TokenType::UNKNOWN, trimmed, lineNumber};
}

std::vector<Token> Lexer::tokenizeLine(const std::string& line, int lineNumber) {
    std::vector<Token> tokens;
    std::string token;
    bool inString = false;
    bool expectOperand = false;
    bool expectValueAfterComma = false;
    bool lastWasOpcode = false;

    std::string trimmedLine = trim(line);
    if (trimmedLine.empty()) return tokens;

    size_t i = 0;
    while (i < trimmedLine.length()) {
        char c = trimmedLine[i];

        if (!inString && (c == '#' || (c == '/' && i + 1 < trimmedLine.length() && trimmedLine[i + 1] == '/'))) {
            if (!token.empty()) {
                Token t = classifyToken(token, lineNumber);
                tokens.push_back(t);
                lastWasOpcode = (t.type == TokenType::OPCODE || t.type == TokenType::STANDALONE);
                if (t.type == TokenType::OPCODE) expectOperand = true;
                else if (expectOperand && (t.type == TokenType::REGISTER || t.type == TokenType::IMMEDIATE || t.type == TokenType::MEMORY)) expectOperand = false;
                token.clear();
            }
            break;
        }

        if (c == '"') {
            if (!inString) {
                inString = true;
                if (!token.empty()) {
                    Token t = classifyToken(token, lineNumber);
                    tokens.push_back(t);
                    lastWasOpcode = (t.type == TokenType::OPCODE || t.type == TokenType::STANDALONE);
                    if (t.type == TokenType::OPCODE) expectOperand = true;
                    if (lastWasOpcode && i + 1 < trimmedLine.length() && trimmedLine[i + 1] == ',') {
                        tokens.push_back({TokenType::ERROR, "Unexpected comma after opcode", lineNumber});
                        lastWasOpcode = false;
                        expectOperand = false;
                        i++;
                    }
                    token.clear();
                }
            } else {
                inString = false;
                tokens.push_back({TokenType::STRING, token, lineNumber});
                lastWasOpcode = false;
                token.clear();
            }
        }
        else if (inString) {
            token += c;
        }
        else if (std::isspace(c)) {
            if (!token.empty()) {
                Token t = classifyToken(token, lineNumber);
                tokens.push_back(t);
                lastWasOpcode = (t.type == TokenType::OPCODE || t.type == TokenType::STANDALONE);
                if (t.type == TokenType::OPCODE) expectOperand = true;
                else if (expectOperand && (t.type == TokenType::REGISTER || t.type == TokenType::IMMEDIATE || t.type == TokenType::MEMORY)) expectOperand = false;
                token.clear();
                expectValueAfterComma = false;
                if (lastWasOpcode && i + 1 < trimmedLine.length() && trimmedLine[i + 1] == ',') {
                    tokens.push_back({TokenType::ERROR, "Unexpected comma after opcode", lineNumber});
                    lastWasOpcode = false;
                    expectOperand = false;
                    i++;
                }
            }
        }
        else if (c == ',') {
            if (!token.empty()) {
                Token t = classifyToken(token, lineNumber);
                tokens.push_back(t);
                lastWasOpcode = (t.type == TokenType::OPCODE || t.type == TokenType::STANDALONE);
                if (t.type == TokenType::OPCODE) expectOperand = true;
                else if (expectOperand && (t.type == TokenType::REGISTER || t.type == TokenType::IMMEDIATE || t.type == TokenType::MEMORY)) expectOperand = false;
                token.clear();
                if (lastWasOpcode && i + 1 < trimmedLine.length() && trimmedLine[i + 1] == ',') {
                    tokens.push_back({TokenType::ERROR, "Unexpected comma after opcode", lineNumber});
                    lastWasOpcode = false;
                    expectOperand = false;
                    i++;
                }
            }
            if (lastWasOpcode && tokens.back().type != TokenType::ERROR) {
                tokens.push_back({TokenType::ERROR, "Unexpected comma after opcode", lineNumber});
                lastWasOpcode = false;
                expectOperand = false;
                expectValueAfterComma = false;
            } else {
                expectValueAfterComma = true;
            }
        }
        else {
            token += c;
        }
        ++i;
    }

    if (!token.empty()) {
        Token t = classifyToken(token, lineNumber);
        tokens.push_back(t);
        lastWasOpcode = (t.type == TokenType::OPCODE || t.type == TokenType::STANDALONE);
        if (t.type == TokenType::OPCODE) expectOperand = true;
        else if (expectOperand && (t.type == TokenType::REGISTER || t.type == TokenType::IMMEDIATE || t.type == TokenType::MEMORY)) expectOperand = false;
        expectValueAfterComma = false;
    }

    if (inString) {
        tokens.push_back({TokenType::ERROR, "Unterminated string: \"" + token + "\"", lineNumber});
    }
    if (expectOperand && !tokens.empty() && tokens.back().type != TokenType::ERROR) {
        tokens.push_back({TokenType::ERROR, "Missing operand after opcode", lineNumber});
    }
    if (expectValueAfterComma && !tokens.empty() && tokens.back().type != TokenType::ERROR) {
        tokens.push_back({TokenType::ERROR, "Missing operand after comma", lineNumber});
    }

    return tokens;
}

std::vector<std::vector<Token>> Lexer::Tokenizer(const std::string& filename) {
    std::vector<std::vector<Token>> tokenizedLines;
    std::vector<Token*> unknownTokens;
    std::ifstream inputFile(filename);
    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open file '" << filename << "'\n";
        return tokenizedLines;
    }

    std::string line;
    int lineNumber = 0;

    while (std::getline(inputFile, line)) {
        ++lineNumber;
        std::vector<Token> tokens = tokenizeLine(line, lineNumber);
        if (!tokens.empty()) {
            tokenizedLines.push_back(tokens);
            for (Token& token : tokenizedLines.back()) {
                if (token.type == TokenType::ERROR) {
                    std::cout << "Error on line " << token.lineNumber << ": " << token.value << std::endl;
                    inputFile.close();
                    return tokenizedLines;
                }
                else if (token.type == TokenType::UNKNOWN) {
                    unknownTokens.push_back(&token);
                }
            }
        }
    }
    inputFile.close();

    for (Token* token : unknownTokens) {
        if (definedLabels.count(token->value)) {
            token->type = TokenType::LABEL;
        }
    }

    return tokenizedLines;
}

#endif