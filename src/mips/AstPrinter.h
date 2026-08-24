#pragma once

#include "Ast.h"
#include <string>

namespace MipsyncEngine::Mips {

std::string PrintProgram(const Ast::Program& program, int indent = 0);
std::string PrintExpr(const Ast::Expr& expr, int indent = 0);
std::string PrintStmt(const Ast::Stmt& stmt, int indent = 0);

} // namespace MipsyncEngine::Mips
