#include "UGLIRVerifier.hpp"
#include "UGLIRTypeUtils.hpp"

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Checks backend-independent invariants of one already lowered, single-entry module. */
        class ModuleValidator
        {
        public:
            /** Binds validation to an immutable module for the lifetime of this checker. */
            explicit ModuleValidator(const Module &module) : mModule(module) {}

            /** Validates types, entry points, resource bindings, expressions, and structured control flow. */
            std::vector<UGLIRValidationDiagnostic> validate()
            {
                size_t entryCount = 0;
                for (size_t index = 0; index < mModule.types.size(); ++index)
                {
                    const Type &type = mModule.types[index];
                    if (type.name.empty()) { diagnose(type.sourceLocation, "type name is empty."); }
                    for (size_t previous = 0; previous < index; ++previous)
                    {
                        if (mModule.types[previous].name == type.name) { diagnose(type.sourceLocation, "duplicate type: " + type.name); }
                    }
                    for (const TypeField &field : type.fields) { validateType(field.type, field.sourceLocation); }
                    validateTypeShape(type);
                    std::vector<std::string> ancestors;
                    validateTypeDependencies(type, ancestors);
                }
                for (size_t index = 0; index < mModule.functions.size(); ++index)
                {
                    const Function &function = mModule.functions[index];
                    validateType(function.returnType, function.sourceLocation);
                    if (function.name.empty()) { diagnose(function.sourceLocation, "function name is empty."); }
                    for (size_t previous = 0; previous < index; ++previous)
                    {
                        if (mModule.functions[previous].name == function.name) { diagnose(function.sourceLocation, "duplicate function: " + function.name); }
                    }
                    if (function.isEntryPoint)
                    {
                        ++entryCount;
                        if (function.stage == ShaderStage::None || function.stage != mModule.reflection.stage ||
                            function.entryKind == ShaderEntryKind::None || function.entryKind != mModule.reflection.entryKind)
                        {
                            diagnose(function.sourceLocation, "entry point and reflection stage disagree.");
                        }
                        if (function.stage == ShaderStage::Compute &&
                            (function.workgroupSize[0] == 0 || function.workgroupSize[1] == 0 || function.workgroupSize[2] == 0))
                        {
                            diagnose(function.sourceLocation, "compute workgroup dimensions must be positive.");
                        }
                    }
                    for (const FunctionParameter &parameter : function.parameters)
                    {
                        validateType(parameter.type, parameter.sourceLocation);
                        if (parameter.isConstReference && (parameter.passingMode == ParameterPassingMode::Out || parameter.passingMode == ParameterPassingMode::InOut))
                        {
                            diagnose(parameter.sourceLocation, "a const parameter cannot use output parameter passing.");
                        }
                    }
                    for (const Statement &statement : function.body) { validateStatement(statement, function, 0, 0, 0); }
                }
                if (entryCount != 1) { diagnose(mModule.sourceLocation, "a shader module must contain exactly one entry point."); }
                for (size_t index = 0; index < mModule.reflection.resources.size(); ++index)
                {
                    const ResourceBinding &resource = mModule.reflection.resources[index];
                    for (size_t previous = 0; previous < index; ++previous)
                    {
                        const ResourceBinding &other = mModule.reflection.resources[previous];
                        if (resource.bindGroupIndex == other.bindGroupIndex && resource.bindingIndex == other.bindingIndex)
                        {
                            diagnose(resource.sourceLocation, "duplicate resource binding: " + resource.name);
                        }
                    }
                }
                return std::move(mDiagnostics);
            }

        private:
            const Module &mModule;
            std::vector<UGLIRValidationDiagnostic> mDiagnostics;

            /** Appends a source-bound invariant violation using the existing compiler diagnostic ABI. */
            void diagnose(const SourceLocation &location, const std::string &message)
            {
                mDiagnostics.push_back({location, "UGLIR validation: " + message});
            }

            /** Rejects unregistered types while accepting the canonical scalar and vector vocabulary. */
            void validateType(const std::string &name, const SourceLocation &location)
            {
                if (name != "void" && name != "resource_selector" && findTypeByName(mModule, name) == nullptr && !describeValueType(mModule, name))
                {
                    diagnose(location, "unknown type: " + name);
                }
            }

            /** Rejects unsupported widths and impossible aggregate dimensions before either backend consumes them. */
            void validateTypeShape(const Type &type)
            {
                if (type.kind < TypeKind::Void || type.kind > TypeKind::ReferenceAlias)
                {
                    diagnose(type.sourceLocation, "unknown type kind.");
                }
                if (type.scalarKind != ScalarKind::None)
                {
                    const uint32_t expected = type.scalarKind == ScalarKind::Half ? 16u : type.scalarKind == ScalarKind::Bool ? 1u : 32u;
                    if (type.bitWidth != expected) { diagnose(type.sourceLocation, "unsupported scalar bit width in type: " + type.name); }
                }
                if (type.kind == TypeKind::Vector && (type.vectorWidth < 2 || type.vectorWidth > 4))
                {
                    diagnose(type.sourceLocation, "vector dimensions must be between two and four.");
                }
                if (type.kind == TypeKind::Matrix && (type.matrixRows < 2 || type.matrixRows > 4 || type.matrixColumns < 2 || type.matrixColumns > 4))
                {
                    diagnose(type.sourceLocation, "matrix dimensions must be between two and four.");
                }
                if (type.kind == TypeKind::Array && type.arrayCount == 0) { diagnose(type.sourceLocation, "value arrays require a positive element count."); }
                if ((type.kind == TypeKind::Array || type.kind == TypeKind::Workgroup || type.kind == TypeKind::ReferenceAlias) && type.elementType.empty())
                {
                    diagnose(type.sourceLocation, "aggregate type is missing its element type.");
                }
                if (!type.elementType.empty()) { validateType(type.elementType, type.sourceLocation); }
            }

            /** Detects recursive by-value aggregates that would otherwise recurse indefinitely in layout emission. */
            void validateTypeDependencies(const Type &type, std::vector<std::string> &ancestors)
            {
                if (type.kind != TypeKind::Struct && type.kind != TypeKind::Array && type.kind != TypeKind::Workgroup && type.kind != TypeKind::ReferenceAlias) { return; }
                if (ancestors.size() > 512) { diagnose(type.sourceLocation, "type nesting exceeds the supported depth of 512."); return; }
                for (const std::string &ancestor : ancestors)
                {
                    if (ancestor == type.name) { diagnose(type.sourceLocation, "recursive value type: " + type.name); return; }
                }
                ancestors.push_back(type.name);
                for (const TypeField &field : type.fields)
                {
                    if (const Type *dependency = findTypeByName(mModule, field.type)) { validateTypeDependencies(*dependency, ancestors); }
                }
                if (const Type *dependency = findTypeByName(mModule, type.elementType)) { validateTypeDependencies(*dependency, ancestors); }
                ancestors.pop_back();
            }

            /** Returns whether an expression can denote writable storage before target-specific checks. */
            bool isStorageExpression(const Expression &expression) const
            {
                return expression.kind == ExpressionKind::DeclRef || expression.kind == ExpressionKind::MemberRef ||
                       expression.kind == ExpressionKind::Subscript;
            }

            /** Rejects matrix multiplication shapes that cannot share the UGL shader ABI across backends. */
            void validateMathMulShape(const Expression &expression)
            {
                if (expression.intrinsicCallKind != IntrinsicCallKind::MathMul || expression.operands.size() < 2u)
                {
                    return;
                }
                const std::optional<ValueTypeDescription> lhsType = describeValueType(mModule, expression.operands[0].type);
                const std::optional<ValueTypeDescription> rhsType = describeValueType(mModule, expression.operands[1].type);
                if (!lhsType.has_value() || !rhsType.has_value())
                {
                    return;
                }
                const bool lhsIsMatrix = lhsType->matrixRows != 0u && lhsType->matrixColumns != 0u;
                const bool rhsIsMatrix = rhsType->matrixRows != 0u && rhsType->matrixColumns != 0u;
                if (lhsIsMatrix && rhsIsMatrix)
                {
                    if (lhsType->matrixRows != lhsType->matrixColumns || rhsType->matrixRows != rhsType->matrixColumns ||
                        lhsType->matrixRows != rhsType->matrixRows)
                    {
                        diagnose(expression.sourceLocation,
                                 "non-square or mismatched matrix-matrix multiplication is unsupported by the cross-backend shader ABI.");
                    }
                    return;
                }
                if (!lhsIsMatrix && rhsIsMatrix && lhsType->vectorWidth > 1u &&
                    rhsType->matrixRows != rhsType->matrixColumns)
                {
                    diagnose(expression.sourceLocation,
                             "non-square vector-matrix multiplication is unsupported by the cross-backend shader ABI.");
                }
            }

            /** Checks expression shape without evaluating, duplicating, or reordering any operand. */
            void validateExpression(const Expression &expression, size_t depth)
            {
                if (depth > 512) { diagnose(expression.sourceLocation, "expression nesting exceeds the supported depth of 512."); return; }
                validateType(expression.type, expression.sourceLocation);
                if (!expression.computationType.empty())
                {
                    validateType(expression.computationType, expression.sourceLocation);
                    if (expression.kind != ExpressionKind::Store || expression.operatorName.empty() || expression.operatorName == "=")
                        diagnose(expression.sourceLocation, "computation type is only valid for compound assignment.");
                }
                size_t requiredOperands = expression.operands.size();
                switch (expression.kind)
                {
                case ExpressionKind::Literal:
                case ExpressionKind::DeclRef:
                case ExpressionKind::ThisRef: requiredOperands = 0; break;
                case ExpressionKind::MemberRef:
                case ExpressionKind::Cast:
                case ExpressionKind::Unary:
                case ExpressionKind::Load: requiredOperands = 1; break;
                case ExpressionKind::Subscript:
                case ExpressionKind::Binary:
                case ExpressionKind::Store: requiredOperands = 2; break;
                case ExpressionKind::Conditional: requiredOperands = 3; break;
                case ExpressionKind::Call:
                case ExpressionKind::Construct: break;
                default: diagnose(expression.sourceLocation, "unknown expression kind."); return;
                }
                if (expression.operands.size() != requiredOperands)
                {
                    diagnose(expression.sourceLocation, "expression has an invalid operand count.");
                    return;
                }
                if (expression.isPostfix && (expression.kind != ExpressionKind::Unary ||
                    (expression.operatorName != "++" && expression.operatorName != "--")))
                {
                    diagnose(expression.sourceLocation, "postfix evaluation is only valid for increment or decrement.");
                }
                if (expression.isEagerLogical && (expression.kind != ExpressionKind::Binary ||
                    (expression.operatorName != "&&" && expression.operatorName != "||")))
                {
                    diagnose(expression.sourceLocation, "eager logical evaluation is only valid for overloaded logical operators.");
                }
                if (expression.kind == ExpressionKind::Binary && !expression.isEagerLogical &&
                    (expression.operatorName == "&&" || expression.operatorName == "||") && expression.type != "bool")
                {
                    diagnose(expression.sourceLocation, "short-circuit logical expressions must produce a scalar bool.");
                }
                if ((expression.kind == ExpressionKind::Store ||
                    (expression.kind == ExpressionKind::Unary && (expression.operatorName == "++" || expression.operatorName == "--"))) &&
                    !isStorageExpression(expression.operands.front()))
                {
                    diagnose(expression.sourceLocation, "write destination is not a storage expression.");
                }
                if (expression.kind == ExpressionKind::Call && expression.intrinsicCallKind == IntrinsicCallKind::MathMul)
                {
                    validateMathMulShape(expression);
                }
                if (expression.kind == ExpressionKind::Call && expression.intrinsicCallKind == IntrinsicCallKind::None)
                {
                    bool foundCallee = false;
                    for (const Function &callee : mModule.functions)
                    {
                        if (callee.name != expression.name) { continue; }
                        foundCallee = true;
                        if (expression.type != callee.returnType)
                        {
                            diagnose(expression.sourceLocation, "function result type does not match its declaration: " + callee.name);
                        }
                        if (callee.parameters.size() != expression.operands.size())
                        {
                            diagnose(expression.sourceLocation, "function argument count does not match its declaration: " + callee.name);
                            break;
                        }
                        for (size_t index = 0; index < callee.parameters.size(); ++index)
                        {
                            if (expression.operands[index].type != callee.parameters[index].type)
                            {
                                diagnose(expression.operands[index].sourceLocation, "function argument type does not match parameter \"" +
                                    callee.parameters[index].name + "\" of " + callee.name);
                            }
                            const ParameterPassingMode mode = callee.parameters[index].passingMode;
                            if ((mode == ParameterPassingMode::Out || mode == ParameterPassingMode::InOut) && !isStorageExpression(expression.operands[index]))
                            {
                                diagnose(expression.sourceLocation, "output argument does not refer to writable storage: " + callee.name);
                            }
                        }
                        break;
                    }
                    if (!foundCallee) { diagnose(expression.sourceLocation, "function call has no definition: " + expression.name); }
                }
                for (const ConstructComponent &component : expression.constructInfo.components)
                {
                    if (component.operandIndex >= expression.operands.size())
                    {
                        diagnose(expression.sourceLocation, "constructor component references a missing operand.");
                    }
                }
                for (const Expression &operand : expression.operands) { validateExpression(operand, depth + 1); }
            }

            /** Checks statement arity and branch destinations while preserving lexical loop and switch nesting. */
            void validateStatement(const Statement &statement, const Function &function, size_t loops, size_t switches, size_t depth)
            {
                if (depth > 512) { diagnose(statement.sourceLocation, "statement nesting exceeds the supported depth of 512."); return; }
                size_t minimum = 0;
                size_t maximum = 0;
                switch (statement.kind)
                {
                case StatementKind::VariableDeclaration:
                    validateType(statement.type, statement.sourceLocation);
                    maximum = 1; break;
                case StatementKind::Expression:
                case StatementKind::If:
                case StatementKind::While:
                case StatementKind::Do:
                case StatementKind::Switch: minimum = maximum = 1; break;
                case StatementKind::For: maximum = 2; break;
                case StatementKind::Return:
                    if (function.returnType != "void") { minimum = maximum = 1; }
                    break;
                case StatementKind::Break:
                    if (loops == 0 && switches == 0) { diagnose(statement.sourceLocation, "break has no enclosing loop or switch."); }
                    break;
                case StatementKind::Continue:
                    if (loops == 0) { diagnose(statement.sourceLocation, "continue has no enclosing loop."); }
                    break;
                case StatementKind::Block: break;
                default: diagnose(statement.sourceLocation, "unknown statement kind."); return;
                }
                if (statement.expressions.size() < minimum || statement.expressions.size() > maximum)
                {
                    diagnose(statement.sourceLocation, "statement has an invalid expression count.");
                }
                if (statement.kind == StatementKind::For || statement.kind == StatementKind::While || statement.kind == StatementKind::Do) { ++loops; }
                if (statement.kind == StatementKind::Switch) { ++switches; }
                for (const Expression &expression : statement.expressions) { validateExpression(expression, 0); }
                for (const Statement &child : statement.children) { validateStatement(child, function, loops, switches, depth + 1); }
                for (const Statement &child : statement.elseChildren) { validateStatement(child, function, loops, switches, depth + 1); }
                size_t defaultCount = 0;
                for (const SwitchCase &arm : statement.switchCases)
                {
                    if (arm.isDefault) { ++defaultCount; }
                    for (const Expression &label : arm.labels) { validateExpression(label, 0); }
                    for (const Statement &child : arm.body) { validateStatement(child, function, loops, switches, depth + 1); }
                }
                if (defaultCount > 1) { diagnose(statement.sourceLocation, "switch has multiple default arms."); }
            }
        };
    }

    std::vector<UGLIRValidationDiagnostic> validateModule(const Module &module)
    {
        return ModuleValidator(module).validate();
    }
}
