#pragma once

#include <CodeGen/BaseShaderBinding.hpp>

#include <clang/AST/RecursiveASTVisitor.h>
#include <map>
#include <string>
#include <vector>
namespace UGLC::CodeGen
{
    struct ShaderBindGroupInfo
    {
        bool isRenderSet = false;
        std::string name; // 绑定组名称
        std::string type;
        int bindingIndex;                         // 绑定索引
        clang::CXXRecordDecl *typeDecl = nullptr; // 绑定组类型的 CXXRecordDecl
        // Keep the base resource view attached to the bind-group slot so
        // host layout generation and every backend can share the same binding
        // metadata instead of re-inferring resource semantics independently.
        std::vector<BaseShaderResourceBinding> resourceBindings;
    };
    using BindGroupInfoMap = std::map<int, ShaderBindGroupInfo>;
} // namespace UGLC::CodeGen
