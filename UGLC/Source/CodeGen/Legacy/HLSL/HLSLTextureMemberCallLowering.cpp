#include "HLSLTextureMemberCallLowering.hpp"

#include <CodeGen/TextureMemberCallUtils.hpp>

#include <llvm/Support/Casting.h>

#include <stdexcept>
#include <string_view>

namespace UGLC::CodeGen::HLSL
{
    HLSLTextureMemberCallLowering::HLSLTextureMemberCallLowering(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::optional<std::string> HLSLTextureMemberCallLowering::tryTranslateTexture2DMemberCall(const clang::CXXMemberCallExpr *expr, const std::string &textureExpr, const std::string &methodName, const bool isReadWriteTexture)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return textureExpr + ".GetDimensions(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")";
        case TextureMemberCallKind::Write:
            return textureExpr + "[" + translateTextureArgument(expr, 0) + "] = " + translateTextureArgument(expr, 1);
        case TextureMemberCallKind::Read:
            if (isReadWriteTexture)
            {
                return textureExpr + "[" + translateTextureArgument(expr, 0) + "]";
            }
            return textureExpr + ".Load(int3(" + translateTextureArgument(expr, 0) + ", " + translateOptionalTextureArgument(expr, 1, "0") + "))";
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".SampleGrad(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", " + translateTextureArgument(expr, 3) + ")";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".SampleLevel(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ")";
        case TextureMemberCallKind::Sample:
            return textureExpr + ".Sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")";
        case TextureMemberCallKind::GatherCmp: {
            std::string result = textureExpr + ".GatherCmp(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2);
            appendOptionalTextureArgument(result, expr, 3);
            result += ")";
            return result;
        }
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha: {
            std::string result = textureExpr + "." + std::string(getHLSLGatherIntrinsic(methodKind)) + "(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1);
            appendOptionalTextureArgument(result, expr, 2);
            result += ")";
            return result;
        }
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLTextureMemberCallLowering::tryTranslateTexture2DArrayMemberCall(const clang::CXXMemberCallExpr *expr, const std::string &textureExpr, const std::string &methodName, const bool isReadWriteTexture)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return textureExpr + ".GetDimensions(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ")";
        case TextureMemberCallKind::Write:
            return textureExpr + "[uint3(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")] = " + translateTextureArgument(expr, 2);
        case TextureMemberCallKind::Read: {
            const std::string layerExpr = translateOptionalTextureArgument(expr, 1, "0");
            if (isReadWriteTexture)
            {
                return textureExpr + "[uint3(" + translateTextureArgument(expr, 0) + ", " + layerExpr + ")]";
            }
            return textureExpr + ".Load(int4(" + translateTextureArgument(expr, 0) + ", " + layerExpr + ", " + translateOptionalTextureArgument(expr, 2, "0") + "))";
        }
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".SampleGrad(" + translateTextureArgument(expr, 0) + ", float3(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + "), " + translateTextureArgument(expr, 3) + ", " + translateTextureArgument(expr, 4) + ")";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".SampleLevel(" + translateTextureArgument(expr, 0) + ", float3(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + "), " + translateTextureArgument(expr, 3) + ")";
        case TextureMemberCallKind::Sample:
            return textureExpr + ".Sample(" + translateTextureArgument(expr, 0) + ", float3(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + "))";
        case TextureMemberCallKind::GatherCmp: {
            std::string result = textureExpr + ".GatherCmp(" + translateTextureArgument(expr, 0) + ", float3(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 3) + "), " + translateTextureArgument(expr, 2);
            appendOptionalTextureArgument(result, expr, 4);
            result += ")";
            return result;
        }
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha: {
            std::string result = textureExpr + "." + std::string(getHLSLGatherIntrinsic(methodKind)) + "(" + translateTextureArgument(expr, 0) + ", float3(" + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ")";
            appendOptionalTextureArgument(result, expr, 3);
            result += ")";
            return result;
        }
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::optional<std::string> HLSLTextureMemberCallLowering::tryTranslateTexture3DMemberCall(const clang::CXXMemberCallExpr *expr, const std::string &textureExpr, const std::string &methodName, const bool isReadWriteTexture)
    {
        const TextureMemberCallKind methodKind = classifyTextureMemberCall(methodName);
        switch (methodKind)
        {
        case TextureMemberCallKind::GetDimensions:
            return textureExpr + ".GetDimensions(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ")";
        case TextureMemberCallKind::Write:
            return textureExpr + "[" + translateTextureArgument(expr, 0) + "] = " + translateTextureArgument(expr, 1);
        case TextureMemberCallKind::Read:
            if (isReadWriteTexture)
            {
                return textureExpr + "[" + translateTextureArgument(expr, 0) + "]";
            }
            return textureExpr + ".Load(int4(" + translateTextureArgument(expr, 0) + ", " + translateOptionalTextureArgument(expr, 1, "0") + "))";
        case TextureMemberCallKind::SampleGrad:
            return textureExpr + ".SampleGrad(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ", " + translateTextureArgument(expr, 3) + ")";
        case TextureMemberCallKind::SampleLevel:
            return textureExpr + ".SampleLevel(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ", " + translateTextureArgument(expr, 2) + ")";
        case TextureMemberCallKind::Sample:
            return textureExpr + ".Sample(" + translateTextureArgument(expr, 0) + ", " + translateTextureArgument(expr, 1) + ")";
        case TextureMemberCallKind::Gather:
        case TextureMemberCallKind::GatherRed:
        case TextureMemberCallKind::GatherGreen:
        case TextureMemberCallKind::GatherBlue:
        case TextureMemberCallKind::GatherAlpha:
        case TextureMemberCallKind::GatherCmp:
            throw std::runtime_error("Texture3D does not support gather lowering.");
        case TextureMemberCallKind::Unknown:
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::string HLSLTextureMemberCallLowering::translateTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex) const
    {
        return mVisitor.TranslateExpr(expr->getArg(argIndex));
    }

    std::string HLSLTextureMemberCallLowering::translateOptionalTextureArgument(const clang::CXXMemberCallExpr *expr, unsigned argIndex, std::string_view fallbackValue) const
    {
        if (expr->getNumArgs() <= argIndex || isDefaultArgumentExpr(expr->getArg(argIndex)))
        {
            return std::string(fallbackValue);
        }
        return translateTextureArgument(expr, argIndex);
    }

    void HLSLTextureMemberCallLowering::appendOptionalTextureArgument(std::string &result, const clang::CXXMemberCallExpr *expr, unsigned argIndex) const
    {
        if (expr->getNumArgs() > argIndex && !isDefaultArgumentExpr(expr->getArg(argIndex)))
        {
            result += ", " + translateTextureArgument(expr, argIndex);
        }
    }

    bool HLSLTextureMemberCallLowering::isDefaultArgumentExpr(const clang::Expr *expr)
    {
        if (expr == nullptr)
        {
            return true;
        }
        return llvm::isa<clang::CXXDefaultArgExpr>(expr) || llvm::isa<clang::CXXDefaultInitExpr>(expr);
    }
} // namespace UGLC::CodeGen::HLSL
