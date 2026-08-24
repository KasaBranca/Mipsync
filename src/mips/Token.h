#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace MipsyncEngine::Mips {

enum class TokenType {
    EndOfFile,
    Invalid,

    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,

    // Keywords
    KwClass,
    KwPublic,
    KwVoid,
    KwIf,
    KwElse,
    KwWhile,
    KwFor,
    KwBreak,
    KwContinue,
    KwReturn,
    KwTrue,
    KwFalse,
    KwNull,
    KwExtends,
    KwVar,
    KwEnum,
    KwYield,
    KwNew,

    // Operators / punctuation
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Assign,
    EqualEqual,
    BangEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,
    Bang,
    AndAnd,
    OrOr,
    Dot,
    Comma,
    Semicolon,
    Colon,
    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    LessLess, // not used v0.1
    GreaterGreater,
};

struct SourceLocation {
    int line = 1;
    int column = 1;
    int offset = 0;
};

struct Token {
    TokenType type = TokenType::EndOfFile;
    std::string lexeme;
    SourceLocation location;

    double floatValue = 0.0;
    int64_t intValue = 0;
};

const char* TokenTypeName(TokenType type);

} // namespace MipsyncEngine::Mips
