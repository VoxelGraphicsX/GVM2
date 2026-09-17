#include "CPPHostShaderOnlyGuard.hpp"

#include "CPPVisitor.hpp"

#include <CodeGen/RenderSetMemberCallUtils.hpp>
#include <CodeGen/ShaderBarrierBuiltinUtils.hpp>
#include <CodeGen/ShaderTypeClassificationUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>
#include <clang/AST/ExprCXX.h>
#include <llvm/Support/Casting.h>

namespace UGLC::CodeGen::CPP
{
    namespace
    {
        std::optional<std::string> describeStructuredBufferSubscriptOperation(CPPVisitor &visitor, const clang::CXXOperatorCallExpr *operatorCall)
        {
            if (operatorCall == nullptr || operatorCall->getOperator() != clang::OO_Subscript || operatorCall->getNumArgs() == 0)
            {
                return std::nullopt;
            }

            const std::string objectTypeName = visitor.generateTypeCanonicalName(visitor.getUnqualifiedType(operatorCall->getArg(0)->getType()));
            if (objectTypeName.starts_with("UGL::StructuredBuffer<"))
            {
                return "UGL::StructuredBuffer::operator[]";
            }
            if (objectTypeName.starts_with("UGL::RWStructuredBuffer<"))
            {
                return "UGL::RWStructuredBuffer::operator[]";
            }

            return std::nullopt;
        }

        /** Returns a host shader-only label for RenderSet data-pack calls. */
        std::optional<std::string> describeRenderSetDataPackOperation(CPPVisitor &visitor, const clang::CXXMemberCallExpr *memberCall)
        {
            if (memberCall == nullptr || memberCall->getMethodDecl() == nullptr)
            {
                return std::nullopt;
            }

            const clang::CXXMethodDecl *methodDecl = memberCall->getMethodDecl();
            const std::string methodName = methodDecl->getNameAsString();
            const std::string ownerTypeName = visitor.getClassCanonicalName(methodDecl->getParent(), nullptr);
            if (isRenderSetBufferComponentType(ownerTypeName))
            {
                switch (classifyRenderSetBufferComponentCall(methodName))
                {
                case RenderSetBufferComponentCallKind::GetRaw:
                case RenderSetBufferComponentCallKind::CheckValid:
                case RenderSetBufferComponentCallKind::Get:
                    return "UGL::BufferComponentDataPack::" + methodName;
                case RenderSetBufferComponentCallKind::Unknown:
                    break;
                }
            }

            if (isRenderSetTextureComponentType(ownerTypeName))
            {
                switch (classifyRenderSetTextureComponentCall(methodName))
                {
                case RenderSetTextureComponentCallKind::Get:
                    return "UGL::TextureComponentDataPack::" + methodName;
                case RenderSetTextureComponentCallKind::Unknown:
                    break;
                }
            }

            if (isRenderSetDataPackType(ownerTypeName))
            {
                switch (classifyRenderSetDataPackCall(methodName))
                {
                case RenderSetDataPackCallKind::GetRenderEntityInfo:
                case RenderSetDataPackCallKind::GetRenderEntityCMDParams:
                case RenderSetDataPackCallKind::GetRenderEntityIndexCount:
                case RenderSetDataPackCallKind::GetRenderEntityInstanceCount:
                case RenderSetDataPackCallKind::GetRenderEntityFirstIndex:
                case RenderSetDataPackCallKind::GetRenderEntityVertexOffset:
                case RenderSetDataPackCallKind::GetRenderEntityGlobalInstanceBase:
                case RenderSetDataPackCallKind::GetRenderEntityVersion:
                case RenderSetDataPackCallKind::CheckValid:
                    return "UGL::RenderSetDataPack::" + methodName;
                case RenderSetDataPackCallKind::Unknown:
                    break;
                }
            }

            return std::nullopt;
        }
    } // namespace

    CPPHostShaderOnlyGuard::CPPHostShaderOnlyGuard(CPPVisitor &visitor)
        : mVisitor(visitor)
    {
    }

