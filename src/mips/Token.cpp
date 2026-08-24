#include "Token.h"

namespace MipsyncEngine::Mips {

const char* TokenTypeName(TokenType type) {
    switch (type) {
        case TokenType::EndOfFile: return "EOF";
        case TokenType::Invalid: return "Invalid";
        case TokenType::Identifier: return "Identifier";
        case TokenType::IntLiteral: return "IntLiteral";
        case TokenType::FloatLiteral: return "FloatLiteral";
        case TokenType::StringLiteral: return "StringLiteral";
        case TokenType::KwClass: return "class";
        case TokenType::KwPublic: return "public";
        case TokenType::KwVoid: return "void";
        case TokenType::KwIf: return "if";
        case TokenType::KwElse: return "else";
        case TokenType::KwWhile: return "while";
        case TokenType::KwFor: return "for";
        case TokenType::KwBreak: return "break";
        case TokenType::KwContinue: return "continue";
        case TokenType::KwReturn: return "return";
        case TokenType::KwTrue: return "true";
        case TokenType::KwFalse: return "false";
        case TokenType::KwNull: return "null";
        case TokenType::KwExtends: return "extends";
        case TokenType::KwVar: return "var";
        case TokenType::KwEnum: return "enum";
        case TokenType::KwYield: return "yield";
        case TokenType::KwNew: return "new";
        case TokenType::Plus: return "+";
        case TokenType::Minus: return "-";
        case TokenType::Star: return "*";
        case TokenType::Slash: return "/";
        case TokenType::Percent: return "%";
        case TokenType::Assign: return "=";
        case TokenType::EqualEqual: return "==";
        case TokenType::BangEqual: return "!=";
        case TokenType::Less: return "<";
        case TokenType::Greater: return ">";
        case TokenType::LessEqual: return "<=";
        case TokenType::GreaterEqual: return ">=";
        case TokenType::Bang: return "!";
        case TokenType::AndAnd: return "&&";
        case TokenType::OrOr: return "||";
        case TokenType::Dot: return ".";
        case TokenType::Comma: return ",";
        case TokenType::Semicolon: return ";";
        case TokenType::Colon: return ":";
        case TokenType::LParen: return "(";
        case TokenType::RParen: return ")";
        case TokenType::LBrace: return "{";
        case TokenType::RBrace: return "}";
        case TokenType::LBracket: return "[";
        case TokenType::RBracket: return "]";
        default: return "?";
    }
}

} // namespace MipsyncEngine::Mips
