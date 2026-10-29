#pragma once

#include "UGLIRCore.hpp"
#include <unordered_map>

namespace UGLC::CodeGen::UGLIR
{
    /** Holds transitive resource requirements in reflection order and direct callees; the unchanged module must outlive this result. */
    struct FunctionResourceUsage
    {
        std::vector<uint8_t> requiredResources;
        std::vector<const Function *> callees;
    };

    using FunctionResourceUsageMap = std::unordered_map<const Function *, FunctionResourceUsage>;

    /** Resolves a direct or member-path resource reference; returns null for value accesses and local aliases. */
    const ResourceBinding *findReflectedResource(const Module &module, const Expression &expression);

    /** Computes function resource requirements through resolved calls without changing reflection or bindings.
     * Resource-alias arguments are represented by specialized callee bodies, not runtime value parameters.
     * The module must remain alive and unchanged while consumers use the returned function pointers.
     */
    FunctionResourceUsageMap collectFunctionResourceRequirements(const Module &module);
} // namespace UGLC::CodeGen::UGLIR