    std::optional<std::string> CPPHostShaderOnlyGuard::describeTextureOperation(const std::string &objectTypeName, const std::string &methodName) const
    {
        if (methodName != "sample" && methodName != "sampleLevel" && methodName != "sampleGrad" && methodName != "gather" && methodName != "gatherRed" && methodName != "gatherGreen" && methodName != "gatherBlue" && methodName != "gatherAlpha" && methodName != "gatherCmp" && methodName != "read" && methodName != "write" && methodName != "getDimensions")
        {
            return std::nullopt;
        }

        std::string typeLabel;
        if (objectTypeName.starts_with("UGL::Texture2D<"))
        {
            typeLabel = "UGL::Texture2D";
        }
        else if (objectTypeName.starts_with(mUGLShaderTexture2DAccessPacker) || objectTypeName.starts_with(mUGLShaderBaseTexture2DAccessPacker))
        {
            typeLabel = "UGL::Texture2D";
        }
        else if (objectTypeName.starts_with("UGL::Texture2DArray<"))
        {
            typeLabel = "UGL::Texture2DArray";
        }
        else if (objectTypeName.starts_with(mUGLShaderTexture2DArrayAccessPacker) || objectTypeName.starts_with(mUGLShaderBaseTexture2DArrayAccessPacker))
        {
            typeLabel = "UGL::Texture2DArray";
        }
        else if (objectTypeName.starts_with("UGL::Texture3D<"))
        {
            typeLabel = "UGL::Texture3D";
        }
        else if (objectTypeName.starts_with(mUGLShaderTexture3DAccessPacker))
        {
            typeLabel = "UGL::Texture3D";
        }
        else if (objectTypeName.starts_with("UGL::RWTexture2D<"))
        {
            typeLabel = "UGL::RWTexture2D";
        }
        else if (objectTypeName.starts_with(mUGLShaderRWTexture2DAccessPacker))
        {
            typeLabel = "UGL::RWTexture2D";
        }
        else if (objectTypeName.starts_with("UGL::RWTexture2DArray<"))
        {
            typeLabel = "UGL::RWTexture2DArray";
        }
        else if (objectTypeName.starts_with(mUGLShaderRWTexture2DArrayAccessPacker))
        {
            typeLabel = "UGL::RWTexture2DArray";
        }
        else if (objectTypeName.starts_with("UGL::RWTexture3D<"))
        {
            typeLabel = "UGL::RWTexture3D";
        }
        else if (objectTypeName.starts_with(mUGLShaderRWTexture3DAccessPacker))
        {
            typeLabel = "UGL::RWTexture3D";
        }

        if (typeLabel.empty())
        {
            return std::nullopt;
        }

        return typeLabel + "::" + methodName;
    }

    std::string CPPHostShaderOnlyGuard::makeCallExpr(const std::string &returnTypeName, const std::string &operationName) const
    {
        return "[]<class __UGLC_Dummy = void>() -> " + returnTypeName + " { static_assert(::UGL::UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, \"UGLC generated host stub reached for shader-only operation: " + operationName + "\"); return {}; }()";
    }

    std::string CPPHostShaderOnlyGuard::makeVoidCallExpr(const std::string &operationName) const
    {
        return "[]<class __UGLC_Dummy = void>() { static_assert(::UGL::UGLC_HostDetail::AlwaysFalse_v<__UGLC_Dummy>, \"UGLC generated host stub reached for shader-only operation: " + operationName + "\"); }()";
    }

    std::string CPPHostShaderOnlyGuard::normalizeScalarVectorCanonicalName(const std::string &canonicalName) const
    {
        if (canonicalName == "float" || canonicalName == "float1" || canonicalName == "UGL::float1")
        {
            return "UGL::float";
        }
        if (canonicalName == "float2" || canonicalName == "float3" || canonicalName == "float4")
        {
            return "UGL::" + canonicalName;
        }
        if (canonicalName == "int" || canonicalName == "int1" || canonicalName == "UGL::int1")
        {
            return "UGL::int";
        }
        if (canonicalName == "int2" || canonicalName == "int3" || canonicalName == "int4")
        {
            return "UGL::" + canonicalName;
        }
        if (canonicalName == "uint" || canonicalName == "uint1" || canonicalName == "unsigned int" || canonicalName == "UGL::uint1")
        {
            return "UGL::uint";
        }
        if (canonicalName == "uint2" || canonicalName == "uint3" || canonicalName == "uint4")
        {
            return "UGL::" + canonicalName;
        }
        if (canonicalName == "half" || canonicalName == "half1" || canonicalName == "UGL::half1")
        {
            return "UGL::half";
        }
        if (canonicalName == "half2" || canonicalName == "half3" || canonicalName == "half4")
        {
            return "UGL::" + canonicalName;
        }
        if (canonicalName == "bool" || canonicalName == "bool1" || canonicalName == "UGL::bool1")
        {
            return "UGL::bool";
        }
        if (canonicalName == "bool2" || canonicalName == "bool3" || canonicalName == "bool4")
        {
            return "UGL::" + canonicalName;
        }

        return canonicalName;
    }

