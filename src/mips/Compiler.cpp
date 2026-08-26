#include "Compiler.h"
#include <cstring>

namespace MipsyncEngine::Mips {

namespace {

bool IsAxisName(const std::string& name, char& axis) {
    if (name == "x") { axis = 0; return true; }
    if (name == "y") { axis = 1; return true; }
    if (name == "z") { axis = 2; return true; }
    return false;
}

bool TryEvalFieldDefault(const Ast::Expr& expr, double& out) {
    if (expr.kind == Ast::Expr::Kind::Literal) {
        if (expr.literal->kind == Ast::LiteralExpr::Kind::Float) {
            out = expr.literal->floatValue;
            return true;
        }
        if (expr.literal->kind == Ast::LiteralExpr::Kind::Int) {
            out = static_cast<double>(expr.literal->intValue);
            return true;
        }
        if (expr.literal->kind == Ast::LiteralExpr::Kind::Bool) {
            out = expr.literal->boolValue ? 1.0 : 0.0;
            return true;
        }
        return false;
    }
    if (expr.kind == Ast::Expr::Kind::Unary && expr.unary &&
        expr.unary->op == Ast::UnaryOp::Neg && expr.unary->operand) {
        double inner = 0.0;
        if (!TryEvalFieldDefault(*expr.unary->operand, inner))
            return false;
        out = -inner;
        return true;
    }
    return false;
}

bool ContainsStringLiteral(const Ast::Expr& expr) {
    switch (expr.kind) {
    case Ast::Expr::Kind::Literal:
        return expr.literal && expr.literal->kind == Ast::LiteralExpr::Kind::String;
    case Ast::Expr::Kind::Binary:
        return expr.binary &&
               ((expr.binary->left && ContainsStringLiteral(*expr.binary->left)) ||
                (expr.binary->right && ContainsStringLiteral(*expr.binary->right)));
    case Ast::Expr::Kind::Unary:
        return expr.unary && expr.unary->operand && ContainsStringLiteral(*expr.unary->operand);
    case Ast::Expr::Kind::Call:
        if (!expr.call) return false;
        if (expr.call->callee && ContainsStringLiteral(*expr.call->callee)) return true;
        for (const auto& arg : expr.call->arguments)
            if (arg && ContainsStringLiteral(*arg)) return true;
        return false;
    case Ast::Expr::Kind::Member:
        return expr.member && expr.member->object && ContainsStringLiteral(*expr.member->object);
    case Ast::Expr::Kind::GenericCall:
        if (!expr.genericCall) return false;
        for (const auto& arg : expr.genericCall->arguments)
            if (arg && ContainsStringLiteral(*arg)) return true;
        return false;
    case Ast::Expr::Kind::ArrayLiteral:
        if (!expr.arrayLiteral) return false;
        for (const auto& element : expr.arrayLiteral->elements)
            if (element && ContainsStringLiteral(*element)) return true;
        return false;
    case Ast::Expr::Kind::Index:
        return expr.index &&
               ((expr.index->object && ContainsStringLiteral(*expr.index->object)) ||
                (expr.index->index && ContainsStringLiteral(*expr.index->index)));
    case Ast::Expr::Kind::New:
        if (!expr.newExpr) return false;
        if (expr.newExpr->arraySize && ContainsStringLiteral(*expr.newExpr->arraySize)) return true;
        for (const auto& arg : expr.newExpr->arguments)
            if (arg && ContainsStringLiteral(*arg)) return true;
        return false;
    default:
        return false;
    }
}

} // namespace

namespace {

void BuildEnumConstants(const Ast::Program& program,
                        std::unordered_map<std::string, int64_t>& out) {
    for (const auto& enumDecl : program.enums) {
        if (!enumDecl)
            continue;
        for (const auto& member : enumDecl->members) {
            out[enumDecl->name + "." + member.name] = member.value;
            out[member.name] = member.value;
        }
    }
}

} // namespace

Compiler::Compiler(const Ast::ClassDecl& classDecl, const Ast::Program* program,
                   std::string fileName)
    : m_Class(classDecl), m_Program(program), m_FileName(std::move(fileName)) {
    if (m_Program)
        BuildEnumConstants(*m_Program, m_EnumConstants);
}

uint16_t Compiler::AddNumberConstant(double value) {
    for (size_t i = 0; i < m_Module->numberConstants.size(); ++i) {
        if (m_Module->numberConstants[i] == value)
            return static_cast<uint16_t>(i);
    }
    m_Module->numberConstants.push_back(value);
    return static_cast<uint16_t>(m_Module->numberConstants.size() - 1);
}

uint16_t Compiler::AddStringConstant(const std::string& value) {
    for (size_t i = 0; i < m_Module->stringConstants.size(); ++i) {
        if (m_Module->stringConstants[i] == value)
            return static_cast<uint16_t>(i);
    }
    m_Module->stringConstants.push_back(value);
    return static_cast<uint16_t>(m_Module->stringConstants.size() - 1);
}

uint16_t Compiler::AddNameConstant(const std::string& name) {
    for (size_t i = 0; i < m_Module->nameConstants.size(); ++i) {
        if (m_Module->nameConstants[i] == name)
            return static_cast<uint16_t>(i);
    }
    m_Module->nameConstants.push_back(name);
    return static_cast<uint16_t>(m_Module->nameConstants.size() - 1);
}

bool Compiler::IsIdentifier(const Ast::Expr& expr, const char* name) const {
    return expr.kind == Ast::Expr::Kind::Identifier && expr.identifier->name == name;
}

bool Compiler::IsMemberChain(const Ast::Expr& expr, const char* base, const char* member) const {
    if (expr.kind != Ast::Expr::Kind::Member)
        return false;
    if (expr.member->member != member)
        return false;
    return IsIdentifier(*expr.member->object, base);
}

bool Compiler::IsTransformVecMemberAssign(const Ast::Expr& expr, std::string& outVecMember) const {
    if (expr.kind != Ast::Expr::Kind::Member)
        return false;
    if (!expr.member->object || expr.member->object->kind != Ast::Expr::Kind::Identifier)
        return false;
    if (expr.member->object->identifier->name != "transform")
        return false;
    if (expr.member->member != "rotation" && expr.member->member != "position" &&
        expr.member->member != "scale")
        return false;
    outVecMember = expr.member->member;
    return true;
}

bool Compiler::IsTransformVecChain(const Ast::Expr& expr, std::string& outVecMember) const {
    if (expr.kind != Ast::Expr::Kind::Member)
        return false;
    if (!IsIdentifier(*expr.member->object, "transform"))
        return false;
    if (expr.member->member != "rotation" && expr.member->member != "position" &&
        expr.member->member != "scale" && expr.member->member != "worldPosition")
        return false;
    outVecMember = expr.member->member;
    return true;
}

bool Compiler::IsTransformVecAxis(const Ast::Expr& expr, std::string& outVecMember,
                                  char& outAxis) const {
    if (expr.kind != Ast::Expr::Kind::Member)
        return false;
    if (!IsTransformVecChain(*expr.member->object, outVecMember))
        return false;
    return IsAxisName(expr.member->member, outAxis);
}

int Compiler::FindLocal(const std::string& name) const {
    for (size_t i = 0; i < m_Locals.size(); ++i)
        if (m_Locals[i] == name)
            return static_cast<int>(i);
    return -1;
}

int Compiler::AllocateLocal(const std::string& name) {
    if (FindLocal(name) >= 0) {
        m_Errors.push_back(m_FileName + ": local '" + name + "' shadows existing variable");
        return FindLocal(name);
    }
    m_Locals.push_back(name);
    if (m_Locals.size() > m_LocalCount)
        m_LocalCount = static_cast<uint32_t>(m_Locals.size());
    return static_cast<int>(m_Locals.size()) - 1;
}

void Compiler::BeginScope() {
    m_ScopeBases.push_back(m_Locals.size());
}

void Compiler::EndScope() {
    if (m_ScopeBases.empty())
        return;
    m_Locals.resize(m_ScopeBases.back());
    m_ScopeBases.pop_back();
}

void Compiler::AddError(const Ast::Stmt& stmt, const std::string& message) {
    m_Errors.push_back(m_FileName + "(" + std::to_string(stmt.location.line) + "," +
                       std::to_string(stmt.location.column) + "): " + message);
}

std::unique_ptr<CompiledModule> Compiler::Compile() {
    m_Module = std::make_unique<CompiledModule>();
    m_Module->className = m_Class.name;

    for (const auto& field : m_Class.fields) {
        CompiledField cf;
        cf.name = field->name;
        cf.typeName = field->typeName;
        cf.hasGetter = field->hasGetter;
        cf.hasSetter = field->hasSetter;
        if (field->typeName == "bool")
            cf.valueKind = FieldValueKind::Bool;
        else if (field->typeName == "AudioClip")
            cf.valueKind = FieldValueKind::AudioClip;
        else if (field->typeName == "Entity" || field->typeName == "Camera")
            cf.valueKind = FieldValueKind::EntityReference;
        else if (field->typeName.size() >= 2 &&
                 field->typeName.compare(field->typeName.size() - 2, 2, "[]") == 0)
            cf.valueKind = FieldValueKind::Array;
        double defaultValue = 0.0;
        if (field->initializer)
            TryEvalFieldDefault(*field->initializer, defaultValue);
        cf.defaultConstIndex = AddNumberConstant(defaultValue);
        m_Module->fields.push_back(std::move(cf));
    }

    for (const auto& method : m_Class.methods) {
        if (method)
            CompileMethod(*method);
    }

    return std::move(m_Module);
}

void Compiler::CompileMethod(const Ast::MethodDecl& method) {
    m_Writer = BytecodeWriter{};
    m_LocalCount = 0;
    m_Locals.clear();
    m_ScopeBases.clear();
    m_Loops.clear();

    if (method.body)
        CompileBlock(*method.body);

    m_Writer.EmitOp(OpCode::Return);

    CompiledMethod compiled;
    compiled.name = method.name;
    compiled.returnType = method.returnType;
    compiled.code = m_Writer.Code();
    compiled.localCount = m_LocalCount;
    compiled.parameterCount = static_cast<uint16_t>(
        std::min<size_t>(method.parameters.size(), 0xFFFFu));
    compiled.isPublic = method.isPublic;
    m_Module->methods.push_back(std::move(compiled));
}

void Compiler::CompileBlock(const Ast::BlockStmt& block) {
    BeginScope();
    for (const auto& stmt : block.statements) {
        if (stmt)
            CompileStatement(*stmt);
    }
    EndScope();
}

void Compiler::CompileStatement(const Ast::Stmt& stmt) {
    switch (stmt.kind) {
    case Ast::Stmt::Kind::Block:
        if (stmt.block) CompileBlock(*stmt.block);
        break;
    case Ast::Stmt::Kind::Expr:
        if (stmt.exprStmt && stmt.exprStmt->expression) {
            if (stmt.exprStmt->expression->kind == Ast::Expr::Kind::Binary &&
                stmt.exprStmt->expression->binary->op == Ast::BinaryOp::Assign) {
                CompileAssignment(*stmt.exprStmt->expression->binary->left,
                                *stmt.exprStmt->expression->binary->right);
            } else {
                CompileExpression(*stmt.exprStmt->expression);
                m_Writer.EmitOp(OpCode::Pop);
            }
        }
        break;
    case Ast::Stmt::Kind::If: {
        if (!stmt.ifStmt) break;
        CompileExpression(*stmt.ifStmt->condition);
        const size_t jumpFalse = m_Writer.EmitJumpIfFalsePlaceholder();
        CompileStatement(*stmt.ifStmt->thenBranch);
        if (stmt.ifStmt->elseBranch) {
            const size_t jumpEnd = m_Writer.EmitJumpPlaceholder();
            m_Writer.PatchJump(jumpFalse, m_Writer.CurrentOffset());
            CompileStatement(*stmt.ifStmt->elseBranch);
            m_Writer.PatchJump(jumpEnd, m_Writer.CurrentOffset());
        } else {
            m_Writer.PatchJump(jumpFalse, m_Writer.CurrentOffset());
        }
        break;
    }
    case Ast::Stmt::Kind::While: {
        if (!stmt.whileStmt) break;
        const size_t loopStart = m_Writer.CurrentOffset();
        CompileExpression(*stmt.whileStmt->condition);
        const size_t jumpExit = m_Writer.EmitJumpIfFalsePlaceholder();
        m_Loops.push_back({});
        CompileStatement(*stmt.whileStmt->body);
        for (size_t jump : m_Loops.back().continueJumps)
            m_Writer.PatchJump(jump, loopStart);
        m_Writer.EmitOp(OpCode::Jump);
        m_Writer.EmitI32(static_cast<int32_t>(loopStart) - static_cast<int32_t>(m_Writer.CurrentOffset() + 5));
        m_Writer.PatchJump(jumpExit, m_Writer.CurrentOffset());
        for (size_t jump : m_Loops.back().breakJumps)
            m_Writer.PatchJump(jump, m_Writer.CurrentOffset());
        m_Loops.pop_back();
        break;
    }
    case Ast::Stmt::Kind::For: {
        if (!stmt.forStmt) break;
        BeginScope();
        if (stmt.forStmt->initializer)
            CompileStatement(*stmt.forStmt->initializer);
        const size_t loopStart = m_Writer.CurrentOffset();
        if (stmt.forStmt->condition) {
            CompileExpression(*stmt.forStmt->condition);
        } else {
            m_Writer.EmitOp(OpCode::PushBool);
            m_Writer.EmitU8(1);
        }
        const size_t jumpExit = m_Writer.EmitJumpIfFalsePlaceholder();
        m_Loops.push_back({});
        if (stmt.forStmt->body)
            CompileStatement(*stmt.forStmt->body);
        const size_t continueTarget = m_Writer.CurrentOffset();
        for (size_t jump : m_Loops.back().continueJumps)
            m_Writer.PatchJump(jump, continueTarget);
        if (stmt.forStmt->increment) {
            if (stmt.forStmt->increment->kind == Ast::Expr::Kind::Binary &&
                stmt.forStmt->increment->binary->op == Ast::BinaryOp::Assign) {
                CompileAssignment(*stmt.forStmt->increment->binary->left,
                                  *stmt.forStmt->increment->binary->right);
            } else {
                CompileExpression(*stmt.forStmt->increment);
                m_Writer.EmitOp(OpCode::Pop);
            }
        }
        m_Writer.EmitOp(OpCode::Jump);
        m_Writer.EmitI32(static_cast<int32_t>(loopStart) -
                         static_cast<int32_t>(m_Writer.CurrentOffset() + 5));
        const size_t loopEnd = m_Writer.CurrentOffset();
        m_Writer.PatchJump(jumpExit, loopEnd);
        for (size_t jump : m_Loops.back().breakJumps)
            m_Writer.PatchJump(jump, loopEnd);
        m_Loops.pop_back();
        EndScope();
        break;
    }
    case Ast::Stmt::Kind::Break:
        if (m_Loops.empty())
            AddError(stmt, "break can only be used inside a loop");
        else
            m_Loops.back().breakJumps.push_back(m_Writer.EmitJumpPlaceholder());
        break;
    case Ast::Stmt::Kind::Continue:
        if (m_Loops.empty())
            AddError(stmt, "continue can only be used inside a loop");
        else
            m_Loops.back().continueJumps.push_back(m_Writer.EmitJumpPlaceholder());
        break;

    case Ast::Stmt::Kind::Return:
        if (stmt.returnStmt && stmt.returnStmt->value)
            CompileExpression(*stmt.returnStmt->value);
        m_Writer.EmitOp(OpCode::Return);
        break;
    case Ast::Stmt::Kind::Yield:
        if (!stmt.yieldStmt) break;
        if (stmt.yieldStmt->isBreak) {
            m_Writer.EmitOp(OpCode::YieldBreak);
        } else if (stmt.yieldStmt->value && stmt.yieldStmt->value->kind == Ast::Expr::Kind::New &&
                   stmt.yieldStmt->value->newExpr &&
                   stmt.yieldStmt->value->newExpr->typeName == "WaitForSeconds" &&
                   stmt.yieldStmt->value->newExpr->arguments.size() == 1) {
            CompileExpression(*stmt.yieldStmt->value->newExpr->arguments[0]);
            m_Writer.EmitOp(OpCode::YieldSeconds);
        } else if (stmt.yieldStmt->value && stmt.yieldStmt->value->kind == Ast::Expr::Kind::Call &&
                   stmt.yieldStmt->value->call && stmt.yieldStmt->value->call->callee &&
                   stmt.yieldStmt->value->call->callee->kind == Ast::Expr::Kind::Identifier &&
                   stmt.yieldStmt->value->call->callee->identifier->name == "WaitForSeconds" &&
                   stmt.yieldStmt->value->call->arguments.size() == 1) {
            CompileExpression(*stmt.yieldStmt->value->call->arguments[0]);
            m_Writer.EmitOp(OpCode::YieldSeconds);
        } else {
            m_Writer.EmitOp(OpCode::YieldNext);
        }
        break;
    case Ast::Stmt::Kind::VarDecl:
        if (stmt.varDecl) {
            const int slot = AllocateLocal(stmt.varDecl->name);
            if (stmt.varDecl->initializer) {
                CompileExpression(*stmt.varDecl->initializer);
                m_Writer.EmitOp(OpCode::SetLocal);
                m_Writer.EmitU16(static_cast<uint16_t>(slot));
            }
        }
        break;
    default:
        break;
    }
}

void Compiler::CompileAssignment(const Ast::Expr& lhs, const Ast::Expr& rhs) {
    if (lhs.kind == Ast::Expr::Kind::Index && lhs.index) {
        CompileExpression(*lhs.index->object);
        CompileExpression(*lhs.index->index);
        CompileExpression(rhs);
        m_Writer.EmitOp(OpCode::SetIndex);
        return;
    }
    if (lhs.kind == Ast::Expr::Kind::Member && lhs.member && lhs.member->object) {
        HostFunc setter{};
        bool isAudioProperty = true;
        const std::string& member = lhs.member->member;
        if (member == "clip") setter = HostFunc::AudioSource_SetClip;
        else if (member == "volume") setter = HostFunc::AudioSource_SetVolume;
        else if (member == "loop") setter = HostFunc::AudioSource_SetLoop;
        else if (member == "mute") setter = HostFunc::AudioSource_SetMute;
        else if (member == "playOnAwake") setter = HostFunc::AudioSource_SetPlayOnAwake;
        else if (member == "enabled") setter = HostFunc::AudioSource_SetEnabled;
        else isAudioProperty = false;

        if (isAudioProperty) {
            if (setter == HostFunc::AudioSource_SetClip) {
                m_Module->ps1CompatibilityErrors.push_back(
                    m_FileName + "(" + std::to_string(lhs.location.line) + "," +
                    std::to_string(lhs.location.column) +
                    "): AudioSource.clip runtime assignment is not available on PS1; "
                    "configure the clip in scene/build data");
            }
            CompileExpression(*lhs.member->object);
            CompileExpression(rhs);
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(setter));
            m_Writer.EmitU8(2);
            return;
        }
    }
    CompileExpression(rhs);
    CompileLValueStore(lhs);
}

