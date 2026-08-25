#pragma once

#include "Token.h"
#include <memory>
#include <string>
#include <vector>

namespace MipsyncEngine::Mips::Ast {

enum class BinaryOp {
    Add, Sub, Mul, Div, Mod,
    Eq, NotEq, Less, Greater, LessEq, GreaterEq,
    And, Or,
    Assign,
};

enum class UnaryOp { Neg, Not };

struct Expr;
struct Stmt;
struct BlockStmt;

struct EnumMember {
    std::string name;
    int64_t value = 0;
};

struct EnumDecl {
    SourceLocation location;
    std::string name;
    std::vector<EnumMember> members;
};

struct Program {
    std::vector<std::unique_ptr<EnumDecl>> enums;
    std::vector<std::unique_ptr<struct ClassDecl>> classes;
};

struct ClassDecl {
    SourceLocation location;
    std::string name;
    std::string baseName;
    std::vector<std::unique_ptr<struct FieldDecl>> fields;
    std::vector<std::unique_ptr<struct MethodDecl>> methods;
};

struct FieldDecl {
    SourceLocation location;
    std::string typeName;
    std::string name;
    std::unique_ptr<Expr> initializer;
    bool isAutoProperty = false;
    bool hasGetter = true;
    bool hasSetter = true;
};

struct MethodDecl {
    SourceLocation location;
    std::string returnType = "void";
    std::string name;
    std::vector<std::pair<std::string, std::string>> parameters; // type, name
    std::unique_ptr<BlockStmt> body;
    bool isPublic = false;
};

struct BlockStmt {
    SourceLocation location;
    std::vector<std::unique_ptr<Stmt>> statements;
};

struct ExprStmt {
    SourceLocation location;
    std::unique_ptr<Expr> expression;
};

struct IfStmt {
    SourceLocation location;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> thenBranch;
    std::unique_ptr<Stmt> elseBranch;
};

struct WhileStmt {
    SourceLocation location;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
};

struct ForStmt {
    SourceLocation location;
    std::unique_ptr<Stmt> initializer;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> increment;
    std::unique_ptr<Stmt> body;
};

struct ReturnStmt {
    SourceLocation location;
    std::unique_ptr<Expr> value;
};

struct YieldStmt {
    SourceLocation location;
    bool isBreak = false;
    std::unique_ptr<Expr> value;
};

struct VarDeclStmt {
    SourceLocation location;
    std::string typeName;
    std::string name;
    std::unique_ptr<Expr> initializer;
};

struct Stmt {
    enum class Kind {
        Block, Expr, If, While, For, Break, Continue, Return, Yield, VarDecl
    };

    Kind kind = Kind::Block;
    SourceLocation location;

    std::unique_ptr<BlockStmt> block;
    std::unique_ptr<ExprStmt> exprStmt;
    std::unique_ptr<IfStmt> ifStmt;
    std::unique_ptr<WhileStmt> whileStmt;
    std::unique_ptr<ForStmt> forStmt;
    std::unique_ptr<ReturnStmt> returnStmt;
    std::unique_ptr<YieldStmt> yieldStmt;
    std::unique_ptr<VarDeclStmt> varDecl;
};

struct LiteralExpr {
    enum class Kind { Int, Float, String, Bool, Null } kind = Kind::Null;
    int64_t intValue = 0;
    double floatValue = 0.0;
    std::string stringValue;
    bool boolValue = false;
};

struct IdentifierExpr {
    std::string name;
};

struct BinaryExpr {
    BinaryOp op = BinaryOp::Add;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct UnaryExpr {
    UnaryOp op = UnaryOp::Neg;
    std::unique_ptr<Expr> operand;
};

struct CallExpr {
    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct MemberExpr {
    std::unique_ptr<Expr> object;
    std::string member;
};

struct GenericCallExpr {
    std::string calleeName; // e.g. GetComponent
    std::string typeArgument;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct ArrayLiteralExpr {
    std::vector<std::unique_ptr<Expr>> elements;
};

struct IndexExpr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
};

struct NewExpr {
    std::string typeName;
    std::unique_ptr<Expr> arraySize;
    std::vector<std::unique_ptr<Expr>> arguments;
};

struct Expr {
    enum class Kind {
        Literal, Identifier, Binary, Unary, Call, Member, GenericCall,
        ArrayLiteral, Index, New
    };

    Kind kind = Kind::Literal;
    SourceLocation location;

    std::unique_ptr<LiteralExpr> literal;
    std::unique_ptr<IdentifierExpr> identifier;
    std::unique_ptr<BinaryExpr> binary;
    std::unique_ptr<UnaryExpr> unary;
    std::unique_ptr<CallExpr> call;
    std::unique_ptr<MemberExpr> member;
    std::unique_ptr<GenericCallExpr> genericCall;
    std::unique_ptr<ArrayLiteralExpr> arrayLiteral;
    std::unique_ptr<IndexExpr> index;
    std::unique_ptr<NewExpr> newExpr;
};

} // namespace MipsyncEngine::Mips::Ast