    std::string CPPHostShaderOnlyGuard::inferTextureAccessReturnType(const clang::QualType &textureElementType, int unwrapDepth)
    {
        if (unwrapDepth > 8)
        {
            mVisitor.throwCodegenError("Exceeded texture element unwrap depth while inferring a host shader-only stub return type for \"" + mVisitor.generateTypeCanonicalName(textureElementType) + "\".");
        }

        clang::QualType resolvedType = mVisitor.getUnqualifiedType(textureElementType);
        std::string canonicalName = normalizeScalarVectorCanonicalName(mVisitor.generateTypeCanonicalName(resolvedType));
        std::string diagnosticTypeName = mVisitor.generateTypeCanonicalName(resolvedType);
        if (diagnosticTypeName == "_Bool")
        {
            diagnosticTypeName = "bool";
        }

        if (resolvedType->isBooleanType() || canonicalName == "UGL::bool" || canonicalName == "UGL::bool2" || canonicalName == "UGL::bool3" || canonicalName == "UGL::bool4")
        {
            mVisitor.throwCodegenError("Cannot synthesize a host shader-only stub return type for unsupported texture element type \"" + diagnosticTypeName + "\".");
        }

        if (resolvedType->isRealFloatingType() || canonicalName == "UGL::float" || canonicalName == "UGL::float2" || canonicalName == "UGL::float3" || canonicalName == "UGL::float4")
        {
            return "float4";
        }

        if (canonicalName == "UGL::half" || canonicalName == "UGL::half2" || canonicalName == "UGL::half3" || canonicalName == "UGL::half4")
        {
            return "half4";
        }

        if (resolvedType->isUnsignedIntegerType() || canonicalName == "UGL::uint" || canonicalName == "UGL::uint2" || canonicalName == "UGL::uint3" || canonicalName == "UGL::uint4")
        {
            return "uint4";
        }

        if (resolvedType->isSignedIntegerType() || canonicalName == "UGL::int" || canonicalName == "UGL::int2" || canonicalName == "UGL::int3" || canonicalName == "UGL::int4")
        {
            return "int4";
        }

        if (auto trueType = mVisitor.resolveRecordNestedTrueType(resolvedType))
        {
            return inferTextureAccessReturnType(*trueType, unwrapDepth + 1);
        }

        mVisitor.throwCodegenError("Cannot synthesize a host shader-only stub return type for unsupported texture element type \"" + diagnosticTypeName + "\".");
    }

    std::optional<std::string> CPPHostShaderOnlyGuard::inferTextureReturnType(const clang::QualType &objectType, const std::string &methodName)
    {
        if (methodName == "write" || methodName == "getDimensions")
        {
            return std::nullopt;
        }

        clang::QualType resolvedType = mVisitor.getUnqualifiedType(objectType);
        std::string objectTypeName = mVisitor.generateTypeCanonicalName(resolvedType);
        const auto templateArgs = mVisitor.getTemplateArgumentsFromType(resolvedType);
        auto requireFirstTemplateTypeArgument = [&]() -> clang::QualType {
            if (templateArgs.empty() || templateArgs.front().getAsType().isNull())
            {
                mVisitor.throwCodegenError("Cannot infer a host shader-only stub return type from texture object type \"" + objectTypeName + "\" because it does not expose a first template type argument.");
            }

            return mVisitor.getUnqualifiedType(templateArgs.front().getAsType());
        };

        if (objectTypeName.starts_with("UGL::Texture2D<") || objectTypeName.starts_with("UGL::Texture2DArray<") || objectTypeName.starts_with("UGL::Texture3D<") || objectTypeName.starts_with("UGL::RWTexture3D<") || objectTypeName.starts_with(mUGLShaderTexture2DAccessPacker) || objectTypeName.starts_with(mUGLShaderBaseTexture2DAccessPacker) || objectTypeName.starts_with(mUGLShaderTexture2DArrayAccessPacker) || objectTypeName.starts_with(mUGLShaderBaseTexture2DArrayAccessPacker) || objectTypeName.starts_with(mUGLShaderTexture3DAccessPacker) || objectTypeName.starts_with(mUGLShaderRWTexture2DAccessPacker) ||
            objectTypeName.starts_with(mUGLShaderRWTexture2DArrayAccessPacker) || objectTypeName.starts_with(mUGLShaderRWTexture3DAccessPacker))
        {
            return inferTextureAccessReturnType(requireFirstTemplateTypeArgument());
        }

        if (objectTypeName.starts_with("UGL::RWTexture2D<") || objectTypeName.starts_with("UGL::RWTexture2DArray<"))
        {
            return mVisitor.generateTypeCanonicalName(requireFirstTemplateTypeArgument(), &mVisitor.mTypeConvertor);
        }

        return std::nullopt;
    }