void Compiler::CompileLValueStore(const Ast::Expr& lhs) {
    std::string vecMember;
    if (IsTransformVecMemberAssign(lhs, vecMember)) {
        m_Writer.EmitOp(OpCode::GetGlobal);
        m_Writer.EmitU16(AddNameConstant("transform"));
        m_Writer.EmitOp(OpCode::GetMember);
        m_Writer.EmitU16(AddNameConstant(vecMember));
        m_Writer.EmitOp(OpCode::SetVec3FromValue);
        return;
    }

    char axis = 0;
    if (IsTransformVecAxis(lhs, vecMember, axis)) {
        m_Writer.EmitOp(OpCode::GetGlobal);
        m_Writer.EmitU16(AddNameConstant("transform"));
        m_Writer.EmitOp(OpCode::GetMember);
        m_Writer.EmitU16(AddNameConstant(vecMember));
        m_Writer.EmitOp(OpCode::SetVec3Axis);
        m_Writer.EmitU8(static_cast<uint8_t>(axis));
        return;
    }

    if (lhs.kind == Ast::Expr::Kind::Identifier) {
        const std::string& name = lhs.identifier->name;
        const int localIdx = FindLocal(name);
        if (localIdx >= 0) {
            m_Writer.EmitOp(OpCode::SetLocal);
            m_Writer.EmitU16(static_cast<uint16_t>(localIdx));
            return;
        }
        const int fieldIdx = m_Module->FindFieldIndex(name);
        if (fieldIdx >= 0) {
            if (!m_Module->fields[static_cast<size_t>(fieldIdx)].hasSetter) {
                m_Errors.push_back(m_FileName + ": field '" + name + "' is read-only");
                m_Writer.EmitOp(OpCode::Pop);
                return;
            }
            m_Writer.EmitOp(OpCode::SetField);
            m_Writer.EmitU16(static_cast<uint16_t>(fieldIdx));
            return;
        }
    }

    m_Errors.push_back(m_FileName + ": unsupported assignment target");
    m_Writer.EmitOp(OpCode::Pop);
}

