#include "AstPrinter.h"
#include <sstream>

namespace MipsyncEngine::Mips {

namespace {

std::string Ind(int n) {
    return std::string(static_cast<size_t>(n * 2), ' ');
}

const char* BinaryOpName(Ast::BinaryOp op) {
    switch (op) {
        case Ast::BinaryOp::Add: return "+";
        case Ast::BinaryOp::Sub: return "-";
        case Ast::BinaryOp::Mul: return "*";
        case Ast::BinaryOp::Div: return "/";
        case Ast::BinaryOp::Mod: return "%";
        case Ast::BinaryOp::Eq: return "==";
        case Ast::BinaryOp::NotEq: return "!=";
        case Ast::BinaryOp::Less: return "<";
        case Ast::BinaryOp::Greater: return ">";
        case Ast::BinaryOp::LessEq: return "<=";
        case Ast::BinaryOp::GreaterEq: return ">=";
        case Ast::BinaryOp::And: return "&&";
        case Ast::BinaryOp::Or: return "||";
        case Ast::BinaryOp::Assign: return "=";
        default: return "?";
    }
}

} // namespace

std::string PrintExpr(const Ast::Expr& expr, int indent) {
    std::ostringstream out;
    const std::string pad = Ind(indent);

    switch (expr.kind) {
    case Ast::Expr::Kind::Literal:
        out << pad << "Literal(";
        switch (expr.literal->kind) {
            case Ast::LiteralExpr::Kind::Int: out << "int, " << expr.literal->intValue; break;
            case Ast::LiteralExpr::Kind::Float: out << "float, " << expr.literal->floatValue; break;
            case Ast::LiteralExpr::Kind::String: out << "string, \"" << expr.literal->stringValue << "\""; break;
            case Ast::LiteralExpr::Kind::Bool: out << "bool, " << (expr.literal->boolValue ? "true" : "false"); break;
            case Ast::LiteralExpr::Kind::Null: out << "null"; break;
        }
        out << ")\n";
        break;
    case Ast::Expr::Kind::Identifier:
        out << pad << "Identifier(" << expr.identifier->name << ")\n";
        break;
    case Ast::Expr::Kind::Binary:
        out << pad << "Binary(" << BinaryOpName(expr.binary->op) << ")\n";
        out << PrintExpr(*expr.binary->left, indent + 1);
        out << PrintExpr(*expr.binary->right, indent + 1);
        break;
    case Ast::Expr::Kind::Unary:
        out << pad << "Unary(" << (expr.unary->op == Ast::UnaryOp::Not ? "!" : "-") << ")\n";
        out << PrintExpr(*expr.unary->operand, indent + 1);
        break;
    case Ast::Expr::Kind::Call:
        out << pad << "Call\n";
        out << PrintExpr(*expr.call->callee, indent + 1);
        for (const auto& arg : expr.call->arguments)
            out << PrintExpr(*arg, indent + 1);
        break;
    case Ast::Expr::Kind::Member:
        out << pad << "Member(." << expr.member->member << ")\n";
        out << PrintExpr(*expr.member->object, indent + 1);
        break;
    case Ast::Expr::Kind::GenericCall:
        out << pad << "GenericCall(" << expr.genericCall->calleeName
            << "<" << expr.genericCall->typeArgument << ">)\n";
        for (const auto& arg : expr.genericCall->arguments)
            out << PrintExpr(*arg, indent + 1);
        break;
    case Ast::Expr::Kind::ArrayLiteral:
        out << pad << "ArrayLiteral\n";
        for (const auto& element : expr.arrayLiteral->elements)
            out << PrintExpr(*element, indent + 1);
        break;
    case Ast::Expr::Kind::Index:
        out << pad << "Index\n";
        out << PrintExpr(*expr.index->object, indent + 1);
        out << PrintExpr(*expr.index->index, indent + 1);
        break;
    case Ast::Expr::Kind::New:
        out << pad << "New(" << expr.newExpr->typeName << ")\n";
        if (expr.newExpr->arraySize)
            out << PrintExpr(*expr.newExpr->arraySize, indent + 1);
        for (const auto& arg : expr.newExpr->arguments)
            out << PrintExpr(*arg, indent + 1);
        break;
    }

    return out.str();
}

std::string PrintStmt(const Ast::Stmt& stmt, int indent) {
    std::ostringstream out;
    const std::string pad = Ind(indent);

    switch (stmt.kind) {
    case Ast::Stmt::Kind::Block:
        out << pad << "Block\n";
        for (const auto& inner : stmt.block->statements)
            out << PrintStmt(*inner, indent + 1);
        break;
    case Ast::Stmt::Kind::Expr:
        out << pad << "ExprStmt\n";
        if (stmt.exprStmt->expression)
            out << PrintExpr(*stmt.exprStmt->expression, indent + 1);
        break;
    case Ast::Stmt::Kind::If:
        out << pad << "If\n";
        out << PrintExpr(*stmt.ifStmt->condition, indent + 1);
        out << PrintStmt(*stmt.ifStmt->thenBranch, indent + 1);
        if (stmt.ifStmt->elseBranch)
            out << PrintStmt(*stmt.ifStmt->elseBranch, indent + 1);
        break;
    case Ast::Stmt::Kind::While:
        out << pad << "While\n";
        out << PrintExpr(*stmt.whileStmt->condition, indent + 1);
        out << PrintStmt(*stmt.whileStmt->body, indent + 1);
        break;
    case Ast::Stmt::Kind::For:
        out << pad << "For\n";
        if (stmt.forStmt->initializer) out << PrintStmt(*stmt.forStmt->initializer, indent + 1);
        if (stmt.forStmt->condition) out << PrintExpr(*stmt.forStmt->condition, indent + 1);
        if (stmt.forStmt->increment) out << PrintExpr(*stmt.forStmt->increment, indent + 1);
        out << PrintStmt(*stmt.forStmt->body, indent + 1);
        break;
    case Ast::Stmt::Kind::Break:
        out << pad << "Break\n";
        break;
    case Ast::Stmt::Kind::Continue:
        out << pad << "Continue\n";
        break;
    case Ast::Stmt::Kind::Return:
        out << pad << "Return\n";
        if (stmt.returnStmt->value)
            out << PrintExpr(*stmt.returnStmt->value, indent + 1);
        break;
    case Ast::Stmt::Kind::Yield:
        out << pad << (stmt.yieldStmt->isBreak ? "YieldBreak\n" : "YieldReturn\n");
        if (stmt.yieldStmt->value)
            out << PrintExpr(*stmt.yieldStmt->value, indent + 1);
        break;
    case Ast::Stmt::Kind::VarDecl:
        out << pad << "VarDecl(" << stmt.varDecl->typeName << " " << stmt.varDecl->name << ")\n";
        if (stmt.varDecl->initializer)
            out << PrintExpr(*stmt.varDecl->initializer, indent + 1);
        break;
    }

    return out.str();
}

std::string PrintProgram(const Ast::Program& program, int indent) {
    std::ostringstream out;
    out << Ind(indent) << "Program\n";

    for (const auto& cls : program.classes) {
        out << Ind(indent + 1) << "Class(" << cls->name << " : " << cls->baseName << ")\n";
        for (const auto& field : cls->fields) {
            out << Ind(indent + 2) << "Field(" << field->typeName << " " << field->name << ")\n";
            if (field->initializer)
                out << PrintExpr(*field->initializer, indent + 3);
        }
        for (const auto& method : cls->methods) {
            out << Ind(indent + 2) << "Method(" << method->name << ")\n";
            if (method->body) {
                for (const auto& stmt : method->body->statements)
                    out << PrintStmt(*stmt, indent + 3);
            }
        }
    }

    return out.str();
}

} // namespace MipsyncEngine::Mips