    bool CPPHostShaderOnlyGuard::isKnownFunction(const clang::FunctionDecl *func) const
    {
        if (func == nullptr)
        {
            return false;
        }

        const std::string qualifiedName = func->getQualifiedNameAsString();
        if (isShaderBarrierBuiltin(classifyShaderBarrierBuiltin(qualifiedName)))
        {
            return true;
        }
        if (qualifiedName == mUGLFunctionDiscardFragmentName)
        {
            return true;
        }
        if (qualifiedName == mUGLFunctionClipName)
        {
            return true;
        }
        if (qualifiedName == "UGL::sincos")
        {
            return true;
        }
        return qualifiedName == mUGLFunctionWaveGetLaneIndexName || qualifiedName == mUGLFunctionWaveGetLaneCountName || qualifiedName == mUGLFunctionWaveReadLaneAtName || qualifiedName == mUGLFunctionWaveReadLaneFirstName || qualifiedName == mUGLFunctionWaveActiveBallotName || qualifiedName == mUGLFunctionWaveActiveCountBitsName || qualifiedName == mUGLFunctionWavePrefixCountBitsName || qualifiedName == mUGLFunctionWavePrefixSumName || qualifiedName == mUGLFunctionWaveMatchName ||
               qualifiedName == mUGLFunctionWaveReadAcrossXName || qualifiedName == mUGLFunctionWaveReadAcrossYName || qualifiedName == mUGLFunctionWaveReadAcrossDiagonalName || qualifiedName == mUGLFunctionQuadReadLaneAtName || qualifiedName == mUGLFunctionQuadReadAcrossXName || qualifiedName == mUGLFunctionQuadReadAcrossYName || qualifiedName == mUGLFunctionQuadReadAcrossDiagonalName;
    }

    bool CPPHostShaderOnlyGuard::stmtUsesShaderOnlyOperation(const clang::Stmt *stmt)
    {
        if (stmt == nullptr)
        {
            return false;
        }

        if (const auto *memberCall = llvm::dyn_cast<clang::CXXMemberCallExpr>(stmt))
        {
            clang::Expr *baseObjectExpr = memberCall->getImplicitObjectArgument();
            const std::string methodName = memberCall->getMethodDecl() != nullptr ? memberCall->getMethodDecl()->getNameAsString() : "";
            const std::string implicitObjectTypeName = baseObjectExpr != nullptr ? mVisitor.generateTypeCanonicalName(baseObjectExpr->getType()) : "";
            if (describeTextureOperation(implicitObjectTypeName, methodName).has_value())
            {
                return true;
            }
            if (describeRenderSetDataPackOperation(mVisitor, memberCall).has_value())
            {
                return true;
            }

            if (const auto *methodDecl = memberCall->getMethodDecl(); methodDecl != nullptr && methodDecl->hasBody() && functionRequiresDeletedDefinition(methodDecl))
            {
                return true;
            }
        }
        else if (const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(stmt))
        {
            if (const auto *operatorCall = llvm::dyn_cast<clang::CXXOperatorCallExpr>(callExpr); describeStructuredBufferSubscriptOperation(mVisitor, operatorCall).has_value())
            {
                return true;
            }

            if (const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl()))
            {
                if (isKnownFunction(calleeDecl))
                {
                    return true;
                }

                if (calleeDecl->hasBody() && functionRequiresDeletedDefinition(calleeDecl))
                {
                    return true;
                }
            }
        }

        for (const clang::Stmt *child : stmt->children())
        {
            if (stmtUsesShaderOnlyOperation(child))
            {
                return true;
            }
        }

        return false;
    }

    bool CPPHostShaderOnlyGuard::functionRequiresDeletedDefinition(const clang::FunctionDecl *func)
    {
        if (func == nullptr || func->hasBody() == false)
        {
            return false;
        }

        const clang::FunctionDecl *analysisTarget = func->getDefinition() != nullptr ? func->getDefinition() : func;
        if (auto cacheIter = mDeletedFunctionCache.find(analysisTarget); cacheIter != mDeletedFunctionCache.end())
        {
            return cacheIter->second;
        }

        if (mDeletedFunctionInProgress.contains(analysisTarget))
        {
            return false;
        }

        mDeletedFunctionInProgress.emplace(analysisTarget);
        const bool requiresDelete = stmtUsesShaderOnlyOperation(analysisTarget->getBody());
        mDeletedFunctionInProgress.erase(analysisTarget);
        mDeletedFunctionCache.emplace(analysisTarget, requiresDelete);
        return requiresDelete;
    }

    std::string CPPHostShaderOnlyGuard::generateDeletedFunctionDeclaration(const clang::FunctionDecl *func, const std::string &funcNameOverride)
    {
        const std::string funcName = funcNameOverride.empty() ? func->getNameAsString() : funcNameOverride;
        std::string signature = mVisitor.generateFunctionSignature(func, funcName);
        while (signature.empty() == false && (signature.back() == '\n' || signature.back() == '\r'))
        {
            signature.pop_back();
        }

        std::string result;
        const std::string reason = "the original DSL body depends on shader-only operations.";
        result += mVisitor.mSpaceManager.getSpace() + "// Host declaration is deleted because " + reason + mVisitor.NewLine();
        result += signature + " = delete" + mVisitor.EOS();
        return result;
    }
} // namespace UGLC::CodeGen::CPP