void Compiler::CompileExpression(const Ast::Expr& expr) {
    switch (expr.kind) {
    case Ast::Expr::Kind::Literal:
        if (expr.literal->kind == Ast::LiteralExpr::Kind::Int ||
            expr.literal->kind == Ast::LiteralExpr::Kind::Float) {
            const double v = (expr.literal->kind == Ast::LiteralExpr::Kind::Int)
                ? static_cast<double>(expr.literal->intValue) : expr.literal->floatValue;
            m_Writer.EmitOp(OpCode::PushConst);
            m_Writer.EmitU16(AddNumberConstant(v));
        } else if (expr.literal->kind == Ast::LiteralExpr::Kind::String) {
            m_Writer.EmitOp(OpCode::PushString);
            m_Writer.EmitU16(AddStringConstant(expr.literal->stringValue));
        } else if (expr.literal->kind == Ast::LiteralExpr::Kind::Bool) {
            m_Writer.EmitOp(OpCode::PushBool);
            m_Writer.EmitU8(expr.literal->boolValue ? 1 : 0);
        } else {
            m_Writer.EmitOp(OpCode::PushConst);
            m_Writer.EmitU16(AddNumberConstant(0));
        }
        break;

    case Ast::Expr::Kind::Identifier: {
        const std::string& name = expr.identifier->name;
        const int localIdx = FindLocal(name);
        if (localIdx >= 0) {
            m_Writer.EmitOp(OpCode::PushLocal);
            m_Writer.EmitU16(static_cast<uint16_t>(localIdx));
            break;
        }
        const int fieldIdx = m_Module->FindFieldIndex(name);
        if (fieldIdx >= 0) {
            if (!m_Module->fields[static_cast<size_t>(fieldIdx)].hasGetter) {
                m_Errors.push_back(m_FileName + ": field '" + name + "' has no getter");
                m_Writer.EmitOp(OpCode::PushConst);
                m_Writer.EmitU16(AddNumberConstant(0));
                break;
            }
            m_Writer.EmitOp(OpCode::PushField);
            m_Writer.EmitU16(static_cast<uint16_t>(fieldIdx));
            break;
        }
        if (const auto enumIt = m_EnumConstants.find(name); enumIt != m_EnumConstants.end()) {
            m_Writer.EmitOp(OpCode::PushConst);
            m_Writer.EmitU16(AddNumberConstant(static_cast<double>(enumIt->second)));
            break;
        }
        m_Writer.EmitOp(OpCode::GetGlobal);
        m_Writer.EmitU16(AddNameConstant(name));
        break;
    }

    case Ast::Expr::Kind::Binary: {
        if (expr.binary->op == Ast::BinaryOp::Assign) {
            CompileAssignment(*expr.binary->left, *expr.binary->right);
            break;
        }
        if (expr.binary->op == Ast::BinaryOp::Add && ContainsStringLiteral(expr)) {
            m_Module->ps1CompatibilityErrors.push_back(
                m_FileName + "(" + std::to_string(expr.location.line) + "," +
                std::to_string(expr.location.column) +
                "): string concatenation is not available on the PS1 runtime");
        }
        CompileExpression(*expr.binary->left);
        CompileExpression(*expr.binary->right);
        switch (expr.binary->op) {
            case Ast::BinaryOp::Add: m_Writer.EmitOp(OpCode::Add); break;
            case Ast::BinaryOp::Sub: m_Writer.EmitOp(OpCode::Sub); break;
            case Ast::BinaryOp::Mul: m_Writer.EmitOp(OpCode::Mul); break;
            case Ast::BinaryOp::Div: m_Writer.EmitOp(OpCode::Div); break;
            case Ast::BinaryOp::Mod: m_Writer.EmitOp(OpCode::Mod); break;
            case Ast::BinaryOp::Eq: m_Writer.EmitOp(OpCode::Eq); break;
            case Ast::BinaryOp::NotEq: m_Writer.EmitOp(OpCode::Ne); break;
            case Ast::BinaryOp::Less: m_Writer.EmitOp(OpCode::Lt); break;
            case Ast::BinaryOp::Greater: m_Writer.EmitOp(OpCode::Gt); break;
            case Ast::BinaryOp::LessEq: m_Writer.EmitOp(OpCode::Le); break;
            case Ast::BinaryOp::GreaterEq: m_Writer.EmitOp(OpCode::Ge); break;
            case Ast::BinaryOp::And: m_Writer.EmitOp(OpCode::And); break;
            case Ast::BinaryOp::Or: m_Writer.EmitOp(OpCode::Or); break;
            default: break;
        }
        break;
    }

    case Ast::Expr::Kind::Unary:
        CompileExpression(*expr.unary->operand);
        m_Writer.EmitOp(expr.unary->op == Ast::UnaryOp::Not ? OpCode::Not : OpCode::Neg);
        break;

    case Ast::Expr::Kind::Member: {
        if (expr.member->member == "Length" || expr.member->member == "Count") {
            CompileExpression(*expr.member->object);
            m_Writer.EmitOp(OpCode::ArrayLength);
            break;
        }
        // Time.deltaTime
        if (IsMemberChain(expr, "Time", "deltaTime")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Time_DeltaTime));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Input", "mouseDeltaX")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Input_MouseDeltaX));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Input", "mouseDeltaY")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Input_MouseDeltaY));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Input", "cursorLocked")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Input_GetCursorLocked));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Vector3", "up")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Vector3_Up));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Vector3", "forward")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Vector3_Forward));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Vector3", "right")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Vector3_Right));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Physics", "otherEntityId")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Physics_OtherEntityId));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Camera", "rightX")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Camera_RightX));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Camera", "rightZ")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Camera_RightZ));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Camera", "forwardX")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Camera_ForwardX));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Camera", "forwardZ")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Camera_ForwardZ));
            m_Writer.EmitU8(0);
            break;
        }
        if (IsMemberChain(expr, "Camera", "yaw")) {
            m_Writer.EmitOp(OpCode::CallHost);
            m_Writer.EmitU16(static_cast<uint16_t>(HostFunc::Camera_Yaw));
            m_Writer.EmitU8(0);
            break;
        }
        // transform.position.x / transform.rotation.y / transform.scale.z
        std::string vecMember;
        char axis = 0;
        if (IsTransformVecAxis(expr, vecMember, axis)) {
            m_Writer.EmitOp(OpCode::GetGlobal);
            m_Writer.EmitU16(AddNameConstant("transform"));
            m_Writer.EmitOp(OpCode::GetMember);
            m_Writer.EmitU16(AddNameConstant(vecMember));
            m_Writer.EmitOp(OpCode::GetVec3Axis);
            m_Writer.EmitU8(static_cast<uint8_t>(axis));
            break;
        }
        if (IsTransformVecChain(expr, vecMember)) {
            m_Writer.EmitOp(OpCode::GetGlobal);
            m_Writer.EmitU16(AddNameConstant("transform"));
            m_Writer.EmitOp(OpCode::GetMember);
            m_Writer.EmitU16(AddNameConstant(vecMember));
            break;
        }
        CompileExpression(*expr.member->object);
        m_Writer.EmitOp(OpCode::GetMember);
        m_Writer.EmitU16(AddNameConstant(expr.member->member));
        break;
    }

    case Ast::Expr::Kind::GenericCall: {
        if (expr.genericCall->calleeName == "GetComponent" &&
            expr.genericCall->arguments.empty()) {
            m_Writer.EmitOp(OpCode::GetComponent);
            m_Writer.EmitU16(AddNameConstant(expr.genericCall->typeArgument));
            break;
        }
        m_Errors.push_back(m_FileName + ": unsupported generic call: " + expr.genericCall->calleeName);
        m_Writer.EmitOp(OpCode::PushConst);
        m_Writer.EmitU16(AddNumberConstant(0));
        break;
    }

    case Ast::Expr::Kind::Call: {
        if (TryCompileBuiltinCall(*expr.call))
            break;
        m_Errors.push_back(m_FileName + ": unsupported call expression");
        break;
    }

    case Ast::Expr::Kind::ArrayLiteral: {
        for (const auto& element : expr.arrayLiteral->elements)
            CompileExpression(*element);
        m_Writer.EmitOp(OpCode::NewArray);
        m_Writer.EmitU16(static_cast<uint16_t>(expr.arrayLiteral->elements.size()));
        break;
    }

    case Ast::Expr::Kind::Index:
        CompileExpression(*expr.index->object);
        CompileExpression(*expr.index->index);
        m_Writer.EmitOp(OpCode::GetIndex);
        break;

    case Ast::Expr::Kind::New:
        if (expr.newExpr->arraySize) {
            CompileExpression(*expr.newExpr->arraySize);
            m_Writer.EmitOp(OpCode::NewArraySized);
        } else {
            m_Errors.push_back(m_FileName + ": unsupported constructor: " + expr.newExpr->typeName);
            m_Writer.EmitOp(OpCode::PushConst);
            m_Writer.EmitU16(AddNumberConstant(0));
        }
        break;

    default:
        m_Errors.push_back(m_FileName + ": unsupported expression");
        m_Writer.EmitOp(OpCode::PushConst);
        m_Writer.EmitU16(AddNumberConstant(0));
        break;
    }
}

