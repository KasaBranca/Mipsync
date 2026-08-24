#include "Lexer.h"
#include <cctype>
#include <unordered_map>

namespace MipsyncEngine::Mips {

namespace {

const std::unordered_map<std::string, TokenType> kKeywords = {
    { "class", TokenType::KwClass },
    { "public", TokenType::KwPublic },
    { "void", TokenType::KwVoid },
    { "if", TokenType::KwIf },
    { "else", TokenType::KwElse },
    { "while", TokenType::KwWhile },
    { "for", TokenType::KwFor },
    { "break", TokenType::KwBreak },
    { "continue", TokenType::KwContinue },
    { "return", TokenType::KwReturn },
    { "true", TokenType::KwTrue },
    { "false", TokenType::KwFalse },
    { "null", TokenType::KwNull },
    { "extends", TokenType::KwExtends },
    { "var", TokenType::KwVar },
    { "enum", TokenType::KwEnum },
    { "yield", TokenType::KwYield },
    { "new", TokenType::KwNew },
};

} // namespace

Lexer::Lexer(std::string source, std::string fileName)
    : m_Source(std::move(source)), m_FileName(std::move(fileName)) {}

bool Lexer::IsAtEnd() const {
    return m_Index >= static_cast<int>(m_Source.size());
}

char Lexer::Current() const {
    if (IsAtEnd()) return '\0';
    return m_Source[static_cast<size_t>(m_Index)];
}

char Lexer::Peek(int offset) const {
    const int idx = m_Index + offset;
    if (idx < 0 || idx >= static_cast<int>(m_Source.size()))
        return '\0';
    return m_Source[static_cast<size_t>(idx)];
}

void Lexer::Advance(int count) {
    for (int i = 0; i < count && !IsAtEnd(); ++i) {
        if (m_Source[static_cast<size_t>(m_Index)] == '\n') {
            ++m_Line;
            m_Column = 1;
        } else {
            ++m_Column;
        }
        ++m_Index;
    }
}

void Lexer::SkipWhitespaceAndComments() {
    while (!IsAtEnd()) {
        const char c = Current();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            Advance();
            continue;
        }
        if (c == '/' && Peek() == '/') {
            while (!IsAtEnd() && Current() != '\n')
                Advance();
            continue;
        }
        if (c == '/' && Peek() == '*') {
            Advance(2);
            while (!IsAtEnd() && !(Current() == '*' && Peek() == '/'))
                Advance();
            if (!IsAtEnd())
                Advance(2);
            continue;
        }
        break;
    }
}

Token Lexer::MakeToken(TokenType type, std::string lexeme, SourceLocation loc) {
    Token token;
    token.type = type;
    token.lexeme = std::move(lexeme);
    token.location = loc;
    return token;
}

Token Lexer::IdentifierOrKeyword() {
    const SourceLocation start{ m_Line, m_Column, m_Index };
    std::string text;
    while (!IsAtEnd() && (std::isalnum(static_cast<unsigned char>(Current())) || Current() == '_'))
        text += Current(), Advance();

    auto it = kKeywords.find(text);
    const TokenType type = (it != kKeywords.end()) ? it->second : TokenType::Identifier;
    return MakeToken(type, text, start);
}

Token Lexer::Number() {
    const SourceLocation start{ m_Line, m_Column, m_Index };
    std::string text;
    bool isFloat = false;

    while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Current())))
        text += Current(), Advance();

    if (Current() == '.' && std::isdigit(static_cast<unsigned char>(Peek()))) {
        isFloat = true;
        text += Current();
        Advance();
        while (!IsAtEnd() && std::isdigit(static_cast<unsigned char>(Current())))
            text += Current(), Advance();
    }

    Token token = MakeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral, text, start);
    try {
        if (isFloat)
            token.floatValue = std::stod(text);
        else
            token.intValue = std::stoll(text);
    } catch (...) {
        m_Errors.push_back(m_FileName + "(" + std::to_string(start.line) + "," +
                           std::to_string(start.column) + "): invalid numeric literal '" + text + "'");
        token.type = TokenType::Invalid;
    }
    return token;
}

