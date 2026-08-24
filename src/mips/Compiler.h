#pragma once

#include "Ast.h"
#include "Bytecode.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MipsyncEngine::Mips {

class Compiler {
public:
    explicit Compiler(const Ast::ClassDecl& classDecl, const Ast::Program* program = nullptr,
                      std::string fileName = "<mips>");

    std::unique_ptr<CompiledModule> Compile();
    const std::vector<std::string>& GetErrors() const { return m_Errors; }

private:
    uint16_t AddNumberConstant(double value);
    uint16_t AddStringConstant(const std::string& value);
    uint16_t AddNameConstant(const std::string& name);

    void CompileMethod(const Ast::MethodDecl& method);
    void CompileBlock(const Ast::BlockStmt& block);
    void CompileStatement(const Ast::Stmt& stmt);
    void CompileExpression(const Ast::Expr& expr);
    void CompileAssignment(const Ast::Expr& lhs, const Ast::Expr& rhs);
    void CompileLValueStore(const Ast::Expr& lhs);
    bool TryCompileBuiltinCall(const Ast::CallExpr& call);

    bool IsTransformVecAxis(const Ast::Expr& expr, std::string& outVecMember,
                            char& outAxis) const;
    bool IsTransformVecChain(const Ast::Expr& expr, std::string& outVecMember) const;
    bool IsTransformVecMemberAssign(const Ast::Expr& expr, std::string& outVecMember) const;
    int FindLocal(const std::string& name) const;
    int AllocateLocal(const std::string& name);
    void BeginScope();
    void EndScope();
    void AddError(const Ast::Stmt& stmt, const std::string& message);

    struct LoopContext {
        std::vector<size_t> breakJumps;
        std::vector<size_t> continueJumps;
    };

    bool IsIdentifier(const Ast::Expr& expr, const char* name) const;
    bool IsMemberChain(const Ast::Expr& expr, const char* base, const char* member) const;

    const Ast::ClassDecl& m_Class;
    const Ast::Program* m_Program = nullptr;
    std::unordered_map<std::string, int64_t> m_EnumConstants;
    std::string m_FileName;
    std::unique_ptr<CompiledModule> m_Module;
    BytecodeWriter m_Writer;
    std::vector<std::string> m_Errors;
    uint32_t m_LocalCount = 0;
    std::vector<std::string> m_Locals;
    std::vector<size_t> m_ScopeBases;
    std::vector<LoopContext> m_Loops;
};

std::unique_ptr<CompiledModule> CompileClass(const Ast::ClassDecl& classDecl,
                                             const Ast::Program* program, std::string fileName,
                                             std::vector<std::string>& outErrors);

} // namespace MipsyncEngine::Mips