bool Compiler::TryCompileBuiltinCall(const Ast::CallExpr& call) {
    if (!call.callee)
        return false;

    const size_t argc = call.arguments.size();
    auto compileArgs = [&]() {
        for (const auto& arg : call.arguments) {
            if (arg) CompileExpression(*arg);
        }
    };
    auto emit = [&](HostFunc func, uint8_t argCount) {
        compileArgs();
        m_Writer.EmitOp(OpCode::CallHost);
        m_Writer.EmitU16(static_cast<uint16_t>(func));
        m_Writer.EmitU8(argCount);
    };
    auto tryAnimatorMethods = [&](const Ast::Expr& objectExpr, const std::string& methodName) -> bool {
        const bool hasTarget = objectExpr.kind != Ast::Expr::Kind::Identifier;
        if (hasTarget)
            CompileExpression(objectExpr);
        const uint8_t hostArgc = static_cast<uint8_t>(argc + (hasTarget ? 1 : 0));
        if (methodName == "SetFloat" && argc == 2) {
            emit(HostFunc::Animator_SetFloat, hostArgc);
            return true;
        }
        if (methodName == "SetBool" && argc == 2) {
            emit(HostFunc::Animator_SetBool, hostArgc);
            return true;
        }
        if (methodName == "SetInt" && argc == 2) {
            emit(HostFunc::Animator_SetInt, hostArgc);
            return true;
        }
        if (methodName == "SetTrigger" && argc == 1) {
            emit(HostFunc::Animator_SetTrigger, hostArgc);
            return true;
        }
        return false;
    };
    auto tryAudioSourceMethods = [&](const Ast::Expr& objectExpr,
                                     const std::string& methodName) -> bool {
        HostFunc func{};
        if (argc != 0)
            return false;
        if (methodName == "Play") func = HostFunc::AudioSource_Play;
        else if (methodName == "Stop") func = HostFunc::AudioSource_Stop;
        else if (methodName == "Pause") func = HostFunc::AudioSource_Pause;
        else if (methodName == "UnPause") func = HostFunc::AudioSource_UnPause;
        else return false;

        const bool isGlobal = objectExpr.kind == Ast::Expr::Kind::Identifier &&
                              objectExpr.identifier->name == "AudioSource";
        if (!isGlobal)
            CompileExpression(objectExpr);
        m_Writer.EmitOp(OpCode::CallHost);
        m_Writer.EmitU16(static_cast<uint16_t>(func));
        m_Writer.EmitU8(isGlobal ? 0 : 1);
        return true;
    };

    if (call.callee->kind == Ast::Expr::Kind::Identifier) {
        const std::string& name = call.callee->identifier->name;
        if (name == "StartCoroutine" && argc == 1 && call.arguments[0] &&
            call.arguments[0]->kind == Ast::Expr::Kind::Call && call.arguments[0]->call &&
            call.arguments[0]->call->callee &&
            call.arguments[0]->call->callee->kind == Ast::Expr::Kind::Identifier) {
            if (!call.arguments[0]->call->arguments.empty()) {
                m_Errors.push_back(m_FileName + ": coroutine arguments are not supported yet");
                m_Writer.EmitOp(OpCode::PushConst);
                m_Writer.EmitU16(AddNumberConstant(0));
                return true;
            }
            m_Writer.EmitOp(OpCode::StartCoroutine);
            m_Writer.EmitU16(AddNameConstant(call.arguments[0]->call->callee->identifier->name));
            return true;
        }
        if (name == "StopAllCoroutines" && argc == 0) {
            m_Writer.EmitOp(OpCode::StopAllCoroutines);
            return true;
        }
        if (name == "Vector3" && argc == 3) {
            emit(HostFunc::Vector3_Create, 3);
            return true;
        }
        return false;
    }

    if (call.callee->kind != Ast::Expr::Kind::Member || !call.callee->member)
        return false;

    const auto& member = *call.callee->member;
    const std::string& methodName = member.member;
    if (!member.object)
        return false;

    if (methodName == "Add" && argc == 1) {
        CompileExpression(*member.object);
        CompileExpression(*call.arguments[0]);
        m_Writer.EmitOp(OpCode::ArrayAdd);
        return true;
    }
    if (methodName == "RemoveAt" && argc == 1) {
        CompileExpression(*member.object);
        CompileExpression(*call.arguments[0]);
        m_Writer.EmitOp(OpCode::ArrayRemoveAt);
        return true;
    }
    if (methodName == "Clear" && argc == 0) {
        CompileExpression(*member.object);
        m_Writer.EmitOp(OpCode::ArrayClear);
        return true;
    }

    if (member.object->kind != Ast::Expr::Kind::Identifier) {
        if (tryAnimatorMethods(*member.object, methodName))
            return true;
        if (tryAudioSourceMethods(*member.object, methodName))
            return true;
        return false;
    }

    const std::string& targetName = member.object->identifier->name;

    if (tryAudioSourceMethods(*member.object, methodName))
        return true;

    if ((targetName == "Log" && methodName == "Info") ||
        (targetName == "Debug" && methodName == "Log")) {
        emit(HostFunc::Log_Info, static_cast<uint8_t>(argc));
        return true;
    }
    if (targetName == "Input" && methodName == "GetKey" && argc == 1) {
        emit(HostFunc::Input_GetKey, 1);
        return true;
    }
    if (targetName == "Input" && methodName == "GetKeyDown" && argc == 1) {
        emit(HostFunc::Input_GetKeyDown, 1);
        return true;
    }
    if (targetName == "Input" && methodName == "GetKeyUp" && argc == 1) {
        emit(HostFunc::Input_GetKeyUp, 1);
        return true;
    }
    if (targetName == "Input" && methodName == "GetAxis" && argc == 1) {
        emit(HostFunc::Input_GetAxis, 1);
        return true;
    }
    if (targetName == "Input" && methodName == "SetCursorLocked" && argc == 1) {
        emit(HostFunc::Input_SetCursorLocked, 1);
        return true;
    }
    if (targetName == "Physics" && methodName == "Raycast" && argc == 7) {
        emit(HostFunc::Physics_Raycast, 7);
        return true;
    }
    if (targetName == "Physics" && methodName == "Move" && argc == 3) {
        emit(HostFunc::Physics_Move, 3);
        return true;
    }
    if (targetName == "Physics" && methodName == "IsGrounded" && argc == 0) {
        emit(HostFunc::Physics_IsGrounded, 0);
        return true;
    }
    if (targetName == "Camera" && methodName == "Follow" && argc == 6) {
        emit(HostFunc::Camera_Follow, 6);
        return true;
    }
    if (targetName == "Vector3") {
        if (methodName == "Add" && argc == 2)    { emit(HostFunc::Vector3_Add, 2); return true; }
        if (methodName == "Sub" && argc == 2)    { emit(HostFunc::Vector3_Sub, 2); return true; }
        if (methodName == "Scale" && argc == 2)  { emit(HostFunc::Vector3_Scale, 2); return true; }
        if (methodName == "Length" && argc == 1) { emit(HostFunc::Vector3_Length, 1); return true; }
        if (methodName == "Normalize" && argc == 1) { emit(HostFunc::Vector3_Normalize, 1); return true; }
    }
    if (targetName == "Mathf") {
        if (methodName == "Sin" && argc == 1)    { emit(HostFunc::Mathf_Sin, 1); return true; }
        if (methodName == "Cos" && argc == 1)    { emit(HostFunc::Mathf_Cos, 1); return true; }
        if (methodName == "Sqrt" && argc == 1)   { emit(HostFunc::Mathf_Sqrt, 1); return true; }
        if (methodName == "Abs" && argc == 1)    { emit(HostFunc::Mathf_Abs, 1); return true; }
        if (methodName == "Clamp" && argc == 3)  { emit(HostFunc::Mathf_Clamp, 3); return true; }
        if (methodName == "Atan2" && argc == 2)  { emit(HostFunc::Mathf_Atan2, 2); return true; }
        if (methodName == "Min" && argc == 2)    { emit(HostFunc::Mathf_Min, 2); return true; }
        if (methodName == "Max" && argc == 2)    { emit(HostFunc::Mathf_Max, 2); return true; }
        if (methodName == "Lerp" && argc == 3)   { emit(HostFunc::Mathf_Lerp, 3); return true; }
        if (methodName == "Floor" && argc == 1)  { emit(HostFunc::Mathf_Floor, 1); return true; }
        if (methodName == "Ceil" && argc == 1)   { emit(HostFunc::Mathf_Ceil, 1); return true; }
        if (methodName == "Round" && argc == 1)  { emit(HostFunc::Mathf_Round, 1); return true; }
        if (methodName == "Sign" && argc == 1)   { emit(HostFunc::Mathf_Sign, 1); return true; }
    }
    if (targetName == "Animator")
        return tryAnimatorMethods(*member.object, methodName);
    if (targetName == "Scene" && methodName == "Load" && argc == 1) {
        emit(HostFunc::Scene_Load, 1);
        return true;
    }
    if (targetName == "Scene" && methodName == "LoadBuildIndex" && argc == 1) {
        emit(HostFunc::Scene_LoadBuildIndex, 1);
        return true;
    }
    if (targetName == "Application" && methodName == "Quit" && argc == 0) {
        emit(HostFunc::Application_Quit, 0);
        return true;
    }
    if (targetName == "Save") {
        if (methodName == "GetInt" && argc == 2) {
            emit(HostFunc::Save_GetInt, 2);
            return true;
        }
        if (methodName == "SetInt" && argc == 2) {
            emit(HostFunc::Save_SetInt, 2);
            return true;
        }
        if (methodName == "GetFloat" && argc == 2) {
            emit(HostFunc::Save_GetFloat, 2);
            return true;
        }
        if (methodName == "SetFloat" && argc == 2) {
            emit(HostFunc::Save_SetFloat, 2);
            return true;
        }
        if (methodName == "GetBool" && argc == 2) {
            emit(HostFunc::Save_GetBool, 2);
            return true;
        }
        if (methodName == "SetBool" && argc == 2) {
            emit(HostFunc::Save_SetBool, 2);
            return true;
        }
        if (methodName == "GetString" && argc == 2) {
            emit(HostFunc::Save_GetString, 2);
            return true;
        }
        if (methodName == "SetString" && argc == 2) {
            emit(HostFunc::Save_SetString, 2);
            return true;
        }
        if (methodName == "Write" && argc == 1) {
            emit(HostFunc::Save_Write, 1);
            return true;
        }
        if (methodName == "Read" && argc == 1) {
            emit(HostFunc::Save_Read, 1);
            return true;
        }
    }
    return false;
}

std::unique_ptr<CompiledModule> CompileClass(const Ast::ClassDecl& classDecl,
                                             const Ast::Program* program, std::string fileName,
                                             std::vector<std::string>& outErrors) {
    Compiler compiler(classDecl, program, fileName);
    auto module = compiler.Compile();
    const auto& errors = compiler.GetErrors();
    outErrors.insert(outErrors.end(), errors.begin(), errors.end());
    return module;
}

} // namespace MipsyncEngine::Mips
