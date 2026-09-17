#pragma once

#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/Decl.h>

#include <stdexcept>
#include <string>

namespace UGLC::CodeGen
{
    inline std::string describeShaderFunctionKind(const clang::FunctionDecl *func)
    {
        if (func == nullptr)
        {
            return "shader";
        }

        const std::string funcName = func->getNameAsString();
        if (funcName == mUGLComputeShaderFunctionName)
        {
            return "compute";
        }
        if (funcName == mUGLVertexShaderFunctionName)
        {
            return "vertex";
        }
        if (funcName == mUGLFragmentShaderFunctionName)
        {
            return "fragment";
        }
        if (funcName == mUGLPixelShaderFunctionName)
        {
            return "pixel";
        }
        if (funcName == mUGLDomainShaderFunctionName)
        {
            return "domain";
        }
        if (funcName == mUGLHullShaderFunctionName)
        {
            return "hull";
        }
        return "shader";
    }

    inline std::string buildBindGroupSlotSupportRangeMessage(const ShaderBackendCapabilities &capabilities, int bindGroupBufferOffset)
    {
        const int maxSupportedSlot = capabilities.maxBindGroupCount - 1 - bindGroupBufferOffset;
        if (bindGroupBufferOffset > 0)
        {
            return "The current shader entry already reserves "
                   + std::to_string(bindGroupBufferOffset)
                   + " " + std::string(capabilities.diagnosticDisplayName)
                   + " buffer slot(s) before bind groups are appended, so only [[Slot0]] through [[Slot"
                   + std::to_string(maxSupportedSlot) + "]] are valid here.";
        }

        return "The " + std::string(capabilities.diagnosticDisplayName)
               + " backend only supports [[Slot0]] through [[Slot"
               + std::to_string(maxSupportedSlot)
               + "]] for shader bind-group parameters. Attributes beyond this range, such as [[Slot"
               + std::to_string(maxSupportedSlot + 1) + "]], are rejected before code generation.";
    }

    inline void validateBindGroupSlotIndexOrThrow(const ShaderBackendCapabilities &capabilities,
                                                  const std::string &ownerKind,
                                                  const std::string &ownerQualifiedName,
                                                  const std::string &bindGroupName,
                                                  int bindGroupIndex,
                                                  int bindGroupBufferOffset)
    {
        const int finalBufferIndex = bindGroupIndex + bindGroupBufferOffset;
        if (finalBufferIndex < capabilities.maxBindGroupCount)
        {
            return;
        }

        std::string error = ownerKind + " \"" + ownerQualifiedName
                            + "\" uses bind group \"" + bindGroupName + "\" on [["
                            + mUGLAttributeSlotName + std::to_string(bindGroupIndex)
                            + "]], but the " + std::string(capabilities.diagnosticDisplayName)
                            + " backend only supports " + std::to_string(capabilities.maxBindGroupCount)
                            + " bind-group parameters per shader entry (buffer(0) through buffer("
                            + std::to_string(capabilities.maxBindGroupCount - 1) + ")). This slot would map to [[buffer("
                            + std::to_string(finalBufferIndex) + ")]].";

        if (bindGroupBufferOffset > 0)
        {
            error += " The current shader entry already reserves "
                     + std::to_string(bindGroupBufferOffset)
                     + " " + std::string(capabilities.diagnosticDisplayName)
                     + " buffer slot(s) before bind groups are appended.";
        }

        error += " Reduce the highest [[SlotN]] or the total number of bind groups.";
        throw std::runtime_error(error);
    }

    inline void validateBindGroupInfoMapAgainstBackendOrThrow(const ShaderBackendCapabilities &capabilities,
                                                              const std::string &ownerKind,
                                                              const clang::CXXRecordDecl *shaderClassDecl,
                                                              const BindGroupInfoMap &bindGroupInfoMap,
                                                              int bindGroupBufferOffset)
    {
        if (shaderClassDecl == nullptr)
        {
            return;
        }

        for (const auto &[slotIndex, bindGroupInfo] : bindGroupInfoMap)
        {
            (void)slotIndex;
            validateBindGroupSlotIndexOrThrow(capabilities,
                                              ownerKind,
                                              shaderClassDecl->getQualifiedNameAsString(),
                                              bindGroupInfo.name,
                                              bindGroupInfo.bindingIndex,
                                              bindGroupBufferOffset);
        }
    }

    inline void validateWaveLaneQuerySupportOrThrow(const ShaderBackendCapabilities &capabilities, const clang::FunctionDecl *entryFunction)
    {
        const bool isCompute = entryFunction != nullptr && entryFunction->getNameAsString() == mUGLComputeShaderFunctionName;
        const bool supported = isCompute ? capabilities.supportsWaveLaneQueriesInCompute : capabilities.supportsWaveLaneQueriesInRender;
        if (supported)
        {
            return;
        }

        throw std::runtime_error("WaveGetLaneIndex() and WaveGetLaneCount() are currently only supported in compute shaders, but \""
                                 + (entryFunction == nullptr ? std::string("<unknown>") : entryFunction->getQualifiedNameAsString())
                                 + "\" is a " + describeShaderFunctionKind(entryFunction) + " shader entry.");
    }

