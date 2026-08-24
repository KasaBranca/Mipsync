#include "Parser.h"
#include "MipsSpec.h"
#include <utility>

namespace MipsyncEngine::Mips {

Parser::Parser(std::vector<Token> tokens, std::string fileName)
    : m_FileName(std::move(fileName)), m_Tokens(std::move(tokens)) {}

const Token& Parser::Peek(int offset) const {
    const size_t index = m_Current + static_cast<size_t>(offset);
    if (index >= m_Tokens.size())
        return m_Tokens.back();
    return m_Tokens[index];
}

const Token& Parser::Previous() const {
    return m_Tokens[m_Current > 0 ? m_Current - 1 : 0];
}

bool Parser::IsAtEnd() const {
    return Peek().type == TokenType::EndOfFile;
}

bool Parser::Check(TokenType type) const {
    if (IsAtEnd()) return false;
    return Peek().type == type;
}

bool Parser::Match(TokenType type) {
    if (!Check(type)) return false;
    Advance();
    return true;
}

Token Parser::Advance() {
    if (!IsAtEnd())
        ++m_Current;
    return Previous();
}

Token Parser::Consume(TokenType type, const char* message) {
    if (Check(type))
        return Advance();
    Error(Peek(), message);
    return Peek();
}

void Parser::Error(const Token& token, const std::string& message) {
    m_Errors.push_back(m_FileName + "(" + std::to_string(token.location.line) + "," +
                       std::to_string(token.location.column) + "): " + message);
}

void Parser::Synchronize() {
    Advance();
    while (!IsAtEnd()) {
        switch (Peek().type) {
            case TokenType::KwClass:
            case TokenType::KwVoid:
            case TokenType::KwIf:
            case TokenType::KwWhile:
            case TokenType::KwFor:
            case TokenType::KwReturn:
            case TokenType::KwPublic:
                return;
            default:
                Advance();
                break;
        }
    }
}

std::unique_ptr<Ast::EnumDecl> Parser::ParseEnum() {
    const Token enumToken = Consume(TokenType::KwEnum, "expected 'enum'");
    const Token nameToken = Consume(TokenType::Identifier, "expected enum name");

    auto decl = std::make_unique<Ast::EnumDecl>();
    decl->location = enumToken.location;
    decl->name = nameToken.lexeme;
    Consume(TokenType::LBrace, "expected '{' after enum name");

    int64_t nextValue = 0;
    while (!Check(TokenType::RBrace) && !IsAtEnd()) {
        const Token memberToken = Consume(TokenType::Identifier, "expected enum member name");
        Ast::EnumMember member;
        member.name = memberToken.lexeme;
        member.value = nextValue;
        if (Match(TokenType::Assign)) {
            if (!Check(TokenType::IntLiteral)) {
                Error(Peek(), "expected integer value after '=' in enum");
            } else {
                member.value = Advance().intValue;
                nextValue = member.value + 1;
            }
        } else {
            nextValue = member.value + 1;
        }
        decl->members.push_back(std::move(member));
        if (!Check(TokenType::RBrace))
            Consume(TokenType::Comma, "expected ',' between enum members");
    }
    Consume(TokenType::RBrace, "expected '}' after enum");
    Consume(TokenType::Semicolon, "expected ';' after enum");
    return decl;
}

std::unique_ptr<Ast::Program> Parser::ParseProgram() {
    auto program = std::make_unique<Ast::Program>();
    while (!IsAtEnd()) {
        if (Check(TokenType::KwEnum)) {
            program->enums.push_back(ParseEnum());
        } else if (Check(TokenType::KwClass)) {
            program->classes.push_back(ParseClass());
        } else {
            Error(Peek(), "expected top-level 'enum' or 'class'");
            Synchronize();
        }
    }
    return program;
}

std::unique_ptr<Ast::ClassDecl> Parser::ParseClass() {
    const Token classToken = Consume(TokenType::KwClass, "expected 'class'");
    if (!Check(TokenType::Identifier)) {
        Error(Peek(), "expected class name");
        return nullptr;
    }
    const Token nameToken = Advance();

    auto decl = std::make_unique<Ast::ClassDecl>();
    decl->location = classToken.location;
    decl->name = nameToken.lexeme;
    decl->baseName = kBaseClassName;

    if (Match(TokenType::Colon)) {
        if (!Check(TokenType::Identifier)) {
            Error(Peek(), "expected base class name after ':'");
        } else {
            decl->baseName = Advance().lexeme;
        }
    }

    Consume(TokenType::LBrace, "expected '{' after class declaration");

    while (!Check(TokenType::RBrace) && !IsAtEnd()) {
        if (Check(TokenType::KwPublic)) {
            Advance();
            if (LooksLikeField()) {
                decl->fields.push_back(ParseField());
            } else {
                decl->methods.push_back(ParseMethod());
            }
        } else if (Check(TokenType::KwVoid)) {
            decl->methods.push_back(ParseMethod());
        } else {
            Error(Peek(), "expected 'public' field or method");
            Synchronize();
        }
    }

    Consume(TokenType::RBrace, "expected '}' after class body");
    return decl;
}

std::string Parser::ParseTypeName(bool allowVoid) {
    Token token;
    if (allowVoid && Match(TokenType::KwVoid))
        token = Previous();
    else
        token = Consume(TokenType::Identifier, "expected type name");
    std::string type = token.lexeme;
    if (Match(TokenType::LBracket)) {
        Consume(TokenType::RBracket, "expected ']' in array type");
        type += "[]";
    }
    return type;
}

bool Parser::LooksLikeField() const {
    if (!Check(TokenType::Identifier))
        return false;
    int offset = 1;
    if (Peek(offset).type == TokenType::LBracket && Peek(offset + 1).type == TokenType::RBracket)
        offset += 2;
    if (Peek(offset).type != TokenType::Identifier)
        return false;
    return Peek(offset + 1).type != TokenType::LParen;
}

std::unique_ptr<Ast::FieldDecl> Parser::ParseField() {
    const SourceLocation typeLocation = Peek().location;
    const std::string typeName = ParseTypeName();
    const Token nameToken = Consume(TokenType::Identifier, "expected field name");

    auto field = std::make_unique<Ast::FieldDecl>();
    field->location = typeLocation;
    field->typeName = typeName;
    field->name = nameToken.lexeme;

    if (Match(TokenType::LBrace)) {
        field->isAutoProperty = true;
        while (!Check(TokenType::RBrace) && !IsAtEnd()) {
            if (!Check(TokenType::Identifier)) {
                Error(Peek(), "expected 'get' or 'set' in property");
                break;
            }
            const std::string kw = Advance().lexeme;
            if (kw == "get")
                field->hasGetter = true;
            else if (kw == "set")
                field->hasSetter = true;
            else
                Error(Peek(), "expected 'get' or 'set' in property");
            Consume(TokenType::Semicolon, "expected ';' after property accessor");
        }
        Consume(TokenType::RBrace, "expected '}' after property accessors");
        if (!field->hasGetter && !field->hasSetter) {
            field->hasGetter = true;
            field->hasSetter = true;
        }
        Consume(TokenType::Semicolon, "expected ';' after property");
        return field;
    }

    if (Match(TokenType::Assign))
        field->initializer = ParseExpression();

    Consume(TokenType::Semicolon, "expected ';' after field");
    return field;
}

std::unique_ptr<Ast::MethodDecl> Parser::ParseMethod() {
    if (!Check(TokenType::KwVoid) && !Check(TokenType::Identifier)) {
        Error(Peek(), "expected return type or 'void'");
        return nullptr;
    }

    const std::string returnType = ParseTypeName(true);
    const Token nameToken = Consume(TokenType::Identifier, "expected method name");

    auto method = std::make_unique<Ast::MethodDecl>();
    method->location = nameToken.location;
    method->name = nameToken.lexeme;
    method->returnType = returnType;

    Consume(TokenType::LParen, "expected '(' after method name");
    if (!Check(TokenType::RParen)) {
        do {
            const std::string paramType = ParseTypeName();
            const Token paramName = Consume(TokenType::Identifier, "expected parameter name");
            method->parameters.emplace_back(paramType, paramName.lexeme);
        } while (Match(TokenType::Comma));
    }
    Consume(TokenType::RParen, "expected ')' after parameters");
    method->body = ParseBlock();
    return method;
}

std::unique_ptr<Ast::BlockStmt> Parser::ParseBlock() {
    const Token brace = Consume(TokenType::LBrace, "expected '{'");
    auto block = std::make_unique<Ast::BlockStmt>();
    block->location = brace.location;

    while (!Check(TokenType::RBrace) && !IsAtEnd()) {
        block->statements.push_back(ParseStatement());
    }

    Consume(TokenType::RBrace, "expected '}'");
    return block;
}

std::unique_ptr<Ast::Stmt> Parser::ParseStatement() {
    if (Check(TokenType::LBrace))
        return ParseVarDeclOrExprStatement();

    if (Match(TokenType::KwIf)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::If;
        stmt->location = Previous().location;
        stmt->ifStmt = std::make_unique<Ast::IfStmt>();
        stmt->ifStmt->location = stmt->location;

        Consume(TokenType::LParen, "expected '(' after 'if'");
        stmt->ifStmt->condition = ParseExpression();
        Consume(TokenType::RParen, "expected ')' after if condition");
        stmt->ifStmt->thenBranch = ParseStatement();
        if (Match(TokenType::KwElse))
            stmt->ifStmt->elseBranch = ParseStatement();
        return stmt;
    }

    if (Match(TokenType::KwWhile)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::While;
        stmt->location = Previous().location;
        stmt->whileStmt = std::make_unique<Ast::WhileStmt>();
        stmt->whileStmt->location = stmt->location;

        Consume(TokenType::LParen, "expected '(' after 'while'");
        stmt->whileStmt->condition = ParseExpression();
        Consume(TokenType::RParen, "expected ')' after while condition");
        stmt->whileStmt->body = ParseStatement();
        return stmt;
    }

    if (Match(TokenType::KwFor)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::For;
        stmt->location = Previous().location;
        stmt->forStmt = std::make_unique<Ast::ForStmt>();
        stmt->forStmt->location = stmt->location;

        Consume(TokenType::LParen, "expected '(' after 'for'");
        if (!Check(TokenType::Semicolon))
            stmt->forStmt->initializer = ParseVarDeclOrExprStatement();
        else
            Advance();

        if (!Check(TokenType::Semicolon))
            stmt->forStmt->condition = ParseExpression();
        Consume(TokenType::Semicolon, "expected ';' in for-loop");

        if (!Check(TokenType::RParen))
            stmt->forStmt->increment = ParseExpression();
        Consume(TokenType::RParen, "expected ')' after for clauses");
        stmt->forStmt->body = ParseStatement();
        return stmt;
    }

    if (Match(TokenType::KwBreak)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::Break;
        stmt->location = Previous().location;
        Consume(TokenType::Semicolon, "expected ';' after break");
        return stmt;
    }

    if (Match(TokenType::KwContinue)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::Continue;
        stmt->location = Previous().location;
        Consume(TokenType::Semicolon, "expected ';' after continue");
        return stmt;
    }

    if (Match(TokenType::KwReturn)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::Return;
        stmt->location = Previous().location;
        stmt->returnStmt = std::make_unique<Ast::ReturnStmt>();
        stmt->returnStmt->location = stmt->location;
        if (!Check(TokenType::Semicolon))
            stmt->returnStmt->value = ParseExpression();
        Consume(TokenType::Semicolon, "expected ';' after return");
        return stmt;
    }

    if (Match(TokenType::KwYield)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::Yield;
        stmt->location = Previous().location;
        stmt->yieldStmt = std::make_unique<Ast::YieldStmt>();
        stmt->yieldStmt->location = stmt->location;
        if (Match(TokenType::KwBreak)) {
            stmt->yieldStmt->isBreak = true;
        } else {
            Consume(TokenType::KwReturn, "expected 'return' or 'break' after yield");
            stmt->yieldStmt->value = ParseExpression();
        }
        Consume(TokenType::Semicolon, "expected ';' after yield");
        return stmt;
    }

    return ParseVarDeclOrExprStatement();
}

std::unique_ptr<Ast::Stmt> Parser::ParseVarDeclOrExprStatement() {
    if (Check(TokenType::LBrace)) {
        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::Block;
        stmt->location = Peek().location;
        stmt->block = ParseBlock();
        return stmt;
    }

    // Local variable: var name (= expr)?  OR  Type name (= expr)?;
    const bool typedArrayLocal = Check(TokenType::Identifier) &&
        Peek(1).type == TokenType::LBracket && Peek(2).type == TokenType::RBracket &&
        Peek(3).type == TokenType::Identifier;
    if (Check(TokenType::KwVar) || typedArrayLocal ||
        (Check(TokenType::Identifier) && Peek(1).type == TokenType::Identifier)) {
        std::string typeName;
        if (Match(TokenType::KwVar))
            typeName = "var";
        else
            typeName = ParseTypeName();

        const Token nameToken = Consume(TokenType::Identifier, "expected variable name");

        auto stmt = std::make_unique<Ast::Stmt>();
        stmt->kind = Ast::Stmt::Kind::VarDecl;
        stmt->location = nameToken.location;
        stmt->varDecl = std::make_unique<Ast::VarDeclStmt>();
        stmt->varDecl->location = nameToken.location;
        stmt->varDecl->typeName = typeName;
        stmt->varDecl->name = nameToken.lexeme;

        if (Match(TokenType::Assign))
            stmt->varDecl->initializer = ParseExpression();
        Consume(TokenType::Semicolon, "expected ';' after local declaration");
        return stmt;
    }

    auto stmt = std::make_unique<Ast::Stmt>();
    stmt->kind = Ast::Stmt::Kind::Expr;
    stmt->location = Peek().location;
    stmt->exprStmt = std::make_unique<Ast::ExprStmt>();
    stmt->exprStmt->location = stmt->location;
    stmt->exprStmt->expression = ParseExpression();
    Consume(TokenType::Semicolon, "expected ';' after expression");
    return stmt;
}

std::unique_ptr<Ast::Expr> Parser::ParseExpression() {
    return ParseAssignment();
}

std::unique_ptr<Ast::Expr> Parser::ParseAssignment() {
    auto expr = ParseLogicalOr();
    if (Match(TokenType::Assign)) {
        const Token op = Previous();
        auto right = ParseAssignment();
        auto binary = std::make_unique<Ast::Expr>();
        binary->kind = Ast::Expr::Kind::Binary;
        binary->location = op.location;
        binary->binary = std::make_unique<Ast::BinaryExpr>();
        binary->binary->op = Ast::BinaryOp::Assign;
        binary->binary->left = std::move(expr);
        binary->binary->right = std::move(right);
        return binary;
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseLogicalOr() {
    auto expr = ParseLogicalAnd();
    while (Match(TokenType::OrOr)) {
        const Token op = Previous();
        auto right = ParseLogicalAnd();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        node->binary->op = Ast::BinaryOp::Or;
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseLogicalAnd() {
    auto expr = ParseEquality();
    while (Match(TokenType::AndAnd)) {
        const Token op = Previous();
        auto right = ParseEquality();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        node->binary->op = Ast::BinaryOp::And;
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseEquality() {
    auto expr = ParseComparison();
    while (Check(TokenType::EqualEqual) || Check(TokenType::BangEqual)) {
        const Token op = Advance();
        auto right = ParseComparison();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        node->binary->op = (op.type == TokenType::EqualEqual) ? Ast::BinaryOp::Eq : Ast::BinaryOp::NotEq;
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseComparison() {
    auto expr = ParseTerm();
    while (Check(TokenType::Less) || Check(TokenType::Greater) ||
           Check(TokenType::LessEqual) || Check(TokenType::GreaterEqual)) {
        const Token op = Advance();
        auto right = ParseTerm();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        switch (op.type) {
            case TokenType::Less: node->binary->op = Ast::BinaryOp::Less; break;
            case TokenType::Greater: node->binary->op = Ast::BinaryOp::Greater; break;
            case TokenType::LessEqual: node->binary->op = Ast::BinaryOp::LessEq; break;
            default: node->binary->op = Ast::BinaryOp::GreaterEq; break;
        }
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseTerm() {
    auto expr = ParseFactor();
    while (Check(TokenType::Plus) || Check(TokenType::Minus)) {
        const Token op = Advance();
        auto right = ParseFactor();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        node->binary->op = (op.type == TokenType::Plus) ? Ast::BinaryOp::Add : Ast::BinaryOp::Sub;
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseFactor() {
    auto expr = ParseUnary();
    while (Check(TokenType::Star) || Check(TokenType::Slash) || Check(TokenType::Percent)) {
        const Token op = Advance();
        auto right = ParseUnary();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Binary;
        node->location = op.location;
        node->binary = std::make_unique<Ast::BinaryExpr>();
        if (op.type == TokenType::Star) node->binary->op = Ast::BinaryOp::Mul;
        else if (op.type == TokenType::Slash) node->binary->op = Ast::BinaryOp::Div;
        else node->binary->op = Ast::BinaryOp::Mod;
        node->binary->left = std::move(expr);
        node->binary->right = std::move(right);
        expr = std::move(node);
    }
    return expr;
}

std::unique_ptr<Ast::Expr> Parser::ParseUnary() {
    if (Match(TokenType::Bang)) {
        const Token op = Previous();
        auto operand = ParseUnary();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Unary;
        node->location = op.location;
        node->unary = std::make_unique<Ast::UnaryExpr>();
        node->unary->op = Ast::UnaryOp::Not;
        node->unary->operand = std::move(operand);
        return node;
    }
    if (Match(TokenType::Minus)) {
        const Token op = Previous();
        auto operand = ParseUnary();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Unary;
        node->location = op.location;
        node->unary = std::make_unique<Ast::UnaryExpr>();
        node->unary->op = Ast::UnaryOp::Neg;
        node->unary->operand = std::move(operand);
        return node;
    }
    return ParseCallOrPrimary();
}

std::unique_ptr<Ast::Expr> Parser::ParseCallOrPrimary() {
    auto expr = ParsePrimary();

    while (true) {
        if (Match(TokenType::Dot)) {
            const Token member = Consume(TokenType::Identifier, "expected member name after '.'");
            auto node = std::make_unique<Ast::Expr>();
            node->kind = Ast::Expr::Kind::Member;
            node->location = member.location;
            node->member = std::make_unique<Ast::MemberExpr>();
            node->member->object = std::move(expr);
            node->member->member = member.lexeme;
            expr = std::move(node);
        } else if (Match(TokenType::LParen)) {
            expr = FinishCall(std::move(expr));
        } else if (Match(TokenType::LBracket)) {
            auto node = std::make_unique<Ast::Expr>();
            node->kind = Ast::Expr::Kind::Index;
            node->location = Previous().location;
            node->index = std::make_unique<Ast::IndexExpr>();
            node->index->object = std::move(expr);
            node->index->index = ParseExpression();
            Consume(TokenType::RBracket, "expected ']' after index");
            expr = std::move(node);
        } else if (Check(TokenType::Less) && expr->kind == Ast::Expr::Kind::Identifier &&
                   Peek(1).type == TokenType::Identifier && Peek(2).type == TokenType::Greater &&
                   Peek(3).type == TokenType::LParen) {
            // GetComponent<Rotator>() — must not steal comparison like `speed < 0.0`
            const std::string calleeName = expr->identifier->name;
            Advance(); // <
            const Token typeArg = Consume(TokenType::Identifier, "expected type name inside <>");
            Consume(TokenType::Greater, "expected '>' after type argument");
            Consume(TokenType::LParen, "expected '(' after generic call");
            auto node = std::make_unique<Ast::Expr>();
            node->kind = Ast::Expr::Kind::GenericCall;
            node->location = expr->location;
            node->genericCall = std::make_unique<Ast::GenericCallExpr>();
            node->genericCall->calleeName = calleeName;
            node->genericCall->typeArgument = typeArg.lexeme;
            if (!Check(TokenType::RParen)) {
                do {
                    node->genericCall->arguments.push_back(ParseExpression());
                } while (Match(TokenType::Comma));
            }
            Consume(TokenType::RParen, "expected ')' after arguments");
            expr = std::move(node);
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Ast::Expr> Parser::FinishCall(std::unique_ptr<Ast::Expr> callee) {
    auto node = std::make_unique<Ast::Expr>();
    node->kind = Ast::Expr::Kind::Call;
    node->location = callee->location;
    node->call = std::make_unique<Ast::CallExpr>();
    node->call->callee = std::move(callee);

    if (!Check(TokenType::RParen)) {
        do {
            node->call->arguments.push_back(ParseExpression());
        } while (Match(TokenType::Comma));
    }
    Consume(TokenType::RParen, "expected ')' after arguments");
    return node;
}

std::unique_ptr<Ast::Expr> Parser::ParsePrimary() {
    if (Match(TokenType::LBracket)) {
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::ArrayLiteral;
        node->location = Previous().location;
        node->arrayLiteral = std::make_unique<Ast::ArrayLiteralExpr>();
        if (!Check(TokenType::RBracket)) {
            do { node->arrayLiteral->elements.push_back(ParseExpression()); }
            while (Match(TokenType::Comma));
        }
        Consume(TokenType::RBracket, "expected ']' after array literal");
        return node;
    }
    if (Match(TokenType::KwNew)) {
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::New;
        node->location = Previous().location;
        node->newExpr = std::make_unique<Ast::NewExpr>();
        node->newExpr->typeName = Consume(TokenType::Identifier, "expected type after new").lexeme;
        if (Match(TokenType::LBracket)) {
            node->newExpr->arraySize = ParseExpression();
            Consume(TokenType::RBracket, "expected ']' after array size");
        } else {
            Consume(TokenType::LParen, "expected '(' after constructed type");
            if (!Check(TokenType::RParen)) {
                do { node->newExpr->arguments.push_back(ParseExpression()); }
                while (Match(TokenType::Comma));
            }
            Consume(TokenType::RParen, "expected ')' after constructor arguments");
        }
        return node;
    }
    if (Match(TokenType::IntLiteral)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Literal;
        node->location = token.location;
        node->literal = std::make_unique<Ast::LiteralExpr>();
        node->literal->kind = Ast::LiteralExpr::Kind::Int;
        node->literal->intValue = token.intValue;
        return node;
    }
    if (Match(TokenType::FloatLiteral)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Literal;
        node->location = token.location;
        node->literal = std::make_unique<Ast::LiteralExpr>();
        node->literal->kind = Ast::LiteralExpr::Kind::Float;
        node->literal->floatValue = token.floatValue;
        return node;
    }
    if (Match(TokenType::StringLiteral)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Literal;
        node->location = token.location;
        node->literal = std::make_unique<Ast::LiteralExpr>();
        node->literal->kind = Ast::LiteralExpr::Kind::String;
        node->literal->stringValue = token.lexeme;
        return node;
    }
    if (Match(TokenType::KwTrue) || Match(TokenType::KwFalse)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Literal;
        node->location = token.location;
        node->literal = std::make_unique<Ast::LiteralExpr>();
        node->literal->kind = Ast::LiteralExpr::Kind::Bool;
        node->literal->boolValue = (token.type == TokenType::KwTrue);
        return node;
    }
    if (Match(TokenType::KwNull)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Literal;
        node->location = token.location;
        node->literal = std::make_unique<Ast::LiteralExpr>();
        node->literal->kind = Ast::LiteralExpr::Kind::Null;
        return node;
    }
    if (Match(TokenType::LParen)) {
        auto expr = ParseExpression();
        Consume(TokenType::RParen, "expected ')' after expression");
        return expr;
    }
    if (Match(TokenType::Identifier)) {
        const Token token = Previous();
        auto node = std::make_unique<Ast::Expr>();
        node->kind = Ast::Expr::Kind::Identifier;
        node->location = token.location;
        node->identifier = std::make_unique<Ast::IdentifierExpr>();
        node->identifier->name = token.lexeme;
        return node;
    }

    const Token token = Peek();
    Error(token, "expected expression");
    Advance();
    return nullptr;
}

} // namespace MipsyncEngine::Mips
