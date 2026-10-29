#include "HLSLShaderBuiltinTranslator.hpp"

#include <CodeGen/ShaderBarrierBuiltinUtils.hpp>
#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/UGLC.Constants.hpp>

namespace UGLC::CodeGen::HLSL
{
    HLSLShaderBuiltinTranslator::HLSLShaderBuiltinTranslator(BaseASTVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    void HLSLShaderBuiltinTranslator::validateUnsupportedBuiltinOrThrow(const clang::FunctionDecl *callee, const clang::FunctionDecl *entryFunction) const
    {
        if (callee == nullptr)
        {
            return;
        }

        const std::string qualifiedName = callee->getCanonicalDecl()->getQualifiedNameAsString();
        const auto &capabilities = getHLSLSPIRVShaderBackendCapabilities();
        if (qualifiedName == mUGLFunctionWaveGetLaneIndexName || qualifiedName == mUGLFunctionWaveGetLaneCountName)
        {
            validateWaveLaneQuerySupportOrThrow(capabilities, entryFunction);
        }
        if (qualifiedName == mUGLFunctionWaveReadLaneAtName)
        {
            validateWaveReadLaneAtSupportOrThrow(capabilities, entryFunction);
        }
        if (qualifiedName == mUGLFunctionWaveReadLaneFirstName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WaveReadLaneFirst");
        }
        if (qualifiedName == mUGLFunctionWaveActiveBallotName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WaveActiveBallot");
        }
        if (qualifiedName == mUGLFunctionWaveActiveCountBitsName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WaveActiveCountBits");
        }
        if (qualifiedName == mUGLFunctionWavePrefixCountBitsName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WavePrefixCountBits");
        }
        if (qualifiedName == mUGLFunctionWavePrefixSumName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WavePrefixSum");
        }
        if (qualifiedName == mUGLFunctionWaveMatchName)
        {
            validateWaveCollectiveSupportOrThrow(capabilities, entryFunction, "WaveMatch");
        }
        if (qualifiedName == mUGLFunctionQuadReadLaneAtName || qualifiedName == mUGLFunctionQuadReadAcrossXName || qualifiedName == mUGLFunctionQuadReadAcrossYName || qualifiedName == mUGLFunctionQuadReadAcrossDiagonalName || qualifiedName == mUGLFunctionWaveReadAcrossXName || qualifiedName == mUGLFunctionWaveReadAcrossYName || qualifiedName == mUGLFunctionWaveReadAcrossDiagonalName)
        {
            return;
        }
    }

    std::optional<std::string> HLSLShaderBuiltinTranslator::tryTranslateCallExpr(const clang::CallExpr *expr, const clang::FunctionDecl *callee, const clang::FunctionDecl *entryFunction) const
    {
        validateUnsupportedBuiltinOrThrow(callee, entryFunction);
        if (expr == nullptr || callee == nullptr)
        {
            return std::nullopt;
        }

        const std::string qualifiedName = callee->getCanonicalDecl()->getQualifiedNameAsString();
        if (qualifiedName == mUGLFunctionDiscardFragmentName)
        {
            validateDiscardFragmentSupportOrThrow(entryFunction);
            return "discard";
        }
        if (qualifiedName == mUGLFunctionClipName)
        {
            validateFragmentKillBuiltinSupportOrThrow(entryFunction, "clip");
            return "clip(" + mVisitor.TranslateExpr(expr->getArg(0)) + ")";
        }

        const ShaderBarrierBuiltinKind barrierKind = classifyShaderBarrierBuiltin(qualifiedName);
        if (isShaderBarrierBuiltin(barrierKind))
        {
            validateShaderBarrierBuiltinSupportOrThrow(entryFunction, getShaderBarrierBuiltinDisplayName(barrierKind));
            return makeHLSLShaderBarrierBuiltinCall(barrierKind);
        }

        if (qualifiedName == mUGLFunctionWaveReadAcrossXName)
        {
            return "QuadReadAcrossX(" + mVisitor.TranslateExpr(expr->getArg(0)) + ")";
        }
        if (qualifiedName == mUGLFunctionWaveReadAcrossYName)
        {
            return "QuadReadAcrossY(" + mVisitor.TranslateExpr(expr->getArg(0)) + ")";
        }
        if (qualifiedName == mUGLFunctionWaveReadAcrossDiagonalName)
        {
            return "QuadReadAcrossDiagonal(" + mVisitor.TranslateExpr(expr->getArg(0)) + ")";
        }

        return std::nullopt;
    }
} // namespace UGLC::CodeGen::HLSL