Token Lexer::String() {
    const SourceLocation start{ m_Line, m_Column, m_Index };
    Advance(); // opening "
    std::string value;
    while (!IsAtEnd() && Current() != '"') {
        if (Current() == '\\') {
            Advance();
            if (IsAtEnd()) break;
            switch (Current()) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                default: value += Current(); break;
            }
            Advance();
        } else {
            value += Current();
            Advance();
        }
    }
    if (IsAtEnd() || Current() != '"') {
        m_Errors.push_back(m_FileName + "(" + std::to_string(start.line) + "," +
                           std::to_string(start.column) + "): unterminated string literal");
        return MakeToken(TokenType::Invalid, value, start);
    }
    Advance(); // closing "
    return MakeToken(TokenType::StringLiteral, value, start);
}

const std::vector<Token>& Lexer::Tokenize() {
    m_Tokens.clear();
    m_Errors.clear();

    while (!IsAtEnd()) {
        SkipWhitespaceAndComments();
        if (IsAtEnd())
            break;

        const SourceLocation start{ m_Line, m_Column, m_Index };
        const char c = Current();

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            m_Tokens.push_back(IdentifierOrKeyword());
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(c))) {
            m_Tokens.push_back(Number());
            continue;
        }
        if (c == '"') {
            m_Tokens.push_back(String());
            continue;
        }

        auto push = [&](TokenType type, const char* lex, int adv = 1) {
            m_Tokens.push_back(MakeToken(type, lex, start));
            Advance(adv);
        };

        switch (c) {
        case '+': push(TokenType::Plus, "+"); break;
        case '-': push(TokenType::Minus, "-"); break;
        case '*': push(TokenType::Star, "*"); break;
        case '/': push(TokenType::Slash, "/"); break;
        case '%': push(TokenType::Percent, "%"); break;
        case '(': push(TokenType::LParen, "("); break;
        case ')': push(TokenType::RParen, ")"); break;
        case '{': push(TokenType::LBrace, "{"); break;
        case '}': push(TokenType::RBrace, "}"); break;
        case '[': push(TokenType::LBracket, "["); break;
        case ']': push(TokenType::RBracket, "]"); break;
        case ',': push(TokenType::Comma, ","); break;
        case ';': push(TokenType::Semicolon, ";"); break;
        case ':': push(TokenType::Colon, ":"); break;
        case '.': push(TokenType::Dot, "."); break;
        case '!':
            if (Peek() == '=') push(TokenType::BangEqual, "!=", 2);
            else push(TokenType::Bang, "!");
            break;
        case '=':
            if (Peek() == '=') push(TokenType::EqualEqual, "==", 2);
            else push(TokenType::Assign, "=");
            break;
        case '<':
            if (Peek() == '=') push(TokenType::LessEqual, "<=", 2);
            else push(TokenType::Less, "<");
            break;
        case '>':
            if (Peek() == '=') push(TokenType::GreaterEqual, ">=", 2);
            else push(TokenType::Greater, ">");
            break;
        case '&':
            if (Peek() == '&') push(TokenType::AndAnd, "&&", 2);
            else goto invalid_char;
        case '|':
            if (Peek() == '|') push(TokenType::OrOr, "||", 2);
            else goto invalid_char;
        default:
        invalid_char:
            m_Errors.push_back(m_FileName + "(" + std::to_string(start.line) + "," +
                               std::to_string(start.column) + "): unexpected character '" +
                               std::string(1, c) + "'");
            m_Tokens.push_back(MakeToken(TokenType::Invalid, std::string(1, c), start));
            Advance();
            break;
        }
    }

    m_Tokens.push_back(MakeToken(TokenType::EndOfFile, "", SourceLocation{ m_Line, m_Column, m_Index }));
    return m_Tokens;
}

} // namespace MipsyncEngine::Mips
