#pragma once

#include "Ast.h"
#include "Lexer.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips {

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string fileName = "<mips>");

    std::unique_ptr<Ast::Program> ParseProgram();
    const std::vector<std::string>& GetErrors() const { return m_Errors; }

private:
    const Token& Peek(int offset = 0) const;
    const Token& Previous() const;
    bool IsAtEnd() const;
    bool Check(TokenType type) const;
    bool Match(TokenType type);
    Token Advance();
    Token Consume(TokenType type, const char* message);
    void Error(const Token& token, const std::string& message);
    void Synchronize();

    std::unique_ptr<Ast::EnumDecl> ParseEnum();
    std::unique_ptr<Ast::ClassDecl> ParseClass();
    std::unique_ptr<Ast::FieldDecl> ParseField();
    std::unique_ptr<Ast::MethodDecl> ParseMethod();
    std::unique_ptr<Ast::BlockStmt> ParseBlock();
    std::unique_ptr<Ast::Stmt> ParseStatement();
    std::unique_ptr<Ast::Stmt> ParseVarDeclOrExprStatement();
    std::unique_ptr<Ast::Expr> ParseExpression();
    std::unique_ptr<Ast::Expr> ParseAssignment();
    std::unique_ptr<Ast::Expr> ParseLogicalOr();
    std::unique_ptr<Ast::Expr> ParseLogicalAnd();
    std::unique_ptr<Ast::Expr> ParseEquality();
    std::unique_ptr<Ast::Expr> ParseComparison();
    std::unique_ptr<Ast::Expr> ParseTerm();
    std::unique_ptr<Ast::Expr> ParseFactor();
    std::unique_ptr<Ast::Expr> ParseUnary();
    std::unique_ptr<Ast::Expr> ParseCallOrPrimary();
    std::unique_ptr<Ast::Expr> ParsePrimary();
    std::unique_ptr<Ast::Expr> FinishCall(std::unique_ptr<Ast::Expr> callee);
    std::string ParseTypeName(bool allowVoid = false);
    bool LooksLikeField() const;

    std::string m_FileName;
    std::vector<Token> m_Tokens;
    size_t m_Current = 0;
    std::vector<std::string> m_Errors;
};

} // namespace MipsyncEngine::Mips
