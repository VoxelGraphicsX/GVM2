#include "MSLShaderBuiltinTranslator.hpp"

#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/ShaderBackendValidation.hpp>
#include <CodeGen/ShaderBarrierBuiltinUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

namespace UGLC::CodeGen::MSL
{
    MSLShaderBuiltinTranslator::MSLShaderBuiltinTranslator(BaseASTVisitor &visitor, MSLWaveBuiltinAnalyzer &waveBuiltinAnalyzer)
        : mVisitor(visitor),
          mWaveBuiltinAnalyzer(waveBuiltinAnalyzer)
    {
    }

    std::optional<std::string> MSLShaderBuiltinTranslator::tryTranslateCallExpr(const clang::CallExpr *expr,
                                                                                const clang::FunctionDecl *callee,
                                                                                const clang::FunctionDecl *entryFunction,
                                                                                const clang::FunctionDecl *currentFunction) const
    {
        if (expr == nullptr || callee == nullptr)
        {
            return std::nullopt;
        }

        const std::string qualifiedName = callee->getCanonicalDecl()->getQualifiedNameAsString();
        if (qualifiedName == mUGLFunctionDiscardFragmentName)
        {
            validateDiscardFragmentSupportOrThrow(entryFunction);
            return "discard_fragment()";
        }
        if (qualifiedName == mUGLFunctionClipName)
        {
            validateFragmentKillBuiltinSupportOrThrow(entryFunction, "clip");
            return "UGLC_clip(" + mVisitor.TranslateExpr(expr->getArg(0)) + ")";
        }

        const ShaderBarrierBuiltinKind barrierKind = classifyShaderBarrierBuiltin(qualifiedName);
        if (isShaderBarrierBuiltin(barrierKind))
        {
            validateShaderBarrierBuiltinSupportOrThrow(entryFunction, getShaderBarrierBuiltinDisplayName(barrierKind));
            return makeMSLShaderBarrierBuiltinCall(barrierKind);
        }

        if (qualifiedName == mUGLFunctionWaveGetLaneIndexName)
        {
            return mWaveBuiltinAnalyzer.getCurrentBuiltinValueName(currentFunction, true);
        }
        if (qualifiedName == mUGLFunctionWaveGetLaneCountName)
        {
            return mWaveBuiltinAnalyzer.getCurrentBuiltinValueName(currentFunction, false);
        }
        if (qualifiedName == mUGLFunctionWaveReadLaneAtName)
        {
            if (entryFunction != nullptr && entryFunction->getNameAsString() != mUGLComputeShaderFunctionName)
            {
                validateWaveReadLaneAtSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction);
            }
        }
        if (qualifiedName == mUGLFunctionWaveReadLaneFirstName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WaveReadLaneFirst");
        }
        if (qualifiedName == mUGLFunctionWaveActiveBallotName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WaveActiveBallot");
            return "UGLC_WaveActiveBallot(" + mVisitor.TranslateExpr(expr->getArg(0)) + ", " + mWaveBuiltinAnalyzer.getCurrentBuiltinValueName(currentFunction, false) + ")";
        }
        if (qualifiedName == mUGLFunctionWaveActiveCountBitsName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WaveActiveCountBits");
        }
        if (qualifiedName == mUGLFunctionWavePrefixCountBitsName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WavePrefixCountBits");
        }
        if (qualifiedName == mUGLFunctionWavePrefixSumName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WavePrefixSum");
        }
        if (qualifiedName == mUGLFunctionWaveMatchName)
        {
            validateWaveCollectiveSupportOrThrow(getMSLShaderBackendCapabilities(), entryFunction, "WaveMatch");
            return "UGLC_WaveMatch(" + mVisitor.TranslateExpr(expr->getArg(0)) + ", " + mWaveBuiltinAnalyzer.getCurrentBuiltinValueName(currentFunction, true) + ", " + mWaveBuiltinAnalyzer.getCurrentBuiltinValueName(currentFunction, false) + ")";
        }

        return std::nullopt;
    }
} // namespace UGLC::CodeGen::MSL
