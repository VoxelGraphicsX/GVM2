#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clang
{
    class CXXMemberCallExpr;
    class CXXRecordDecl;
    class FieldDecl;
    class FunctionDecl;
    class ParmVarDecl;
}

namespace UGLC::CodeGen
{
    class BaseASTVisitor;
}

namespace UGLC::CodeGen::PixelLocalInputPlan
{
    struct AttachmentPlan
    {
        const clang::FieldDecl *fieldDecl = nullptr;
        std::string fieldName;
        std::uint32_t colorAttachmentIndex = 0u;
        std::uint32_t inputAttachmentIndex = 0u;
    };

    struct ParameterPlan
    {
        const clang::ParmVarDecl *paramDecl = nullptr;
        const clang::CXXRecordDecl *recordDecl = nullptr;
        std::vector<AttachmentPlan> attachments;
    };

    struct FunctionPlan
    {
        std::vector<ParameterPlan> parameters;
    };

    bool functionHasPixelLocalInputParameter(const BaseASTVisitor &visitor, const clang::FunctionDecl *func);
    bool isDirectPixelLocalInputFieldRead(const BaseASTVisitor &visitor, const clang::FunctionDecl *func, const clang::CXXMemberCallExpr *expr);
    ParameterPlan collectParameterPlan(const BaseASTVisitor &visitor, const clang::FunctionDecl *shaderFunc, const clang::ParmVarDecl *param);
    FunctionPlan collectFunctionPlan(const BaseASTVisitor &visitor, const clang::FunctionDecl *shaderFunc);
} // namespace UGLC::CodeGen::PixelLocalInputPlan