    inline void validateWaveCollectiveSupportOrThrow(const ShaderBackendCapabilities &capabilities,
                                                     const clang::FunctionDecl *entryFunction,
                                                     const std::string &builtinDisplayName)
    {
        const bool isCompute = entryFunction != nullptr && entryFunction->getNameAsString() == mUGLComputeShaderFunctionName;
        const bool supported = isCompute ? capabilities.supportsWaveCollectivesInCompute : capabilities.supportsWaveCollectivesInRender;
        if (supported)
        {
            return;
        }

        throw std::runtime_error(builtinDisplayName + "() is currently only supported in compute shaders, but \""
                                 + (entryFunction == nullptr ? std::string("<unknown>") : entryFunction->getQualifiedNameAsString())
                                 + "\" is a " + describeShaderFunctionKind(entryFunction) + " shader entry.");
    }

    inline void validateWaveReadLaneAtSupportOrThrow(const ShaderBackendCapabilities &capabilities, const clang::FunctionDecl *entryFunction)
    {
        const bool isCompute = entryFunction != nullptr && entryFunction->getNameAsString() == mUGLComputeShaderFunctionName;
        const bool supported = isCompute ? capabilities.supportsWaveReadLaneAtInCompute : capabilities.supportsWaveReadLaneAtInRender;
        if (supported)
        {
            return;
        }

        std::string error = "WaveReadLaneAt() is currently only supported in compute shaders, but \""
                            + (entryFunction == nullptr ? std::string("<unknown>") : entryFunction->getQualifiedNameAsString())
                            + "\" is a " + describeShaderFunctionKind(entryFunction) + " shader entry.";
        if (!isCompute && capabilities.supportsQuadReadLaneAtInRender)
        {
            error += " Use QuadReadLaneAt() if quad semantics were intended.";
        }
        throw std::runtime_error(error);
    }

    inline void validateShaderBarrierBuiltinSupportOrThrow(const clang::FunctionDecl *entryFunction,
                                                           const std::string &builtinDisplayName)
    {
        const bool isCompute = entryFunction != nullptr && entryFunction->getNameAsString() == mUGLComputeShaderFunctionName;
        if (isCompute)
        {
            return;
        }

        throw std::runtime_error(builtinDisplayName + "() is only supported in compute shaders, but \""
                                 + (entryFunction == nullptr ? std::string("<unknown>") : entryFunction->getQualifiedNameAsString())
                                 + "\" is a " + describeShaderFunctionKind(entryFunction) + " shader entry.");
    }

    /** Returns true when a shader entry supports fragment-kill operations such as discard_fragment() and clip(). */
    inline bool isFragmentKillCapableEntry(const clang::FunctionDecl *entryFunction)
    {
        const std::string entryName = entryFunction == nullptr ? std::string() : entryFunction->getNameAsString();
        return entryName == mUGLFragmentShaderFunctionName || entryName == mUGLPixelShaderFunctionName;
    }

    /** Ensures a fragment-kill DSL builtin is only emitted from fragment-capable shader entries. */
    inline void validateFragmentKillBuiltinSupportOrThrow(const clang::FunctionDecl *entryFunction,
                                                          const std::string &builtinDisplayName)
    {
        if (isFragmentKillCapableEntry(entryFunction))
        {
            return;
        }

        throw std::runtime_error(builtinDisplayName + "() is only supported in fragment or pixel-local shaders, but \""
                                 + (entryFunction == nullptr ? std::string("<unknown>") : entryFunction->getQualifiedNameAsString())
                                 + "\" is a " + describeShaderFunctionKind(entryFunction) + " shader entry.");
    }

    /**
     * @brief Ensures the fragment-discard DSL builtin is only emitted from fragment-capable entries.
     *
     * DSL example:
     * @code
     * if (alpha < 0.5f) { UGL::discard_fragment(); }
     * @endcode
     *
     * HLSL lowers the statement to `discard;`, while MSL lowers it to
     * `discard_fragment();`. Other shader stages have no compatible discard
     * control-flow semantics, so they fail before backend source is emitted.
     */
    inline void validateDiscardFragmentSupportOrThrow(const clang::FunctionDecl *entryFunction)
    {
        validateFragmentKillBuiltinSupportOrThrow(entryFunction, "discard_fragment");
    }

    inline void validateRenderStageSupportOrThrow(const ShaderBackendCapabilities &capabilities,
                                                  const clang::CXXRecordDecl *renderClassDecl,
                                                  const clang::FunctionDecl *hullShaderFunction,
                                                  const clang::FunctionDecl *domainShaderFunction)
    {
        if (renderClassDecl == nullptr)
        {
            return;
        }

        if (hullShaderFunction != nullptr && !capabilities.supportsHullShader)
        {
            throw std::runtime_error("RenderClass \"" + renderClassDecl->getQualifiedNameAsString()
                                     + "\" declares a hull shader entry, but the "
                                     + std::string(capabilities.diagnosticDisplayName)
                                     + " backend does not support hull shader generation yet.");
        }

        if (domainShaderFunction != nullptr && !capabilities.supportsDomainShader)
        {
            throw std::runtime_error("RenderClass \"" + renderClassDecl->getQualifiedNameAsString()
                                     + "\" declares a domain shader entry, but the "
                                     + std::string(capabilities.diagnosticDisplayName)
                                     + " backend does not support domain shader generation yet.");
        }
    }
} // namespace UGLC::CodeGen
