#pragma once

#include <clang/AST/Type.h>

#include <string>
#include <unordered_set>

namespace clang
{
    class CXXRecordDecl;
    class CXXMemberCallExpr;
    class Expr;
    class FunctionDecl;
    class ParmVarDecl;
}

namespace UGLC::CodeGen::PixelLocalFieldAnalysis
{
    struct RenderTargetWriteFieldAnalysis
    {
        std::unordered_set<std::string> fields;
        bool conservativeAllWrites = false;
        size_t renderTargetVariableCount = 0;
        size_t returnedVariableCount = 0;
        size_t writtenVariableCount = 0;
        bool sawUnknownRenderTargetReturn = false;
    };

    const clang::Expr *stripTransparentExprWrappers(const clang::Expr *expr);
    const clang::CXXRecordDecl *getSelfOrPointeeCXXRecordDecl(clang::QualType type);
    bool isDirectPixelLocalInputFieldRead(const clang::CXXMemberCallExpr *expr, const clang::ParmVarDecl *pixelLocalInputParam, std::string *fieldName = nullptr);
    std::unordered_set<std::string> collectPixelLocalReadFields(const clang::FunctionDecl *shaderFunc, const clang::ParmVarDecl *pixelLocalInputParam);
    RenderTargetWriteFieldAnalysis collectRenderTargetWriteFieldAnalysis(const clang::FunctionDecl *shaderFunc, const clang::CXXRecordDecl *renderTargetRecord);
} // namespace UGLC::CodeGen::PixelLocalFieldAnalysis
