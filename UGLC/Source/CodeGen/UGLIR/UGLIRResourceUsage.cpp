#include "UGLIRResourceUsage.hpp"
#include "UGLIRTypeUtils.hpp"

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Joins resource member names using the reflection spelling. */
        std::string joinResourcePath(const std::vector<std::string> &path)
        {
            std::string result;
            for (const std::string &segment : path)
            {
                if (segment.empty())
                {
                    continue;
                }
                if (!result.empty())
                {
                    result += ".";
                }
                result += segment;
            }
            return result;
        }

        /** Resolves member paths rooted in shader this or a reflected resource group. */
        bool collectResourceMemberPath(const Module &module, const Expression &expression, std::vector<std::string> &memberPath)
        {
            if (expression.kind == ExpressionKind::ThisRef)
            {
                return true;
            }
            if (expression.kind == ExpressionKind::DeclRef)
            {
                const std::string prefix = expression.name + ".";
                for (const ResourceBinding &resource : module.reflection.resources)
                {
                    if (resource.name.rfind(prefix, 0) == 0)
                    {
                        memberPath.push_back(expression.name);
                        return true;
                    }
                }
                return false;
            }
            if (expression.kind == ExpressionKind::Cast ||
                expression.kind == ExpressionKind::Load ||
                expression.kind == ExpressionKind::Construct)
            {
                return !expression.operands.empty() && collectResourceMemberPath(module, expression.operands.front(), memberPath);
            }
            if (expression.kind != ExpressionKind::MemberRef || expression.operands.empty())
            {
                return false;
            }
            if (!collectResourceMemberPath(module, expression.operands.front(), memberPath))
            {
                return false;
            }
            if (!expression.name.empty())
            {
                memberPath.push_back(expression.name);
            }
            return true;
        }

        /** Records resource references and resolved helper calls without counting erased resource-alias arguments. */
        void collectFunctionExpressionResources(const Module &module, const std::unordered_map<std::string, const Function *> &functionsByName, const Expression &expression, FunctionResourceUsage &usage)
        {
            if (const ResourceBinding *resource = findReflectedResource(module, expression))
            {
                for (size_t index = 0; index < module.reflection.resources.size(); ++index)
                {
                    if (&module.reflection.resources[index] == resource)
                    {
                        usage.requiredResources[index] = 1u;
                        break;
                    }
                }
            }
            const auto callee = functionsByName.find(expression.name);
            const bool internalCall = expression.kind == ExpressionKind::Call &&
                                      expression.intrinsicCallKind == IntrinsicCallKind::None &&
                                      callee != functionsByName.end() && !callee->second->isEntryPoint;
            if (internalCall)
            {
                usage.callees.push_back(callee->second);
            }
            for (const Expression &operand : expression.operands)
            {
                const Type *operandType = findTypeByName(module, operand.type);
            if (!internalCall || operandType == nullptr || operandType->kind != TypeKind::Resource)
                {
                    collectFunctionExpressionResources(module, functionsByName, operand, usage);
                }
            }
        }

        /** Collects resources from every expression and nested control-flow arm in a function body. */
        void collectFunctionStatementResources(const Module &module, const std::unordered_map<std::string, const Function *> &functionsByName, const std::vector<Statement> &statements, FunctionResourceUsage &usage)
        {
            for (const Statement &statement : statements)
            {
                for (const Expression &expression : statement.expressions)
                {
                    collectFunctionExpressionResources(module, functionsByName, expression, usage);
                }
                collectFunctionStatementResources(module, functionsByName, statement.children, usage);
                collectFunctionStatementResources(module, functionsByName, statement.elseChildren, usage);
                for (const SwitchCase &switchCase : statement.switchCases)
                {
                    for (const Expression &label : switchCase.labels)
                    {
                        collectFunctionExpressionResources(module, functionsByName, label, usage);
                    }
                    collectFunctionStatementResources(module, functionsByName, switchCase.body, usage);
                }
            }
        }

    } // namespace

    /** Resolves resource identity independently of a target backend. */
    const ResourceBinding *findReflectedResource(const Module &module, const Expression &expression)
    {
        if (expression.kind == ExpressionKind::DeclRef)
        {
            for (const ResourceBinding &resource : module.reflection.resources)
                if (resource.name == expression.name) return &resource;
        }
        std::vector<std::string> path;
        if (!collectResourceMemberPath(module, expression, path)) return nullptr;
        const std::string name = joinResourcePath(path);
        for (const ResourceBinding &resource : module.reflection.resources)
            if (resource.name == name) return &resource;
        return nullptr;
    }

    /** Computes transitive function resource requirements while leaving reflected bindings unchanged. */
    FunctionResourceUsageMap collectFunctionResourceRequirements(const Module &module)
    {
        FunctionResourceUsageMap usageByFunction;
        std::unordered_map<std::string, const Function *> functionsByName;
        for (const Function &function : module.functions) functionsByName.emplace(function.name, &function);
        for (const Function &function : module.functions)
        {
            FunctionResourceUsage &usage = usageByFunction[&function];
            usage.requiredResources.resize(module.reflection.resources.size(), 0u);
            collectFunctionStatementResources(module, functionsByName, function.body, usage);
        }
        bool changed = true;
        while (changed)
        {
            changed = false;
            for (auto &entry : usageByFunction)
            {
                FunctionResourceUsage &usage = entry.second;
                for (const Function *callee : usage.callees)
                {
                    const auto &calleeResources = usageByFunction.at(callee).requiredResources;
                    for (size_t index = 0; index < usage.requiredResources.size(); ++index)
                    {
                        if (calleeResources[index] && !usage.requiredResources[index])
                        {
                            usage.requiredResources[index] = 1u;
                            changed = true;
                        }
                    }
                }
            }
        }
        return usageByFunction;
    }

} // namespace UGLC::CodeGen::UGLIR
