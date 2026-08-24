#pragma once

#include "Token.h"
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

class Lexer {
public:
    explicit Lexer(std::string source, std::string fileName = "<mips>");

    const std::vector<Token>& Tokenize();
    const std::vector<std::string>& GetErrors() const { return m_Errors; }

private:
    bool IsAtEnd() const;
    char Current() const;
    char Peek(int offset = 1) const;
    void Advance(int count = 1);
    void SkipWhitespaceAndComments();
    Token MakeToken(TokenType type, std::string lexeme, SourceLocation loc);
    Token IdentifierOrKeyword();
    Token Number();
    Token String();

    std::string m_Source;
    std::string m_FileName;
    int m_Index = 0;
    int m_Line = 1;
    int m_Column = 1;
    std::vector<Token> m_Tokens;
    std::vector<std::string> m_Errors;
};

} // namespace MipsyncEngine::Mips
