#include "SPIRVModuleBuilder.hpp"

#include "SPIRVCapabilityTracker.hpp"
#include "SPIRVInstructionBuilder.hpp"
#include "SPIRVTypeSystem.hpp"

#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/UGLIR/UGLIRResourceUsage.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spirv/unified1/GLSL.std.450.h>
#include <spirv/unified1/spirv.hpp>

namespace UGLC::CodeGen::SPIRVEmitter
{
    namespace
    {
        /** Stores one SPIR-V SSA value together with its UGLIR type and pointer state. */
        struct SPIRVValue
        {
            uint32_t id = 0;
            std::string typeName;
            std::optional<UGLIR::ValueTypeDescription> valueType;
            bool isPointer = false;
            bool isMemoryObjectDeclaration = false;
            bool isResourceRoot = false;
            spv::StorageClass storageClass = spv::StorageClassFunction;
        };

        /** Stores the ids required to emit calls and definitions for one UGLIR function. */
        struct SPIRVFunctionInfo
        {
            const UGLIR::Function *function = nullptr;
            uint32_t functionId = 0;
            uint32_t functionTypeId = 0;
            uint32_t returnTypeId = 0;
            std::vector<uint32_t> parameterTypeIds;
            std::vector<bool> parameterIsPointer;
        };

        /** Stores SPIR-V ids and reflection metadata for one storage buffer resource. */
        struct SPIRVResourceInfo
        {
            const UGLIR::ResourceBinding *binding = nullptr;
            uint32_t variableId = 0;
            uint32_t blockTypeId = 0;
            uint32_t elementTypeId = 0;
            uint32_t objectTypeId = 0;
            uint32_t elementPointerTypeId = 0;
            bool isObjectArray = false;
            std::string variableName;
        };

        /** Stores one reflected stage input or output global variable. */
        struct SPIRVStageIOInfo
        {
            const UGLIR::StageIOBinding *binding = nullptr;
            uint32_t variableId = 0;
            uint32_t pointerTypeId = 0;
            spv::StorageClass storageClass = spv::StorageClassInput;
            std::string valueTypeName;
        };

        /** Describes one UGL matrix shape as both row and column vector views. */
        struct SPIRVMatrixShape
        {
            std::string scalarTypeName;
            std::string rowVectorTypeName;
            std::string columnVectorTypeName;
            uint32_t rowCount = 0;
            uint32_t columnCount = 0;
        };

        /** Describes one field in the lowered draw-info storage record. */
        struct SPIRVDrawInfoField
        {
            uint32_t fieldIndex = 0;
            const char *typeName = "u32";
        };

        /** Stores merge and continue labels for the innermost structured loop. */
        struct SPIRVLoopTargets
        {
            uint32_t breakLabelId = 0;
            uint32_t continueLabelId = 0;
        };

        /** Stores the ids needed to materialize a by-value function parameter into a mutable function local. */
        struct SPIRVParameterLocalCopy
        {
            uint32_t pointerTypeId = 0;
            uint32_t pointerId = 0;
            uint32_t parameterId = 0;
        };

        /** Stores runtime state for a local shader resource alias that can be assigned from multiple reflected resources. */
        struct SPIRVResourceAliasState
        {
            uint32_t selectorPointerId = 0;
            std::vector<const SPIRVResourceInfo *> resources;
        };

        /** Stores one temporary pointer argument that must be copied back after a helper call returns. */
        struct SPIRVPointerArgumentCopyBack
        {
            SPIRVValue destinationPointer;
            SPIRVValue temporaryPointer;
            UGLIR::SourceLocation sourceLocation;
        };

        /** Stores mutable state while emitting one SPIR-V function body. */
        struct SPIRVFunctionContext
        {
            SPIRVInstructionBuilder body;
            const SPIRVFunctionInfo *functionInfo = nullptr;
            uint32_t currentBlockId = 0;
            bool blockTerminated = false;
            std::unordered_map<std::string, SPIRVValue> localPointers;
            std::unordered_map<std::string, SPIRVValue> parameterValues;
            std::unordered_map<std::string, const SPIRVResourceInfo *> resourceAliases;
            std::unordered_map<std::string, SPIRVValue> resourceObjectAliases;
            std::unordered_map<std::string, SPIRVResourceAliasState> resourceAliasStates;
            std::unordered_map<std::string, SPIRVValue> constantArrayPointers;
            std::unordered_map<std::string, SPIRVValue> pointerArgumentTemporaries;
            uint32_t drawCommandParamsValueId = 0;
            uint32_t argumentRootPointerId = 0;
            std::vector<SPIRVLoopTargets> loopTargets;
            std::vector<SPIRVParameterLocalCopy> parameterLocalCopies;
        };

        /** Returns the compact backend type key for one structured value type. */
        std::string valueTypeKey(const UGLIR::ValueTypeDescription &valueType)
        {
            return spvValueTypeKey(valueType);
        }

        /** Returns the runtime entry-point name for a UGLIR entry kind. */
        std::string entryPointNameForEntryKind(UGLIR::ShaderEntryKind entryKind)
        {
            switch (entryKind)
            {
            case UGLIR::ShaderEntryKind::Vertex:
                return UGLC::CodeGen::VertexShaderEntryName;
            case UGLIR::ShaderEntryKind::Fragment:
            case UGLIR::ShaderEntryKind::PixelLocal:
                return UGLC::CodeGen::FragmentShaderEntryName;
            case UGLIR::ShaderEntryKind::Compute:
                return UGLC::CodeGen::ComputeShaderEntryName;
            default:
                return UGLC::CodeGen::ComputeShaderEntryName;
            }
        }

        /** Maps a UGLIR shader stage to a SPIR-V execution model. */
        spv::ExecutionModel executionModelForStage(UGLIR::ShaderStage stage)
        {
            switch (stage)
            {
            case UGLIR::ShaderStage::Vertex:
                return spv::ExecutionModelVertex;
            case UGLIR::ShaderStage::Fragment:
                return spv::ExecutionModelFragment;
            case UGLIR::ShaderStage::Compute:
                return spv::ExecutionModelGLCompute;
            default:
                return spv::ExecutionModelGLCompute;
            }
        }

        /** Maps a structured UGL texture format to the SPIR-V image format enum. */
        spv::ImageFormat spirvImageFormatForTextureFormat(UGLIR::TextureFormat textureFormat)
        {
            switch (textureFormat)
            {
            case UGLIR::TextureFormat::RGBA8Unorm:
                return spv::ImageFormatRgba8;
            case UGLIR::TextureFormat::RGBA8Snorm:
                return spv::ImageFormatRgba8Snorm;
            case UGLIR::TextureFormat::RGBA8Uint:
                return spv::ImageFormatRgba8ui;
            case UGLIR::TextureFormat::RGBA8Sint:
                return spv::ImageFormatRgba8i;
            case UGLIR::TextureFormat::RGBA32Float:
                return spv::ImageFormatRgba32f;
            case UGLIR::TextureFormat::RGBA32Uint:
                return spv::ImageFormatRgba32ui;
            case UGLIR::TextureFormat::RGBA32Sint:
                return spv::ImageFormatRgba32i;
            case UGLIR::TextureFormat::R32Float:
                return spv::ImageFormatR32f;
            case UGLIR::TextureFormat::R32Uint:
                return spv::ImageFormatR32ui;
            case UGLIR::TextureFormat::R32Sint:
                return spv::ImageFormatR32i;
            case UGLIR::TextureFormat::RG32Float:
                return spv::ImageFormatRg32f;
            case UGLIR::TextureFormat::RG32Uint:
                return spv::ImageFormatRg32ui;
            case UGLIR::TextureFormat::RG32Sint:
                return spv::ImageFormatRg32i;
            case UGLIR::TextureFormat::RG16Float:
                return spv::ImageFormatRg16f;
            case UGLIR::TextureFormat::RG16Unorm:
                return spv::ImageFormatRg16;
            case UGLIR::TextureFormat::RG16Snorm:
                return spv::ImageFormatRg16Snorm;
            case UGLIR::TextureFormat::RG16Uint:
                return spv::ImageFormatRg16ui;
            case UGLIR::TextureFormat::RG16Sint:
                return spv::ImageFormatRg16i;
            case UGLIR::TextureFormat::RG8Unorm:
                return spv::ImageFormatRg8;
            case UGLIR::TextureFormat::RG8Snorm:
                return spv::ImageFormatRg8Snorm;
            case UGLIR::TextureFormat::RG8Uint:
                return spv::ImageFormatRg8ui;
            case UGLIR::TextureFormat::RG8Sint:
                return spv::ImageFormatRg8i;
            case UGLIR::TextureFormat::R16Unorm:
                return spv::ImageFormatR16;
            case UGLIR::TextureFormat::R16Snorm:
                return spv::ImageFormatR16Snorm;
            case UGLIR::TextureFormat::R16Uint:
                return spv::ImageFormatR16ui;
            case UGLIR::TextureFormat::R16Sint:
                return spv::ImageFormatR16i;
            case UGLIR::TextureFormat::R16Float:
                return spv::ImageFormatR16f;
            case UGLIR::TextureFormat::R8Unorm:
                return spv::ImageFormatR8;
            case UGLIR::TextureFormat::R8Snorm:
                return spv::ImageFormatR8Snorm;
            case UGLIR::TextureFormat::R8Uint:
                return spv::ImageFormatR8ui;
            case UGLIR::TextureFormat::R8Sint:
                return spv::ImageFormatR8i;
            case UGLIR::TextureFormat::RGBA16Float:
                return spv::ImageFormatRgba16f;
            default:
                return spv::ImageFormatUnknown;
            }
        }

        /** Converts an arbitrary UGLIR symbol into a stable SPIR-V debug identifier. */
        std::string sanitizeSPIRVName(const std::string &name)
        {
            std::string result;
            result.reserve(name.size() + 1u);
            for (char ch : name)
            {
                result.push_back((std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') ? ch : '_');
            }
            if (result.empty())
            {
                result = "unnamed";
            }
            if (!std::isalpha(static_cast<unsigned char>(result.front())) && result.front() != '_')
            {
                result.insert(result.begin(), '_');
            }
            return result;
        }


        /** Parses an unsigned integer literal without relying on exception control flow. */
        uint32_t parseUnsignedLiteral(const std::string &value)
        {
            const char *begin = value.c_str();
            char *end = nullptr;
            const unsigned long parsed = std::strtoul(begin, &end, 10);
            (void)end;
            return static_cast<uint32_t>(parsed);
        }

        /** Parses a 32-bit floating-point literal into its IEEE-754 payload. */
        uint32_t parseFloatLiteralBits(const std::string &value)
        {
            const float parsed = std::strtof(value.c_str(), nullptr);
            uint32_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
            return bits;
        }

        /** Converts a 32-bit floating-point bit pattern to a 16-bit IEEE-754 half payload with round-to-nearest-even. */
        uint32_t convertFloatBitsToHalfBits(uint32_t floatBits)
        {
            llvm::APFloat value(llvm::APFloat::IEEEsingle(), llvm::APInt(32u, floatBits));
            bool losesInformation = false;
            (void)value.convert(llvm::APFloat::IEEEhalf(), llvm::APFloat::rmNearestTiesToEven, &losesInformation);
            return static_cast<uint32_t>(value.bitcastToAPInt().getZExtValue());
        }

        /** Parses a floating-point literal into the low 16 bits of a SPIR-V half constant word. */
        uint32_t parseHalfLiteralBits(const std::string &value)
        {
            return convertFloatBitsToHalfBits(parseFloatLiteralBits(value));
        }

        /** Returns the vector component index for a supported scalar swizzle name. */
        uint32_t getVectorComponentIndex(const std::string &name)
        {
            if (name == "x" || name == "r")
            {
                return 0;
            }
            if (name == "y" || name == "g")
            {
                return 1;
            }
            if (name == "z" || name == "b")
            {
                return 2;
            }
            if (name == "w" || name == "a")
            {
                return 3;
            }
            return UINT32_MAX;
        }

        /** Returns component indices for a scalar or vector swizzle name. */
        std::vector<uint32_t> getVectorComponentIndices(const std::string &name)
        {
            std::vector<uint32_t> result;
            if (name.empty() || name.size() > 4u)
            {
                return result;
            }
            result.reserve(name.size());
            for (const char component : name)
            {
                const uint32_t index = getVectorComponentIndex(std::string(1, component));
                if (index == UINT32_MAX)
                {
                    result.clear();
                    return result;
                }
                result.push_back(index);
            }
            return result;
        }

        /** Emits a direct SPIR-V module from one Phase-6 UGLIR compute module. */
        class UGLIRSPIRVModuleBuilder
        {
        public:
            /** Creates a builder that references a UGLIR module without taking ownership of it. */
            explicit UGLIRSPIRVModuleBuilder(const UGLIR::Module &module)
                : mModule(module)
            {
                registerBuiltinValueTypeKeys();
                for (const UGLIR::Type &type : module.types)
                {
                    mTypesByName.emplace(type.name, &type);
                }
            }

            /** Builds the module words or returns diagnostics when the UGLIR subset is unsupported. */
            SPIRVModuleBuildResult run()
            {
                SPIRVModuleBuildResult result;
                const UGLIR::Function *entryFunction = findEntryFunction();
                if (entryFunction == nullptr)
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                mCapabilityTracker.declareCapability(spv::CapabilityShader);
                mMemoryModel.appendInstruction(spv::OpMemoryModel, {
                    static_cast<uint32_t>(mModule.reflection.arguments.enabled ? spv::AddressingModelPhysicalStorageBuffer64 : spv::AddressingModelLogical),
                    static_cast<uint32_t>(spv::MemoryModelGLSL450),
                });

                if (mModule.reflection.arguments.enabled)
                {
                    mCapabilityTracker.declareCapability(spv::CapabilityPhysicalStorageBufferAddresses);
                    mCapabilityTracker.declareExtension("SPV_KHR_physical_storage_buffer");
                    buildArgumentRoot();
                }
                registerFunctionSignatures();
                buildResources();
                buildEntryInterface(*entryFunction);
                emitFunctions();
                emitEntryPoint(*entryFunction);

                if (!mDiagnostics.empty())
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                result.words = assembleModuleWords();
                return result;
            }

        private:
            const UGLIR::Module &mModule;
            uint32_t mNextId = 1;
            uint32_t mVoidTypeId = 0;
            uint32_t mBoolTypeId = 0;
            uint32_t mUIntTypeId = 0;
            uint32_t mIntTypeId = 0;
            uint32_t mFloatTypeId = 0;
            uint32_t mHalfTypeId = 0;
            uint32_t mTrueConstantId = 0;
            uint32_t mFalseConstantId = 0;
            uint32_t mGlobalInvocationIdVariableId = 0;
            uint32_t mLocalInvocationIdVariableId = 0;
            uint32_t mWorkgroupIdVariableId = 0;
            uint32_t mLocalInvocationIndexVariableId = 0;
            uint32_t mSubgroupLocalInvocationIdVariableId = 0;
            uint32_t mSubgroupSizeVariableId = 0;
            uint32_t mInstanceIdVariableId = 0;
            uint32_t mEntryPointFunctionId = 0;
            uint32_t mGLSLStd450ImportId = 0;
            uint32_t mSamplerTypeId = 0;
            uint32_t mSubpassCoordinateZeroId = 0;
            uint32_t mArgumentPushConstantId = 0;
            uint32_t mArgumentRootTypeId = 0;
            std::unordered_map<uint64_t, uint32_t> mArgumentBufferBlockTypeIds;
            std::unordered_map<std::string, const UGLIR::Type *> mTypesByName;
            mutable std::unordered_map<std::string, UGLIR::ValueTypeDescription> mResolvedValueTypesByKey;
            std::unordered_map<std::string, uint32_t> mTypeIdsByName;
            // Buffer aggregates have separate ids so memory decorations never affect function values.
            std::unordered_map<std::string, uint32_t> mStorageTypeIdsByName;
            std::unordered_map<std::string, uint32_t> mVectorTypeIdsByShape;
            std::unordered_map<std::string, uint32_t> mMatrixTypeIdsByShape;
            std::unordered_map<std::string, uint32_t> mArrayTypeIdsByShape;
            std::unordered_map<uint32_t, uint32_t> mUniformBlockTypeIdsByElementType;
            std::unordered_map<std::string, uint32_t> mPointerTypeIds;
            std::unordered_map<std::string, uint32_t> mFunctionTypeIds;
            std::unordered_map<std::string, uint32_t> mUIntConstants;
            std::unordered_map<std::string, uint32_t> mIntConstants;
            std::unordered_map<std::string, uint32_t> mFloatConstants;
            std::unordered_map<std::string, uint32_t> mHalfConstants;
            std::unordered_map<std::string, uint32_t> mNullConstants;
            std::unordered_map<std::string, uint32_t> mImageTypeIds;
            std::unordered_map<uint32_t, uint32_t> mSampledImageTypeIds;
            std::unordered_map<std::string, SPIRVFunctionInfo> mFunctionsByName;
            std::unordered_map<std::string, SPIRVResourceInfo> mResourcesByUGLIRName;
            std::unordered_set<uint32_t> mBlockDecoratedTypeIds;
            std::unordered_set<uint32_t> mNonUniformDecoratedIds;
            std::vector<SPIRVStageIOInfo> mStageIOVariables;
            std::vector<uint32_t> mEntryPointInterfaceIds;
            std::vector<SPIRVModuleBuildDiagnostic> mDiagnostics;
            SPIRVCapabilityTracker mCapabilityTracker;
            SPIRVInstructionBuilder mExtInstImports;
            SPIRVInstructionBuilder mMemoryModel;
            SPIRVInstructionBuilder mEntryPoints;
            SPIRVInstructionBuilder mExecutionModes;
            SPIRVInstructionBuilder mDebugNames;
            SPIRVInstructionBuilder mAnnotations;
            SPIRVInstructionBuilder mTypesConstantsGlobals;
            SPIRVInstructionBuilder mFunctions;

            /** Allocates one fresh SPIR-V result id. */
            uint32_t allocateId()
            {
                return mNextId++;
            }

            /** Records an unsupported UGLIR construct diagnostic. */
            void addDiagnostic(const UGLIR::SourceLocation &location, std::string message)
            {
                mDiagnostics.push_back({location, std::move(message)});
            }

            /** Creates a structured scalar value type description for direct SPIR-V type selection. */
            static UGLIR::ValueTypeDescription makeScalarValueType(UGLIR::ScalarKind scalarKind)
            {
                return UGLIR::ValueTypeDescription{
                    .scalarKind = scalarKind,
                    .bitWidth = scalarKind == UGLIR::ScalarKind::Half ? 16u : scalarKind == UGLIR::ScalarKind::Bool ? 1u : 32u,
                    .vectorWidth = 1u,
                };
            }

            /** Creates a structured vector value type description for direct SPIR-V type selection. */
            static UGLIR::ValueTypeDescription makeVectorValueType(UGLIR::ScalarKind scalarKind, uint32_t vectorWidth)
            {
                UGLIR::ValueTypeDescription valueType = makeScalarValueType(scalarKind);
                valueType.vectorWidth = std::max<uint32_t>(1u, vectorWidth);
                return valueType;
            }

            /** Returns the backend type key for the structured bool scalar type. */
            static std::string boolTypeKey()
            {
                return valueTypeKey(makeScalarValueType(UGLIR::ScalarKind::Bool));
            }

            /** Returns the backend type key for the structured uint scalar type. */
            static std::string uintTypeKey()
            {
                return valueTypeKey(makeScalarValueType(UGLIR::ScalarKind::UInt));
            }

            /** Returns the backend type key for the structured int scalar type. */
            static std::string intTypeKey()
            {
                return valueTypeKey(makeScalarValueType(UGLIR::ScalarKind::Int));
            }

            /** Returns the backend type key for the structured float scalar type. */
            static std::string floatTypeKey()
            {
                return valueTypeKey(makeScalarValueType(UGLIR::ScalarKind::Float));
            }

            /** Returns the backend type key for the structured half scalar type. */
            static std::string halfTypeKey()
            {
                return valueTypeKey(makeScalarValueType(UGLIR::ScalarKind::Half));
            }

            /** Registers one structured value type under both its backend key and a source type alias. */
            void registerValueTypeAlias(const std::string &typeName, const UGLIR::ValueTypeDescription &valueType) const
            {
                if (!typeName.empty())
                {
                    mResolvedValueTypesByKey[typeName] = valueType;
                }
                mResolvedValueTypesByKey[valueTypeKey(valueType)] = valueType;
            }

            /** Registers the built-in scalar and vector type keys generated from structured scalar categories. */
            void registerBuiltinValueTypeKeys()
            {
                const UGLIR::ScalarKind scalarKinds[] = {
                    UGLIR::ScalarKind::Bool,
                    UGLIR::ScalarKind::Int,
                    UGLIR::ScalarKind::UInt,
                    UGLIR::ScalarKind::Float,
                    UGLIR::ScalarKind::Half,
                };
                for (UGLIR::ScalarKind scalarKind : scalarKinds)
                {
                    for (uint32_t width = 1u; width <= 4u; ++width)
                    {
                        registerValueTypeAlias(valueTypeKey(makeVectorValueType(scalarKind, width)),
                                               makeVectorValueType(scalarKind, width));
                    }
                }
            }

            /** Resolves one value type name through UGLIR metadata instead of backend spelling heuristics. */
            std::optional<UGLIR::ValueTypeDescription> describeTypeName(const std::string &typeName) const
            {
                if (const auto existing = mResolvedValueTypesByKey.find(typeName); existing != mResolvedValueTypesByKey.end())
                {
                    return existing->second;
                }
                std::optional<UGLIR::ValueTypeDescription> registeredType = UGLIR::describeRegisteredValueType(mModule, typeName);
                if (registeredType.has_value())
                {
                    registerValueTypeAlias(typeName, *registeredType);
                    return registeredType;
                }
                return std::nullopt;
            }

            /** Returns the backend type key for a registered value type or the original user type name. */
            std::string valueTypeKeyForName(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                return valueType.has_value() ? valueTypeKey(*valueType) : typeName;
            }

            /** Returns the scalar lane category for a registered value type name. */
            UGLIR::ScalarKind valueTypeScalarKind(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                return valueType.has_value() ? valueType->scalarKind : UGLIR::ScalarKind::None;
            }

            /** Returns the scalar lane count for a registered scalar or vector value type name. */
            uint32_t valueTypeVectorWidth(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                return valueType.has_value() ? spvVectorWidth(*valueType) : 1u;
            }

            /** Returns the scalar backend key for a registered scalar or vector value type name. */
            std::string valueTypeScalarKey(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                return valueType.has_value() ? spvScalarTypeKey(*valueType) : typeName;
            }

            /** Returns the backend vector key for a registered scalar key and lane count. */
            std::string vectorKeyForScalar(const std::string &scalarTypeName, uint32_t vectorWidth) const
            {
                const std::optional<UGLIR::ValueTypeDescription> scalarType = describeTypeName(scalarTypeName);
                if (scalarType.has_value())
                {
                    return spvVectorTypeKey(scalarType->scalarKind, vectorWidth);
                }
                return scalarTypeName;
            }

            /** Returns true when a registered value type is a scalar uint value. */
            bool isUnsignedIntegerScalarValue(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::UInt && valueTypeVectorWidth(typeName) == 1u;
            }

            /** Returns true when a registered value type is a scalar int value. */
            bool isSignedIntegerScalarValue(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Int && valueTypeVectorWidth(typeName) == 1u;
            }

            /** Returns true when a registered value type is a scalar float value. */
            bool isFloatScalarValue(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Float && valueTypeVectorWidth(typeName) == 1u;
            }

            /** Returns true when a registered value type is a scalar half value. */
            bool isHalfScalarValue(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Half && valueTypeVectorWidth(typeName) == 1u;
            }

            /** Returns true when a registered value type is a scalar float or half value. */
            bool isFloatLikeScalarValue(const std::string &typeName) const
            {
                return isFloatScalarValue(typeName) || isHalfScalarValue(typeName);
            }

            /** Returns true when a registered value type is a vector float or half value. */
            bool isFloatLikeVectorValue(const std::string &typeName) const
            {
                const UGLIR::ScalarKind scalarKind = valueTypeScalarKind(typeName);
                return valueTypeVectorWidth(typeName) > 1u &&
                       (scalarKind == UGLIR::ScalarKind::Float || scalarKind == UGLIR::ScalarKind::Half);
            }

            /** Returns true when a registered value type is a scalar or vector float or half value. */
            bool isFloatLikeScalarOrVectorValue(const std::string &typeName) const
            {
                return isFloatLikeScalarValue(typeName) || isFloatLikeVectorValue(typeName);
            }

            /** Returns true when a registered value type is a scalar or vector bool value. */
            bool isBooleanScalarOrVectorValue(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Bool;
            }

            /** Returns true when a registered type name denotes a scalar or vector value type. */
            bool hasScalarOrVectorValueType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                return valueType.has_value() && valueType->scalarKind != UGLIR::ScalarKind::None && valueType->matrixColumns == 0u;
            }

            /** Returns the bool scalar or vector key matching a registered value type shape. */
            std::string boolTypeKeyForValueType(const std::string &typeName) const
            {
                return spvVectorTypeKey(UGLIR::ScalarKind::Bool, valueTypeVectorWidth(typeName));
            }

            /** Returns true when a registered scalar/vector/matrix value type contains half lanes. */
            bool valueTypeContainsHalf(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Half;
            }

            /** Returns true when a registered scalar or vector value type uses unsigned integer lanes. */
            bool isUnsignedIntegerOrVectorTypeName(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::UInt;
            }

            /** Returns true when a registered scalar or vector value type uses signed integer lanes. */
            bool isSignedIntegerOrVectorTypeName(const std::string &typeName) const
            {
                return valueTypeScalarKind(typeName) == UGLIR::ScalarKind::Int;
            }

            /** Returns a conservative byte size for registered scalar and vector value types in storage layouts. */
            uint32_t scalarOrVectorByteSize(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                if (!valueType.has_value())
                {
                    return 0;
                }
                const uint32_t laneSize = valueType->scalarKind == UGLIR::ScalarKind::Half ? 2u : 4u;
                const uint32_t width = spvVectorWidth(*valueType);
                return laneSize * (width >= 3u ? 4u : width);
            }

            /** Returns the Vulkan stage-output ABI type for one reflected stage binding. */
            std::string stageIOABITypeName(const UGLIR::StageIOBinding &binding, spv::StorageClass storageClass) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(binding.type);
                if (!valueType.has_value())
                {
                    return binding.type;
                }
                if (storageClass == spv::StorageClassOutput &&
                    (binding.semanticKind == UGLIR::BuiltinSemanticKind::Color ||
                     binding.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalColor) &&
                    valueType->scalarKind == UGLIR::ScalarKind::Half)
                {
                    return spvVectorTypeKey(UGLIR::ScalarKind::Float, spvVectorWidth(*valueType));
                }
                return valueTypeKey(*valueType);
            }

            /** Attaches a structured value type description to a SPIR-V value when it represents a value type. */
            SPIRVValue attachValueType(SPIRVValue value) const
            {
                if (!value.valueType.has_value() && !value.typeName.empty())
                {
                    value.valueType = describeTypeName(value.typeName);
                }
                return value;
            }

            /** Creates a SPIR-V value and eagerly attaches structured value-type metadata when the UGLIR type is a value type. */
            SPIRVValue makeValue(uint32_t id, std::string typeName, const UGLIR::SourceLocation &location) const
            {
                (void)location;
                SPIRVValue value{
                    .id = id,
                    .typeName = std::move(typeName),
                };
                return attachValueType(std::move(value));
            }

            /** Creates a SPIR-V value with caller-provided structured value-type metadata. */
            SPIRVValue makeTypedValue(uint32_t id, std::string typeName, UGLIR::ValueTypeDescription valueType) const
            {
                return SPIRVValue{
                    .id = id,
                    .typeName = std::move(typeName),
                    .valueType = std::move(valueType),
                };
            }

            /** Creates a SPIR-V pointer value and attaches metadata for its pointee when it is a scalar, vector, or matrix value type. */
            SPIRVValue makePointerValue(uint32_t id,
                                        std::string pointeeTypeName,
                                        spv::StorageClass storageClass,
                                        const UGLIR::SourceLocation &location,
                                        bool isMemoryObjectDeclaration = true) const
            {
                SPIRVValue value = makeValue(id, std::move(pointeeTypeName), location);
                value.isPointer = true;
                value.isMemoryObjectDeclaration = isMemoryObjectDeclaration;
                value.storageClass = storageClass;
                return value;
            }

            /** Creates the sentinel value used by expressions that do not produce a SPIR-V SSA result. */
            SPIRVValue makeVoidValue() const
            {
                return SPIRVValue{.id = 0, .typeName = "void"};
            }

            /** Returns a required structured value type or emits one diagnostic for unsupported value-type emission. */
            std::optional<UGLIR::ValueTypeDescription> requireValueType(const SPIRVValue &value, const UGLIR::SourceLocation &location)
            {
                if (value.valueType.has_value())
                {
                    return value.valueType;
                }
                addDiagnostic(location, "expected scalar, vector, or matrix value type for \"" + value.typeName + "\".");
                return std::nullopt;
            }

            /** Returns the GLSL.std.450 extended instruction import id, declaring it once per module. */
            uint32_t getGLSLStd450ImportId()
            {
                if (mGLSLStd450ImportId == 0)
                {
                    mGLSLStd450ImportId = allocateId();
                    mExtInstImports.appendInstructionWithString(spv::OpExtInstImport, {mGLSLStd450ImportId}, "GLSL.std.450");
                }
                return mGLSLStd450ImportId;
            }

            /** Marks one SPIR-V id as a non-uniform descriptor-indexing value. */
            void decorateNonUniformId(uint32_t id)
            {
                if (id != 0 && mNonUniformDecoratedIds.insert(id).second)
                {
                    mAnnotations.appendInstruction(spv::OpDecorate, {
                        id,
                        static_cast<uint32_t>(spv::DecorationNonUniform),
                    });
                }
            }

            /** Finds the single shader entry supported by Phase 8. */
            const UGLIR::Function *findEntryFunction()
            {
                const UGLIR::Function *entryFunction = nullptr;
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (!function.isEntryPoint)
                    {
                        continue;
                    }
                    if (entryFunction != nullptr)
                    {
                        addDiagnostic(function.sourceLocation, "multiple shader entry functions were found in one UGLIR module.");
                        continue;
                    }
                    entryFunction = &function;
                }
                if (entryFunction == nullptr && mDiagnostics.empty())
                {
                    addDiagnostic(mModule.sourceLocation, "no shader entry function was found in the UGLIR module.");
                }
                return entryFunction;
            }

            /** Returns the canonical type record for a UGLIR type name. */
            const UGLIR::Type *findType(const std::string &typeName) const
            {
                const auto iter = mTypesByName.find(typeName);
                return iter == mTypesByName.end() ? nullptr : iter->second;
            }

            /** Returns true when a lowered type name denotes the module's void type. */
            bool isVoidTypeName(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                return type != nullptr && type->kind == UGLIR::TypeKind::Void;
            }

            /** Returns true when a type stores its value in SPIR-V Workgroup storage. */
            bool isWorkgroupTypeName(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    return false;
                }
                if (type->kind == UGLIR::TypeKind::Workgroup)
                {
                    return true;
                }
                if (type->kind == UGLIR::TypeKind::Array)
                {
                    return isWorkgroupTypeName(type->elementType);
                }
                return false;
            }

            /** Returns the shader value type contained by workgroup-storage wrappers. */
            std::string storageValueTypeName(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type != nullptr && type->kind == UGLIR::TypeKind::Workgroup && !type->elementType.empty())
                {
                    return type->elementType;
                }
                return typeName;
            }

            /** Returns the storage class required for a function-local declaration type. */
            spv::StorageClass storageClassForLocalType(const std::string &typeName) const
            {
                return isWorkgroupTypeName(typeName) ? spv::StorageClassWorkgroup : spv::StorageClassFunction;
            }

            /** Returns a conservative byte size for a UGLIR type in storage-buffer layout. */
            uint32_t getStorageLayoutByteSize(const std::string &typeName) const
            {
                const std::string canonicalTypeName = valueTypeKeyForName(typeName);
                if (canonicalTypeName != typeName)
                {
                    return getStorageLayoutByteSize(canonicalTypeName);
                }

                if (const UGLIR::Type *type = findType(typeName); type != nullptr && type->kind == UGLIR::TypeKind::Workgroup)
                {
                    return getStorageLayoutByteSize(type->elementType);
                }
                if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(typeName))
                {
                    const uint32_t scalarSize = valueTypeContainsHalf(matrixShape->scalarTypeName) ? 2u : 4u;
                    return matrixShape->rowCount * (matrixShape->columnCount == 2u ? 2u : 4u) * scalarSize;
                }
                if (const UGLIR::Type *type = findType(typeName); type != nullptr && type->kind == UGLIR::TypeKind::Struct)
                {
                    uint32_t byteSize = 0;
                    for (const UGLIR::TypeField &field : type->fields)
                    {
                        byteSize = std::max<uint32_t>(byteSize, field.offset + getStorageLayoutByteSize(field.type));
                    }
                    return byteSize;
                }
                if (const UGLIR::Type *type = findType(typeName); type != nullptr && type->kind == UGLIR::TypeKind::Array)
                {
                    return std::max<uint32_t>(1u, type->arrayCount) * getStorageLayoutByteSize(type->elementType);
                }
                return scalarOrVectorByteSize(typeName);
            }

            /** Returns true when a UGLIR type recursively contains a native half field. */
            bool typeContainsHalf(const std::string &typeName) const
            {
                const std::string canonicalName = valueTypeKeyForName(typeName);
                if (valueTypeContainsHalf(canonicalName))
                {
                    return true;
                }
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    return false;
                }
                if (type->kind == UGLIR::TypeKind::Half)
                {
                    return true;
                }
                if (!type->elementType.empty() && typeContainsHalf(type->elementType))
                {
                    return true;
                }
                for (const UGLIR::TypeField &field : type->fields)
                {
                    if (typeContainsHalf(field.type))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns a SPIR-V type id for a supported UGLIR type name. */
            uint32_t getTypeId(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                const std::string canonicalTypeName = valueTypeKeyForName(typeName);
                if (canonicalTypeName != typeName)
                {
                    return getTypeId(canonicalTypeName, location);
                }
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }

                if (const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(typeName);
                    valueType.has_value() && valueType->scalarKind != UGLIR::ScalarKind::None)
                {
                    if (valueType->matrixColumns != 0u && valueType->matrixRows != 0u)
                    {
                        if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(typeName))
                        {
                            return getMatrixTypeId(typeName, *matrixShape, location);
                        }
                        addDiagnostic(location, "unsupported matrix type \"" + typeName + "\".");
                        return getUIntTypeId();
                    }
                    if (valueType->vectorWidth > 1u)
                    {
                        return getBuiltinVectorTypeId(typeName, location);
                    }
                    switch (valueType->scalarKind)
                    {
                    case UGLIR::ScalarKind::Bool:
                        return getBoolTypeId();
                    case UGLIR::ScalarKind::UInt:
                        return getUIntTypeId();
                    case UGLIR::ScalarKind::Int:
                        return getIntTypeId();
                    case UGLIR::ScalarKind::Float:
                        return getFloatTypeId();
                    case UGLIR::ScalarKind::Half:
                        return getHalfTypeId();
                    case UGLIR::ScalarKind::None:
                        break;
                    }
                }
                if (const UGLIR::Type *builtinType = findType(typeName))
                {
                    switch (builtinType->kind)
                    {
                    case UGLIR::TypeKind::Void:
                        return getVoidTypeId();
                    case UGLIR::TypeKind::Bool:
                        return getBoolTypeId();
                    case UGLIR::TypeKind::UInt:
                        return getUIntTypeId();
                    case UGLIR::TypeKind::Int:
                        return getIntTypeId();
                    case UGLIR::TypeKind::Float:
                        return getFloatTypeId();
                    case UGLIR::TypeKind::Half:
                        return getHalfTypeId();
                    case UGLIR::TypeKind::Vector:
                        return getBuiltinVectorTypeId(typeName, location);
                    default:
                        break;
                    }
                }
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    addDiagnostic(location, "unsupported UGLIR type \"" + typeName + "\".");
                    return getUIntTypeId();
                }
                switch (type->kind)
                {
                case UGLIR::TypeKind::Void:
                    return getVoidTypeId();
                case UGLIR::TypeKind::Bool:
                    return getBoolTypeId();
                case UGLIR::TypeKind::UInt:
                    if (type->bitWidth == 32)
                    {
                        return getUIntTypeId();
                    }
                    break;
                case UGLIR::TypeKind::Int:
                    if (type->bitWidth == 32)
                    {
                        return getIntTypeId();
                    }
                    break;
                case UGLIR::TypeKind::Float:
                    if (type->bitWidth == 32)
                    {
                        return getFloatTypeId();
                    }
                    break;
                case UGLIR::TypeKind::Half:
                    return getHalfTypeId();
                case UGLIR::TypeKind::Vector:
                    return getVectorTypeId(typeName, *type);
                case UGLIR::TypeKind::Matrix:
                    if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(typeName))
                    {
                        return getMatrixTypeId(typeName, *matrixShape, location);
                    }
                    break;
                case UGLIR::TypeKind::Array:
                    return getArrayTypeId(typeName, *type);
                case UGLIR::TypeKind::Struct:
                    return getStructTypeId(typeName, *type);
                case UGLIR::TypeKind::Buffer:
                    if (mModule.reflection.arguments.enabled)
                    {
                        return getVectorTypeIdForShape("u32", 4, location);
                    }
                    break;
                case UGLIR::TypeKind::Texture:
                case UGLIR::TypeKind::Sampler:
                    if (mModule.reflection.arguments.enabled)
                    {
                        return getUIntTypeId();
                    }
                    break;
                case UGLIR::TypeKind::Workgroup:
                    return getTypeId(type->elementType, type->sourceLocation);
                default:
                    break;
                }

                addDiagnostic(location, "unsupported UGLIR type kind for direct SPIR-V emission: \"" + typeName + "\".");
                return getUIntTypeId();
            }

            /** Returns the SPIR-V void type id. */
            uint32_t getVoidTypeId()
            {
                if (mVoidTypeId == 0)
                {
                    mVoidTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeVoid, {mVoidTypeId});
                    mTypeIdsByName.emplace("void", mVoidTypeId);
                }
                return mVoidTypeId;
            }

            /** Returns the SPIR-V bool type id. */
            uint32_t getBoolTypeId()
            {
                if (mBoolTypeId == 0)
                {
                    mBoolTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeBool, {mBoolTypeId});
                    mTypeIdsByName.emplace(boolTypeKey(), mBoolTypeId);
                }
                return mBoolTypeId;
            }

            /** Returns the SPIR-V unsigned 32-bit integer type id. */
            uint32_t getUIntTypeId()
            {
                if (mUIntTypeId == 0)
                {
                    mUIntTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeInt, {mUIntTypeId, 32u, 0u});
                    mTypeIdsByName.emplace("u32", mUIntTypeId);
                    mTypeIdsByName.emplace(uintTypeKey(), mUIntTypeId);
                    mTypeIdsByName.emplace("uint32_t", mUIntTypeId);
                    mTypeIdsByName.emplace("unsigned int", mUIntTypeId);
                }
                return mUIntTypeId;
            }

            /** Returns the SPIR-V signed 32-bit integer type id. */
            uint32_t getIntTypeId()
            {
                if (mIntTypeId == 0)
                {
                    mIntTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeInt, {mIntTypeId, 32u, 1u});
                    mTypeIdsByName.emplace("i32", mIntTypeId);
                    mTypeIdsByName.emplace(intTypeKey(), mIntTypeId);
                    mTypeIdsByName.emplace("int32_t", mIntTypeId);
                }
                return mIntTypeId;
            }

            /** Returns the SPIR-V 32-bit float type id. */
            uint32_t getFloatTypeId()
            {
                if (mFloatTypeId == 0)
                {
                    mFloatTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeFloat, {mFloatTypeId, 32u});
                    mTypeIdsByName.emplace("f32", mFloatTypeId);
                    mTypeIdsByName.emplace(floatTypeKey(), mFloatTypeId);
                }
                return mFloatTypeId;
            }

            /** Returns the SPIR-V native 16-bit float type id. */
            uint32_t getHalfTypeId()
            {
                if (mHalfTypeId == 0)
                {
                    mCapabilityTracker.ensureFloat16Capability();
                    mHalfTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeFloat, {mHalfTypeId, 16u});
                    mTypeIdsByName.emplace("f16", mHalfTypeId);
                    mTypeIdsByName.emplace(halfTypeKey(), mHalfTypeId);
                }
                return mHalfTypeId;
            }

            /** Returns a SPIR-V vector type id, de-duplicated by scalar type and lane count. */
            uint32_t getVectorTypeIdForShape(const std::string &scalarTypeName, uint32_t vectorWidth, const UGLIR::SourceLocation &location)
            {
                const std::string canonicalScalarTypeName = valueTypeScalarKey(scalarTypeName);
                const std::string shapeKey = canonicalScalarTypeName + ":" + std::to_string(vectorWidth);
                if (const auto existing = mVectorTypeIdsByShape.find(shapeKey); existing != mVectorTypeIdsByShape.end())
                {
                    return existing->second;
                }
                const uint32_t elementTypeId = getTypeId(canonicalScalarTypeName, location);
                const uint32_t vectorTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeVector, {vectorTypeId, elementTypeId, vectorWidth});
                mVectorTypeIdsByShape.emplace(shapeKey, vectorTypeId);
                mTypeIdsByName.emplace(vectorKeyForScalar(canonicalScalarTypeName, vectorWidth), vectorTypeId);
                return vectorTypeId;
            }

            /** Returns a SPIR-V vector type id for a supported vector UGLIR type. */
            uint32_t getVectorTypeId(const std::string &typeName, const UGLIR::Type &type)
            {
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }
                const uint32_t vectorTypeId = getVectorTypeIdForShape(type.elementType, type.vectorWidth, type.sourceLocation);
                mTypeIdsByName.emplace(typeName, vectorTypeId);
                return vectorTypeId;
            }

            /** Returns a vector type id for a built-in vector type spelling that may not appear in the UGLIR type table. */
            uint32_t getBuiltinVectorTypeId(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }
                const uint32_t vectorWidth = valueTypeVectorWidth(typeName);
                if (vectorWidth < 2u || vectorWidth > 4u)
                {
                    addDiagnostic(location, "unsupported vector type \"" + typeName + "\".");
                    return getUIntTypeId();
                }
                uint32_t elementTypeId = getFloatTypeId();
                const UGLIR::ScalarKind scalarKind = valueTypeScalarKind(typeName);
                if (scalarKind == UGLIR::ScalarKind::UInt)
                {
                    elementTypeId = getUIntTypeId();
                }
                else if (scalarKind == UGLIR::ScalarKind::Half)
                {
                    elementTypeId = getHalfTypeId();
                }
                else if (scalarKind == UGLIR::ScalarKind::Int)
                {
                    elementTypeId = getIntTypeId();
                }
                else if (scalarKind == UGLIR::ScalarKind::Bool)
                {
                    elementTypeId = getBoolTypeId();
                }
                else if (scalarKind != UGLIR::ScalarKind::Float)
                {
                    addDiagnostic(location, "unsupported vector scalar type for \"" + typeName + "\".");
                    return getUIntTypeId();
                }
                (void)elementTypeId;
                const uint32_t vectorTypeId = getVectorTypeIdForShape(spvScalarTypeKey(scalarKind), vectorWidth, location);
                mTypeIdsByName.emplace(typeName, vectorTypeId);
                return vectorTypeId;
            }

            /** Returns a native SPIR-V matrix type id for a UGL matrix value. */
            uint32_t getMatrixTypeId(const std::string &typeName,
                                     const SPIRVMatrixShape &matrixShape,
                                     const UGLIR::SourceLocation &location)
            {
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }
                if (matrixShape.rowCount < 2u || matrixShape.columnCount < 2u)
                {
                    addDiagnostic(location, "direct SPIR-V matrix values require at least two rows and two columns.");
                    return getUIntTypeId();
                }
                const std::string canonicalScalarTypeName = valueTypeScalarKey(matrixShape.scalarTypeName);
                const std::string columnVectorTypeName = vectorKeyForScalar(canonicalScalarTypeName, matrixShape.rowCount);
                const uint32_t columnVectorTypeId = getTypeId(columnVectorTypeName, location);
                const std::string key = std::to_string(columnVectorTypeId) + ":" + std::to_string(matrixShape.columnCount);
                if (const auto existingShape = mMatrixTypeIdsByShape.find(key); existingShape != mMatrixTypeIdsByShape.end())
                {
                    mTypeIdsByName.emplace(typeName, existingShape->second);
                    return existingShape->second;
                }
                const uint32_t matrixTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeMatrix, {
                    matrixTypeId,
                    columnVectorTypeId,
                    matrixShape.columnCount,
                });
                mMatrixTypeIdsByShape.emplace(key, matrixTypeId);
                mTypeIdsByName.emplace(typeName, matrixTypeId);
                return matrixTypeId;
            }

            /** Interns a fixed array, decorating only its explicit buffer-layout variant. */
            uint32_t getArrayTypeIdForLayout(uint32_t elementTypeId,
                                            uint32_t elementCount,
                                            uint32_t arrayStride)
            {
                const std::string shapeKey = std::to_string(elementTypeId) + ":" +
                    std::to_string(elementCount) + ":" + std::to_string(arrayStride);
                const auto existing = mArrayTypeIdsByShape.find(shapeKey);
                if (existing != mArrayTypeIdsByShape.end())
                {
                    return existing->second;
                }
                const uint32_t arrayTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeArray, {
                    arrayTypeId, elementTypeId, getUIntConstant(elementCount),
                });
                if (arrayStride != 0u)
                {
                    mAnnotations.appendInstruction(spv::OpDecorate, {
                        arrayTypeId,
                        static_cast<uint32_t>(spv::DecorationArrayStride),
                        arrayStride,
                    });
                }
                mArrayTypeIdsByShape.emplace(shapeKey, arrayTypeId);
                return arrayTypeId;
            }

            /** Returns a computation-only array type without a physical ArrayStride. */
            uint32_t getArrayTypeId(const std::string &typeName, const UGLIR::Type &type)
            {
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }
                if (type.elementType.empty() || type.arrayCount == 0)
                {
                    addDiagnostic(type.sourceLocation, "array type \"" + typeName + "\" is missing an element type or element count.");
                    return getUIntTypeId();
                }
                const uint32_t elementTypeId = getTypeId(type.elementType, type.sourceLocation);
                const uint32_t arrayTypeId = getArrayTypeIdForLayout(elementTypeId, type.arrayCount, 0u);
                mTypeIdsByName.emplace(typeName, arrayTypeId);
                return arrayTypeId;
            }

            /** Returns a computation-only struct; member values never inherit buffer decorations. */
            uint32_t getStructTypeId(const std::string &typeName, const UGLIR::Type &type)
            {
                const auto existing = mTypeIdsByName.find(typeName);
                if (existing != mTypeIdsByName.end())
                {
                    return existing->second;
                }
                const uint32_t structTypeId = allocateId();
                mTypeIdsByName.emplace(typeName, structTypeId);
                std::vector<uint32_t> operands{structTypeId};
                for (const UGLIR::TypeField &field : type.fields)
                {
                    operands.push_back(getTypeId(field.type, field.sourceLocation));
                }
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, operands);
                mDebugNames.appendInstructionWithString(spv::OpName, {structTypeId}, sanitizeSPIRVName(typeName));
                for (uint32_t index = 0; index < type.fields.size(); ++index)
                {
                    mDebugNames.appendInstructionWithString(spv::OpMemberName, {structTypeId, index}, sanitizeSPIRVName(type.fields[index].name));
                }
                return structTypeId;
            }

            /** Builds a buffer struct using the existing reflected ABI and nested storage types. */
            uint32_t getStorageStructTypeId(const std::string &typeName, const UGLIR::Type &type)
            {
                const uint32_t structTypeId = allocateId();
                mStorageTypeIdsByName.emplace(typeName, structTypeId);
                std::vector<uint32_t> operands{structTypeId};
                for (const UGLIR::TypeField &field : type.fields)
                {
                    operands.push_back(getStorageTypeId(field.type, field.sourceLocation));
                }
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, operands);
                mDebugNames.appendInstructionWithString(
                    spv::OpName, {structTypeId}, "Storage_" + sanitizeSPIRVName(typeName));
                for (uint32_t index = 0; index < type.fields.size(); ++index)
                {
                    const UGLIR::TypeField &field = type.fields[index];
                    mDebugNames.appendInstructionWithString(
                        spv::OpMemberName, {structTypeId, index}, sanitizeSPIRVName(field.name));
                    mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                        structTypeId, index,
                        static_cast<uint32_t>(spv::DecorationOffset), field.offset,
                    });
                    decorateMatrixBlockMemberLayoutIfNeeded(structTypeId, index, field.type);
                }
                return structTypeId;
            }

            /**
             * Returns an explicit buffer-layout type while preserving all existing byte offsets.
             * Only structs and arrays need distinct ids; scalar, vector and matrix values share
             * their logical types because matrix memory order belongs to the containing member.
             */
            uint32_t getStorageTypeId(const std::string &typeName,
                                      const UGLIR::SourceLocation &location)
            {
                if (describeTypeName(typeName).has_value())
                {
                    return getTypeId(typeName, location);
                }
                const auto existing = mStorageTypeIdsByName.find(typeName);
                if (existing != mStorageTypeIdsByName.end())
                {
                    return existing->second;
                }
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    return getTypeId(typeName, location);
                }
                if (type->kind == UGLIR::TypeKind::Struct)
                {
                    return getStorageStructTypeId(typeName, *type);
                }
                if (type->kind == UGLIR::TypeKind::Array)
                {
                    const uint32_t elementTypeId = getStorageTypeId(type->elementType, location);
                    const uint32_t arrayTypeId = getArrayTypeIdForLayout(
                        elementTypeId, type->arrayCount, getStorageLayoutByteSize(type->elementType));
                    mStorageTypeIdsByName.emplace(typeName, arrayTypeId);
                    return arrayTypeId;
                }
                return getTypeId(typeName, location);
            }

            /** Selects the pointee representation at a memory boundary, never for an SSA value. */
            uint32_t getMemoryValueTypeId(const std::string &typeName,
                                          spv::StorageClass storageClass,
                                          const UGLIR::SourceLocation &location)
            {
                if (storageClass == spv::StorageClassUniform ||
                    storageClass == spv::StorageClassStorageBuffer || storageClass == spv::StorageClassPhysicalStorageBuffer)
                {
                    return getStorageTypeId(typeName, location);
                }
                return getTypeId(typeName, location);
            }

            /** Returns a SPIR-V pointer type id for a pointee type and storage class. */
            uint32_t getPointerTypeId(uint32_t pointeeTypeId, spv::StorageClass storageClass)
            {
                const std::string key = std::to_string(pointeeTypeId) + ":" + std::to_string(static_cast<uint32_t>(storageClass));
                const auto existing = mPointerTypeIds.find(key);
                if (existing != mPointerTypeIds.end())
                {
                    return existing->second;
                }
                const uint32_t pointerTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypePointer, {
                    pointerTypeId,
                    static_cast<uint32_t>(storageClass),
                    pointeeTypeId,
                });
                mPointerTypeIds.emplace(key, pointerTypeId);
                return pointerTypeId;
            }

            /** Returns the SPIR-V storage class used by one reflected resource variable. */
            spv::StorageClass storageClassForResource(const SPIRVResourceInfo &resource) const
            {
                if (resource.binding == nullptr)
                {
                    return spv::StorageClassUniformConstant;
                }
                if (resource.binding->kind == UGLIR::ResourceKind::StorageBuffer)
                {
                    return spv::StorageClassStorageBuffer;
                }
                if (resource.binding->kind == UGLIR::ResourceKind::UniformBuffer)
                {
                    return spv::StorageClassUniform;
                }
                return spv::StorageClassUniformConstant;
            }

            /** Decorates a SPIR-V struct type as a block once for uniform-buffer variables. */
            void decorateBlockTypeOnce(uint32_t typeId)
            {
                if (mBlockDecoratedTypeIds.insert(typeId).second)
                {
                    mAnnotations.appendInstruction(spv::OpDecorate, {
                        typeId,
                        static_cast<uint32_t>(spv::DecorationBlock),
                    });
                }
            }

            /** Returns true when a block member type is a matrix or an array of matrices. */
            bool needsMatrixBlockMemberLayout(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    return false;
                }
                if (type->kind == UGLIR::TypeKind::Workgroup)
                {
                    return needsMatrixBlockMemberLayout(type->elementType);
                }
                if (type->kind == UGLIR::TypeKind::Array)
                {
                    return needsMatrixBlockMemberLayout(type->elementType);
                }
                return type->kind == UGLIR::TypeKind::Matrix;
            }

            /** Emits MatrixStride and RowMajor decorations for matrix-valued block members. */
            void decorateMatrixBlockMemberLayoutIfNeeded(uint32_t blockTypeId, uint32_t memberIndex, const std::string &memberTypeName)
            {
                if (!needsMatrixBlockMemberLayout(memberTypeName))
                {
                    return;
                }
                const UGLIR::Type *matrixType = findType(memberTypeName);
                while (matrixType != nullptr && matrixType->kind != UGLIR::TypeKind::Matrix)
                {
                    matrixType = findType(matrixType->elementType);
                }
                if (matrixType == nullptr)
                {
                    addDiagnostic(mModule.sourceLocation, "matrix block layout has no registered matrix type.");
                    return;
                }
                const uint32_t scalarSize = matrixType->scalarKind == UGLIR::ScalarKind::Half ? 2u : 4u;
                const uint32_t matrixStride = (matrixType->matrixColumns == 2u ? 2u : 4u) * scalarSize;
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                    blockTypeId,
                    memberIndex,
                    static_cast<uint32_t>(spv::DecorationMatrixStride),
                    matrixStride,
                });
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                    blockTypeId,
                    memberIndex,
                    static_cast<uint32_t>(spv::DecorationRowMajor),
                });
            }

            /** Returns the canonical SPIR-V sampler type id. */
            uint32_t getSamplerTypeId()
            {
                if (mSamplerTypeId == 0)
                {
                    mSamplerTypeId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeSampler, {mSamplerTypeId});
                }
                return mSamplerTypeId;
            }

            /** Returns an image type id for a UGLIR texture resource. */
            uint32_t getImageTypeId(const UGLIR::ResourceBinding &resource)
            {
                spv::Dim dimension = spv::Dim2D;
                uint32_t arrayed = 0;
                if (resource.kind == UGLIR::ResourceKind::InputAttachment)
                {
                    dimension = spv::DimSubpassData;
                }
                else if (resource.textureDimension == UGLIR::TextureDimension::Texture2DArray)
                {
                    dimension = spv::Dim2D;
                    arrayed = 1;
                }
                else if (resource.textureDimension == UGLIR::TextureDimension::Texture3D)
                {
                    dimension = spv::Dim3D;
                }
                const uint32_t sampled = resource.kind == UGLIR::ResourceKind::StorageTexture ||
                                                 resource.kind == UGLIR::ResourceKind::InputAttachment
                                             ? 2u
                                             : 1u;
                const uint32_t depthOperand = UGLIR::isDepthTextureFormat(resource.textureFormat) ? 1u : 2u;
                const spv::ImageFormat imageFormat = resource.kind == UGLIR::ResourceKind::StorageTexture
                                                          ? spirvImageFormatForTextureFormat(resource.textureFormat)
                                                          : spv::ImageFormatUnknown;
                if (resource.kind == UGLIR::ResourceKind::StorageTexture &&
                    imageFormat != spv::ImageFormatUnknown &&
                    imageFormat != spv::ImageFormatRgba8 &&
                    imageFormat != spv::ImageFormatRgba8i &&
                    imageFormat != spv::ImageFormatRgba8ui)
                {
                    mCapabilityTracker.ensureStorageImageExtendedFormatsCapability();
                }
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(resource, describeTypeName(resource.elementType));
                if (!payloadType.has_value())
                {
                    addDiagnostic(resource.sourceLocation, "texture or image resource \"" + resource.name + "\" is missing structured payload metadata for direct SPIR-V emission.");
                    return getUIntTypeId();
                }
                const std::string sampledTypeName = spvScalarTypeKey(payloadType->sampledScalarKind);
                std::ostringstream keyStream;
                keyStream << sampledTypeName << ':'
                          << static_cast<uint32_t>(dimension) << ':'
                          << depthOperand << ':'
                          << arrayed << ':'
                          << (resource.isMultisampled ? 1u : 0u) << ':'
                          << sampled << ':'
                          << static_cast<uint32_t>(imageFormat);
                const std::string key = keyStream.str();
                if (const auto existing = mImageTypeIds.find(key); existing != mImageTypeIds.end())
                {
                    return existing->second;
                }
                const uint32_t sampledTypeId = getTypeId(sampledTypeName, resource.sourceLocation);
                const uint32_t imageTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeImage, {
                    imageTypeId,
                    sampledTypeId,
                    static_cast<uint32_t>(dimension),
                    depthOperand,
                    arrayed,
                    resource.isMultisampled ? 1u : 0u,
                    sampled,
                    static_cast<uint32_t>(imageFormat),
                });
                mImageTypeIds.emplace(key, imageTypeId);
                return imageTypeId;
            }

            /** Returns a sampled-image type id for one image type id. */
            uint32_t getSampledImageTypeId(uint32_t imageTypeId)
            {
                const auto existing = mSampledImageTypeIds.find(imageTypeId);
                if (existing != mSampledImageTypeIds.end())
                {
                    return existing->second;
                }
                const uint32_t sampledImageTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeSampledImage, {sampledImageTypeId, imageTypeId});
                mSampledImageTypeIds.emplace(imageTypeId, sampledImageTypeId);
                return sampledImageTypeId;
            }

            /** Returns a SPIR-V unsigned integer constant id. */
            uint32_t getUIntConstant(uint32_t value)
            {
                const std::string key = std::to_string(value);
                const auto existing = mUIntConstants.find(key);
                if (existing != mUIntConstants.end())
                {
                    return existing->second;
                }
                const uint32_t constantId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpConstant, {getUIntTypeId(), constantId, value});
                mUIntConstants.emplace(key, constantId);
                return constantId;
            }

            /** Returns a SPIR-V signed integer constant id. */
            uint32_t getIntConstant(uint32_t valueBits)
            {
                const std::string key = std::to_string(valueBits);
                const auto existing = mIntConstants.find(key);
                if (existing != mIntConstants.end())
                {
                    return existing->second;
                }
                const uint32_t constantId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpConstant, {getIntTypeId(), constantId, valueBits});
                mIntConstants.emplace(key, constantId);
                return constantId;
            }

            /** Returns a SPIR-V 32-bit floating-point constant id. */
            uint32_t getFloatConstant(const std::string &value)
            {
                const auto existing = mFloatConstants.find(value);
                if (existing != mFloatConstants.end())
                {
                    return existing->second;
                }
                const uint32_t constantId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpConstant, {getFloatTypeId(), constantId, parseFloatLiteralBits(value)});
                mFloatConstants.emplace(value, constantId);
                return constantId;
            }

            /** Returns a SPIR-V native 16-bit floating-point constant id. */
            uint32_t getHalfConstant(const std::string &value)
            {
                const auto existing = mHalfConstants.find(value);
                if (existing != mHalfConstants.end())
                {
                    return existing->second;
                }
                const uint32_t constantId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpConstant, {getHalfTypeId(), constantId, parseHalfLiteralBits(value)});
                mHalfConstants.emplace(value, constantId);
                return constantId;
            }

            /** Returns the canonical zero coordinate used by Vulkan subpass input attachment reads. */
            uint32_t getSubpassCoordinateZeroId(const UGLIR::SourceLocation &location)
            {
                if (mSubpassCoordinateZeroId == 0)
                {
                    mSubpassCoordinateZeroId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpConstantComposite, {
                        getTypeId(spvVectorTypeKey(UGLIR::ScalarKind::Int, 2u), location),
                        mSubpassCoordinateZeroId,
                        getIntConstant(0),
                        getIntConstant(0),
                    });
                }
                return mSubpassCoordinateZeroId;
            }

            /** Returns true when a UGLIR type is a resource alias that has no direct SPIR-V function ABI parameter. */
            bool isResourceAliasType(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type != nullptr && mModule.reflection.arguments.enabled &&
                    (type->kind == UGLIR::TypeKind::Buffer || type->kind == UGLIR::TypeKind::Texture || type->kind == UGLIR::TypeKind::Sampler))
                {
                    return false;
                }
                if (type != nullptr &&
                    (type->kind == UGLIR::TypeKind::Resource ||
                     type->kind == UGLIR::TypeKind::Buffer ||
                     type->kind == UGLIR::TypeKind::Texture ||
                     type->kind == UGLIR::TypeKind::Sampler))
                {
                    return true;
                }
                return false;
            }

            /** Returns the canonical SPIR-V true constant id. */
            uint32_t getTrueConstant()
            {
                if (mTrueConstantId == 0)
                {
                    mTrueConstantId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpConstantTrue, {getBoolTypeId(), mTrueConstantId});
                }
                return mTrueConstantId;
            }

            /** Returns the canonical SPIR-V false constant id. */
            uint32_t getFalseConstant()
            {
                if (mFalseConstantId == 0)
                {
                    mFalseConstantId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpConstantFalse, {getBoolTypeId(), mFalseConstantId});
                }
                return mFalseConstantId;
            }

            /** Returns true when an lvalue expression ultimately names the requested function parameter. */
            bool expressionTargetsParameter(const UGLIR::Expression &expression, const std::string &parameterName) const
            {
                if (mModule.reflection.arguments.enabled && !expression.operands.empty())
                {
                    if (const auto *base = findType(expression.operands.front().type); base != nullptr && base->kind == UGLIR::TypeKind::Buffer &&
                        (expression.kind == UGLIR::ExpressionKind::Subscript || expression.kind == UGLIR::ExpressionKind::MemberRef))
                    {
                        return false;
                    }
                }

                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    return expression.name == parameterName;
                }
                if (expression.kind == UGLIR::ExpressionKind::ThisRef)
                {
                    return parameterName == "this";
                }
                if ((expression.kind == UGLIR::ExpressionKind::MemberRef ||
                     expression.kind == UGLIR::ExpressionKind::Subscript ||
                     expression.kind == UGLIR::ExpressionKind::Cast ||
                     expression.kind == UGLIR::ExpressionKind::Load ||
                     expression.kind == UGLIR::ExpressionKind::Construct) &&
                    !expression.operands.empty())
                {
                    return expressionTargetsParameter(expression.operands.front(), parameterName);
                }
                return false;
            }

            /** Returns true when an expression writes to the requested parameter, optionally including writable call arguments. */
            bool expressionWritesParameter(const UGLIR::Expression &expression,
                                           const std::string &parameterName,
                                           bool includeWritableCalls = false) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Store &&
                    !expression.operands.empty() &&
                    expressionTargetsParameter(expression.operands.front(), parameterName))
                {
                    return true;
                }
                if (expression.kind == UGLIR::ExpressionKind::Unary &&
                    (expression.operatorName == "++" || expression.operatorName == "--") &&
                    !expression.operands.empty() &&
                    expressionTargetsParameter(expression.operands.front(), parameterName))
                {
                    return true;
                }
                if (includeWritableCalls && expression.kind == UGLIR::ExpressionKind::Call)
                {
                    const UGLIR::Function *callee = nullptr;
                    for (const UGLIR::Function &candidate : mModule.functions)
                    {
                        if (candidate.name == expression.name)
                        {
                            callee = &candidate;
                            break;
                        }
                    }
                    if (callee != nullptr)
                    {
                        size_t parameterIndex = 0;
                        for (const UGLIR::Expression &argument : expression.operands)
                        {
                            const UGLIR::FunctionParameter *targetParameter =
                                parameterIndex < callee->parameters.size()
                                    ? &callee->parameters[parameterIndex]
                                    : nullptr;
                            ++parameterIndex;
                            if (isResourceAliasType(argument.type))
                            {
                                continue;
                            }
                            const UGLIR::Type *targetParameterType = targetParameter == nullptr ? nullptr : findType(targetParameter->type);
                            const bool targetIsArray = targetParameterType != nullptr && targetParameterType->kind == UGLIR::TypeKind::Array;
                            const bool targetMayWrite = targetParameter != nullptr &&
                                                        (targetIsArray ||
                                                         (targetParameter->isReference && !targetParameter->isConstReference) ||
                                                         targetParameter->passingMode == UGLIR::ParameterPassingMode::Out ||
                                                         targetParameter->passingMode == UGLIR::ParameterPassingMode::InOut);
                            if (targetMayWrite && expressionTargetsParameter(argument, parameterName))
                            {
                                return true;
                            }
                        }
                    }
                    if (callee == nullptr)
                    {
                        switch (expression.intrinsicCallKind)
                        {
                        case UGLIR::IntrinsicCallKind::MathSincos:
                            for (size_t index = 1u; index < expression.operands.size() && index <= 2u; ++index)
                            {
                                if (expressionTargetsParameter(expression.operands[index], parameterName))
                                {
                                    return true;
                                }
                            }
                            break;
                        case UGLIR::IntrinsicCallKind::MathModf:
                            if (expression.operands.size() > 1u &&
                                expressionTargetsParameter(expression.operands[1], parameterName))
                            {
                                return true;
                            }
                            break;
                        case UGLIR::IntrinsicCallKind::AtomicStore:
                        case UGLIR::IntrinsicCallKind::AtomicAdd:
                        case UGLIR::IntrinsicCallKind::AtomicAnd:
                        case UGLIR::IntrinsicCallKind::AtomicOr:
                        case UGLIR::IntrinsicCallKind::AtomicMin:
                        case UGLIR::IntrinsicCallKind::AtomicMax:
                        case UGLIR::IntrinsicCallKind::AtomicCompareExchange:
                            for (size_t index = 0u; index < expression.operands.size(); ++index)
                            {
                                const bool isAtomicCompareExchangeResult =
                                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicCompareExchange && index == 3u;
                                const bool isWritableIntrinsicOperand =
                                    index == 0u || isAtomicCompareExchangeResult;
                                if (isWritableIntrinsicOperand &&
                                    expressionTargetsParameter(expression.operands[index], parameterName))
                                {
                                    return true;
                                }
                            }
                            break;
                        case UGLIR::IntrinsicCallKind::None:
                            for (const UGLIR::Expression &argument : expression.operands)
                            {
                                if (expressionTargetsParameter(argument, parameterName))
                                {
                                    return true;
                                }
                            }
                            break;
                        default:
                            break;
                        }
                    }
                }
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (expressionWritesParameter(operand, parameterName, includeWritableCalls))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when a statement subtree writes to the requested parameter, optionally including writable calls. */
            bool statementWritesParameter(const UGLIR::Statement &statement,
                                          const std::string &parameterName,
                                          bool includeWritableCalls = false) const
            {
                for (const UGLIR::Expression &expression : statement.expressions)
                {
                    if (expressionWritesParameter(expression, parameterName, includeWritableCalls))
                    {
                        return true;
                    }
                }
                for (const UGLIR::Statement &child : statement.children)
                {
                    if (statementWritesParameter(child, parameterName, includeWritableCalls))
                    {
                        return true;
                    }
                }
                for (const UGLIR::Statement &child : statement.elseChildren)
                {
                    if (statementWritesParameter(child, parameterName, includeWritableCalls))
                    {
                        return true;
                    }
                }
                for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                {
                    for (const UGLIR::Statement &caseStatement : switchCase.body)
                    {
                        if (statementWritesParameter(caseStatement, parameterName, includeWritableCalls))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            /** Returns true when a function body writes to one of its parameters, optionally including writable calls. */
            bool functionWritesParameter(const UGLIR::Function &function,
                                         const std::string &parameterName,
                                         bool includeWritableCalls = false) const
            {
                for (const UGLIR::Statement &statement : function.body)
                {
                    if (statementWritesParameter(statement, parameterName, includeWritableCalls))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when any reference parameter can be written directly or through a writable helper call. */
            bool functionHasWritableReference(const UGLIR::Function &function) const
            {
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    const UGLIR::Type *parameterType = findType(parameter.type);
                    const bool isArrayParameter = parameterType != nullptr && parameterType->kind == UGLIR::TypeKind::Array;
                    if ((parameter.isReference || isArrayParameter) && functionWritesParameter(function, parameter.name, true))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when a helper parameter must be lowered as a writable function pointer. */
            bool functionParameterRequiresPointer(const UGLIR::Function &function, const UGLIR::FunctionParameter &parameter) const
            {
                if (function.isEntryPoint || isResourceAliasType(parameter.type))
                {
                    return false;
                }
                const UGLIR::Type *parameterType = findType(parameter.type);
                if (parameterType != nullptr && parameterType->kind == UGLIR::TypeKind::Array)
                {
                    return true;
                }
                if (parameter.isReference && parameter.passingMode == UGLIR::ParameterPassingMode::Value)
                {
                    return functionHasWritableReference(function);
                }
                if (parameter.isReference && !parameter.isConstReference)
                {
                    if (parameter.passingMode == UGLIR::ParameterPassingMode::Out ||
                        parameter.passingMode == UGLIR::ParameterPassingMode::InOut)
                    {
                        return true;
                    }
                    return functionWritesParameter(function, parameter.name);
                }
                return false;
            }

            /** Registers all UGLIR function ids and SPIR-V function type records before emitting bodies. */
            void registerFunctionSignatures()
            {
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (mFunctionsByName.find(function.name) != mFunctionsByName.end())
                    {
                        continue;
                    }

                    SPIRVFunctionInfo info;
                    info.function = &function;
                    info.functionId = allocateId();
                    info.returnTypeId = function.isEntryPoint ? getVoidTypeId() : getTypeId(function.returnType, function.sourceLocation);
                    std::vector<uint32_t> parameterTypeIds;
                    if (!function.isEntryPoint)
                    {
                        for (const UGLIR::FunctionParameter &parameter : function.parameters)
                        {
                            if (isResourceAliasType(parameter.type))
                            {
                                continue;
                            }
                            const bool parameterIsPointer = functionParameterRequiresPointer(function, parameter);
                            const uint32_t parameterTypeId = parameterIsPointer
                                                                 ? getPointerTypeId(getTypeId(parameter.type, parameter.sourceLocation), spv::StorageClassFunction)
                                                                 : getTypeId(parameter.type, parameter.sourceLocation);
                            info.parameterIsPointer.push_back(parameterIsPointer);
                            info.parameterTypeIds.push_back(parameterTypeId);
                            parameterTypeIds.push_back(parameterTypeId);
                        }
                    }
                    std::ostringstream functionTypeKey;
                    functionTypeKey << info.returnTypeId;
                    for (uint32_t parameterTypeId : parameterTypeIds)
                    {
                        functionTypeKey << ":" << parameterTypeId;
                    }
                    const std::string key = functionTypeKey.str();
                    const auto existingFunctionType = mFunctionTypeIds.find(key);
                    if (existingFunctionType != mFunctionTypeIds.end())
                    {
                        info.functionTypeId = existingFunctionType->second;
                    }
                    else
                    {
                        info.functionTypeId = allocateId();
                        std::vector<uint32_t> functionTypeOperands{info.functionTypeId, info.returnTypeId};
                        functionTypeOperands.insert(functionTypeOperands.end(), parameterTypeIds.begin(), parameterTypeIds.end());
                        mTypesConstantsGlobals.appendInstruction(spv::OpTypeFunction, functionTypeOperands);
                        mFunctionTypeIds.emplace(key, info.functionTypeId);
                    }
                    mFunctionsByName.emplace(function.name, info);
                    mDebugNames.appendInstructionWithString(spv::OpName, {info.functionId}, function.isEntryPoint ? entryPointNameForEntryKind(function.entryKind) : sanitizeSPIRVName(function.name));
                    if (function.isEntryPoint)
                    {
                        mEntryPointFunctionId = info.functionId;
                    }
                }
            }

            /** Declares the internal uvec2 push root and the fixed word layout shared with generated host code. */
            void buildArgumentRoot()
            {
                const auto &location = mModule.sourceLocation;
                const auto addressType = getVectorTypeIdForShape("u32", 2, location);
                const auto pushType = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, {pushType, addressType});
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {pushType, 0, static_cast<uint32_t>(spv::DecorationOffset), 0});
                decorateBlockTypeOnce(pushType);
                mArgumentPushConstantId = allocateId();
                const auto pushPointer = getPointerTypeId(pushType, spv::StorageClassPushConstant);
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {pushPointer, mArgumentPushConstantId, static_cast<uint32_t>(spv::StorageClassPushConstant)});
                mDebugNames.appendInstructionWithString(spv::OpName, {mArgumentPushConstantId}, "__ugl_args_root");
                mEntryPointInterfaceIds.push_back(mArgumentPushConstantId);
                const auto wordsType = getArrayTypeIdForLayout(getUIntTypeId(), mModule.reflection.arguments.byteSize / 4, 4);
                mArgumentRootTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, {mArgumentRootTypeId, wordsType});
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {mArgumentRootTypeId, 0, static_cast<uint32_t>(spv::DecorationOffset), 0});
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {mArgumentRootTypeId, 0, static_cast<uint32_t>(spv::DecorationNonWritable)});
                decorateBlockTypeOnce(mArgumentRootTypeId);
            }

            /** Loads the root once in the function entry block so it dominates every argument access. */
            void emitArgumentRootLoad(SPIRVFunctionContext &context)
            {
                const auto addressType = getVectorTypeIdForShape("u32", 2, mModule.sourceLocation);
                const auto pushPointer = getPointerTypeId(addressType, spv::StorageClassPushConstant);
                const auto pointer = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {pushPointer, pointer, mArgumentPushConstantId, getUIntConstant(0)});
                const auto address = allocateId();
                context.body.appendInstruction(spv::OpLoad, {addressType, address, pointer});
                context.argumentRootPointerId = allocateId();
                const auto rootPointer = getPointerTypeId(mArgumentRootTypeId, spv::StorageClassPhysicalStorageBuffer);
                context.body.appendInstruction(spv::OpBitcast, {rootPointer, context.argumentRootPointerId, address});
            }

            /** Loads one encoded 32-bit word without introducing Int64 or scalar-block-layout requirements. */
            uint32_t emitArgumentWord(SPIRVFunctionContext &context, uint32_t wordOffset)
            {
                const auto pointerType = getPointerTypeId(getUIntTypeId(), spv::StorageClassPhysicalStorageBuffer);
                const auto pointer = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {pointerType, pointer, context.argumentRootPointerId,
                    getUIntConstant(0), getUIntConstant(wordOffset)});
                const auto value = allocateId();
                context.body.appendInstruction(spv::OpLoad, {getUIntTypeId(), value, pointer,
                    static_cast<uint32_t>(spv::MemoryAccessAlignedMask), 4});
                return value;
            }

            /** Reconstructs one ordinary value or four-word resource handle from the compiler-owned layout. */
            SPIRVValue emitArgumentValue(SPIRVFunctionContext &context, const std::string &typeName, const std::string &path)
            {
                const auto &location = mModule.sourceLocation;
                for (const auto &resource : mModule.reflection.resources)
                {
                    if (resource.name == path)
                    {
                        if (resource.kind == UGLIR::ResourceKind::Texture || resource.kind == UGLIR::ResourceKind::StorageTexture ||
                            resource.kind == UGLIR::ResourceKind::Sampler)
                        {
                            return makeValue(emitArgumentWord(context, resource.argumentAddressOffset / 4), typeName, location);
                        }
                        const auto vectorType = spvVectorTypeKey(UGLIR::ScalarKind::UInt, 4);
                        const auto value = emitVectorValueFromComponents(context, vectorType,
                            {emitArgumentWord(context, resource.argumentAddressOffset / 4),
                             emitArgumentWord(context, resource.argumentAddressOffset / 4 + 1),
                             emitArgumentWord(context, resource.argumentLengthOffset / 4),
                             emitArgumentWord(context, resource.argumentLengthOffset / 4 + 1)}, location);
                        return makeValue(value.id, findType(typeName) != nullptr ? typeName : vectorType, location);
                    }
                }
                for (const auto &field : mModule.reflection.arguments.values)
                {
                    if (field.path == path)
                    {
                        const auto shape = describeTypeName(typeName);
                        if (!shape)
                        {
                            addDiagnostic(location, "Args field has no scalar representation: " + path);
                            return {};
                        }
                        const auto scalarType = spvScalarTypeKey(shape->scalarKind);
                        std::vector<uint32_t> components;
                        for (uint32_t lane = 0; lane < field.scalarCount; ++lane)
                        {
                            const auto word = emitArgumentWord(context, field.offset / 4 + lane);
                            auto component = word;
                            if (shape->scalarKind == UGLIR::ScalarKind::Bool)
                            {
                                component = allocateId();
                                context.body.appendInstruction(spv::OpINotEqual, {getBoolTypeId(), component, word, getUIntConstant(0)});
                            }
                            else if (shape->scalarKind == UGLIR::ScalarKind::Half)
                            {
                                const auto unpacked = emitGLSLExtInst(context, spvVectorTypeKey(UGLIR::ScalarKind::Float, 2), GLSLstd450UnpackHalf2x16,
                                    {makeValue(word, "u32", location)}, location);
                                const auto first = allocateId();
                                context.body.appendInstruction(spv::OpCompositeExtract, {getFloatTypeId(), first, unpacked.id, 0});
                                component = allocateId();
                                context.body.appendInstruction(spv::OpFConvert, {getHalfTypeId(), component, first});
                            }
                            else if (shape->scalarKind != UGLIR::ScalarKind::UInt)
                            {
                                component = allocateId();
                                context.body.appendInstruction(spv::OpBitcast, {getTypeId(scalarType, location), component, word});
                            }
                            components.push_back(component);
                        }
                        if (shape->matrixColumns != 0)
                        {
                            std::vector<SPIRVValue> rows;
                            for (uint32_t row = 0; row < shape->matrixRows; ++row)
                            {
                                const auto begin = components.begin() + row * shape->matrixColumns;
                                rows.push_back(emitVectorValueFromComponents(context,
                                    spvVectorTypeKey(shape->scalarKind, shape->matrixColumns),
                                    std::vector<uint32_t>(begin, begin + shape->matrixColumns), location));
                            }
                            return emitMatrixValueFromRows(context, typeName, rows, location);
                        }
                        if (components.size() == 1)
                        {
                            return makeValue(components[0], typeName, location);
                        }
                        return emitVectorValueFromComponents(context, typeName, components, location);
                    }
                }
                const auto *type = findType(typeName);
                if (type == nullptr)
                {
                    addDiagnostic(location, "Missing Args type " + typeName);
                    return {};
                }
                if (type->kind == UGLIR::TypeKind::Resource && !type->elementType.empty())
                {
                    return emitArgumentValue(context, type->elementType, path);
                }
                std::vector<uint32_t> operands{getTypeId(typeName, location), allocateId()};
                if (type->kind == UGLIR::TypeKind::Array)
                {
                    for (uint32_t index = 0; index < type->arrayCount; ++index)
                    {
                        operands.push_back(emitArgumentValue(context, type->elementType, path + "[" + std::to_string(index) + "]").id);
                    }
                }
                else
                {
                    for (const auto &field : type->fields)
                    {
                        operands.push_back(emitArgumentValue(context, field.type, path + "." + field.name).id);
                    }
                }
                context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                return makeValue(operands[1], typeName, location);
            }

            /** Identifies either a direct reflected buffer or a buffer handle held in an ordinary value. */
            bool isArgumentBufferExpression(const SPIRVFunctionContext &context, const UGLIR::Expression &expression) const
            {
                if (const auto *resource = findResourceForExpression(context, expression))
                {
                    return resource->binding->kind == UGLIR::ResourceKind::StorageBuffer || resource->binding->kind == UGLIR::ResourceKind::UniformBuffer;
                }
                const auto *type = findType(expression.type);
                return type != nullptr && type->kind == UGLIR::TypeKind::Buffer;
            }

            /** Uses the largest stored scalar component alignment, including two-byte half elements. */
            uint32_t argumentMemoryAlignment(const std::string &typeName) const
            {
                if (const auto shape = describeTypeName(typeName))
                {
                    return shape->scalarKind == UGLIR::ScalarKind::Half ? 2u : 4u;
                }
                const auto *type = findType(typeName);
                if (type == nullptr)
                {
                    return 4;
                }
                if (type->kind == UGLIR::TypeKind::Array)
                {
                    return argumentMemoryAlignment(type->elementType);
                }
                uint32_t alignment = 1;
                for (const auto &field : type->fields)
                {
                    alignment = std::max(alignment, argumentMemoryAlignment(field.type));
                }
                return alignment;
            }

            /** Addresses an existing uniform/storage element through its captured BDA handle and original element layout. */
            SPIRVValue emitArgumentBufferPointer(SPIRVFunctionContext &context, const UGLIR::Expression &expression, uint32_t index)
            {
                const auto &location = expression.sourceLocation;
                const auto *resource = findResourceForExpression(context, expression);
                const auto *type = findType(expression.type);
                const auto elementName = resource != nullptr ? resource->binding->elementType : type->elementType;
                const auto value = resource != nullptr ? emitArgumentValue(context, expression.type, resource->binding->name) : emitRValue(context, expression);
                const auto address = allocateId();
                const auto vectorType = getVectorTypeIdForShape("u32", 2, location);
                context.body.appendInstruction(spv::OpVectorShuffle, {vectorType, address, value.id, value.id, 0, 1});
                const auto elementType = getStorageTypeId(elementName, location);
                const bool readOnly = resource != nullptr ? resource->binding->effectiveAccessMode() == UGLIR::AccessMode::Read : type->accessMode == UGLIR::AccessMode::Read;
                const uint64_t blockKey = (uint64_t(elementType) << 1) | uint64_t(readOnly);
                uint32_t blockType = 0;
                if (const auto found = mArgumentBufferBlockTypeIds.find(blockKey); found != mArgumentBufferBlockTypeIds.end())
                {
                    blockType = found->second;
                }
                else
                {
                    const auto arrayType = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeRuntimeArray, {arrayType, elementType});
                    mAnnotations.appendInstruction(spv::OpDecorate, {arrayType, static_cast<uint32_t>(spv::DecorationArrayStride), getStorageLayoutByteSize(elementName)});
                    blockType = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, {blockType, arrayType});
                    mAnnotations.appendInstruction(spv::OpMemberDecorate, {blockType, 0, static_cast<uint32_t>(spv::DecorationOffset), 0});
                    if (readOnly)
                    {
                        mAnnotations.appendInstruction(spv::OpMemberDecorate, {blockType, 0, static_cast<uint32_t>(spv::DecorationNonWritable)});
                    }
                    decorateMatrixBlockMemberLayoutIfNeeded(blockType, 0, elementName);
                    decorateBlockTypeOnce(blockType);
                    mArgumentBufferBlockTypeIds.emplace(blockKey, blockType);
                }
                const auto base = allocateId();
                context.body.appendInstruction(spv::OpBitcast, {getPointerTypeId(blockType, spv::StorageClassPhysicalStorageBuffer), base, address});
                const auto pointer = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {getPointerTypeId(elementType, spv::StorageClassPhysicalStorageBuffer),
                    pointer, base, getUIntConstant(0), index});
                return makePointerValue(pointer, elementName, spv::StorageClassPhysicalStorageBuffer, location, false);
            }

            /** Builds SPIR-V storage buffer globals from UGLIR reflection resources. */
            void buildResources()
            {
                for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                {
                    if (mModule.reflection.arguments.enabled && resource.kind != UGLIR::ResourceKind::InputAttachment)
                    {
                        if (typeContainsHalf(resource.elementType))
                        {
                            mCapabilityTracker.ensureStorageBuffer16BitAccessCapability();
                        }
                        if (resource.kind == UGLIR::ResourceKind::Texture || resource.kind == UGLIR::ResourceKind::StorageTexture ||
                            resource.kind == UGLIR::ResourceKind::Sampler)
                        {
                            const SPIRVResourceInfo *existing = nullptr;
                            for (const auto &[name, candidate] : mResourcesByUGLIRName)
                            {
                                if (candidate.binding->argumentType == resource.argumentType)
                                {
                                    existing = &candidate;
                                    break;
                                }
                            }
                            if (existing != nullptr)
                            {
                                auto alias = *existing;
                                alias.binding = &resource;
                                mResourcesByUGLIRName.emplace(resource.name, std::move(alias));
                            }
                            else
                            {
                                buildOpaqueResource(resource);
                            }
                        }
                        else
                        {
                            mResourcesByUGLIRName.emplace(resource.name, makeResourceInfo(resource));
                        }
                        continue;
                    }
                    declareBufferCapabilities(resource);
                    switch (resource.kind)
                    {
                    case UGLIR::ResourceKind::Texture:
                    case UGLIR::ResourceKind::StorageTexture:
                    case UGLIR::ResourceKind::Sampler:
                    case UGLIR::ResourceKind::InputAttachment:
                        buildOpaqueResource(resource);
                        break;
                    case UGLIR::ResourceKind::UniformBuffer:
                        buildUniformResource(resource);
                        break;
                    case UGLIR::ResourceKind::StorageBuffer:
                        buildStorageResource(resource);
                        break;
                    default:
                        addDiagnostic(resource.sourceLocation,
                            "unsupported resource kind for direct SPIR-V emission.");
                        break;
                    }
                }
            }

            /** Declares existing half-storage capabilities independently of aggregate layout. */
            void declareBufferCapabilities(const UGLIR::ResourceBinding &resource)
            {
                if (!typeContainsHalf(resource.elementType))
                {
                    return;
                }
                if (resource.kind == UGLIR::ResourceKind::StorageBuffer)
                {
                    mCapabilityTracker.ensureStorageBuffer16BitAccessCapability();
                }
                if (resource.kind == UGLIR::ResourceKind::UniformBuffer)
                {
                    mCapabilityTracker.ensureUniformAndStorageBuffer16BitAccessCapability();
                }
            }

            /** Creates compiler-owned metadata borrowing one immutable reflection resource. */
            SPIRVResourceInfo makeResourceInfo(const UGLIR::ResourceBinding &resource) const
            {
                SPIRVResourceInfo resourceInfo;
                resourceInfo.binding = &resource;
                resourceInfo.variableName = sanitizeSPIRVName(resource.name);
                return resourceInfo;
            }

            /** Publishes descriptor decorations and transfers metadata into the resource table. */
            void registerResourceVariable(SPIRVResourceInfo resourceInfo)
            {
                const UGLIR::ResourceBinding &resource = *resourceInfo.binding;
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    resourceInfo.variableId,
                    static_cast<uint32_t>(spv::DecorationDescriptorSet), (mModule.reflection.arguments.enabled && resource.kind != UGLIR::ResourceKind::InputAttachment) ? 0u : resource.bindGroupIndex,
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    resourceInfo.variableId,
                    static_cast<uint32_t>(spv::DecorationBinding), (mModule.reflection.arguments.enabled && resource.kind != UGLIR::ResourceKind::InputAttachment) ? resource.argumentTableBinding : resource.bindingIndex,
                });
                mDebugNames.appendInstructionWithString(
                    spv::OpName, {resourceInfo.variableId}, resourceInfo.variableName);
                mResourcesByUGLIRName.emplace(resource.name, std::move(resourceInfo));
            }

            /** Builds a texture, sampler or input-attachment descriptor without a buffer layout. */
            void buildOpaqueResource(const UGLIR::ResourceBinding &resource)
            {
                SPIRVResourceInfo resourceInfo = makeResourceInfo(resource);
                resourceInfo.objectTypeId = resource.kind == UGLIR::ResourceKind::Sampler
                    ? getSamplerTypeId() : getImageTypeId(resource);
                uint32_t variableTypeId = resourceInfo.objectTypeId;
                if ((mModule.reflection.arguments.enabled && resource.kind != UGLIR::ResourceKind::InputAttachment) ||
                    (resource.kind == UGLIR::ResourceKind::Texture && resource.arrayCount > 1u))
                {
                    variableTypeId = allocateId();
                    mCapabilityTracker.ensureRuntimeDescriptorArrayCapability();
                    mTypesConstantsGlobals.appendInstruction(spv::OpTypeRuntimeArray, {
                        variableTypeId, resourceInfo.objectTypeId,
                    });
                    resourceInfo.isObjectArray = true;
                    resourceInfo.elementPointerTypeId = getPointerTypeId(
                        resourceInfo.objectTypeId, spv::StorageClassUniformConstant);
                }
                const uint32_t pointerTypeId = getPointerTypeId(
                    variableTypeId, spv::StorageClassUniformConstant);
                resourceInfo.variableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId, resourceInfo.variableId,
                    static_cast<uint32_t>(spv::StorageClassUniformConstant),
                });
                if (resource.kind == UGLIR::ResourceKind::InputAttachment)
                {
                    mCapabilityTracker.ensureInputAttachmentCapability();
                    mAnnotations.appendInstruction(spv::OpDecorate, {
                        resourceInfo.variableId,
                        static_cast<uint32_t>(spv::DecorationInputAttachmentIndex),
                        resource.inputAttachmentIndex,
                    });
                }
                registerResourceVariable(std::move(resourceInfo));
            }

            /** Returns the reusable Uniform block wrapper around an explicit-layout element. */
            uint32_t getUniformBlockTypeId(const UGLIR::ResourceBinding &resource,
                                           uint32_t elementTypeId)
            {
                const auto existing = mUniformBlockTypeIdsByElementType.find(elementTypeId);
                if (existing != mUniformBlockTypeIdsByElementType.end())
                {
                    return existing->second;
                }
                const uint32_t blockTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, {
                    blockTypeId, elementTypeId,
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {blockTypeId},
                    "UniformBlock_" + sanitizeSPIRVName(resource.elementType));
                // A wrapper contains exactly one member starting at byte zero.
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                    blockTypeId, 0u, static_cast<uint32_t>(spv::DecorationOffset), 0u,
                });
                decorateMatrixBlockMemberLayoutIfNeeded(blockTypeId, 0u, resource.elementType);
                decorateBlockTypeOnce(blockTypeId);
                mUniformBlockTypeIdsByElementType.emplace(elementTypeId, blockTypeId);
                return blockTypeId;
            }

            /** Builds a Uniform resource with the original host ABI and a separate value type. */
            void buildUniformResource(const UGLIR::ResourceBinding &resource)
            {
                if (findType(resource.elementType) == nullptr)
                {
                    addDiagnostic(resource.sourceLocation,
                        "UniformBuffer direct SPIR-V emission requires a known element type.");
                    return;
                }
                SPIRVResourceInfo resourceInfo = makeResourceInfo(resource);
                resourceInfo.elementTypeId = getStorageTypeId(
                    resource.elementType, resource.sourceLocation);
                resourceInfo.blockTypeId = getUniformBlockTypeId(resource, resourceInfo.elementTypeId);
                resourceInfo.elementPointerTypeId = getPointerTypeId(
                    resourceInfo.elementTypeId, spv::StorageClassUniform);
                const uint32_t blockPointerTypeId = getPointerTypeId(
                    resourceInfo.blockTypeId, spv::StorageClassUniform);
                resourceInfo.variableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    blockPointerTypeId, resourceInfo.variableId,
                    static_cast<uint32_t>(spv::StorageClassUniform),
                });
                registerResourceVariable(std::move(resourceInfo));
            }

            /** Builds a runtime-array buffer using only reflected strides and member offsets. */
            void buildStorageResource(const UGLIR::ResourceBinding &resource)
            {
                SPIRVResourceInfo resourceInfo = makeResourceInfo(resource);
                resourceInfo.elementTypeId = getStorageTypeId(
                    resource.elementType, resource.sourceLocation);
                const uint32_t runtimeArrayTypeId = allocateId();
                resourceInfo.blockTypeId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeRuntimeArray, {
                    runtimeArrayTypeId, resourceInfo.elementTypeId,
                });
                mTypesConstantsGlobals.appendInstruction(spv::OpTypeStruct, {
                    resourceInfo.blockTypeId, runtimeArrayTypeId,
                });
                const uint32_t blockPointerTypeId = getPointerTypeId(
                    resourceInfo.blockTypeId, spv::StorageClassStorageBuffer);
                resourceInfo.elementPointerTypeId = getPointerTypeId(
                    resourceInfo.elementTypeId, spv::StorageClassStorageBuffer);
                resourceInfo.variableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    blockPointerTypeId, resourceInfo.variableId,
                    static_cast<uint32_t>(spv::StorageClassStorageBuffer),
                });
                decorateStorageResource(resourceInfo, runtimeArrayTypeId);
                registerResourceVariable(std::move(resourceInfo));
            }

            /** Decorates the buffer wrapper while leaving all computation types undecorated. */
            void decorateStorageResource(const SPIRVResourceInfo &resourceInfo,
                                          uint32_t runtimeArrayTypeId)
            {
                const UGLIR::ResourceBinding &resource = *resourceInfo.binding;
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    runtimeArrayTypeId, static_cast<uint32_t>(spv::DecorationArrayStride),
                    getStorageLayoutByteSize(resource.elementType),
                });
                // The runtime array is the wrapper's sole member, at byte zero.
                mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                    resourceInfo.blockTypeId, 0u,
                    static_cast<uint32_t>(spv::DecorationOffset), 0u,
                });
                decorateMatrixBlockMemberLayoutIfNeeded(
                    resourceInfo.blockTypeId, 0u, resource.elementType);
                decorateBlockTypeOnce(resourceInfo.blockTypeId);
                if (resource.effectiveAccessMode() == UGLIR::AccessMode::Read)
                {
                    mAnnotations.appendInstruction(spv::OpMemberDecorate, {
                        resourceInfo.blockTypeId, 0u,
                        static_cast<uint32_t>(spv::DecorationNonWritable),
                    });
                }
            }

            /** Builds the SPIR-V entry point, execution modes, and stage interface variables. */
            void buildEntryInterface(const UGLIR::Function &entryFunction)
            {
                if (entryFunction.stage == UGLIR::ShaderStage::Compute)
                {
                    buildComputeEntryInterface(entryFunction);
                    return;
                }
                buildRasterStageInterfaceVariables();
                if (entryFunction.stage == UGLIR::ShaderStage::Fragment)
                {
                    buildFragmentExecutionModes();
                }
            }

            /** Declares one compute entry builtin, preserving the existing semantic contract. */
            void declareComputeEntryBuiltin(const UGLIR::FunctionParameter &parameter)
            {
                switch (parameter.semanticKind)
                {
                case UGLIR::BuiltinSemanticKind::DispatchThreadID:
                    getGlobalInvocationIdVariableId(parameter);
                    return;
                case UGLIR::BuiltinSemanticKind::GroupThreadID:
                    getLocalInvocationIdVariableId(parameter);
                    return;
                case UGLIR::BuiltinSemanticKind::GroupID:
                    getWorkgroupIdVariableId(parameter);
                    return;
                case UGLIR::BuiltinSemanticKind::GroupIndex:
                    getLocalInvocationIndexVariableId(parameter);
                    return;
                case UGLIR::BuiltinSemanticKind::None:
                    return;
                default:
                    addDiagnostic(parameter.sourceLocation,
                        "unsupported compute entry semantic \"" +
                        UGLIR::semanticDisplayName(parameter.semanticKind, parameter.semanticIndex) +
                        "\" for direct SPIR-V emission.");
                    return;
                }
            }

            /** Declares compute inputs and LocalSize before emitting their function uses. */
            void buildComputeEntryInterface(const UGLIR::Function &entryFunction)
            {
                for (const UGLIR::FunctionParameter &parameter : entryFunction.parameters)
                {
                    declareComputeEntryBuiltin(parameter);
                }
                if (moduleUsesWaveIntrinsics())
                {
                    getSubgroupLocalInvocationIdVariableId(mModule.sourceLocation);
                    getSubgroupSizeVariableId(mModule.sourceLocation);
                }
                mExecutionModes.appendInstruction(spv::OpExecutionMode, {
                    mEntryPointFunctionId,
                    static_cast<uint32_t>(spv::ExecutionModeLocalSize),
                    entryFunction.workgroupSize[0],
                    entryFunction.workgroupSize[1],
                    entryFunction.workgroupSize[2],
                });
            }

            /** Preserves fragment origin and explicit-depth execution modes. */
            void buildFragmentExecutionModes()
            {
                mExecutionModes.appendInstruction(spv::OpExecutionMode, {
                    mEntryPointFunctionId,
                    static_cast<uint32_t>(spv::ExecutionModeOriginUpperLeft),
                });
                for (const UGLIR::StageIOBinding &binding : mModule.reflection.stageOutputs)
                {
                    if (binding.semanticKind == UGLIR::BuiltinSemanticKind::Depth)
                    {
                        mExecutionModes.appendInstruction(spv::OpExecutionMode, {
                            mEntryPointFunctionId,
                            static_cast<uint32_t>(spv::ExecutionModeDepthReplacing),
                        });
                        return;
                    }
                }
            }

            /**
             * Collects each declared global exactly once for the SPIR-V 1.4 interface.
             * The specification permits this superset of the entry's static call tree.
             * Function-local OpVariable instructions live in function bodies, not this section.
             */
            std::vector<uint32_t> collectEntryPointGlobalVariables() const
            {
                // SPIR-V binary instructions encode word count in the high 16 bits and
                // opcode in the low 16 bits; OpVariable's result id is its second operand.
                constexpr uint32_t instructionWordCountShift = 16u;
                constexpr uint32_t instructionOpcodeMask = 0xffffu;
                constexpr size_t variableResultIdWordOffset = 2u;
                std::vector<uint32_t> globalVariables;
                const std::vector<uint32_t> &words = mTypesConstantsGlobals.words();
                size_t wordOffset = 0;
                while (wordOffset < words.size())
                {
                    const uint32_t instructionHeader = words[wordOffset];
                    const uint32_t opcode = instructionHeader & instructionOpcodeMask;
                    if (opcode == static_cast<uint32_t>(spv::OpVariable))
                    {
                        globalVariables.push_back(words[wordOffset + variableResultIdWordOffset]);
                    }
                    wordOffset += instructionHeader >> instructionWordCountShift;
                }
                return globalVariables;
            }

            /** Emits the entry only after late-declared Workgroup and Private globals are known. */
            void emitEntryPoint(const UGLIR::Function &entryFunction)
            {
                mEntryPointInterfaceIds = collectEntryPointGlobalVariables();
                mEntryPoints.appendInstructionWithString(spv::OpEntryPoint, {
                    static_cast<uint32_t>(executionModelForStage(entryFunction.stage)),
                    mEntryPointFunctionId,
                }, entryPointNameForEntryKind(entryFunction.entryKind), mEntryPointInterfaceIds);
            }

            /** Builds declared SPIR-V input and output variables for vertex/fragment stages. */
            void buildRasterStageInterfaceVariables()
            {
                for (const UGLIR::StageIOBinding &binding : mModule.reflection.stageInputs)
                {
                    addStageIOVariable(binding, spv::StorageClassInput);
                }
                if (moduleUsesDrawInfoBuiltins())
                {
                    ensureInstanceIdInputVariable(mModule.sourceLocation);
                }
                for (const UGLIR::StageIOBinding &binding : mModule.reflection.stageOutputs)
                {
                    addStageIOVariable(binding, spv::StorageClassOutput);
                }
            }

            /** Returns true when the entry ABI needs DrawEntityID or DrawEntityInstanceID decoding. */
            bool moduleUsesDrawInfoBuiltins() const
            {
                for (const UGLIR::StageIOBinding &binding : mModule.reflection.stageInputs)
                {
                    if (binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                        binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID)
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when any reachable UGLIR expression uses a wave/subgroup intrinsic. */
            bool moduleUsesWaveIntrinsics() const
            {
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (statementListUsesWaveIntrinsics(function.body))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when a statement list contains a wave/subgroup intrinsic call. */
            bool statementListUsesWaveIntrinsics(const std::vector<UGLIR::Statement> &statements) const
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        if (expressionUsesWaveIntrinsics(expression))
                        {
                            return true;
                        }
                    }
                    if (statementListUsesWaveIntrinsics(statement.children) ||
                        statementListUsesWaveIntrinsics(statement.elseChildren))
                    {
                        return true;
                    }
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        if (statementListUsesWaveIntrinsics(switchCase.body))
                        {
                            return true;
                        }
                    }
                }
                return false;
            }

            /** Returns true when an expression tree contains a wave/subgroup intrinsic call. */
            bool expressionUsesWaveIntrinsics(const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Call && isWaveIntrinsicCallKind(expression.intrinsicCallKind))
                {
                    return true;
                }
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (expressionUsesWaveIntrinsics(operand))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Ensures a vertex InstanceIndex input exists for DrawInfo command-parameter decoding. */
            uint32_t ensureInstanceIdInputVariable(const UGLIR::SourceLocation &)
            {
                if (mInstanceIdVariableId != 0)
                {
                    return mInstanceIdVariableId;
                }
                for (const SPIRVStageIOInfo &stageIO : mStageIOVariables)
                {
                    if (stageIO.storageClass == spv::StorageClassInput &&
                        stageIO.binding != nullptr &&
                        stageIO.binding->semanticKind == UGLIR::BuiltinSemanticKind::InstanceID)
                    {
                        mInstanceIdVariableId = stageIO.variableId;
                        return mInstanceIdVariableId;
                    }
                }
                const uint32_t pointerTypeId = getPointerTypeId(getUIntTypeId(), spv::StorageClassInput);
                mInstanceIdVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mInstanceIdVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mInstanceIdVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInInstanceIndex),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mInstanceIdVariableId}, "gl_InstanceIndex");
                mEntryPointInterfaceIds.push_back(mInstanceIdVariableId);
                return mInstanceIdVariableId;
            }

            /** Adds one SPIR-V stage input or output variable from UGLIR reflection. */
            void addStageIOVariable(const UGLIR::StageIOBinding &binding, spv::StorageClass storageClass)
            {
                if (binding.semanticKind == UGLIR::BuiltinSemanticKind::StageInput ||
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalInput ||
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalDepth ||
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID ||
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::VertexInput)
                {
                    return;
                }
                if (storageClass == spv::StorageClassInput &&
                    binding.semanticKind == UGLIR::BuiltinSemanticKind::Depth)
                {
                    return;
                }
                if (storageClass == spv::StorageClassInput &&
                    mModule.reflection.entryKind == UGLIR::ShaderEntryKind::PixelLocal &&
                    (binding.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalColor ||
                     binding.semanticKind == UGLIR::BuiltinSemanticKind::Color))
                {
                    return;
                }
                const std::string valueTypeName = stageIOABITypeName(binding, storageClass);
                if (valueTypeContainsHalf(valueTypeName))
                {
                    mCapabilityTracker.ensureStorageInputOutput16Capability();
                }
                const uint32_t typeId = getTypeId(valueTypeName, binding.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, storageClass);
                const uint32_t variableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    variableId,
                    static_cast<uint32_t>(storageClass),
                });
                decorateStageIOVariable(binding, storageClass, variableId);
                mDebugNames.appendInstructionWithString(spv::OpName, {variableId}, sanitizeSPIRVName(binding.name));
                mEntryPointInterfaceIds.push_back(variableId);
                mStageIOVariables.push_back(SPIRVStageIOInfo{
                    .binding = &binding,
                    .variableId = variableId,
                    .pointerTypeId = pointerTypeId,
                    .storageClass = storageClass,
                    .valueTypeName = valueTypeName,
                });
            }

            /** Adds SPIR-V BuiltIn or Location decorations for one stage IO variable. */
            void decorateStageIOVariable(const UGLIR::StageIOBinding &binding, spv::StorageClass storageClass, uint32_t variableId)
            {
                switch (binding.semanticKind)
                {
                case UGLIR::BuiltinSemanticKind::VertexID:
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInVertexIndex)});
                    return;
                case UGLIR::BuiltinSemanticKind::InstanceID:
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInInstanceIndex)});
                    return;
                case UGLIR::BuiltinSemanticKind::PixelCoord:
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInFragCoord)});
                    return;
                case UGLIR::BuiltinSemanticKind::SampleIndex:
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInSampleId)});
                    return;
                case UGLIR::BuiltinSemanticKind::PrimitiveID:
                    mCapabilityTracker.ensureFragmentPrimitiveIdCapability();
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInPrimitiveId)});
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationFlat)});
                    return;
                case UGLIR::BuiltinSemanticKind::Barycentrics:
                    mCapabilityTracker.ensureFragmentBarycentricCapability();
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInBaryCoordKHR)});
                    return;
                case UGLIR::BuiltinSemanticKind::Position:
                {
                    const spv::BuiltIn builtIn = storageClass == spv::StorageClassInput && mModule.reflection.stage == UGLIR::ShaderStage::Fragment
                                                     ? spv::BuiltInFragCoord
                                                     : spv::BuiltInPosition;
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(builtIn)});
                    return;
                }
                case UGLIR::BuiltinSemanticKind::ClipDistance:
                    mCapabilityTracker.ensureClipDistanceCapability();
                    mAnnotations.appendInstruction(spv::OpDecorate,
                        {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn),
                         static_cast<uint32_t>(spv::BuiltInClipDistance)});
                    return;
                case UGLIR::BuiltinSemanticKind::Depth:
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationBuiltIn), static_cast<uint32_t>(spv::BuiltInFragDepth)});
                    return;
                default:
                    break;
                }
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    variableId,
                    static_cast<uint32_t>(spv::DecorationLocation),
                    binding.location,
                });
                const std::string valueTypeName = stageIOABITypeName(binding, storageClass);
                const bool canUseFlatInterpolation = (mModule.reflection.stage == UGLIR::ShaderStage::Fragment &&
                                                       storageClass == spv::StorageClassInput) ||
                                                      (mModule.reflection.stage == UGLIR::ShaderStage::Vertex &&
                                                       storageClass == spv::StorageClassOutput);
                if (canUseFlatInterpolation &&
                    (isSignedIntegerOrVectorTypeName(valueTypeName) ||
                     isUnsignedIntegerOrVectorTypeName(valueTypeName) ||
                     isBooleanScalarOrVectorValue(valueTypeName)))
                {
                    mAnnotations.appendInstruction(spv::OpDecorate, {variableId, static_cast<uint32_t>(spv::DecorationFlat)});
                }
            }

            /** Returns the SPIR-V input variable for the GlobalInvocationId builtin. */
            uint32_t getGlobalInvocationIdVariableId(const UGLIR::FunctionParameter &parameter)
            {
                if (mGlobalInvocationIdVariableId != 0)
                {
                    return mGlobalInvocationIdVariableId;
                }
                const uint32_t typeId = getTypeId(parameter.type, parameter.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassInput);
                mGlobalInvocationIdVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mGlobalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mGlobalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInGlobalInvocationId),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mGlobalInvocationIdVariableId}, "gl_GlobalInvocationId");
                return mGlobalInvocationIdVariableId;
            }

            /** Returns the SPIR-V input variable for the LocalInvocationId builtin. */
            uint32_t getLocalInvocationIdVariableId(const UGLIR::FunctionParameter &parameter)
            {
                if (mLocalInvocationIdVariableId != 0)
                {
                    return mLocalInvocationIdVariableId;
                }
                const uint32_t typeId = getTypeId(parameter.type, parameter.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassInput);
                mLocalInvocationIdVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mLocalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mLocalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInLocalInvocationId),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mLocalInvocationIdVariableId}, "gl_LocalInvocationId");
                return mLocalInvocationIdVariableId;
            }

            /** Returns the SPIR-V input variable for the WorkgroupId builtin. */
            uint32_t getWorkgroupIdVariableId(const UGLIR::FunctionParameter &parameter)
            {
                if (mWorkgroupIdVariableId != 0)
                {
                    return mWorkgroupIdVariableId;
                }
                const uint32_t typeId = getTypeId(parameter.type, parameter.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassInput);
                mWorkgroupIdVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mWorkgroupIdVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mWorkgroupIdVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInWorkgroupId),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mWorkgroupIdVariableId}, "gl_WorkgroupId");
                return mWorkgroupIdVariableId;
            }

            /** Returns the SPIR-V input variable for the LocalInvocationIndex builtin. */
            uint32_t getLocalInvocationIndexVariableId(const UGLIR::FunctionParameter &parameter)
            {
                (void)parameter;
                if (mLocalInvocationIndexVariableId != 0)
                {
                    return mLocalInvocationIndexVariableId;
                }
                const uint32_t pointerTypeId = getPointerTypeId(getUIntTypeId(), spv::StorageClassInput);
                mLocalInvocationIndexVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mLocalInvocationIndexVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mLocalInvocationIndexVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInLocalInvocationIndex),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mLocalInvocationIndexVariableId}, "gl_LocalInvocationIndex");
                return mLocalInvocationIndexVariableId;
            }

            /** Returns the SPIR-V input variable for the SubgroupLocalInvocationId builtin. */
            uint32_t getSubgroupLocalInvocationIdVariableId(const UGLIR::SourceLocation &location)
            {
                (void)location;
                mCapabilityTracker.ensureGroupNonUniformCapability();
                if (mSubgroupLocalInvocationIdVariableId != 0)
                {
                    return mSubgroupLocalInvocationIdVariableId;
                }
                const uint32_t pointerTypeId = getPointerTypeId(getUIntTypeId(), spv::StorageClassInput);
                mSubgroupLocalInvocationIdVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mSubgroupLocalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mSubgroupLocalInvocationIdVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInSubgroupLocalInvocationId),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mSubgroupLocalInvocationIdVariableId}, "gl_SubgroupLocalInvocationId");
                return mSubgroupLocalInvocationIdVariableId;
            }

            /** Returns the SPIR-V input variable for the SubgroupSize builtin. */
            uint32_t getSubgroupSizeVariableId(const UGLIR::SourceLocation &location)
            {
                (void)location;
                mCapabilityTracker.ensureGroupNonUniformCapability();
                if (mSubgroupSizeVariableId != 0)
                {
                    return mSubgroupSizeVariableId;
                }
                const uint32_t pointerTypeId = getPointerTypeId(getUIntTypeId(), spv::StorageClassInput);
                mSubgroupSizeVariableId = allocateId();
                mTypesConstantsGlobals.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    mSubgroupSizeVariableId,
                    static_cast<uint32_t>(spv::StorageClassInput),
                });
                mAnnotations.appendInstruction(spv::OpDecorate, {
                    mSubgroupSizeVariableId,
                    static_cast<uint32_t>(spv::DecorationBuiltIn),
                    static_cast<uint32_t>(spv::BuiltInSubgroupSize),
                });
                mDebugNames.appendInstructionWithString(spv::OpName, {mSubgroupSizeVariableId}, "gl_SubgroupSize");
                return mSubgroupSizeVariableId;
            }

            /** Emits helper functions before the entry point so call targets are easy to inspect in disassembly. */
            void emitFunctions()
            {
                std::unordered_set<std::string> emittedFunctions;
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (!function.isEntryPoint && emittedFunctions.insert(function.name).second)
                    {
                        emitFunction(function);
                    }
                }
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (function.isEntryPoint && emittedFunctions.insert(function.name).second)
                    {
                        emitFunction(function);
                    }
                }
            }

            /** Emits one SPIR-V function definition. */
            void emitFunction(const UGLIR::Function &function)
            {
                const auto iter = mFunctionsByName.find(function.name);
                if (iter == mFunctionsByName.end())
                {
                    addDiagnostic(function.sourceLocation, "internal error: missing SPIR-V function signature for \"" + function.name + "\".");
                    return;
                }

                SPIRVFunctionContext context;
                context.functionInfo = &iter->second;
                const spv::FunctionControlMask functionControl =
                    function.isEntryPoint ? spv::FunctionControlMaskNone : spv::FunctionControlInlineMask;
                context.body.appendInstruction(spv::OpFunction, {
                    iter->second.returnTypeId,
                    iter->second.functionId,
                    static_cast<uint32_t>(functionControl),
                    iter->second.functionTypeId,
                });

                if (!function.isEntryPoint)
                {
                    size_t emittedParameterIndex = 0;
                    for (size_t index = 0; index < function.parameters.size(); ++index)
                    {
                        const UGLIR::FunctionParameter &parameter = function.parameters[index];
                        if (isResourceAliasType(parameter.type))
                        {
                            continue;
                        }
                        const uint32_t parameterId = allocateId();
                        context.body.appendInstruction(spv::OpFunctionParameter, {
                            iter->second.parameterTypeIds[emittedParameterIndex++],
                            parameterId,
                        });
                        const bool parameterIsPointer = iter->second.parameterIsPointer[emittedParameterIndex - 1u];
                        if (parameterIsPointer)
                        {
                            context.localPointers.emplace(parameter.name,
                                                          makePointerValue(parameterId,
                                                                           parameter.type,
                                                                           spv::StorageClassFunction,
                                                                           parameter.sourceLocation));
                        }
                        else
                        {
                            if (functionWritesParameter(function, parameter.name, true))
                            {
                                const uint32_t valueTypeId = getTypeId(parameter.type, parameter.sourceLocation);
                                const uint32_t pointerTypeId = getPointerTypeId(valueTypeId, spv::StorageClassFunction);
                                const uint32_t pointerId = allocateId();
                                context.parameterLocalCopies.push_back(SPIRVParameterLocalCopy{
                                    .pointerTypeId = pointerTypeId,
                                    .pointerId = pointerId,
                                    .parameterId = parameterId,
                                });
                                context.localPointers.emplace(parameter.name,
                                                              makePointerValue(pointerId,
                                                                               parameter.type,
                                                                               spv::StorageClassFunction,
                                                                               parameter.sourceLocation));
                                mDebugNames.appendInstructionWithString(spv::OpName, {pointerId}, sanitizeSPIRVName(parameter.name));
                            }
                            else
                            {
                                context.parameterValues.emplace(parameter.name,
                                                                makeValue(parameterId,
                                                                          parameter.type,
                                                                          parameter.sourceLocation));
                            }
                        }
                        mDebugNames.appendInstructionWithString(spv::OpName, {parameterId}, sanitizeSPIRVName(parameter.name));
                    }
                }

                beginBlock(context, allocateId());
                declareFunctionParameterLocalCopies(context);
                declareEntryParameterLocals(context, function);
                declareLocalVariables(context, function.body);
                declareConstantArrayTemporaries(context, function.body);
                declarePointerArgumentTemporaries(context, function.body);
                if (mModule.reflection.arguments.enabled)
                {
                    emitArgumentRootLoad(context);
                }
                emitFunctionParameterLocalCopyStores(context);
                emitEntryParameterMaterialization(context, function);
                emitStatementList(context, function.body, 0u);
                if (!context.blockTerminated)
                {
                    if (isVoidTypeName(function.returnType))
                    {
                        context.body.appendInstruction(spv::OpReturn, {});
                    }
                    else
                    {
                        addDiagnostic(function.sourceLocation, "non-void function \"" + function.name + "\" can fall through without a return value.");
                        context.body.appendInstruction(spv::OpReturnValue, {getDefaultValueId(function.returnType, function.sourceLocation)});
                    }
                    context.blockTerminated = true;
                }
                context.body.appendInstruction(spv::OpFunctionEnd, {});
                mFunctions.appendBuilder(context.body);
            }

            /** Starts a new SPIR-V basic block in a function body. */
            void beginBlock(SPIRVFunctionContext &context, uint32_t labelId)
            {
                context.currentBlockId = labelId;
                context.blockTerminated = false;
                context.body.appendInstruction(spv::OpLabel, {labelId});
            }

            /** Declares mutable local variables that back by-value function parameters. */
            void declareFunctionParameterLocalCopies(SPIRVFunctionContext &context)
            {
                for (const SPIRVParameterLocalCopy &parameterCopy : context.parameterLocalCopies)
                {
                    context.body.appendInstruction(spv::OpVariable, {
                        parameterCopy.pointerTypeId,
                        parameterCopy.pointerId,
                        static_cast<uint32_t>(spv::StorageClassFunction),
                    });
                }
            }

            /** Returns a stable function-local key for a constant array expression that must be materialized before indexing. */
            std::string makeConstantArrayTemporaryKey(const UGLIR::Expression &expression) const
            {
                std::ostringstream stream;
                stream << expression.sourceLocation.file << ':' << expression.sourceLocation.line << ':' << expression.sourceLocation.column << ':' << expression.type;
                return stream.str();
            }

            /** Returns a stable function-local key for a helper pointer argument temporary. */
            std::string makePointerArgumentTemporaryKey(const std::string &calleeName,
                                                        size_t emittedArgumentIndex,
                                                        const UGLIR::Expression &argument,
                                                        const std::string &targetType) const
            {
                std::ostringstream stream;
                stream << calleeName << ':' << emittedArgumentIndex << ':'
                       << argument.sourceLocation.file << ':' << argument.sourceLocation.line << ':' << argument.sourceLocation.column << ':'
                       << targetType;
                return stream.str();
            }

            /** Declares one function-local temporary array used to index a constructed constant array value. */
            void declareConstantArrayTemporary(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                const std::string key = makeConstantArrayTemporaryKey(expression);
                if (context.constantArrayPointers.contains(key))
                {
                    return;
                }
                const uint32_t typeId = getTypeId(expression.type, expression.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassFunction);
                const uint32_t pointerId = allocateId();
                context.body.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    pointerId,
                    static_cast<uint32_t>(spv::StorageClassFunction),
                });
                context.constantArrayPointers.emplace(key,
                                                      makePointerValue(pointerId,
                                                                       expression.type,
                                                                       spv::StorageClassFunction,
                                                                       expression.sourceLocation));
                mDebugNames.appendInstructionWithString(spv::OpName, {pointerId}, "__uglir_constant_array");
            }

            /** Scans one expression tree and declares temporaries for constructed arrays that are indexed at runtime. */
            void declareConstantArrayTemporariesInExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.kind == UGLIR::ExpressionKind::Subscript && !expression.operands.empty())
                {
                    const UGLIR::Expression &baseExpression = expression.operands.front();
                    const UGLIR::Type *baseType = findType(baseExpression.type);
                    if (baseType == nullptr)
                    {
                        baseType = findType(valueTypeKeyForName(baseExpression.type));
                    }
                    if (baseExpression.kind == UGLIR::ExpressionKind::Construct &&
                        baseType != nullptr &&
                        baseType->kind == UGLIR::TypeKind::Array)
                    {
                        declareConstantArrayTemporary(context, baseExpression);
                    }
                }
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    declareConstantArrayTemporariesInExpression(context, operand);
                }
            }

            /** Declares all constructed-array temporaries required by a statement subtree before ordinary instructions. */
            void declareConstantArrayTemporaries(SPIRVFunctionContext &context, const std::vector<UGLIR::Statement> &statements)
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        declareConstantArrayTemporariesInExpression(context, expression);
                    }
                    declareConstantArrayTemporaries(context, statement.children);
                    declareConstantArrayTemporaries(context, statement.elseChildren);
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        declareConstantArrayTemporaries(context, switchCase.body);
                    }
                }
            }

            /** Declares one function-local temporary used when a pointer helper argument cannot receive a subobject pointer directly. */
            void declarePointerArgumentTemporary(SPIRVFunctionContext &context,
                                                 const std::string &key,
                                                 const std::string &targetType,
                                                 const UGLIR::SourceLocation &location)
            {
                if (context.pointerArgumentTemporaries.contains(key))
                {
                    return;
                }
                const uint32_t typeId = getTypeId(targetType, location);
                const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassFunction);
                const uint32_t pointerId = allocateId();
                context.body.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    pointerId,
                    static_cast<uint32_t>(spv::StorageClassFunction),
                });
                context.pointerArgumentTemporaries.emplace(key,
                                                           makePointerValue(pointerId,
                                                                            targetType,
                                                                            spv::StorageClassFunction,
                                                                            location));
                mDebugNames.appendInstructionWithString(spv::OpName, {pointerId}, "__uglir_pointer_arg");
            }

            /** Scans one call expression and declares temporaries for pointer parameters that may receive subobject arguments. */
            void declarePointerArgumentTemporariesForCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                const auto iter = mFunctionsByName.find(expression.name);
                if (iter == mFunctionsByName.end() || iter->second.function == nullptr)
                {
                    return;
                }

                size_t emittedArgumentIndex = 0;
                size_t parameterIndex = 0;
                for (const UGLIR::Expression &argument : expression.operands)
                {
                    const UGLIR::FunctionParameter *targetParameter =
                        parameterIndex < iter->second.function->parameters.size()
                            ? &iter->second.function->parameters[parameterIndex]
                            : nullptr;
                    ++parameterIndex;
                    if (isResourceAliasType(argument.type))
                    {
                        continue;
                    }
                    const bool argumentIsPointer = emittedArgumentIndex < iter->second.parameterIsPointer.size() &&
                                                   iter->second.parameterIsPointer[emittedArgumentIndex];
                    if (argumentIsPointer && targetParameter != nullptr)
                    {
                        const std::string key = makePointerArgumentTemporaryKey(expression.name,
                                                                                emittedArgumentIndex,
                                                                                argument,
                                                                                targetParameter->type);
                        declarePointerArgumentTemporary(context, key, targetParameter->type, argument.sourceLocation);
                    }
                    ++emittedArgumentIndex;
                }
            }

            /** Scans one expression tree and declares pointer argument temporaries before ordinary instructions are emitted. */
            void declarePointerArgumentTemporariesInExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.kind == UGLIR::ExpressionKind::Call)
                {
                    declarePointerArgumentTemporariesForCall(context, expression);
                }
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    declarePointerArgumentTemporariesInExpression(context, operand);
                }
            }

            /** Declares all helper pointer argument temporaries required by a statement subtree. */
            void declarePointerArgumentTemporaries(SPIRVFunctionContext &context, const std::vector<UGLIR::Statement> &statements)
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        declarePointerArgumentTemporariesInExpression(context, expression);
                    }
                    declarePointerArgumentTemporaries(context, statement.children);
                    declarePointerArgumentTemporaries(context, statement.elseChildren);
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        declarePointerArgumentTemporaries(context, switchCase.body);
                    }
                }
            }

            /** Copies immutable SPIR-V function parameters into the mutable locals used by C++ parameter assignments. */
            void emitFunctionParameterLocalCopyStores(SPIRVFunctionContext &context)
            {
                for (const SPIRVParameterLocalCopy &parameterCopy : context.parameterLocalCopies)
                {
                    context.body.appendInstruction(spv::OpStore, {
                        parameterCopy.pointerId,
                        parameterCopy.parameterId,
                    });
                }
            }

            /** Emits an unconditional branch when the current block is not already terminated. */
            void branchTo(SPIRVFunctionContext &context, uint32_t labelId)
            {
                if (!context.blockTerminated)
                {
                    context.body.appendInstruction(spv::OpBranch, {labelId});
                    context.blockTerminated = true;
                }
            }

            /** Declares the selector local used by a resource alias that may be assigned in runtime control flow. */
            void declareResourceAliasSelector(SPIRVFunctionContext &context, const std::string &aliasName)
            {
                SPIRVResourceAliasState &state = context.resourceAliasStates[aliasName];
                if (state.selectorPointerId != 0)
                {
                    return;
                }
                const uint32_t pointerTypeId = getPointerTypeId(getUIntTypeId(), spv::StorageClassFunction);
                state.selectorPointerId = allocateId();
                context.body.appendInstruction(spv::OpVariable, {
                    pointerTypeId,
                    state.selectorPointerId,
                    static_cast<uint32_t>(spv::StorageClassFunction),
                    getUIntConstant(0u),
                });
                mDebugNames.appendInstructionWithString(spv::OpName,
                                                        {state.selectorPointerId},
                                                        "__uglir_resource_alias_" + sanitizeSPIRVName(aliasName) + "_selector");
            }

            /** Records one resource assignment into a local alias and emits the selector store for that assignment. */
            void assignResourceAlias(SPIRVFunctionContext &context,
                                     const std::string &aliasName,
                                     const SPIRVResourceInfo &resource,
                                     const UGLIR::SourceLocation &location)
            {
                auto stateIter = context.resourceAliasStates.find(aliasName);
                if (stateIter == context.resourceAliasStates.end() || stateIter->second.selectorPointerId == 0)
                {
                    addDiagnostic(location, "resource alias \"" + aliasName + "\" was assigned before its selector was declared.");
                    return;
                }

                SPIRVResourceAliasState &state = stateIter->second;
                uint32_t selector = 0;
                bool foundExistingResource = false;
                for (uint32_t index = 0; index < state.resources.size(); ++index)
                {
                    if (state.resources[index] == &resource)
                    {
                        selector = index;
                        foundExistingResource = true;
                        break;
                    }
                }
                if (!foundExistingResource)
                {
                    selector = static_cast<uint32_t>(state.resources.size());
                    state.resources.push_back(&resource);
                }

                context.resourceAliases[aliasName] = &resource;
                context.resourceObjectAliases.erase(aliasName);
                context.body.appendInstruction(spv::OpStore, {state.selectorPointerId, getUIntConstant(selector)});
            }

            /** Declares function-scope locals that represent shader entry parameters. */
            void declareEntryParameterLocals(SPIRVFunctionContext &context, const UGLIR::Function &function)
            {
                if (!function.isEntryPoint)
                {
                    return;
                }
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (isResourceAliasType(parameter.type))
                    {
                        continue;
                    }
                    const uint32_t typeId = getTypeId(parameter.type, parameter.sourceLocation);
                    const uint32_t pointerTypeId = getPointerTypeId(typeId, spv::StorageClassFunction);
                    const uint32_t pointerId = allocateId();
                    context.body.appendInstruction(spv::OpVariable, {
                        pointerTypeId,
                        pointerId,
                        static_cast<uint32_t>(spv::StorageClassFunction),
                    });
                    context.localPointers.emplace(parameter.name,
                                                  makePointerValue(pointerId,
                                                                   parameter.type,
                                                                   spv::StorageClassFunction,
                                                                   parameter.sourceLocation));
                    mDebugNames.appendInstructionWithString(spv::OpName, {pointerId}, sanitizeSPIRVName(parameter.name));
                }
            }

            /** Declares every local variable in a function before ordinary instructions as required by SPIR-V. */
            void declareLocalVariables(SPIRVFunctionContext &context, const std::vector<UGLIR::Statement> &statements)
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    if (statement.kind == UGLIR::StatementKind::VariableDeclaration)
                    {
                        if (isResourceAliasType(statement.type))
                        {
                            declareResourceAliasSelector(context, statement.name);
                            continue;
                        }
                        if (!context.localPointers.contains(statement.name))
                        {
                            const uint32_t typeId = getTypeId(statement.type, statement.sourceLocation);
                            const spv::StorageClass storageClass = storageClassForLocalType(statement.type);
                            const uint32_t pointerTypeId = getPointerTypeId(typeId, storageClass);
                            const uint32_t pointerId = allocateId();
                            SPIRVInstructionBuilder &variableSection = storageClass == spv::StorageClassWorkgroup ? mTypesConstantsGlobals : context.body;
                            variableSection.appendInstruction(spv::OpVariable, {
                                pointerTypeId,
                                pointerId,
                                static_cast<uint32_t>(storageClass),
                            });
                            context.localPointers.emplace(statement.name,
                                                          makePointerValue(pointerId,
                                                                           statement.type,
                                                                           storageClass,
                                                                           statement.sourceLocation));
                            mDebugNames.appendInstructionWithString(spv::OpName, {pointerId}, sanitizeSPIRVName(statement.name));
                        }
                    }
                    declareLocalVariables(context, statement.children);
                    declareLocalVariables(context, statement.elseChildren);
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        declareLocalVariables(context, switchCase.body);
                    }
                }
            }

            /** Copies supported entry inputs into the local names used by UGLIR expressions. */
            void emitEntryParameterMaterialization(SPIRVFunctionContext &context, const UGLIR::Function &function)
            {
                if (!function.isEntryPoint)
                {
                    return;
                }
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    const auto localIter = context.localPointers.find(parameter.name);
                    if (localIter == context.localPointers.end())
                    {
                        continue;
                    }

                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DispatchThreadID)
                    {
                        emitBuiltinParameterLoad(context, parameter, localIter->second, getGlobalInvocationIdVariableId(parameter));
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::GroupThreadID)
                    {
                        emitBuiltinParameterLoad(context, parameter, localIter->second, getLocalInvocationIdVariableId(parameter));
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::GroupID)
                    {
                        emitBuiltinParameterLoad(context, parameter, localIter->second, getWorkgroupIdVariableId(parameter));
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::GroupIndex)
                    {
                        emitBuiltinParameterLoad(context, parameter, localIter->second, getLocalInvocationIndexVariableId(parameter));
                        continue;
                    }

                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::VertexID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::InstanceID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::PixelCoord ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::SampleIndex ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::PrimitiveID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::Barycentrics)
                    {
                        const SPIRVStageIOInfo *stageInput = findStageIOVariable(spv::StorageClassInput,
                                                                                 parameter.name,
                                                                                 parameter.semanticKind,
                                                                                 parameter.semanticIndex);
                        if (stageInput == nullptr)
                        {
                            addDiagnostic(parameter.sourceLocation, "missing stage input variable for entry parameter \"" + parameter.name + "\".");
                            continue;
                        }
                        emitBuiltinParameterLoad(context, parameter, localIter->second, stageInput->variableId);
                        continue;
                    }

                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID)
                    {
                        const uint32_t commandParamsValueId = emitDrawInfoCommandParamsLoad(context, parameter.sourceLocation);
                        const uint32_t componentId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getUIntTypeId(),
                            componentId,
                            commandParamsValueId,
                            parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ? 0u : 1u,
                        });
                        context.body.appendInstruction(spv::OpStore, {localIter->second.id, componentId});
                        continue;
                    }

                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::StageInput ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalInput ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::VertexInput)
                    {
                        emitEntryStructParameterMaterialization(context, parameter, localIter->second);
                        continue;
                    }
                }
            }

            /** Addresses one physical buffer element without converting its storage representation. */
            SPIRVValue emitStorageBufferElementPointer(SPIRVFunctionContext &context,
                                                       const SPIRVResourceInfo &resource,
                                                       uint32_t indexId,
                                                       const UGLIR::SourceLocation &location)
            {
                const uint32_t pointerId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    resource.elementPointerTypeId,
                    pointerId,
                    resource.variableId,
                    getUIntConstant(0u),
                    indexId,
                });
                return makePointerValue(pointerId, resource.binding->elementType,
                                        spv::StorageClassStorageBuffer, location);
            }

            /** Loads a buffer element and converts its physical aggregate into a computation value. */
            SPIRVValue emitStorageBufferElementLoad(SPIRVFunctionContext &context,
                                                    const SPIRVResourceInfo &resource,
                                                    uint32_t indexId,
                                                    const UGLIR::SourceLocation &location)
            {
                if (resource.binding == nullptr ||
                    resource.binding->kind != UGLIR::ResourceKind::StorageBuffer)
                {
                    addDiagnostic(location, "direct SPIR-V buffer loads require a storage-buffer resource.");
                    return makeValue(getUIntConstant(0u), uintTypeKey(), location);
                }
                return loadIfPointer(context,
                                     emitStorageBufferElementPointer(context, resource, indexId, location),
                                     location);
            }

            /** Emits `min(value, exclusiveBound - 1)` with zero-safe bounds for unsigned indices. */
            uint32_t emitUIntClampToExclusiveBound(SPIRVFunctionContext &context,
                                                   uint32_t valueId,
                                                   uint32_t exclusiveBoundId,
                                                   const UGLIR::SourceLocation &)
            {
                const uint32_t hasElementsId = allocateId();
                context.body.appendInstruction(spv::OpUGreaterThan, {
                    getBoolTypeId(),
                    hasElementsId,
                    exclusiveBoundId,
                    getUIntConstant(0),
                });
                const uint32_t lastIndexId = allocateId();
                context.body.appendInstruction(spv::OpISub, {
                    getUIntTypeId(),
                    lastIndexId,
                    exclusiveBoundId,
                    getUIntConstant(1),
                });
                const uint32_t safeLastIndexId = allocateId();
                context.body.appendInstruction(spv::OpSelect, {
                    getUIntTypeId(),
                    safeLastIndexId,
                    hasElementsId,
                    lastIndexId,
                    getUIntConstant(0),
                });
                const uint32_t inRangeId = allocateId();
                context.body.appendInstruction(spv::OpULessThan, {
                    getBoolTypeId(),
                    inRangeId,
                    valueId,
                    exclusiveBoundId,
                });
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpSelect, {
                    getUIntTypeId(),
                    resultId,
                    inRangeId,
                    valueId,
                    safeLastIndexId,
                });
                return resultId;
            }

            /** Loads one uint2 entry from the generic component access-bounds buffer. */
            SPIRVValue emitComponentAccessBoundLoad(SPIRVFunctionContext &context,
                                                    uint32_t entryIndex,
                                                    const UGLIR::SourceLocation &location)
            {
                const SPIRVResourceInfo *resource = findComponentABIAuxiliaryResource(UGLIR::ResourceRole::AccessBounds);
                if (resource == nullptr)
                {
                    addDiagnostic(location, "component-table direct SPIR-V emission requires access-bounds metadata.");
                    return {};
                }
                return emitStorageBufferElementLoad(context, *resource, getUIntConstant(entryIndex), location);
            }

            /** Extracts one scalar from a generic component access-bounds uint2 entry. */
            uint32_t emitComponentAccessBoundValue(SPIRVFunctionContext &context,
                                                   uint32_t entryIndex,
                                                   uint32_t componentIndex,
                                                   const UGLIR::SourceLocation &location)
            {
                const SPIRVValue bounds = emitComponentAccessBoundLoad(context, entryIndex, location);
                if (bounds.id == 0)
                {
                    return getUIntConstant(0);
                }
                const uint32_t componentId = allocateId();
                context.body.appendInstruction(spv::OpCompositeExtract, {
                    getUIntTypeId(),
                    componentId,
                    bounds.id,
                    componentIndex,
                });
                return componentId;
            }

            /** Loads one draw command parameter record at the requested safe command index. */
            SPIRVValue emitDrawCommandParamsAtIndex(SPIRVFunctionContext &context, uint32_t commandIndexId, const UGLIR::SourceLocation &location)
            {
                const SPIRVResourceInfo *resource = findComponentABIAuxiliaryResource(UGLIR::ResourceRole::CommandParams);
                if (resource == nullptr)
                {
                    addDiagnostic(location, "draw command parameter loads require command-parameter metadata.");
                    return {};
                }
                const uint32_t commandCountId = emitComponentAccessBoundValue(context, 0u, 1u, location);
                const uint32_t safeCommandIndexId = emitUIntClampToExclusiveBound(context, commandIndexId, commandCountId, location);
                return emitStorageBufferElementLoad(context, *resource, safeCommandIndexId, location);
            }

            /** Loads and caches draw command params addressed by Vulkan InstanceIndex. */
            uint32_t emitDrawInfoCommandParamsLoad(SPIRVFunctionContext &context, const UGLIR::SourceLocation &location)
            {
                if (context.drawCommandParamsValueId != 0)
                {
                    return context.drawCommandParamsValueId;
                }
                const uint32_t instanceIdValueId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {
                    getUIntTypeId(),
                    instanceIdValueId,
                    ensureInstanceIdInputVariable(location),
                });
                const SPIRVValue commandParams = emitDrawCommandParamsAtIndex(context, instanceIdValueId, location);
                context.drawCommandParamsValueId = commandParams.id == 0 ? getDefaultValueId(spvVectorTypeKey(UGLIR::ScalarKind::UInt, 2u), location) : commandParams.id;
                return context.drawCommandParamsValueId;
            }

            /** Loads one draw-info record using the shared access-bounds entity count. */
            SPIRVValue emitDrawInfoLoad(SPIRVFunctionContext &context,
                                                uint32_t entityId,
                                                const UGLIR::SourceLocation &location)
            {
                const SPIRVResourceInfo *resource = findComponentABIAuxiliaryResource(UGLIR::ResourceRole::DrawInfo);
                if (resource == nullptr)
                {
                    addDiagnostic(location, "draw-info calls require draw-info metadata.");
                    return {};
                }
                const uint32_t entityCountId = emitComponentAccessBoundValue(context, 0u, 0u, location);
                const uint32_t safeEntityId = emitUIntClampToExclusiveBound(context, entityId, entityCountId, location);
                return emitStorageBufferElementLoad(context, *resource, safeEntityId, location);
            }

            /** Loads one SPIR-V input variable and stores it into the corresponding entry parameter local. */
            void emitBuiltinParameterLoad(SPIRVFunctionContext &context,
                                          const UGLIR::FunctionParameter &parameter,
                                          const SPIRVValue &localPointer,
                                          uint32_t inputVariableId)
            {
                const uint32_t valueId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {
                    getTypeId(parameter.type, parameter.sourceLocation),
                    valueId,
                    inputVariableId,
                });
                context.body.appendInstruction(spv::OpStore, {localPointer.id, valueId});
            }

            /** Materializes a stage input record parameter by copying reflected field variables into a local struct. */
            void emitEntryStructParameterMaterialization(SPIRVFunctionContext &context,
                                                         const UGLIR::FunctionParameter &parameter,
                                                         const SPIRVValue &localPointer)
            {
                const UGLIR::Type *parameterType = findType(parameter.type);
                if (parameterType == nullptr || parameterType->kind != UGLIR::TypeKind::Struct)
                {
                    addDiagnostic(parameter.sourceLocation, "stage input parameter \"" + parameter.name + "\" is not a struct type.");
                    return;
                }
                const bool requiresFieldMaterialization = parameter.semanticKind == UGLIR::BuiltinSemanticKind::StageInput ||
                                                          parameter.semanticKind == UGLIR::BuiltinSemanticKind::VertexInput;
                bool materializedAnyField = false;
                for (const UGLIR::TypeField &field : parameterType->fields)
                {
                    const SPIRVStageIOInfo *stageInput = findStageIOVariable(spv::StorageClassInput,
                                                                             field.name,
                                                                             field.semanticKind,
                                                                             field.semanticIndex);
                    if (stageInput == nullptr)
                    {
                        if (requiresFieldMaterialization)
                        {
                            addDiagnostic(field.sourceLocation,
                                          "missing reflected stage input for field \"" + parameter.name + "." + field.name + "\".");
                        }
                        continue;
                    }
                    const uint32_t fieldTypeId = getTypeId(field.type, field.sourceLocation);
                    const uint32_t fieldPointerTypeId = getPointerTypeId(fieldTypeId, spv::StorageClassFunction);
                    const uint32_t fieldPointerId = allocateId();
                    context.body.appendInstruction(spv::OpAccessChain, {
                        fieldPointerTypeId,
                        fieldPointerId,
                        localPointer.id,
                        getUIntConstant(getStructFieldIndex(parameter.type, field.name, field.sourceLocation)),
                    });
                    const uint32_t valueId = allocateId();
                    context.body.appendInstruction(spv::OpLoad, {
                        getTypeId(stageInput->binding->type, field.sourceLocation),
                        valueId,
                        stageInput->variableId,
                    });
                    const SPIRVValue value = adaptValueToType(context,
                                                              makeValue(valueId, stageInput->binding->type, field.sourceLocation),
                                                              field.type,
                                                              field.sourceLocation);
                    context.body.appendInstruction(spv::OpStore, {fieldPointerId, value.id});
                    materializedAnyField = true;
                }
                if (requiresFieldMaterialization && !materializedAnyField)
                {
                    addDiagnostic(parameter.sourceLocation,
                                  "stage input parameter \"" + parameter.name + "\" was not materialized from any reflected input variable.");
                }
            }

            /** Emits a list of UGLIR statements starting at one child index. */
            void emitStatementList(SPIRVFunctionContext &context, const std::vector<UGLIR::Statement> &statements, size_t startIndex)
            {
                for (size_t index = startIndex; index < statements.size(); ++index)
                {
                    if (context.blockTerminated)
                    {
                        return;
                    }
                    emitStatement(context, statements[index]);
                }
            }

            /** Emits a single UGLIR statement into the current SPIR-V block. */
            void emitStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                switch (statement.kind)
                {
                case UGLIR::StatementKind::Block:
                    emitStatementList(context, statement.children, 0u);
                    return;
                case UGLIR::StatementKind::VariableDeclaration:
                    emitVariableDeclaration(context, statement);
                    return;
                case UGLIR::StatementKind::Return:
                    emitReturnStatement(context, statement);
                    return;
                case UGLIR::StatementKind::If:
                    emitIfStatement(context, statement);
                    return;
                case UGLIR::StatementKind::For:
                    emitForStatement(context, statement);
                    return;
                case UGLIR::StatementKind::While:
                    emitWhileStatement(context, statement);
                    return;
                case UGLIR::StatementKind::Do:
                    emitDoStatement(context, statement);
                    return;
                case UGLIR::StatementKind::Switch:
                    emitSwitchStatement(context, statement);
                    return;
                case UGLIR::StatementKind::Break:
                    emitBreakStatement(context, statement);
                    return;
                case UGLIR::StatementKind::Continue:
                    emitContinueStatement(context, statement);
                    return;
                case UGLIR::StatementKind::Expression:
                    if (statement.expressions.empty())
                    {
                        addDiagnostic(statement.sourceLocation, "expression statement is missing its expression.");
                        return;
                    }
                    emitExpression(context, statement.expressions.front());
                    return;
                default:
                    addDiagnostic(statement.sourceLocation, "unsupported UGLIR statement kind in Phase 6 direct SPIR-V emission.");
                    return;
                }
            }

            /** Emits a variable initializer store for an already-declared local variable. */
            void emitVariableDeclaration(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (isResourceAliasType(statement.type))
                {
                    emitResourceAliasDeclaration(context, statement);
                    return;
                }
                if (statement.expressions.empty())
                {
                    const auto iter = context.localPointers.find(statement.name);
                    if (iter != context.localPointers.end() && iter->second.storageClass == spv::StorageClassFunction)
                    {
                        const std::string defaultType = valueTypeKeyForName(statement.type);
                        if (!describeTypeName(defaultType).has_value() && findType(defaultType) == nullptr)
                        {
                            addDiagnostic(statement.sourceLocation,
                                          "local variable \"" + statement.name + "\" has unsupported default-initializer type \""
                                              + statement.type + "\" with pointer type \"" + iter->second.typeName + "\".");
                        }
                        context.body.appendInstruction(spv::OpStore, {
                            iter->second.id,
                            getDefaultValueId(defaultType, statement.sourceLocation),
                        });
                    }
                    return;
                }
                const auto iter = context.localPointers.find(statement.name);
                if (iter == context.localPointers.end())
                {
                    addDiagnostic(statement.sourceLocation, "local variable \"" + statement.name + "\" was not declared before initialization.");
                    return;
                }
                if (statement.expressions.front().kind == UGLIR::ExpressionKind::Literal)
                {
                    const std::string targetType = valueTypeKeyForName(iter->second.typeName);
                    if (isUnsignedIntegerScalarValue(targetType))
                    {
                        context.body.appendInstruction(spv::OpStore, {iter->second.id, getUIntConstant(parseUnsignedLiteral(statement.expressions.front().value))});
                        return;
                    }
                    if (isSignedIntegerScalarValue(targetType))
                    {
                        context.body.appendInstruction(spv::OpStore, {iter->second.id, getIntConstant(parseUnsignedLiteral(statement.expressions.front().value))});
                        return;
                    }
                    if (isFloatLikeScalarValue(targetType))
                    {
                        context.body.appendInstruction(spv::OpStore, {
                            iter->second.id,
                            isHalfScalarValue(targetType) ? getHalfConstant(statement.expressions.front().value) : getFloatConstant(statement.expressions.front().value),
                        });
                        return;
                    }
                }
                const SPIRVValue value = emitRValue(context, statement.expressions.front());
                emitTypedStore(context, iter->second, value, statement.sourceLocation);
            }

            /** Records a local alias that names an existing reflected resource instead of allocating function storage. */
            void emitResourceAliasDeclaration(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (statement.expressions.empty())
                {
                    return;
                }
                const UGLIR::Expression &initializer = statement.expressions.front();
                const SPIRVResourceInfo *resource = findResourceAliasInitializer(context, statement.expressions.front());
                if (resource == nullptr)
                {
                    addDiagnostic(statement.sourceLocation, "resource alias local \"" + statement.name + "\" cannot be resolved to a reflected resource.");
                    return;
                }
                assignResourceAlias(context, statement.name, *resource, statement.sourceLocation);
                if (resource->binding != nullptr && resource->binding->kind == UGLIR::ResourceKind::Texture)
                {
                    const SPIRVValue textureObject{.id = loadResourceObjectForExpression(context, initializer, *resource),
                                                   .typeName = resource->binding->elementType};
                    if (textureObject.id != 0)
                    {
                        context.resourceObjectAliases.emplace(statement.name, textureObject);
                    }
                }
            }

            /** Resolves an initializer expression that produces a local texture or sampler alias. */
            const SPIRVResourceInfo *findResourceAliasInitializer(SPIRVFunctionContext &context, const UGLIR::Expression &expression) const
            {
                if (const SPIRVResourceInfo *resource = findResourceForExpression(context, expression))
                {
                    return resource;
                }
                if ((expression.kind == UGLIR::ExpressionKind::Cast ||
                     expression.kind == UGLIR::ExpressionKind::Construct ||
                     expression.kind == UGLIR::ExpressionKind::Load) &&
                    !expression.operands.empty())
                {
                    return findResourceAliasInitializer(context, expression.operands.front());
                }
                return nullptr;
            }

            /** Emits a return or return-value instruction. */
            void emitReturnStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                const UGLIR::Function *function = context.functionInfo == nullptr ? nullptr : context.functionInfo->function;
                if (function != nullptr && function->isEntryPoint && function->stage != UGLIR::ShaderStage::Compute)
                {
                    if (!statement.expressions.empty())
                    {
                        const SPIRVValue value = emitRValue(context, statement.expressions.front());
                        emitEntryReturnValueStores(context, *function, value, statement.sourceLocation);
                    }
                    context.body.appendInstruction(spv::OpReturn, {});
                    context.blockTerminated = true;
                    return;
                }
                if (statement.expressions.empty())
                {
                    context.body.appendInstruction(spv::OpReturn, {});
                }
                else
                {
                    SPIRVValue value = emitRValue(context, statement.expressions.front());
                    if (function != nullptr)
                    {
                        value = adaptValueToType(context, value, function->returnType, statement.sourceLocation);
                    }
                    context.body.appendInstruction(spv::OpReturnValue, {value.id});
                }
                context.blockTerminated = true;
            }

            /** Stores clip planes individually for portable SPIRV-Cross lowering.
             * Whole-array builtin stores become invalid C-array assignments in MoltenVK's
             * Metal output. Scalar access chains preserve the same SPIR-V semantics.
             * The frontend has already checked the portable float-array contract.
             */
            void emitClipDistanceOutputStores(SPIRVFunctionContext &context,
                                              const SPIRVStageIOInfo &outputInfo,
                                              const SPIRVValue &outputValue,
                                              const UGLIR::Type &arrayType)
            {
                const uint32_t elementTypeId = getTypeId(arrayType.elementType, arrayType.sourceLocation);
                const uint32_t pointerTypeId = getPointerTypeId(elementTypeId, spv::StorageClassOutput);
                for (uint32_t index = 0u; index < arrayType.arrayCount; ++index)
                {
                    const uint32_t elementValueId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract,
                        {elementTypeId, elementValueId, outputValue.id, index});
                    const uint32_t elementPointerId = allocateId();
                    context.body.appendInstruction(spv::OpAccessChain,
                        {pointerTypeId, elementPointerId, outputInfo.variableId, getUIntConstant(index)});
                    context.body.appendInstruction(spv::OpStore, {elementPointerId, elementValueId});
                }
            }

            /** Stores a raster entry return record into the reflected SPIR-V stage output variables. */
            void emitEntryReturnValueStores(SPIRVFunctionContext &context,
                                            const UGLIR::Function &function,
                                            const SPIRVValue &returnValue,
                                            const UGLIR::SourceLocation &location)
            {
                const UGLIR::Type *returnType = findType(function.returnType);
                if (returnType == nullptr || returnType->kind != UGLIR::TypeKind::Struct)
                {
                    addDiagnostic(location, "raster entry return type \"" + function.returnType + "\" is not a supported stage output struct.");
                    return;
                }
                for (const SPIRVStageIOInfo &outputInfo : mStageIOVariables)
                {
                    if (outputInfo.storageClass != spv::StorageClassOutput || outputInfo.binding == nullptr)
                    {
                        continue;
                    }
                    uint32_t fieldIndex = 0;
                    const UGLIR::TypeField *field = findStructField(function.returnType, outputInfo.binding->name, fieldIndex);
                    if (field == nullptr)
                    {
                        continue;
                    }
                    const uint32_t fieldValueId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract, {
                        getTypeId(field->type, field->sourceLocation),
                        fieldValueId,
                        returnValue.id,
                        fieldIndex,
                    });
                    const SPIRVValue outputValue = adaptValueToType(context,
                                                                    makeValue(fieldValueId, field->type, field->sourceLocation),
                                                                    outputInfo.valueTypeName.empty() ? field->type : outputInfo.valueTypeName,
                                                                    field->sourceLocation);
                    if (outputInfo.binding->semanticKind == UGLIR::BuiltinSemanticKind::ClipDistance)
                    {
                        emitClipDistanceOutputStores(context, outputInfo, outputValue, *findType(field->type));
                    }
                    else
                    {
                        context.body.appendInstruction(spv::OpStore, {outputInfo.variableId, outputValue.id});
                    }
                }
            }

            /** Emits a structured SPIR-V selection for a UGLIR if statement. */
            void emitIfStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "if statement is missing its condition.");
                    return;
                }
                const SPIRVValue condition = adaptValueToType(context,
                                                               emitRValue(context, statement.expressions.front()),
                                                               boolTypeKey(),
                                                               statement.expressions.front().sourceLocation);
                const uint32_t thenLabel = allocateId();
                const uint32_t elseLabel = statement.elseChildren.empty() ? 0u : allocateId();
                const uint32_t mergeLabel = allocateId();
                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, thenLabel, elseLabel == 0u ? mergeLabel : elseLabel});
                context.blockTerminated = true;

                beginBlock(context, thenLabel);
                emitStatementList(context, statement.children, 0u);
                const bool thenTerminates = context.blockTerminated;
                branchTo(context, mergeLabel);

                bool elseTerminates = false;
                if (elseLabel != 0u)
                {
                    beginBlock(context, elseLabel);
                    emitStatementList(context, statement.elseChildren, 0u);
                    elseTerminates = context.blockTerminated;
                    branchTo(context, mergeLabel);
                }

                beginBlock(context, mergeLabel);
                if (elseLabel != 0u && thenTerminates && elseTerminates)
                {
                    context.body.appendInstruction(spv::OpUnreachable, {});
                    context.blockTerminated = true;
                }
            }

            /** Emits a structured SPIR-V loop for a UGLIR for statement. */
            void emitForStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                const bool hasInit = !statement.children.empty() && statement.children.front().kind == UGLIR::StatementKind::VariableDeclaration;
                if (hasInit)
                {
                    emitVariableDeclaration(context, statement.children.front());
                }

                const uint32_t headerLabel = allocateId();
                const uint32_t conditionLabel = allocateId();
                const uint32_t bodyLabel = allocateId();
                const uint32_t continueLabel = allocateId();
                const uint32_t mergeLabel = allocateId();
                branchTo(context, headerLabel);

                beginBlock(context, headerLabel);
                context.body.appendInstruction(spv::OpLoopMerge, {mergeLabel, continueLabel, static_cast<uint32_t>(spv::LoopControlMaskNone)});
                branchTo(context, conditionLabel);
                beginBlock(context, conditionLabel);
                const SPIRVValue condition = statement.expressions.empty()
                                                 ? makeValue(getTrueConstant(), boolTypeKey(), statement.sourceLocation)
                                                 : adaptValueToType(context,
                                                                    emitRValue(context, statement.expressions.front()),
                                                                    boolTypeKey(),
                                                                    statement.expressions.front().sourceLocation);
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, bodyLabel, mergeLabel});
                context.blockTerminated = true;

                beginBlock(context, bodyLabel);
                context.loopTargets.push_back({mergeLabel, continueLabel});
                emitStatementList(context, statement.children, hasInit ? 1u : 0u);
                context.loopTargets.pop_back();
                branchTo(context, continueLabel);

                beginBlock(context, continueLabel);
                if (statement.expressions.size() > 1u)
                {
                    emitExpression(context, statement.expressions[1]);
                }
                branchTo(context, headerLabel);

                beginBlock(context, mergeLabel);
            }

            /** Emits a structured SPIR-V loop for a UGLIR while statement. */
            void emitWhileStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "while statement is missing its condition.");
                    return;
                }

                const uint32_t headerLabel = allocateId();
                const uint32_t conditionLabel = allocateId();
                const uint32_t bodyLabel = allocateId();
                const uint32_t continueLabel = allocateId();
                const uint32_t mergeLabel = allocateId();
                branchTo(context, headerLabel);

                beginBlock(context, headerLabel);
                context.body.appendInstruction(spv::OpLoopMerge, {mergeLabel, continueLabel, static_cast<uint32_t>(spv::LoopControlMaskNone)});
                branchTo(context, conditionLabel);
                beginBlock(context, conditionLabel);
                const SPIRVValue condition = adaptValueToType(context,
                                                               emitRValue(context, statement.expressions.front()),
                                                               boolTypeKey(),
                                                               statement.expressions.front().sourceLocation);
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, bodyLabel, mergeLabel});
                context.blockTerminated = true;

                beginBlock(context, bodyLabel);
                context.loopTargets.push_back({mergeLabel, continueLabel});
                emitStatementList(context, statement.children, 0u);
                context.loopTargets.pop_back();
                branchTo(context, continueLabel);

                beginBlock(context, continueLabel);
                branchTo(context, headerLabel);

                beginBlock(context, mergeLabel);
            }

            /** Emits a structured SPIR-V loop for a UGLIR do-while statement. */
            void emitDoStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "do statement is missing its condition.");
                    return;
                }

                const uint32_t headerLabel = allocateId();
                const uint32_t bodyLabel = allocateId();
                const uint32_t continueLabel = allocateId();
                const uint32_t mergeLabel = allocateId();
                branchTo(context, headerLabel);

                beginBlock(context, headerLabel);
                context.body.appendInstruction(spv::OpLoopMerge, {mergeLabel, continueLabel, static_cast<uint32_t>(spv::LoopControlMaskNone)});
                branchTo(context, bodyLabel);
                beginBlock(context, bodyLabel);
                context.loopTargets.push_back({mergeLabel, continueLabel});
                emitStatementList(context, statement.children, 0u);
                context.loopTargets.pop_back();
                branchTo(context, continueLabel);

                beginBlock(context, continueLabel);
                const SPIRVValue condition = adaptValueToType(context,
                                                               emitRValue(context, statement.expressions.front()),
                                                               boolTypeKey(),
                                                               statement.expressions.front().sourceLocation);
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, headerLabel, mergeLabel});
                context.blockTerminated = true;

                beginBlock(context, mergeLabel);
            }

            /** Returns the 32-bit switch literal value for a supported UGLIR case label. */
            std::optional<uint32_t> getSwitchCaseLiteral(const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Literal)
                {
                    return parseUnsignedLiteral(expression.value);
                }
                if (expression.kind == UGLIR::ExpressionKind::Unary &&
                    expression.operatorName == "-" &&
                    expression.operands.size() == 1u &&
                    expression.operands.front().kind == UGLIR::ExpressionKind::Literal)
                {
                    return static_cast<uint32_t>(-static_cast<int32_t>(parseUnsignedLiteral(expression.operands.front().value)));
                }
                return std::nullopt;
            }

            /** Emits a structured SPIR-V selection for a UGLIR switch statement. */
            void emitSwitchStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "switch statement is missing its selector.");
                    return;
                }

                const SPIRVValue selector = emitRValue(context, statement.expressions.front());
                const uint32_t mergeLabel = allocateId();
                std::vector<uint32_t> caseLabels;
                caseLabels.reserve(statement.switchCases.size());
                uint32_t defaultLabel = mergeLabel;
                bool hasDefaultCase = false;
                for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                {
                    const uint32_t labelId = allocateId();
                    caseLabels.push_back(labelId);
                    if (switchCase.isDefault)
                    {
                        defaultLabel = labelId;
                        hasDefaultCase = true;
                    }
                }

                std::vector<uint32_t> switchOperands{selector.id, defaultLabel};
                for (size_t index = 0; index < statement.switchCases.size(); ++index)
                {
                    const UGLIR::SwitchCase &switchCase = statement.switchCases[index];
                    for (const UGLIR::Expression &label : switchCase.labels)
                    {
                        const std::optional<uint32_t> literal = getSwitchCaseLiteral(label);
                        if (!literal.has_value())
                        {
                            addDiagnostic(label.sourceLocation, "switch case label must lower to a 32-bit integer literal.");
                            continue;
                        }
                        switchOperands.push_back(*literal);
                        switchOperands.push_back(caseLabels[index]);
                    }
                }

                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                context.body.appendInstruction(spv::OpSwitch, switchOperands);
                context.blockTerminated = true;

                context.loopTargets.push_back({mergeLabel, 0u});
                std::vector<bool> caseBodiesTerminate(statement.switchCases.size(), false);
                for (size_t index = 0; index < statement.switchCases.size(); ++index)
                {
                    beginBlock(context, caseLabels[index]);
                    emitStatementList(context, statement.switchCases[index].body, 0u);
                    caseBodiesTerminate[index] = context.blockTerminated;
                    const uint32_t fallthroughLabel = index + 1u < caseLabels.size() ? caseLabels[index + 1u] : mergeLabel;
                    branchTo(context, fallthroughLabel);
                }
                context.loopTargets.pop_back();

                bool allCasesTerminate = hasDefaultCase && !caseBodiesTerminate.empty();
                for (bool caseTerminates : caseBodiesTerminate)
                {
                    allCasesTerminate = allCasesTerminate && caseTerminates;
                }
                beginBlock(context, mergeLabel);
                if (allCasesTerminate)
                {
                    context.body.appendInstruction(spv::OpUnreachable, {});
                    context.blockTerminated = true;
                }
            }

            /** Emits a branch to the innermost loop merge block. */
            void emitBreakStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                if (context.loopTargets.empty())
                {
                    addDiagnostic(statement.sourceLocation, "break statement is not inside a loop.");
                    return;
                }
                branchTo(context, context.loopTargets.back().breakLabelId);
            }

            /** Emits a branch to the innermost loop continue block. */
            void emitContinueStatement(SPIRVFunctionContext &context, const UGLIR::Statement &statement)
            {
                auto targetIter = std::find_if(context.loopTargets.rbegin(), context.loopTargets.rend(), [](const SPIRVLoopTargets &targets) {
                    return targets.continueLabelId != 0u;
                });
                if (targetIter == context.loopTargets.rend())
                {
                    addDiagnostic(statement.sourceLocation, "continue statement is not inside a loop.");
                    return;
                }
                branchTo(context, targetIter->continueLabelId);
            }

            /** Emits a UGLIR expression and returns an rvalue whenever the expression produces a value. */
            SPIRVValue emitExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                SPIRVValue result;
                switch (expression.kind)
                {
                case UGLIR::ExpressionKind::Literal:
                    result = emitLiteralExpression(expression);
                    break;
                case UGLIR::ExpressionKind::ThisRef:
                    if (const auto localIter = context.localPointers.find("this"); localIter != context.localPointers.end())
                    {
                        result = localIter->second;
                        break;
                    }
                    if (const auto parameterIter = context.parameterValues.find("this"); parameterIter != context.parameterValues.end())
                    {
                        result = parameterIter->second;
                        break;
                    }
                    addDiagnostic(expression.sourceLocation, "this reference was emitted outside a member-function helper context.");
                    result = makeValue(0, expression.type, expression.sourceLocation);
                    break;
                case UGLIR::ExpressionKind::DeclRef:
                    result = emitDeclRefExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::MemberRef:
                    result = emitMemberRefExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Subscript:
                    result = emitSubscriptExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Call:
                    result = emitCallExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Construct:
                    result = emitConstructExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Load:
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "load expression is missing its operand.");
                        return {};
                    }
                    result = emitRValue(context, expression.operands.front());
                    break;
                case UGLIR::ExpressionKind::Cast:
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "cast expression is missing its operand.");
                        return {};
                    }
                    if (isVoidTypeName(expression.type))
                    {
                        (void)emitRValue(context, expression.operands.front());
                        result = makeVoidValue();
                        break;
                    }
                    result = adaptValueToType(context, emitRValue(context, expression.operands.front()), expression.type, expression.sourceLocation);
                    break;
                case UGLIR::ExpressionKind::Binary:
                    result = emitBinaryExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Unary:
                    result = emitUnaryExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Conditional:
                    result = emitConditionalExpression(context, expression);
                    break;
                case UGLIR::ExpressionKind::Store:
                    result = emitStoreExpression(context, expression);
                    break;
                }
                return attachValueType(std::move(result));
            }

            // Half conversions remain explicit, but permit target contraction/reassociation.
            // Automatic NoContraction makes MoltenVK emit costly optnone arithmetic functions.
            /** Emits a UGLIR expression and loads through pointers to produce an rvalue. */
            SPIRVValue emitRValue(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                return loadIfPointer(context, emitExpression(context, expression), expression.sourceLocation);
            }

            /**
             * Copies logically matching aggregate types across the computation/storage boundary.
             * Identical scalar, vector and matrix types need no copy. SPIR-V 1.4 preserves
             * member values while the downstream compiler handles physical layout conversion.
             */
            uint32_t emitLogicalTypeCopy(SPIRVFunctionContext &context,
                                         uint32_t valueId,
                                         uint32_t sourceTypeId,
                                         uint32_t targetTypeId)
            {
                if (sourceTypeId == targetTypeId)
                {
                    return valueId;
                }
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpCopyLogical, {
                    targetTypeId, resultId, valueId,
                });
                return resultId;
            }

            /** Loads memory and returns an undecorated logical value for subsequent computation. */
            SPIRVValue loadIfPointer(SPIRVFunctionContext &context, const SPIRVValue &value, const UGLIR::SourceLocation &location)
            {
                if (!value.isPointer)
                {
                    return value;
                }
                if (value.isResourceRoot)
                {
                    addDiagnostic(location, "resource values must be accessed through a read, write, sample, or subscript operation in direct SPIR-V emission.");
                    return {};
                }
                const std::string valueTypeName = storageValueTypeName(value.typeName);
                const uint32_t memoryTypeId = getMemoryValueTypeId(
                    valueTypeName, value.storageClass, location);
                const uint32_t loadedId = allocateId();
                if (value.storageClass == spv::StorageClassPhysicalStorageBuffer)
                {
                    context.body.appendInstruction(spv::OpLoad, {memoryTypeId, loadedId, value.id,
                        static_cast<uint32_t>(spv::MemoryAccessAlignedMask), argumentMemoryAlignment(valueTypeName)});
                }
                else
                {
                    context.body.appendInstruction(spv::OpLoad, {memoryTypeId, loadedId, value.id});
                }
                const uint32_t resultId = emitLogicalTypeCopy(
                    context, loadedId, memoryTypeId, getTypeId(valueTypeName, location));
                return makeValue(resultId, valueTypeName, location);
            }

            /** Converts a scalar value to the requested scalar type when SPIR-V requires matching component types. */
            SPIRVValue convertScalarValueToType(SPIRVFunctionContext &context,
                                                const SPIRVValue &value,
                                                const std::string &targetType,
                                                const UGLIR::SourceLocation &location)
            {
                const std::optional<UGLIR::ValueTypeDescription> sourceValueType = value.valueType.has_value() ? value.valueType : describeTypeName(value.typeName);
                const std::optional<UGLIR::ValueTypeDescription> targetValueType = describeTypeName(targetType);
                const std::string sourceType = sourceValueType.has_value() ? valueTypeKey(*sourceValueType) : valueTypeKeyForName(value.typeName);
                const std::string canonicalTargetType = targetValueType.has_value() ? valueTypeKey(*targetValueType) : valueTypeKeyForName(targetType);
                if (sourceType == canonicalTargetType ||
                    (isHalfScalarValue(sourceType) && isHalfScalarValue(canonicalTargetType)))
                {
                    return makeValue(value.id, canonicalTargetType, location);
                }
                if (valueTypeScalarKey(sourceType) == valueTypeScalarKey(canonicalTargetType))
                {
                    return makeValue(value.id, canonicalTargetType, location);
                }
                const uint32_t resultId = allocateId();
                if (valueTypeScalarKind(sourceType) == UGLIR::ScalarKind::Bool &&
                    (isFloatLikeScalarValue(canonicalTargetType) ||
                     isUnsignedIntegerScalarValue(canonicalTargetType) ||
                     isSignedIntegerScalarValue(canonicalTargetType)))
                {
                    uint32_t trueValue = 0;
                    uint32_t falseValue = 0;
                    if (isHalfScalarValue(canonicalTargetType))
                    {
                        trueValue = getHalfConstant("1.0");
                        falseValue = getHalfConstant("0.0");
                    }
                    else if (isFloatScalarValue(canonicalTargetType))
                    {
                        trueValue = getFloatConstant("1.0");
                        falseValue = getFloatConstant("0.0");
                    }
                    else if (isUnsignedIntegerScalarValue(canonicalTargetType))
                    {
                        trueValue = getUIntConstant(1u);
                        falseValue = getUIntConstant(0u);
                    }
                    else
                    {
                        trueValue = getIntConstant(1);
                        falseValue = getIntConstant(0);
                    }
                    context.body.appendInstruction(spv::OpSelect, {
                        getTypeId(canonicalTargetType, location),
                        resultId,
                        value.id,
                        trueValue,
                        falseValue,
                    });
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isFloatLikeScalarValue(sourceType) && isFloatLikeScalarValue(canonicalTargetType))
                {
                    context.body.appendInstruction(spv::OpFConvert, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isUnsignedIntegerScalarValue(canonicalTargetType) && isUnsignedIntegerScalarValue(sourceType))
                {
                    return makeValue(value.id, canonicalTargetType, location);
                }
                if (isSignedIntegerScalarValue(canonicalTargetType) && isSignedIntegerScalarValue(sourceType))
                {
                    return makeValue(value.id, canonicalTargetType, location);
                }
                if (isFloatScalarValue(canonicalTargetType) && isUnsignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertUToF, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isFloatScalarValue(canonicalTargetType) && isSignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertSToF, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isHalfScalarValue(canonicalTargetType) && isUnsignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertUToF, {getHalfTypeId(), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isHalfScalarValue(canonicalTargetType) && isSignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertSToF, {getHalfTypeId(), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isUnsignedIntegerScalarValue(canonicalTargetType) && isFloatLikeScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertFToU, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (isSignedIntegerScalarValue(canonicalTargetType) && isFloatLikeScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpConvertFToS, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                if (valueTypeScalarKind(canonicalTargetType) == UGLIR::ScalarKind::Bool && isUnsignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpINotEqual, {getBoolTypeId(), resultId, value.id, getUIntConstant(0)});
                    return makeValue(resultId, boolTypeKey(), location);
                }
                if (valueTypeScalarKind(canonicalTargetType) == UGLIR::ScalarKind::Bool && isSignedIntegerScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpINotEqual, {getBoolTypeId(), resultId, value.id, getIntConstant(0)});
                    return makeValue(resultId, boolTypeKey(), location);
                }
                if (valueTypeScalarKind(canonicalTargetType) == UGLIR::ScalarKind::Bool && isFloatScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpFUnordNotEqual, {getBoolTypeId(), resultId, value.id, getFloatConstant("0.0")});
                    return makeValue(resultId, boolTypeKey(), location);
                }
                if (valueTypeScalarKind(canonicalTargetType) == UGLIR::ScalarKind::Bool && isHalfScalarValue(sourceType))
                {
                    context.body.appendInstruction(spv::OpFUnordNotEqual, {getBoolTypeId(), resultId, value.id, getHalfConstant("0.0")});
                    return makeValue(resultId, boolTypeKey(), location);
                }
                if ((isSignedIntegerScalarValue(canonicalTargetType) && isUnsignedIntegerScalarValue(sourceType)) ||
                    (isUnsignedIntegerScalarValue(canonicalTargetType) && isSignedIntegerScalarValue(sourceType)))
                {
                    context.body.appendInstruction(spv::OpBitcast, {getTypeId(canonicalTargetType, location), resultId, value.id});
                    return makeValue(resultId, canonicalTargetType, location);
                }
                addDiagnostic(location, "cannot convert scalar value from \"" + sourceType + "\" to \"" + canonicalTargetType + "\" for direct SPIR-V emission.");
                return value;
            }

            /** Broadcasts a value known from syntax to be scalar even when Clang's callee type spelling is imprecise. */
            SPIRVValue broadcastScalarValueToVector(SPIRVFunctionContext &context,
                                                   const SPIRVValue &value,
                                                   const std::string &targetType,
                                                   const UGLIR::SourceLocation &location)
            {
                const std::string canonicalTargetType = valueTypeKeyForName(targetType);
                const uint32_t targetWidth = valueTypeVectorWidth(canonicalTargetType);
                if (targetWidth <= 1u)
                {
                    return convertScalarValueToType(context, value, canonicalTargetType, location);
                }
                const SPIRVValue scalarValue = convertScalarValueToType(context, value, valueTypeScalarKey(canonicalTargetType), location);
                const uint32_t vectorTypeId = getTypeId(canonicalTargetType, location);
                std::vector<uint32_t> operands{
                    vectorTypeId,
                    allocateId(),
                };
                for (uint32_t index = 0; index < targetWidth; ++index)
                {
                    operands.push_back(scalarValue.id);
                }
                context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                return makeValue(operands[1], canonicalTargetType, location);
            }

            /** Broadcasts or converts one value so a binary/image instruction receives the requested vector type. */
            /** Adapts a vector value to a same-width target type via scalar-kind conversion. Returns true when handled and result was stored. */
            bool adaptSameWidthVectorValue(SPIRVFunctionContext &context, const SPIRVValue &value, SPIRVValue &result, const std::string &canonicalSourceType, const std::string &canonicalTargetType, const UGLIR::SourceLocation &location)
            {
                const uint32_t targetWidth = valueTypeVectorWidth(canonicalTargetType);
                const uint32_t sourceWidth = valueTypeVectorWidth(canonicalSourceType);
                if (sourceWidth == targetWidth)
                {
                    const std::string sourceScalarType = valueTypeScalarKey(canonicalSourceType);
                    const UGLIR::ScalarKind sourceScalarKind = valueTypeScalarKind(canonicalSourceType);
                    const UGLIR::ScalarKind targetScalarKind = valueTypeScalarKind(canonicalTargetType);
                    if (targetScalarKind == UGLIR::ScalarKind::Bool && isUnsignedIntegerScalarValue(sourceScalarType))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpINotEqual, {
                            getTypeId(canonicalTargetType, location),
                            resultId,
                            value.id,
                            getDefaultValueId(canonicalSourceType, location),
                        });
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if (targetScalarKind == UGLIR::ScalarKind::Bool && isSignedIntegerScalarValue(sourceScalarType))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpINotEqual, {
                            getTypeId(canonicalTargetType, location),
                            resultId,
                            value.id,
                            getDefaultValueId(canonicalSourceType, location),
                        });
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if (targetScalarKind == UGLIR::ScalarKind::Bool && isFloatLikeScalarValue(sourceScalarType))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpFUnordNotEqual, {
                            getTypeId(canonicalTargetType, location),
                            resultId,
                            value.id,
                            getDefaultValueId(canonicalSourceType, location),
                        });
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if (isFloatLikeVectorValue(canonicalSourceType) && isFloatLikeVectorValue(canonicalTargetType))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpFConvert, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if ((targetScalarKind == UGLIR::ScalarKind::Float || targetScalarKind == UGLIR::ScalarKind::Half) &&
                        sourceScalarKind == UGLIR::ScalarKind::UInt)
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpConvertUToF, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if ((targetScalarKind == UGLIR::ScalarKind::Float || targetScalarKind == UGLIR::ScalarKind::Half) &&
                        sourceScalarKind == UGLIR::ScalarKind::Int)
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpConvertSToF, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if (targetScalarKind == UGLIR::ScalarKind::UInt &&
                        (sourceScalarKind == UGLIR::ScalarKind::Float || sourceScalarKind == UGLIR::ScalarKind::Half))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpConvertFToU, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if (targetScalarKind == UGLIR::ScalarKind::Int &&
                        (sourceScalarKind == UGLIR::ScalarKind::Float || sourceScalarKind == UGLIR::ScalarKind::Half))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpConvertFToS, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    if ((targetScalarKind == UGLIR::ScalarKind::Int && sourceScalarKind == UGLIR::ScalarKind::UInt) ||
                        (targetScalarKind == UGLIR::ScalarKind::UInt && sourceScalarKind == UGLIR::ScalarKind::Int))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpBitcast, {getTypeId(canonicalTargetType, location), resultId, value.id});
                        result = makeValue(resultId, canonicalTargetType, location);
                        return true;
                    }
                    result = value;
                    return true;
                }
                return false;
            }

            /** Adapts a vector value to a narrower target type by extracting and converting each component. Returns true when handled and result was stored. */
            bool adaptNarrowVectorValue(SPIRVFunctionContext &context, const SPIRVValue &value, SPIRVValue &result, const std::string &canonicalSourceType, const std::string &canonicalTargetType, uint32_t targetWidth, const UGLIR::SourceLocation &location)
            {
                const uint32_t sourceWidth = valueTypeVectorWidth(canonicalSourceType);
                if (sourceWidth > targetWidth)
                {
                    const std::string sourceScalarType = valueTypeScalarKey(canonicalSourceType);
                    const std::string targetScalarType = valueTypeScalarKey(canonicalTargetType);
                    std::vector<uint32_t> componentIds;
                    componentIds.reserve(targetWidth);
                    for (uint32_t componentIndex = 0; componentIndex < targetWidth; ++componentIndex)
                    {
                        const uint32_t extractedId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(sourceScalarType, location),
                            extractedId,
                            value.id,
                            componentIndex,
                        });
                        const SPIRVValue convertedComponent = convertScalarValueToType(context,
                                                                                        makeValue(extractedId, sourceScalarType, location),
                                                                                        targetScalarType,
                                                                                        location);
                        componentIds.push_back(convertedComponent.id);
                    }
                    const uint32_t resultId = allocateId();
                    std::vector<uint32_t> operands{
                        getTypeId(canonicalTargetType, location),
                        resultId,
                    };
                    operands.insert(operands.end(), componentIds.begin(), componentIds.end());
                    context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                    result = makeValue(resultId, canonicalTargetType, location);
                    return true;
                }
                return false;
            }

            SPIRVValue adaptValueToType(SPIRVFunctionContext &context,
                                        const SPIRVValue &value,
                                        const std::string &targetType,
                                        const UGLIR::SourceLocation &location)
            {
                if (mModule.reflection.arguments.enabled)
                {
                    if (const auto *type = findType(targetType); type != nullptr &&
                        (type->kind == UGLIR::TypeKind::Buffer || type->kind == UGLIR::TypeKind::Texture || type->kind == UGLIR::TypeKind::Sampler))
                    {
                        return makeValue(value.id, targetType, location);
                    }
                }
                const std::optional<UGLIR::ValueTypeDescription> sourceValueType = value.valueType.has_value() ? value.valueType : describeTypeName(value.typeName);
                const std::optional<UGLIR::ValueTypeDescription> targetValueType = describeTypeName(targetType);
                const std::string canonicalTargetType = targetValueType.has_value() ? valueTypeKey(*targetValueType) : valueTypeKeyForName(targetType);
                const std::string canonicalSourceType = sourceValueType.has_value() ? valueTypeKey(*sourceValueType) : valueTypeKeyForName(value.typeName);
                if (canonicalSourceType == canonicalTargetType)
                {
                    return value;
                }
                const uint32_t targetWidth = valueTypeVectorWidth(canonicalTargetType);
                const uint32_t sourceWidth = valueTypeVectorWidth(canonicalSourceType);
                if (targetWidth <= 1u)
                {
                    if (sourceWidth > 1u)
                    {
                        const std::string sourceScalarType = valueTypeScalarKey(canonicalSourceType);
                        const uint32_t componentId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(sourceScalarType, location),
                            componentId,
                            value.id,
                            0u,
                        });
                        return convertScalarValueToType(context,
                                                        makeValue(componentId, sourceScalarType, location),
                                                        canonicalTargetType,
                                                        location);
                    }
                    return convertScalarValueToType(context, value, canonicalTargetType, location);
                }
                SPIRVValue result;
                if (adaptSameWidthVectorValue(context, value, result, canonicalSourceType, canonicalTargetType, location))
                {
                    return result;
                }
                if (adaptNarrowVectorValue(context, value, result, canonicalSourceType, canonicalTargetType, targetWidth, location))
                {
                    return result;
                }
                if (sourceWidth != 1u)
                {
                    addDiagnostic(location, "cannot adapt vector value from \"" + canonicalSourceType + "\" to \"" + canonicalTargetType + "\" for direct SPIR-V emission.");
                    return value;
                }

                return broadcastScalarValueToVector(context, value, canonicalTargetType, location);
            }            /** Adapts a logical value and encodes its buffer layout only at the final memory store. */
            void emitTypedStore(SPIRVFunctionContext &context,
                                const SPIRVValue &pointer,
                                const SPIRVValue &value,
                                const UGLIR::SourceLocation &location)
            {
                if (pointer.storageClass == spv::StorageClassUniform)
                {
                    addDiagnostic(location, "UniformBuffer values are read-only and cannot be written by direct SPIR-V emission.");
                    return;
                }
                const std::string valueTypeName = storageValueTypeName(pointer.typeName);
                const SPIRVValue adaptedValue = adaptValueToType(context, value, valueTypeName, location);
                const uint32_t storedId = emitLogicalTypeCopy(
                    context, adaptedValue.id, getTypeId(valueTypeName, location),
                    getMemoryValueTypeId(valueTypeName, pointer.storageClass, location));
                if (pointer.storageClass == spv::StorageClassPhysicalStorageBuffer)
                {
                    context.body.appendInstruction(spv::OpStore, {pointer.id, storedId,
                        static_cast<uint32_t>(spv::MemoryAccessAlignedMask), argumentMemoryAlignment(valueTypeName)});
                }
                else
                {
                    context.body.appendInstruction(spv::OpStore, {pointer.id, storedId});
                }
            }

            /** Returns the dominant arithmetic operand type for one binary operation. */
            std::string binaryOperandTypeName(const SPIRVValue &lhs, const SPIRVValue &rhs) const
            {
                const std::string lhsType = valueTypeKeyForName(lhs.typeName);
                const std::string rhsType = valueTypeKeyForName(rhs.typeName);
                if (valueTypeVectorWidth(lhsType) > 1u)
                {
                    return lhsType;
                }
                if (valueTypeVectorWidth(rhsType) > 1u)
                {
                    return rhsType;
                }
                if (isFloatScalarValue(lhsType) || isFloatScalarValue(rhsType))
                {
                    return floatTypeKey();
                }
                if (isHalfScalarValue(lhsType) || isHalfScalarValue(rhsType))
                {
                    return halfTypeKey();
                }
                return lhsType;
            }

            /** Extends a 2D coordinate with an array layer when a texture-array SPIR-V instruction needs it. */
            SPIRVValue emitImageCoordinate(SPIRVFunctionContext &context,
                                           const UGLIR::Expression &expression,
                                           const SPIRVResourceInfo &textureResource,
                                           size_t coordinateOperandIndex,
                                           size_t layerOperandIndex,
                                           UGLIR::ScalarKind componentScalarKind)
            {
                if (expression.operands.size() <= coordinateOperandIndex)
                {
                    addDiagnostic(expression.sourceLocation, "texture operation is missing its coordinate operand.");
                    return {};
                }
                const std::string componentType = valueTypeKey(makeScalarValueType(componentScalarKind));
                SPIRVValue coordinate = emitRValue(context, expression.operands[coordinateOperandIndex]);
                const bool isArrayTexture = textureResource.binding != nullptr && textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const uint32_t expectedWidth = isArrayTexture ? 3u : valueTypeVectorWidth(coordinate.typeName);
                const std::string targetCoordinateType = spvVectorTypeKey(componentScalarKind, expectedWidth);
                if (!isArrayTexture || valueTypeVectorWidth(coordinate.typeName) >= expectedWidth)
                {
                    return adaptValueToType(context, coordinate, targetCoordinateType, expression.sourceLocation);
                }

                coordinate = adaptValueToType(context,
                                              coordinate,
                                              vectorKeyForScalar(componentType, valueTypeVectorWidth(coordinate.typeName)),
                                              expression.sourceLocation);
                uint32_t zeroLayerId = getUIntConstant(0);
                if (componentScalarKind == UGLIR::ScalarKind::Float || componentScalarKind == UGLIR::ScalarKind::Half)
                {
                    zeroLayerId = componentScalarKind == UGLIR::ScalarKind::Half ? getHalfConstant("0.0") : getFloatConstant("0.0");
                }
                else if (componentScalarKind == UGLIR::ScalarKind::Int)
                {
                    zeroLayerId = getIntConstant(0);
                }
                SPIRVValue layerValue{
                    .id = zeroLayerId,
                    .typeName = componentType,
                };
                layerValue = attachValueType(std::move(layerValue));
                if (layerOperandIndex < expression.operands.size())
                {
                    layerValue = emitRValue(context, expression.operands[layerOperandIndex]);
                    layerValue = adaptValueToType(context, layerValue, componentType, expression.operands[layerOperandIndex].sourceLocation);
                }

                const uint32_t resultId = allocateId();
                const uint32_t component0 = allocateId();
                const uint32_t component1 = allocateId();
                const std::string sourceCoordinateComponentType = valueTypeScalarKey(coordinate.typeName);
                const uint32_t sourceComponentTypeId = getTypeId(sourceCoordinateComponentType, expression.sourceLocation);
                context.body.appendInstruction(spv::OpCompositeExtract, {sourceComponentTypeId, component0, coordinate.id, 0u});
                context.body.appendInstruction(spv::OpCompositeExtract, {sourceComponentTypeId, component1, coordinate.id, 1u});
                const SPIRVValue adaptedComponent0 = adaptValueToType(context,
                                                                       makeValue(component0, sourceCoordinateComponentType, expression.sourceLocation),
                                                                       componentType,
                                                                       expression.sourceLocation);
                const SPIRVValue adaptedComponent1 = adaptValueToType(context,
                                                                       makeValue(component1, sourceCoordinateComponentType, expression.sourceLocation),
                                                                       componentType,
                                                                       expression.sourceLocation);
                context.body.appendInstruction(spv::OpCompositeConstruct, {
                    getTypeId(targetCoordinateType, expression.sourceLocation),
                    resultId,
                    adaptedComponent0.id,
                    adaptedComponent1.id,
                    layerValue.id,
                });
                return makeValue(resultId, targetCoordinateType, expression.sourceLocation);
            }

            /** Returns true when an expression is the default zero offset used by texture gather wrappers. */
            bool isZeroGatherOffsetExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::Expression *candidate = &expression;
                while (candidate->kind == UGLIR::ExpressionKind::Cast && candidate->operands.size() == 1u)
                {
                    candidate = &candidate->operands.front();
                }
                if (candidate->kind != UGLIR::ExpressionKind::Construct)
                {
                    return false;
                }
                if (candidate->constructInfo.kind != UGLIR::ConstructKind::VectorSplat ||
                    candidate->constructInfo.targetComponentCount != 2u ||
                    candidate->operands.size() != 1u)
                {
                    return false;
                }
                const UGLIR::Expression &splatValue = candidate->operands.front();
                return splatValue.kind == UGLIR::ExpressionKind::Literal &&
                       splatValue.value == "0" &&
                       isSignedIntegerScalarValue(valueTypeKeyForName(splatValue.type));
            }

            /** Emits a literal value as a SPIR-V constant. */
            SPIRVValue emitLiteralExpression(const UGLIR::Expression &expression)
            {
                const std::string literalType = valueTypeKeyForName(expression.type);
                if (valueTypeScalarKind(literalType) == UGLIR::ScalarKind::Bool)
                {
                    return makeValue((expression.value == "true" || expression.value == "1") ? getTrueConstant() : getFalseConstant(),
                                     boolTypeKey(),
                                     expression.sourceLocation);
                }
                if (isUnsignedIntegerScalarValue(literalType))
                {
                    return makeValue(getUIntConstant(parseUnsignedLiteral(expression.value)), literalType, expression.sourceLocation);
                }
                if (isSignedIntegerScalarValue(literalType))
                {
                    return makeValue(getIntConstant(parseUnsignedLiteral(expression.value)), literalType, expression.sourceLocation);
                }
                if (isFloatLikeScalarValue(literalType))
                {
                    return makeValue(isHalfScalarValue(literalType) ? getHalfConstant(expression.value) : getFloatConstant(expression.value),
                                     literalType,
                                     expression.sourceLocation);
                }
                addDiagnostic(expression.sourceLocation, "unsupported literal type \"" + expression.type + "\" for direct SPIR-V emission.");
                return makeValue(getUIntConstant(0), "u32", expression.sourceLocation);
            }

            /** Emits a declaration reference as a parameter value or a local pointer. */
            SPIRVValue emitDeclRefExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (const auto parameterIter = context.parameterValues.find(expression.name); parameterIter != context.parameterValues.end())
                {
                    return parameterIter->second;
                }
                if (const auto localIter = context.localPointers.find(expression.name); localIter != context.localPointers.end())
                {
                    return localIter->second;
                }
                if (mModule.reflection.arguments.enabled)
                {
                    if (const auto *resource = findResourceForExpression(context, expression))
                    {
                        return emitArgumentValue(context, expression.type, resource->binding->name);
                    }
                    for (const auto &parameter : mModule.reflection.arguments.parameters)
                    {
                        if (parameter.name == expression.name)
                        {
                            return emitArgumentValue(context, parameter.type, parameter.name);
                        }
                    }
                }
                if (const auto aliasIter = context.resourceAliases.find(expression.name); aliasIter != context.resourceAliases.end())
                {
                    const SPIRVResourceInfo *resource = aliasIter->second;
                    SPIRVValue value = makePointerValue(resource->variableId,
                                                        expression.type,
                                                        storageClassForResource(*resource),
                                                        expression.sourceLocation,
                                                        false);
                    value.isResourceRoot = true;
                    return value;
                }
                addDiagnostic(expression.sourceLocation, "unknown declaration reference \"" + expression.name + "\" in direct SPIR-V emission.");
                return {};
            }

            /** Finds a field on a user struct type and returns its declaration order index. */
            const UGLIR::TypeField *findStructField(const std::string &typeName, const std::string &fieldName, uint32_t &fieldIndex) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr || type->kind != UGLIR::TypeKind::Struct)
                {
                    return nullptr;
                }
                for (uint32_t index = 0; index < type->fields.size(); ++index)
                {
                    if (type->fields[index].name == fieldName)
                    {
                        fieldIndex = index;
                        return &type->fields[index];
                    }
                }
                return nullptr;
            }

            /** Returns a field index in a struct type or records a diagnostic when the field is missing. */
            uint32_t getStructFieldIndex(const std::string &typeName,
                                         const std::string &fieldName,
                                         const UGLIR::SourceLocation &location)
            {
                uint32_t fieldIndex = 0;
                if (findStructField(typeName, fieldName, fieldIndex) != nullptr)
                {
                    return fieldIndex;
                }
                addDiagnostic(location, "struct type \"" + typeName + "\" does not contain field \"" + fieldName + "\".");
                return 0;
            }

            /** Finds a reflected stage input or output variable by storage class, field name, and semantic. */
            const SPIRVStageIOInfo *findStageIOVariable(spv::StorageClass storageClass,
                                                        const std::string &name,
                                                        UGLIR::BuiltinSemanticKind semanticKind,
                                                        uint32_t semanticIndex) const
            {
                for (const SPIRVStageIOInfo &info : mStageIOVariables)
                {
                    if (info.binding == nullptr || info.storageClass != storageClass)
                    {
                        continue;
                    }
                    const bool semanticMatches = semanticKind == UGLIR::BuiltinSemanticKind::None ||
                                                 (info.binding->semanticKind == semanticKind && info.binding->semanticIndex == semanticIndex);
                    if (info.binding->name == name && semanticMatches)
                    {
                        return &info;
                    }
                }
                for (const SPIRVStageIOInfo &info : mStageIOVariables)
                {
                    if (info.binding == nullptr || info.storageClass != storageClass)
                    {
                        continue;
                    }
                    if (info.binding->name == name)
                    {
                        return &info;
                    }
                }
                return nullptr;
            }

            /** Emits a pointer to a field inside a local or parameter struct value. */
            SPIRVValue emitStructMemberPointer(SPIRVFunctionContext &context,
                                                const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    return {};
                }
                const SPIRVValue basePointer = emitMemberBasePointer(context, expression.operands.front());
                if (!basePointer.isPointer)
                {
                    return {};
                }
                const std::vector<uint32_t> componentIndices = getVectorComponentIndices(expression.name);
                if (componentIndices.size() == 1u && valueTypeVectorWidth(basePointer.typeName) > 1u)
                {
                    return emitMemberPointer(context, basePointer,
                        valueTypeScalarKey(basePointer.typeName), componentIndices.front(),
                        expression.sourceLocation);
                }
                uint32_t fieldIndex = 0;
                const UGLIR::TypeField *field = findStructField(basePointer.typeName, expression.name, fieldIndex);
                if (field == nullptr)
                {
                    return {};
                }
                return emitMemberPointer(context, basePointer, field->type, fieldIndex, field->sourceLocation);
            }

            /** Resolves an assignable member base while preserving its original memory class. */
            SPIRVValue emitMemberBasePointer(SPIRVFunctionContext &context,
                                             const UGLIR::Expression &expression)
            {
                SPIRVValue basePointer;
                if (expression.kind == UGLIR::ExpressionKind::DeclRef ||
                    expression.kind == UGLIR::ExpressionKind::Subscript ||
                    expression.kind == UGLIR::ExpressionKind::MemberRef ||
                    expression.kind == UGLIR::ExpressionKind::Cast ||
                    expression.kind == UGLIR::ExpressionKind::Load ||
                    expression.kind == UGLIR::ExpressionKind::Construct)
                {
                    basePointer = emitLValue(context, expression);
                }
                if (!basePointer.isPointer)
                {
                    basePointer = emitExpression(context, expression);
                }
                return basePointer;
            }

            /** Addresses a struct member or vector component in its parent's memory representation. */
            SPIRVValue emitMemberPointer(SPIRVFunctionContext &context,
                                         const SPIRVValue &basePointer,
                                         const std::string &memberTypeName,
                                         uint32_t memberIndex,
                                         const UGLIR::SourceLocation &location)
            {
                const uint32_t memberTypeId = getMemoryValueTypeId(
                    memberTypeName, basePointer.storageClass, location);
                const uint32_t pointerTypeId = getPointerTypeId(memberTypeId, basePointer.storageClass);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    pointerTypeId, resultId, basePointer.id, getUIntConstant(memberIndex),
                });
                return makePointerValue(resultId, memberTypeName, basePointer.storageClass, location, false);
            }

            /** Identifies buffer or workgroup storage without evaluating the expression or following copied values. */
            bool isStorageAccessExpression(const SPIRVFunctionContext &context, const UGLIR::Expression &expression) const
            {
                if (const SPIRVResourceInfo *resource = findResourceForExpression(context, expression);
                    resource != nullptr && resource->binding != nullptr && resource->binding->kind == UGLIR::ResourceKind::StorageBuffer)
                    return true;
                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    const auto pointer = context.localPointers.find(expression.name);
                    return pointer != context.localPointers.end() && pointer->second.storageClass == spv::StorageClassWorkgroup;
                }
                if (expression.operands.empty()) return false;
                if (expression.kind == UGLIR::ExpressionKind::MemberRef)
                {
                    if (valueTypeVectorWidth(valueTypeKeyForName(expression.operands.front().type)) > 1u && expression.name.size() != 1u)
                        return false;
                    return isStorageAccessExpression(context, expression.operands.front());
                }
                if (expression.kind == UGLIR::ExpressionKind::Subscript || expression.kind == UGLIR::ExpressionKind::Load ||
                    (expression.kind == UGLIR::ExpressionKind::Cast &&
                     valueTypeKeyForName(expression.type) == valueTypeKeyForName(expression.operands.front().type)))
                    return isStorageAccessExpression(context, expression.operands.front());
                return false;
            }

            /** Emits a member reference, including exact integer storage loads, component extraction, and resource path folding. */
            SPIRVValue emitMemberRefExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (mModule.reflection.arguments.enabled && !expression.operands.empty())
                {
                    if (const auto *bufferType = findType(expression.operands.front().type);
                        bufferType != nullptr && bufferType->kind == UGLIR::TypeKind::Buffer && bufferType->resourceKind == UGLIR::ResourceKind::UniformBuffer)
                    {
                        const auto base = emitArgumentBufferPointer(context, expression.operands.front(), getUIntConstant(0));
                        uint32_t fieldIndex = 0;
                        if (const auto *field = findStructField(base.typeName, expression.name, fieldIndex))
                        {
                            return loadIfPointer(context, emitMemberPointer(context, base, field->type, fieldIndex, expression.sourceLocation), expression.sourceLocation);
                        }
                    }
                }
                if (!mModule.reflection.arguments.enabled)
                {
                    if (const SPIRVResourceInfo *resource = findResourceForExpression(context, expression))
                    {
                        SPIRVValue value = makePointerValue(resource->variableId,
                                                            resource->binding->elementType,
                                                            storageClassForResource(*resource),
                                                            expression.sourceLocation,
                                                            false);
                        value.isResourceRoot = true;
                        return value;
                    }
                }
                if (expression.operands.empty())
                {
                    return makeValue(0, expression.type, expression.sourceLocation);
                }
	                if (const SPIRVResourceInfo *baseResource = findResourceForExpression(context, expression.operands.front());
	                    baseResource != nullptr &&
	                    baseResource->binding != nullptr &&
	                    baseResource->binding->kind == UGLIR::ResourceKind::UniformBuffer)
	                {
	                    return emitUniformBufferFieldLoad(context, expression.sourceLocation, *baseResource, expression.name);
	                }
                if (const auto valueType = UGLIR::describeValueType(mModule, expression.type);
                    !expression.name.empty() && valueType.has_value() && valueType->matrixColumns == 0u && valueType->bitWidth == 32u &&
                    (valueType->scalarKind == UGLIR::ScalarKind::Int || valueType->scalarKind == UGLIR::ScalarKind::UInt) &&
                    isStorageAccessExpression(context, expression.operands.front()) &&
                    (getVectorComponentIndices(expression.name).size() <= 1u || valueType->vectorWidth == 1u))
                    return loadIfPointer(context, emitStructMemberPointer(context, expression), expression.sourceLocation);
                SPIRVValue baseValue = emitExpression(context, expression.operands.front());
                if (expression.name.empty())
                {
                    return loadIfPointer(context, baseValue, expression.sourceLocation);
                }

                if (baseValue.isPointer)
                {
                    uint32_t fieldIndex = 0;
                    if (const UGLIR::TypeField *field = findStructField(baseValue.typeName, expression.name, fieldIndex))
                    {
                        const SPIRVValue memberPointer = emitStructMemberPointer(context, expression);
                        return loadIfPointer(context, memberPointer, field->sourceLocation);
                    }
                }
                else
                {
                    uint32_t fieldIndex = 0;
                    if (const UGLIR::TypeField *field = findStructField(baseValue.typeName, expression.name, fieldIndex))
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(field->type, field->sourceLocation),
                            resultId,
                            baseValue.id,
                            fieldIndex,
                        });
                        return makeValue(resultId, field->type, field->sourceLocation);
                    }
                }

                if (const SPIRVStageIOInfo *stageInput = findStageIOVariable(spv::StorageClassInput,
                                                                             expression.name,
                                                                             UGLIR::BuiltinSemanticKind::None,
                                                                             0u))
                {
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpLoad, {
                        getTypeId(stageInput->binding->type, expression.sourceLocation),
                        resultId,
                        stageInput->variableId,
                    });
                    return makeValue(resultId, stageInput->binding->type, expression.sourceLocation);
                }

                const std::vector<uint32_t> componentIndices = getVectorComponentIndices(expression.name);
                if (!componentIndices.empty())
                {
                    baseValue = loadIfPointer(context, baseValue, expression.sourceLocation);
                    const std::string componentTypeName = valueTypeScalarKey(baseValue.typeName);
                    const std::string resultTypeName = componentIndices.size() == 1u
                                                           ? componentTypeName
                                                           : vectorKeyForScalar(componentTypeName, static_cast<uint32_t>(componentIndices.size()));
                    const uint32_t resultId = allocateId();
                    if (componentIndices.size() == 1u)
                    {
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(resultTypeName, expression.sourceLocation),
                            resultId,
                            baseValue.id,
                            componentIndices.front(),
                        });
                    }
                    else
                    {
                        std::vector<uint32_t> operands{
                            getTypeId(resultTypeName, expression.sourceLocation),
                            resultId,
                            baseValue.id,
                            baseValue.id,
                        };
                        operands.insert(operands.end(), componentIndices.begin(), componentIndices.end());
                        context.body.appendInstruction(spv::OpVectorShuffle, operands);
                    }
                    return makeValue(resultId, resultTypeName, expression.sourceLocation);
                }

                addDiagnostic(expression.sourceLocation, "unsupported member reference \"" + expression.name + "\" for direct SPIR-V emission.");
                return {};
            }

            /** Emits a read from a fixed-size array SSA value without materializing it into Function storage. */
            SPIRVValue emitFixedArrayValueElementRead(SPIRVFunctionContext &context,
                                                      const SPIRVValue &arrayValue,
                                                      const UGLIR::Type &arrayType,
                                                      const UGLIR::Expression &indexExpression,
                                                      const UGLIR::SourceLocation &location)
            {
                if (arrayType.elementType.empty() || arrayType.arrayCount == 0)
                {
                    addDiagnostic(location, "fixed array value subscript requires a known element type and element count.");
                    return {};
                }
                const std::string elementValueType = storageValueTypeName(arrayType.elementType);
                const uint32_t elementTypeId = getTypeId(elementValueType, location);
                if (const std::optional<uint32_t> literalIndex = getSwitchCaseLiteral(indexExpression))
                {
                    if (*literalIndex >= arrayType.arrayCount)
                    {
                        addDiagnostic(indexExpression.sourceLocation, "fixed array value subscript index is out of bounds.");
                        return {};
                    }
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract, {
                        elementTypeId,
                        resultId,
                        arrayValue.id,
                        *literalIndex,
                    });
                    return makeValue(resultId, elementValueType, location);
                }

                SPIRVValue index = emitRValue(context, indexExpression);
                index = adaptValueToType(context, index, "u32", indexExpression.sourceLocation);
                std::vector<uint32_t> caseLabels;
                for (uint32_t arrayIndex = 0u; arrayIndex < arrayType.arrayCount; ++arrayIndex) { caseLabels.push_back(allocateId()); }
                const uint32_t mergeLabel = allocateId();
                std::vector<uint32_t> switchOperands{index.id, caseLabels.front()};
                for (uint32_t arrayIndex = 1u; arrayIndex < arrayType.arrayCount; ++arrayIndex)
                {
                    switchOperands.push_back(arrayIndex);
                    switchOperands.push_back(caseLabels[arrayIndex]);
                }
                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                context.body.appendInstruction(spv::OpSwitch, switchOperands);
                context.blockTerminated = true;

                // Phi accepts aggregate values in SPIR-V 1.3; OpSelect only accepts scalar or vector results.
                // The default arm preserves the existing element-zero result for an out-of-range dynamic index.
                const uint32_t resultId = allocateId();
                std::vector<uint32_t> phiOperands{elementTypeId, resultId};
                for (uint32_t arrayIndex = 0u; arrayIndex < arrayType.arrayCount; ++arrayIndex)
                {
                    beginBlock(context, caseLabels[arrayIndex]);
                    const uint32_t candidateId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract, {
                        elementTypeId,
                        candidateId,
                        arrayValue.id,
                        arrayIndex,
                    });
                    phiOperands.push_back(candidateId);
                    phiOperands.push_back(caseLabels[arrayIndex]);
                    branchTo(context, mergeLabel);
                }
                beginBlock(context, mergeLabel);
                context.body.appendInstruction(spv::OpPhi, phiOperands);
                return makeValue(resultId, elementValueType, location);
            }

            /** Emits a subscript expression by loading from the subscript lvalue pointer. */
            SPIRVValue emitSubscriptExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() >= 2u)
                {
                    if (expression.operands[0].kind == UGLIR::ExpressionKind::DeclRef &&
                        context.resourceAliasStates.find(expression.operands[0].name) != context.resourceAliasStates.end())
                    {
                        return emitResourceAliasSubscriptLoad(context,
                                                              expression.operands[0].name,
                                                              expression.operands[1],
                                                              expression.sourceLocation);
                    }

                    if ((mModule.reflection.arguments.enabled && isArgumentBufferExpression(context, expression.operands[0])) ||
                        findResourceForExpression(context, expression.operands[0]) != nullptr)
                    {
                        return loadIfPointer(context, emitSubscriptPointer(context, expression), expression.sourceLocation);
                    }

                    const UGLIR::Type *baseExpressionType = findType(expression.operands[0].type);
                    if (baseExpressionType == nullptr)
                    {
                        baseExpressionType = findType(valueTypeKeyForName(expression.operands[0].type));
                    }
                    if (baseExpressionType != nullptr && baseExpressionType->kind == UGLIR::TypeKind::Vector &&
                        (baseExpressionType->scalarKind == UGLIR::ScalarKind::Int || baseExpressionType->scalarKind == UGLIR::ScalarKind::UInt) &&
                        isStorageAccessExpression(context, expression.operands[0]))
                        return loadIfPointer(context, emitSubscriptPointer(context, expression), expression.sourceLocation);
                    if (baseExpressionType != nullptr && baseExpressionType->kind == UGLIR::TypeKind::Array)
                    {
                        SPIRVValue baseValue = emitExpression(context, expression.operands[0]);
                        if (!baseValue.isPointer)
                        {
                            return emitFixedArrayValueElementRead(context,
                                                                  baseValue,
                                                                  *baseExpressionType,
                                                                  expression.operands[1],
                                                                  expression.sourceLocation);
                        }
                        return loadIfPointer(context, emitSubscriptPointer(context, expression), expression.sourceLocation);
                    }

                    SPIRVValue baseValue = emitRValue(context, expression.operands[0]);
                    if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(baseValue.typeName);
                        matrixShape.has_value())
                    {
                        const std::optional<uint32_t> rowIndex = getSwitchCaseLiteral(expression.operands[1]);
                        if (rowIndex.has_value())
                        {
                            return emitMatrixRowValue(context, baseValue, *rowIndex, expression.sourceLocation);
                        }

                        const SPIRVValue dynamicRowIndex = emitRValue(context, expression.operands[1]);
                        if (dynamicRowIndex.id == 0u)
                        {
                            return {};
                        }
                        std::vector<uint32_t> rowComponents;
                        rowComponents.reserve(matrixShape->columnCount);
                        for (uint32_t columnIndex = 0u; columnIndex < matrixShape->columnCount; ++columnIndex)
                        {
                            const SPIRVValue columnValue = emitMatrixColumnValue(context,
                                                                                    baseValue,
                                                                                    columnIndex,
                                                                                    expression.sourceLocation);
                            if (columnValue.id == 0u)
                            {
                                return {};
                            }
                            const uint32_t componentId = allocateId();
                            context.body.appendInstruction(spv::OpVectorExtractDynamic, {
                                getTypeId(matrixShape->scalarTypeName, expression.sourceLocation),
                                componentId,
                                columnValue.id,
                                dynamicRowIndex.id,
                            });
                            rowComponents.push_back(componentId);
                        }
                        return emitVectorValueFromComponents(context,
                                                             matrixShape->rowVectorTypeName,
                                                             rowComponents,
                                                             expression.sourceLocation);
                    }

                    const uint32_t vectorWidth = valueTypeVectorWidth(baseValue.typeName);
                    if (vectorWidth > 1u)
                    {
                        const std::string resultType = valueTypeScalarKey(baseValue.typeName);
                        const uint32_t resultId = allocateId();
                        if (const std::optional<uint32_t> literalIndex = getSwitchCaseLiteral(expression.operands[1]))
                        {
                            context.body.appendInstruction(spv::OpCompositeExtract, {
                                getTypeId(resultType, expression.sourceLocation),
                                resultId,
                                baseValue.id,
                                *literalIndex,
                            });
                        }
                        else
                        {
                            const SPIRVValue index = emitRValue(context, expression.operands[1]);
                            context.body.appendInstruction(spv::OpVectorExtractDynamic, {
                                getTypeId(resultType, expression.sourceLocation),
                                resultId,
                                baseValue.id,
                                index.id,
                            });
                        }
                        return makeValue(resultId, resultType, expression.sourceLocation);
                    }
                }
                return loadIfPointer(context, emitSubscriptPointer(context, expression), expression.sourceLocation);
            }

            /** Loads one element from a concrete storage buffer resource. */
            SPIRVValue emitStorageBufferElementLoad(SPIRVFunctionContext &context,
                                                    const SPIRVResourceInfo &resource,
                                                    const SPIRVValue &index,
                                                    const UGLIR::SourceLocation &location)
            {
                return emitStorageBufferElementLoad(context, resource, index.id, location);
            }

            /** Emits a load through a resource alias whose concrete resource is selected at runtime. */
            SPIRVValue emitResourceAliasSubscriptLoad(SPIRVFunctionContext &context,
                                                      const std::string &aliasName,
                                                      const UGLIR::Expression &indexExpression,
                                                      const UGLIR::SourceLocation &location)
            {
                auto stateIter = context.resourceAliasStates.find(aliasName);
                if (stateIter == context.resourceAliasStates.end() || stateIter->second.resources.empty())
                {
                    addDiagnostic(location, "resource alias \"" + aliasName + "\" was used before it was assigned.");
                    return {};
                }

                const SPIRVResourceAliasState &state = stateIter->second;
                const SPIRVResourceInfo *firstResource = state.resources.front();
                if (firstResource == nullptr || firstResource->binding == nullptr)
                {
                    addDiagnostic(location, "resource alias \"" + aliasName + "\" has an invalid first resource.");
                    return {};
                }
                for (const SPIRVResourceInfo *resource : state.resources)
                {
                    if (resource == nullptr || resource->binding == nullptr ||
                        resource->binding->kind != UGLIR::ResourceKind::StorageBuffer ||
                        resource->binding->elementType != firstResource->binding->elementType)
                    {
                        addDiagnostic(location, "resource alias \"" + aliasName + "\" mixes incompatible storage buffer resources.");
                        return {};
                    }
                }

                const SPIRVValue index = emitRValue(context, indexExpression);
                if (state.resources.size() == 1u)
                {
                    return emitStorageBufferElementLoad(context, *firstResource, index, location);
                }

                const uint32_t selectorId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {
                    getUIntTypeId(),
                    selectorId,
                    state.selectorPointerId,
                });

                std::vector<uint32_t> caseLabels;
                caseLabels.reserve(state.resources.size());
                for (size_t indexValue = 0; indexValue < state.resources.size(); ++indexValue)
                {
                    caseLabels.push_back(allocateId());
                }
                const uint32_t mergeLabel = allocateId();
                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                std::vector<uint32_t> switchOperands{selectorId, caseLabels.front()};
                for (uint32_t indexValue = 0; indexValue < caseLabels.size(); ++indexValue)
                {
                    switchOperands.push_back(indexValue);
                    switchOperands.push_back(caseLabels[indexValue]);
                }
                context.body.appendInstruction(spv::OpSwitch, switchOperands);
                context.blockTerminated = true;

                std::vector<SPIRVValue> loadedValues;
                loadedValues.reserve(state.resources.size());
                for (size_t indexValue = 0; indexValue < state.resources.size(); ++indexValue)
                {
                    beginBlock(context, caseLabels[indexValue]);
                    loadedValues.push_back(emitStorageBufferElementLoad(context, *state.resources[indexValue], index, location));
                    branchTo(context, mergeLabel);
                }

                beginBlock(context, mergeLabel);
                const uint32_t resultId = allocateId();
                std::vector<uint32_t> phiOperands{
                    getTypeId(firstResource->binding->elementType, location),
                    resultId,
                };
                for (size_t indexValue = 0; indexValue < loadedValues.size(); ++indexValue)
                {
                    phiOperands.push_back(loadedValues[indexValue].id);
                    phiOperands.push_back(caseLabels[indexValue]);
                }
                context.body.appendInstruction(spv::OpPhi, phiOperands);
                return makeValue(resultId, firstResource->binding->elementType, location);
            }

            /** Emits a resource-access call (UniformBufferRead fast path plus the bound-resource texture/input-attachment intrinsics). Returns true when the kind matched and result was stored. */
            bool emitResourceAccessCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result)
            {
                if (mModule.reflection.arguments.enabled && expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::UniformBufferRead && !expression.operands.empty())
                {
                    result = loadIfPointer(context, emitArgumentBufferPointer(context, expression.operands.front(), getUIntConstant(0)), expression.sourceLocation);
                    return true;
                }
                switch (expression.intrinsicCallKind)
                {
                case UGLIR::IntrinsicCallKind::UniformBufferRead:
                case UGLIR::IntrinsicCallKind::InputAttachmentRead:
                case UGLIR::IntrinsicCallKind::TextureRead:
                case UGLIR::IntrinsicCallKind::TextureWrite:
                case UGLIR::IntrinsicCallKind::TextureSample:
                case UGLIR::IntrinsicCallKind::TextureSampleLevel:
                case UGLIR::IntrinsicCallKind::TextureSampleGrad:
                case UGLIR::IntrinsicCallKind::TextureGather:
                case UGLIR::IntrinsicCallKind::TextureGatherRed:
                case UGLIR::IntrinsicCallKind::TextureGatherGreen:
                case UGLIR::IntrinsicCallKind::TextureGatherBlue:
                case UGLIR::IntrinsicCallKind::TextureGatherAlpha:
                case UGLIR::IntrinsicCallKind::TextureGetDimensions:
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "resource intrinsic call is missing its resource operand.");
                        result = {};
                        return true;
                    }
                    if (const SPIRVResourceInfo *resource = mModule.reflection.arguments.enabled &&
                        expression.intrinsicCallKind != UGLIR::IntrinsicCallKind::InputAttachmentRead ?
                        findOpaqueArgumentResource(expression.operands.front().type) : findResourceForExpression(context, expression.operands.front()))
                    {
                        if (resource->binding != nullptr &&
                            resource->binding->kind == UGLIR::ResourceKind::UniformBuffer &&
                            (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::UniformBufferRead ||
                             expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureRead))
                        {
                            result = emitUniformBufferReadCall(context, expression, *resource);
                            return true;
                        }
                        if (resource->binding != nullptr &&
                            resource->binding->kind == UGLIR::ResourceKind::InputAttachment &&
                            (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::InputAttachmentRead ||
                             expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureRead))
                        {
                            result = emitPixelLocalInputAttachmentReadCall(context, expression, *resource);
                            return true;
                        }
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureWrite)
                        {
                            result = emitStorageImageWriteCall(context, expression, *resource);
                            return true;
                        }
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureSample ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureSampleLevel ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureSampleGrad)
                        {
                            result = emitTextureSampleCall(context, expression, *resource);
                            return true;
                        }
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGather ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGatherRed ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGatherGreen ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGatherBlue ||
                            expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGatherAlpha)
                        {
                            result = emitTextureGatherCall(context, expression, *resource);
                            return true;
                        }
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureRead)
                        {
                            result = emitTextureReadCall(context, expression, *resource);
                            return true;
                        }
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureGetDimensions)
                        {
                            result = emitTextureDimensionsCall(context, expression, *resource);
                            return true;
                        }
                    }
                    addDiagnostic(expression.sourceLocation, "resource intrinsic call could not resolve a reflected resource operand.");
                    result = {};
                    return true;
                }
                return false;
            }

            /** Emits the non-resource non-math intrinsic calls (discard, clip, barriers, atomics, wave, bitcast, min/max). Returns true when the kind matched and result was stored. */
            bool emitNonMathIntrinsicCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result)
            {
                switch (expression.intrinsicCallKind)
                {
                case UGLIR::IntrinsicCallKind::DiscardFragment:
                    context.body.appendInstruction(spv::OpKill, {});
                    context.blockTerminated = true;
                    result = makeVoidValue();
                    return true;
                case UGLIR::IntrinsicCallKind::Clip:
                    result = emitClipCall(context, expression);
                    return true;
                case UGLIR::IntrinsicCallKind::GroupMemoryBarrier:
                case UGLIR::IntrinsicCallKind::GroupMemoryBarrierWithGroupSync:
                case UGLIR::IntrinsicCallKind::DeviceMemoryBarrier:
                case UGLIR::IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync:
                case UGLIR::IntrinsicCallKind::AllMemoryBarrier:
                case UGLIR::IntrinsicCallKind::AllMemoryBarrierWithGroupSync:
                    emitBarrierCall(context, expression);
                    result = makeVoidValue();
                    return true;
                case UGLIR::IntrinsicCallKind::AtomicAdd:
                case UGLIR::IntrinsicCallKind::AtomicAnd:
                case UGLIR::IntrinsicCallKind::AtomicOr:
                case UGLIR::IntrinsicCallKind::AtomicMin:
                case UGLIR::IntrinsicCallKind::AtomicMax:
                case UGLIR::IntrinsicCallKind::AtomicLoad:
                case UGLIR::IntrinsicCallKind::AtomicStore:
                case UGLIR::IntrinsicCallKind::AtomicCompareExchange:
                    result = emitAtomicCall(context, expression);
                    return true;
                case UGLIR::IntrinsicCallKind::WaveGetLaneIndex:
                case UGLIR::IntrinsicCallKind::WaveGetLaneCount:
                case UGLIR::IntrinsicCallKind::WaveActiveCountBits:
                case UGLIR::IntrinsicCallKind::WavePrefixCountBits:
                case UGLIR::IntrinsicCallKind::WavePrefixSum:
                case UGLIR::IntrinsicCallKind::WaveReadLaneAt:
                case UGLIR::IntrinsicCallKind::WaveReadLaneFirst:
                case UGLIR::IntrinsicCallKind::WaveActiveBallot:
                case UGLIR::IntrinsicCallKind::WaveMatch:
                case UGLIR::IntrinsicCallKind::QuadReadLaneAt:
                case UGLIR::IntrinsicCallKind::QuadReadAcrossX:
                case UGLIR::IntrinsicCallKind::QuadReadAcrossY:
                case UGLIR::IntrinsicCallKind::QuadReadAcrossDiagonal:
                    result = emitWaveIntrinsicCall(context, expression);
                    return true;
                case UGLIR::IntrinsicCallKind::BitcastAsFloat:
                case UGLIR::IntrinsicCallKind::BitcastAsUInt:
                case UGLIR::IntrinsicCallKind::BitcastAsInt:
                    result = emitBitcastIntrinsicCall(context, expression);
                    return true;
                case UGLIR::IntrinsicCallKind::MathMin:
                case UGLIR::IntrinsicCallKind::MathMax:
                    result = emitMinMaxCall(context, expression);
                    return true;
                }
                return false;
            }

            /** Emits the math intrinsic calls delegated to emitMathIntrinsicCall. Returns true when the kind matched and result was stored. */
            bool emitMathCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result)
            {
                switch (expression.intrinsicCallKind)
                {
                case UGLIR::IntrinsicCallKind::MathAbs:
                case UGLIR::IntrinsicCallKind::MathAcos:
                case UGLIR::IntrinsicCallKind::MathAll:
                case UGLIR::IntrinsicCallKind::MathAny:
                case UGLIR::IntrinsicCallKind::MathAsin:
                case UGLIR::IntrinsicCallKind::MathAtan:
                case UGLIR::IntrinsicCallKind::MathAtan2:
                case UGLIR::IntrinsicCallKind::MathCeil:
                case UGLIR::IntrinsicCallKind::MathClamp:
                case UGLIR::IntrinsicCallKind::MathCos:
                case UGLIR::IntrinsicCallKind::MathCross:
                case UGLIR::IntrinsicCallKind::MathDdx:
                case UGLIR::IntrinsicCallKind::MathDdy:
                case UGLIR::IntrinsicCallKind::MathDistance:
                case UGLIR::IntrinsicCallKind::MathDot:
                case UGLIR::IntrinsicCallKind::MathExp:
                case UGLIR::IntrinsicCallKind::MathExp2:
                case UGLIR::IntrinsicCallKind::MathFloor:
                case UGLIR::IntrinsicCallKind::MathFrac:
                case UGLIR::IntrinsicCallKind::MathFmod:
                case UGLIR::IntrinsicCallKind::MathFirstBitHigh:
                case UGLIR::IntrinsicCallKind::MathFirstBitLow:
                case UGLIR::IntrinsicCallKind::MathLength:
                case UGLIR::IntrinsicCallKind::MathLerp:
                case UGLIR::IntrinsicCallKind::MathLog:
                case UGLIR::IntrinsicCallKind::MathLog2:
                case UGLIR::IntrinsicCallKind::MathModf:
                case UGLIR::IntrinsicCallKind::MathMul:
                case UGLIR::IntrinsicCallKind::MathNormalize:
                case UGLIR::IntrinsicCallKind::MathPow:
                case UGLIR::IntrinsicCallKind::MathReflect:
                case UGLIR::IntrinsicCallKind::MathRound:
                case UGLIR::IntrinsicCallKind::MathRsqrt:
                case UGLIR::IntrinsicCallKind::MathSaturate:
                case UGLIR::IntrinsicCallKind::MathSign:
                case UGLIR::IntrinsicCallKind::MathSin:
                case UGLIR::IntrinsicCallKind::MathSincos:
                case UGLIR::IntrinsicCallKind::MathSqrt:
                case UGLIR::IntrinsicCallKind::MathStep:
                case UGLIR::IntrinsicCallKind::MathSmoothstep:
                case UGLIR::IntrinsicCallKind::MathTan:
                case UGLIR::IntrinsicCallKind::MathTranspose:
                    result = emitMathIntrinsicCall(context, expression);
                    return true;
                }
                return false;
            }

            /** Emits a user-function call after intrinsic dispatch; emits the function-call instruction and any pointer-argument copy-backs. Returns true when the value was stored in result. */
            bool emitFunctionCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result)
            {
                if (mFunctionsByName.find(expression.name) == mFunctionsByName.end() &&
                    (isOperatorHelperCallName(expression.name) ||
                     expression.name == "+" || expression.name == "-" || expression.name == "*" || expression.name == "/"))
                {
                    result = emitOperatorFunctionCall(context, expression);
                    return true;
                }
                const auto iter = mFunctionsByName.find(expression.name);
                if (iter == mFunctionsByName.end())
                {
                    addDiagnostic(expression.sourceLocation, "unsupported function call \"" + expression.name + "\" for direct SPIR-V emission.");
                    result = {};
                    return true;
                }
                std::vector<uint32_t> operands;
                const std::string resultType = expression.type.empty() || findType(expression.type) == nullptr
                                                   ? iter->second.function->returnType
                                                   : expression.type;
                const uint32_t resultId = allocateId();
                operands.push_back(getTypeId(resultType, expression.sourceLocation));
                operands.push_back(resultId);
                operands.push_back(iter->second.functionId);
                size_t emittedArgumentIndex = 0;
                size_t parameterIndex = 0;
                std::vector<SPIRVPointerArgumentCopyBack> pointerArgumentCopyBacks;
                for (const UGLIR::Expression &argument : expression.operands)
                {
                    const UGLIR::FunctionParameter *targetParameter =
                        parameterIndex < iter->second.function->parameters.size()
                            ? &iter->second.function->parameters[parameterIndex]
                            : nullptr;
                    ++parameterIndex;
                    if (isResourceAliasType(argument.type))
                    {
                        continue;
                    }
                    const bool argumentIsPointer = emittedArgumentIndex < iter->second.parameterIsPointer.size() &&
                                                   iter->second.parameterIsPointer[emittedArgumentIndex];
                    if (argumentIsPointer)
                    {
                        const SPIRVValue argumentPointer = emitLValue(context, argument);
                        if (argumentPointer.id == 0 || !argumentPointer.isPointer)
                        {
                            addDiagnostic(argument.sourceLocation, "helper pointer argument is not assignable in direct SPIR-V emission.");
                            operands.push_back(0);
                        }
                        else if (targetParameter == nullptr)
                        {
                            addDiagnostic(argument.sourceLocation, "helper pointer argument is missing its target parameter metadata.");
                            operands.push_back(argumentPointer.id);
                        }
                        else
                        {
                            const bool ordinaryReferenceParameter = targetParameter->isReference &&
                                                                     targetParameter->passingMode == UGLIR::ParameterPassingMode::Value;
                            const bool ordinaryMutableReference = ordinaryReferenceParameter &&
                                                                  !targetParameter->isConstReference;
                            const bool explicitValueResultParameter =
                                targetParameter->passingMode == UGLIR::ParameterPassingMode::Out ||
                                targetParameter->passingMode == UGLIR::ParameterPassingMode::InOut;
                            if (ordinaryReferenceParameter &&
                                (!argumentPointer.isMemoryObjectDeclaration || argumentPointer.storageClass != spv::StorageClassFunction))
                            {
                                if (ordinaryMutableReference)
                                {
                                    addDiagnostic(argument.sourceLocation,
                                                  "ordinary mutable reference argument requires a function-local memory object; "
                                                  "a non-memory-object temporary would not preserve aliasing.");
                                }
                                else
                                {
                                    addDiagnostic(argument.sourceLocation,
                                                  "ordinary const reference argument requires a function-local memory object; "
                                                  "a temporary would not preserve aliasing.");
                                }
                                operands.push_back(argumentPointer.id);
                            }
                            else if (explicitValueResultParameter && argumentPointer.storageClass != spv::StorageClassFunction)
                            {
                                addDiagnostic(argument.sourceLocation,
                                              "OUT/INOUT argument cannot cross a non-function SPIR-V storage class; "
                                              "value-result copyback is supported only for function storage.");
                                operands.push_back(argumentPointer.id);
                            }
                            else if (argumentPointer.isMemoryObjectDeclaration)
                            {
                                operands.push_back(argumentPointer.id);
                            }
                            else
                            {
                                const std::string temporaryKey = makePointerArgumentTemporaryKey(expression.name,
                                                                                                 emittedArgumentIndex,
                                                                                                 argument,
                                                                                                 targetParameter->type);
                                const auto temporaryIter = context.pointerArgumentTemporaries.find(temporaryKey);
                                if (temporaryIter == context.pointerArgumentTemporaries.end())
                                {
                                    addDiagnostic(argument.sourceLocation, "helper pointer argument is missing its predeclared SPIR-V temporary.");
                                    operands.push_back(argumentPointer.id);
                                }
                                else
                                {
                                    const SPIRVValue argumentValue = adaptValueToType(context,
                                                                                      loadIfPointer(context, argumentPointer, argument.sourceLocation),
                                                                                      targetParameter->type,
                                                                                      argument.sourceLocation);
                                    emitTypedStore(context, temporaryIter->second, argumentValue, argument.sourceLocation);
                                    operands.push_back(temporaryIter->second.id);
                                    pointerArgumentCopyBacks.push_back(SPIRVPointerArgumentCopyBack{
                                        .destinationPointer = argumentPointer,
                                        .temporaryPointer = temporaryIter->second,
                                        .sourceLocation = argument.sourceLocation,
                                    });
                                }
                            }
                        }
                    }
                    else
                    {
                        SPIRVValue argumentValue;
                        if (targetParameter != nullptr)
                        {
                            if (const SPIRVResourceInfo *resource = findResourceForExpression(context, argument);
                                resource != nullptr &&
                                resource->binding != nullptr &&
                                resource->binding->kind == UGLIR::ResourceKind::UniformBuffer &&
                                resource->binding->elementType == targetParameter->type)
                            {
                                argumentValue = emitUniformBufferResourceLoad(context, argument.sourceLocation, *resource);
                            }
                        }
                        if (argumentValue.id == 0)
                        {
                            argumentValue = emitRValue(context, argument);
                        }
                        if (targetParameter != nullptr)
                        {
                            argumentValue = adaptValueToType(context, argumentValue, targetParameter->type, argument.sourceLocation);
                        }
                        operands.push_back(argumentValue.id);
                    }
                    ++emittedArgumentIndex;
                }
                context.body.appendInstruction(spv::OpFunctionCall, operands);
                for (const SPIRVPointerArgumentCopyBack &copyBack : pointerArgumentCopyBacks)
                {
                    emitTypedStore(context,
                                   copyBack.destinationPointer,
                                   loadIfPointer(context, copyBack.temporaryPointer, copyBack.sourceLocation),
                                   copyBack.sourceLocation);
                }
                result = makeValue(resultId, resultType, expression.sourceLocation);
                return true;
            }

            /** Emits one UGL call expression through the intrinsic dispatch groups, then the user-function path. */
            SPIRVValue emitCallExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                SPIRVValue result;
                if (emitResourceAccessCall(context, expression, result))
                {
                    return result;
                }
                if (emitNonMathIntrinsicCall(context, expression, result))
                {
                    return result;
                }
                if (emitMathCall(context, expression, result))
                {
                    return result;
                }
                if (emitFunctionCall(context, expression, result))
                {
                    return result;
                }
                return {};
            }

            /** Returns true when a lowered call name represents an overloaded operator helper. */
            bool isOperatorHelperCallName(const std::string &name) const
            {
                return name.rfind("operator", 0) == 0 || name.find("operator") != std::string::npos;
            }

            /** Emits a memory or control barrier for UGL barrier builtins. */
            void emitBarrierCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                const bool hasGroupSync = expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::GroupMemoryBarrierWithGroupSync ||
                                          expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync ||
                                          expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AllMemoryBarrierWithGroupSync;
                const bool deviceScope = expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::DeviceMemoryBarrier ||
                                         expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync ||
                                         expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AllMemoryBarrier ||
                                         expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AllMemoryBarrierWithGroupSync;
                const uint32_t executionScope = getUIntConstant(static_cast<uint32_t>(spv::ScopeWorkgroup));
                const uint32_t memoryScope = getUIntConstant(static_cast<uint32_t>(deviceScope ? spv::ScopeDevice : spv::ScopeWorkgroup));
                const uint32_t semantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsAcquireReleaseMask) |
                                                           static_cast<uint32_t>(deviceScope ? spv::MemorySemanticsUniformMemoryMask
                                                                                             : spv::MemorySemanticsWorkgroupMemoryMask));
                if (hasGroupSync)
                {
                    context.body.appendInstruction(spv::OpControlBarrier, {executionScope, memoryScope, semantics});
                    return;
                }
                context.body.appendInstruction(spv::OpMemoryBarrier, {memoryScope, semantics});
            }

            /** Returns true when the structured intrinsic requires subgroup SPIR-V support. */
            bool isWaveIntrinsicCallKind(UGLIR::IntrinsicCallKind intrinsicCallKind) const
            {
                return intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneIndex ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneCount ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveActiveCountBits ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WavePrefixCountBits ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WavePrefixSum ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveReadLaneAt ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveReadLaneFirst ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveActiveBallot ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveMatch ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::QuadReadLaneAt ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::QuadReadAcrossX ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::QuadReadAcrossY ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::QuadReadAcrossDiagonal;
            }

            /** Emits UGL wave/subgroup builtins using Vulkan subgroup SPIR-V instructions. */
            SPIRVValue emitWaveIntrinsicCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                const uint32_t subgroupScope = getUIntConstant(static_cast<uint32_t>(spv::ScopeSubgroup));
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneIndex)
                {
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpLoad, {
                        getUIntTypeId(),
                        resultId,
                        getSubgroupLocalInvocationIdVariableId(expression.sourceLocation),
                    });
                    return makeValue(resultId, "u32", expression.sourceLocation);
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneCount)
                {
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpLoad, {
                        getUIntTypeId(),
                        resultId,
                        getSubgroupSizeVariableId(expression.sourceLocation),
                    });
                    return makeValue(resultId, "u32", expression.sourceLocation);
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveActiveCountBits ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WavePrefixCountBits)
                {
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "wave count intrinsic requires a predicate operand.");
                        return {};
                    }
                    mCapabilityTracker.ensureGroupNonUniformBallotCapability();
                    const SPIRVValue predicate = emitRValue(context, expression.operands.front());
                    const uint32_t ballotId = allocateId();
                    context.body.appendInstruction(spv::OpGroupNonUniformBallot, {
                        getTypeId(spvVectorTypeKey(UGLIR::ScalarKind::UInt, 4u), expression.sourceLocation),
                        ballotId,
                        subgroupScope,
                        predicate.id,
                    });
                    const uint32_t resultId = allocateId();
                    const spv::GroupOperation operation = expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WavePrefixCountBits
                                                              ? spv::GroupOperationExclusiveScan
                                                              : spv::GroupOperationReduce;
                    context.body.appendInstruction(spv::OpGroupNonUniformBallotBitCount, {
                        getUIntTypeId(),
                        resultId,
                        subgroupScope,
                        static_cast<uint32_t>(operation),
                        ballotId,
                    });
                    return makeValue(resultId, "u32", expression.sourceLocation);
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveReadLaneAt)
                {
                    if (expression.operands.size() < 2u)
                    {
                        addDiagnostic(expression.sourceLocation, "WaveReadLaneAt requires value and lane operands.");
                        return {};
                    }
                    mCapabilityTracker.ensureGroupNonUniformShuffleCapability();
                    const SPIRVValue value = emitRValue(context, expression.operands[0]);
                    const SPIRVValue lane = emitRValue(context, expression.operands[1]);
                    const std::string resultType = valueTypeKeyForName(expression.type.empty() ? value.typeName : expression.type);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpGroupNonUniformShuffle, {
                        getTypeId(resultType, expression.sourceLocation),
                        resultId,
                        subgroupScope,
                        value.id,
                        lane.id,
                    });
                    return makeValue(resultId, resultType, expression.sourceLocation);
                }
                addDiagnostic(expression.sourceLocation, "unsupported wave or quad intrinsic \"" + expression.name + "\" for direct SPIR-V emission.");
                return {};
            }

            /** Emits an integer atomic operation for storage-buffer or workgroup lvalues. */
            SPIRVValue emitAtomicCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "atomic builtin is missing its target operand.");
                    return {};
                }
                const SPIRVValue pointer = emitLValue(context, expression.operands.front());
                if (pointer.id == 0u)
                {
                    return {};
                }
                if (pointer.storageClass != spv::StorageClassStorageBuffer && pointer.storageClass != spv::StorageClassPhysicalStorageBuffer && pointer.storageClass != spv::StorageClassWorkgroup)
                {
                    addDiagnostic(expression.operands.front().sourceLocation, "atomic targets must refer to storage-buffer or GroupShared storage; local values are not atomic storage.");
                    return {};
                }
                const std::string valueTypeName = storageValueTypeName(pointer.typeName);
                const uint32_t scope = getUIntConstant(static_cast<uint32_t>(pointer.storageClass == spv::StorageClassWorkgroup ? spv::ScopeWorkgroup : spv::ScopeDevice));
                const uint32_t memorySemanticMask = static_cast<uint32_t>(pointer.storageClass == spv::StorageClassWorkgroup
                                                                              ? spv::MemorySemanticsWorkgroupMemoryMask
                                                                              : spv::MemorySemanticsUniformMemoryMask);
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicLoad)
                {
                    const uint32_t semantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsAcquireMask) | memorySemanticMask);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpAtomicLoad, {getTypeId(valueTypeName, expression.sourceLocation), resultId, pointer.id, scope, semantics});
                    // Preserve acquire ordering when SPIR-V translators lower atomic instructions to relaxed Metal operations.
                    context.body.appendInstruction(spv::OpMemoryBarrier, {scope, semantics});
                    return makeValue(resultId, valueTypeName, expression.sourceLocation);
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicStore)
                {
                    if (expression.operands.size() < 2u)
                    {
                        addDiagnostic(expression.sourceLocation, "atomicStore is missing its value operand.");
                        return {};
                    }
                    const SPIRVValue value = adaptValueToType(context, emitRValue(context, expression.operands[1]), valueTypeName, expression.operands[1].sourceLocation);
                    const uint32_t semantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsReleaseMask) | memorySemanticMask);
                    context.body.appendInstruction(spv::OpMemoryBarrier, {scope, semantics});
                    context.body.appendInstruction(spv::OpAtomicStore, {pointer.id, scope, semantics, value.id});
                    return makeVoidValue();
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicCompareExchange)
                {
                    if (expression.operands.size() < 4u)
                    {
                        addDiagnostic(expression.sourceLocation, "atomicCompareExchange requires target, compare, value, and original-value operands.");
                        return {};
                    }
                    const SPIRVValue compareValue = adaptValueToType(context, emitRValue(context, expression.operands[1]), valueTypeName, expression.operands[1].sourceLocation);
                    const SPIRVValue desiredValue = adaptValueToType(context, emitRValue(context, expression.operands[2]), valueTypeName, expression.operands[2].sourceLocation);
                    const SPIRVValue originalPointer = emitLValue(context, expression.operands[3]);
                    const uint32_t equalSemantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsAcquireReleaseMask) | memorySemanticMask);
                    const uint32_t unequalSemantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsAcquireMask) | memorySemanticMask);
                    const uint32_t releaseSemantics = getUIntConstant(static_cast<uint32_t>(spv::MemorySemanticsReleaseMask) | memorySemanticMask);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpMemoryBarrier, {scope, releaseSemantics});
                    context.body.appendInstruction(spv::OpAtomicCompareExchange, {
                        getTypeId(valueTypeName, expression.sourceLocation),
                        resultId,
                        pointer.id,
                        scope,
                        equalSemantics,
                        unequalSemantics,
                        desiredValue.id,
                        compareValue.id,
                    });
                    context.body.appendInstruction(spv::OpMemoryBarrier, {scope, unequalSemantics});
                    emitTypedStore(context, originalPointer, makeValue(resultId, valueTypeName, expression.sourceLocation),
                        expression.operands[3].sourceLocation);
                    return makeVoidValue();
                }
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "atomic read-modify-write builtin is missing its value operand.");
                    return {};
                }
                const SPIRVValue value = adaptValueToType(context, emitRValue(context, expression.operands[1]), valueTypeName, expression.operands[1].sourceLocation);
                const uint32_t semantics = getUIntConstant(0u);
                spv::Op opcode = spv::OpAtomicIAdd;
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicAnd)
                {
                    opcode = spv::OpAtomicAnd;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicOr)
                {
                    opcode = spv::OpAtomicOr;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicMin)
                {
                    opcode = isSignedIntegerScalarValue(valueTypeKeyForName(valueTypeName)) ? spv::OpAtomicSMin : spv::OpAtomicUMin;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicMax)
                {
                    opcode = isSignedIntegerScalarValue(valueTypeKeyForName(valueTypeName)) ? spv::OpAtomicSMax : spv::OpAtomicUMax;
                }
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(opcode, {getTypeId(valueTypeName, expression.sourceLocation), resultId, pointer.id, scope, semantics, value.id});
                return makeValue(resultId, valueTypeName, expression.sourceLocation);
            }

            /** Emits one UGL bitcast helper as `OpBitcast`. */
            SPIRVValue emitBitcastIntrinsicCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "bitcast intrinsic \"" + expression.name + "\" requires one operand.");
                    return {};
                }
                const SPIRVValue value = emitRValue(context, expression.operands.front());
                const std::string resultType = valueTypeKeyForName(expression.type);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpBitcast, {
                    getTypeId(resultType, expression.sourceLocation),
                    resultId,
                    value.id,
                });
                return makeValue(resultId, resultType, expression.sourceLocation);
            }

            /** Emits one GLSL.std.450 extended instruction with the requested result type and operands. */
            SPIRVValue emitGLSLExtInst(SPIRVFunctionContext &context,
                                       const std::string &resultType,
                                       uint32_t instruction,
                                       const std::vector<SPIRVValue> &arguments,
                                       const UGLIR::SourceLocation &location)
            {
                const uint32_t resultId = allocateId();
                std::vector<uint32_t> operands{
                    getTypeId(resultType, location),
                    resultId,
                    getGLSLStd450ImportId(),
                    instruction,
                };
                for (const SPIRVValue &argument : arguments)
                {
                    operands.push_back(argument.id);
                }
                context.body.appendInstruction(spv::OpExtInst, operands);
                return makeValue(resultId, resultType, location);
            }

            /** Returns the matrix row-array type when a UGLIR struct is a lowered UGL matrix wrapper. */
            const UGLIR::Type *findMatrixRowArrayType(const std::string &matrixTypeName, uint32_t &fieldIndex) const
            {
                const UGLIR::Type *matrixType = findType(matrixTypeName);
                if (matrixType == nullptr || matrixType->kind != UGLIR::TypeKind::Struct)
                {
                    return nullptr;
                }
                const UGLIR::TypeField *field = findStructField(matrixTypeName, "m", fieldIndex);
                if (field == nullptr)
                {
                    return nullptr;
                }
                const UGLIR::Type *arrayType = findType(field->type);
                return arrayType != nullptr && arrayType->kind == UGLIR::TypeKind::Array ? arrayType : nullptr;
            }

            /** Returns the native SPIR-V matrix shape for one structured matrix value type. */
            std::optional<SPIRVMatrixShape> matrixShapeFromValueType(const UGLIR::ValueTypeDescription &valueType) const
            {
                if (valueType.matrixRows == 0u || valueType.matrixColumns == 0u || valueType.scalarKind == UGLIR::ScalarKind::None)
                {
                    return std::nullopt;
                }
                const std::string scalarTypeName = valueTypeKey(makeScalarValueType(valueType.scalarKind));
                return SPIRVMatrixShape{
                    .scalarTypeName = scalarTypeName,
                    .rowVectorTypeName = spvVectorTypeKey(valueType.scalarKind, valueType.matrixColumns),
                    .columnVectorTypeName = spvVectorTypeKey(valueType.scalarKind, valueType.matrixRows),
                    .rowCount = valueType.matrixRows,
                    .columnCount = valueType.matrixColumns,
                };
            }

            /** Returns the native SPIR-V matrix shape for a legacy lowered matrix wrapper struct. */
            std::optional<SPIRVMatrixShape> tryLegacyMatrixWrapperShape(const std::string &matrixTypeName) const
            {
                uint32_t rowArrayFieldIndex = 0;
                const UGLIR::Type *rowArrayType = findMatrixRowArrayType(matrixTypeName, rowArrayFieldIndex);
                if (rowArrayType == nullptr)
                {
                    return std::nullopt;
                }
                const std::string rowTypeName = valueTypeKeyForName(rowArrayType->elementType);
                const uint32_t rowCount = std::max<uint32_t>(1u, rowArrayType->arrayCount);
                const uint32_t columnCount = valueTypeVectorWidth(rowTypeName);
                if (rowCount < 2u || columnCount < 2u)
                {
                    return std::nullopt;
                }
                return SPIRVMatrixShape{
                    .scalarTypeName = valueTypeScalarKey(rowTypeName),
                    .rowVectorTypeName = rowTypeName,
                    .columnVectorTypeName = vectorKeyForScalar(valueTypeScalarKey(rowTypeName), rowCount),
                    .rowCount = rowCount,
                    .columnCount = columnCount,
                };
            }

            /** Returns the native SPIR-V matrix shape for a registered UGLIR type. */
            std::optional<SPIRVMatrixShape> matrixShapeForRegisteredType(const std::string &matrixTypeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeTypeName(matrixTypeName);
                if (valueType.has_value())
                {
                    return matrixShapeFromValueType(*valueType);
                }
                return tryLegacyMatrixWrapperShape(matrixTypeName);
            }

            /** Extracts one row vector from a lowered matrix value. */
            SPIRVValue emitMatrixRowValue(SPIRVFunctionContext &context,
                                          const SPIRVValue &matrixValue,
                                          uint32_t rowIndex,
                                          const UGLIR::SourceLocation &location)
            {
                const SPIRVValue loadedMatrix = loadIfPointer(context, matrixValue, location);
                const UGLIR::Type *directMatrixType = findType(loadedMatrix.typeName);
                if (directMatrixType != nullptr && directMatrixType->kind == UGLIR::TypeKind::Matrix)
                {
                    const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(loadedMatrix.typeName);
                    if (!matrixShape.has_value())
                    {
                        addDiagnostic(location, "matrix row extraction requires matrix shape metadata.");
                        return {};
                    }
                    if (rowIndex >= matrixShape->rowCount)
                    {
                        addDiagnostic(location, "matrix row extraction index is out of range.");
                        return {};
                    }
                    std::vector<uint32_t> rowComponents;
                    rowComponents.reserve(matrixShape->columnCount);
                    for (uint32_t columnIndex = 0; columnIndex < matrixShape->columnCount; ++columnIndex)
                    {
                        const uint32_t columnId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(matrixShape->columnVectorTypeName, location),
                            columnId,
                            loadedMatrix.id,
                            columnIndex,
                        });
                        const uint32_t componentId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(matrixShape->scalarTypeName, location),
                            componentId,
                            columnId,
                            rowIndex,
                        });
                        rowComponents.push_back(componentId);
                    }
                    return emitVectorValueFromComponents(context, matrixShape->rowVectorTypeName, rowComponents, location);
                }
                uint32_t rowArrayFieldIndex = 0;
                const UGLIR::Type *rowArrayType = findMatrixRowArrayType(loadedMatrix.typeName, rowArrayFieldIndex);
                if (rowArrayType == nullptr)
                {
                    addDiagnostic(location, "matrix row extraction requires a lowered matrix struct with row storage.");
                    return {};
                }
                const UGLIR::TypeField *rowArrayField = findStructField(loadedMatrix.typeName, "m", rowArrayFieldIndex);
                const uint32_t rowArrayId = allocateId();
                context.body.appendInstruction(spv::OpCompositeExtract, {
                    getTypeId(rowArrayField->type, location),
                    rowArrayId,
                    loadedMatrix.id,
                    rowArrayFieldIndex,
                });
                const uint32_t rowId = allocateId();
                context.body.appendInstruction(spv::OpCompositeExtract, {
                    getTypeId(rowArrayType->elementType, location),
                    rowId,
                    rowArrayId,
                    rowIndex,
                });
                return makeValue(rowId, rowArrayType->elementType, location);
            }

            /** Extracts one scalar component from a vector value. */
            SPIRVValue emitVectorComponentValue(SPIRVFunctionContext &context,
                                                const SPIRVValue &vectorValue,
                                                uint32_t componentIndex,
                                                const UGLIR::SourceLocation &location)
            {
                const SPIRVValue loadedVector = loadIfPointer(context, vectorValue, location);
                const std::string scalarTypeName = valueTypeScalarKey(loadedVector.typeName);
                const uint32_t componentId = allocateId();
                context.body.appendInstruction(spv::OpCompositeExtract, {
                    getTypeId(scalarTypeName, location),
                    componentId,
                    loadedVector.id,
                    componentIndex,
                });
                return makeValue(componentId, scalarTypeName, location);
            }

            /** Builds a vector value from scalar component ids. */
            SPIRVValue emitVectorValueFromComponents(SPIRVFunctionContext &context,
                                                     const std::string &vectorTypeName,
                                                     const std::vector<uint32_t> &componentIds,
                                                     const UGLIR::SourceLocation &location)
            {
                const std::string resultTypeName = valueTypeKeyForName(vectorTypeName);
                if (componentIds.size() == 1u)
                {
                    return makeValue(componentIds.front(), valueTypeScalarKey(resultTypeName), location);
                }
                const uint32_t resultId = allocateId();
                std::vector<uint32_t> operands{getTypeId(resultTypeName, location), resultId};
                operands.insert(operands.end(), componentIds.begin(), componentIds.end());
                context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                return makeValue(resultId, resultTypeName, location);
            }

            /** Extracts one column vector from a lowered matrix value. */
            SPIRVValue emitMatrixColumnValue(SPIRVFunctionContext &context,
                                             const SPIRVValue &matrixValue,
                                             uint32_t columnIndex,
                                             const UGLIR::SourceLocation &location)
            {
                const SPIRVValue loadedMatrix = loadIfPointer(context, matrixValue, location);
                const UGLIR::Type *directMatrixType = findType(loadedMatrix.typeName);
                if (directMatrixType != nullptr && directMatrixType->kind == UGLIR::TypeKind::Matrix)
                {
                    const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(loadedMatrix.typeName);
                    if (!matrixShape.has_value())
                    {
                        addDiagnostic(location, "matrix column extraction requires matrix shape metadata.");
                        return {};
                    }
                    if (columnIndex >= matrixShape->columnCount)
                    {
                        addDiagnostic(location, "matrix column extraction index is out of range.");
                        return {};
                    }
                    const uint32_t columnId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract, {
                        getTypeId(matrixShape->columnVectorTypeName, location),
                        columnId,
                        loadedMatrix.id,
                        columnIndex,
                    });
                    return makeValue(columnId, matrixShape->columnVectorTypeName, location);
                }
                uint32_t rowArrayFieldIndex = 0;
                const UGLIR::Type *rowArrayType = findMatrixRowArrayType(loadedMatrix.typeName, rowArrayFieldIndex);
                if (rowArrayType == nullptr)
                {
                    addDiagnostic(location, "matrix column extraction requires a lowered matrix struct with row storage.");
                    return {};
                }
                const uint32_t rowCount = std::max<uint32_t>(1u, rowArrayType->arrayCount);
                const std::string rowTypeName = valueTypeKeyForName(rowArrayType->elementType);
                const std::string columnTypeName = vectorKeyForScalar(valueTypeScalarKey(rowTypeName), rowCount);
                std::vector<uint32_t> components;
                for (uint32_t rowIndex = 0; rowIndex < rowCount; ++rowIndex)
                {
                    const SPIRVValue rowValue = emitMatrixRowValue(context, loadedMatrix, rowIndex, location);
                    const SPIRVValue componentValue = emitVectorComponentValue(context, rowValue, columnIndex, location);
                    components.push_back(componentValue.id);
                }
                return emitVectorValueFromComponents(context, columnTypeName, components, location);
            }

            /** Constructs a UGL row-array matrix value from row vectors while preserving the UGLIR matrix type name. */
            SPIRVValue emitMatrixValueFromRows(SPIRVFunctionContext &context,
                                               const std::string &matrixTypeName,
                                               const std::vector<SPIRVValue> &rowValues,
                                               const UGLIR::SourceLocation &location)
            {
                const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(matrixTypeName);
                if (!matrixShape.has_value())
                {
                    addDiagnostic(location, "native matrix construction requires a lowered UGL matrix wrapper type.");
                    return {};
                }
                if (rowValues.size() != matrixShape->rowCount)
                {
                    addDiagnostic(location, "native matrix construction received the wrong row count.");
                    return {};
                }
                const UGLIR::Type *directMatrixType = findType(matrixTypeName);
                if (directMatrixType != nullptr && directMatrixType->kind == UGLIR::TypeKind::Matrix)
                {
                    std::vector<uint32_t> operands{getTypeId(matrixTypeName, location), allocateId()};
                    std::vector<SPIRVValue> adaptedRows;
                    adaptedRows.reserve(matrixShape->rowCount);
                    for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                    {
                        adaptedRows.push_back(adaptValueToType(context, rowValues[rowIndex], matrixShape->rowVectorTypeName, location));
                    }
                    for (uint32_t columnIndex = 0; columnIndex < matrixShape->columnCount; ++columnIndex)
                    {
                        std::vector<uint32_t> columnComponents;
                        columnComponents.reserve(matrixShape->rowCount);
                        for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                        {
                            columnComponents.push_back(emitVectorComponentValue(context, adaptedRows[rowIndex], columnIndex, location).id);
                        }
                        operands.push_back(emitVectorValueFromComponents(context, matrixShape->columnVectorTypeName, columnComponents, location).id);
                    }
                    context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                    return makeValue(operands[1], matrixTypeName, location);
                }
                std::vector<uint32_t> operands{getTypeId(matrixTypeName, location), allocateId()};
                for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                {
                    const SPIRVValue rowValue = adaptValueToType(context, rowValues[rowIndex], matrixShape->rowVectorTypeName, location);
                    operands.push_back(rowValue.id);
                }
                context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                return makeValue(operands[1], matrixTypeName, location);
            }

            /** Emits a SPIR-V dot product and returns its scalar value. */
            SPIRVValue emitDotValue(SPIRVFunctionContext &context,
                                    const SPIRVValue &lhs,
                                    const SPIRVValue &rhs,
                                    const UGLIR::SourceLocation &location)
            {
                const std::string scalarTypeName = valueTypeScalarKey(lhs.typeName);
                const uint32_t dotId = allocateId();
                context.body.appendInstruction(spv::OpDot, {
                    getTypeId(scalarTypeName, location),
                    dotId,
                    lhs.id,
                    rhs.id,
                });
                return makeValue(dotId, scalarTypeName, location);
            }

            /** Emits a UGL matrix-vector multiply by dotting the input with each native matrix column. */
            SPIRVValue emitMatrixVectorMulCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "mul requires matrix and vector operands.");
                    return {};
                }
                const SPIRVValue matrixValue = emitExpression(context, expression.operands[0]);
                SPIRVValue vectorValue = emitRValue(context, expression.operands[1]);
                const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(matrixValue.typeName);
                if (!matrixShape.has_value())
                {
                    return {};
                }
                const std::string resultType = hasScalarOrVectorValueType(expression.type)
                                                   ? valueTypeKeyForName(expression.type)
                                                   : vectorKeyForScalar(matrixShape->scalarTypeName, matrixShape->columnCount);
                vectorValue = adaptValueToType(context,
                                               vectorValue,
                                               matrixShape->columnVectorTypeName,
                                               expression.operands[1].sourceLocation);
                std::vector<uint32_t> resultComponents;
                for (uint32_t columnIndex = 0; columnIndex < matrixShape->columnCount; ++columnIndex)
                {
                    const SPIRVValue columnValue = emitMatrixColumnValue(context, matrixValue, columnIndex, expression.sourceLocation);
                    resultComponents.push_back(emitDotValue(context, vectorValue, columnValue, expression.sourceLocation).id);
                }
                return emitVectorValueFromComponents(context, resultType, resultComponents, expression.sourceLocation);
            }

            /** Emits the square UGL vector-matrix overload with explicit row-dot semantics. */
            SPIRVValue emitVectorMatrixMulCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "mul requires vector and matrix operands.");
                    return {};
                }
                SPIRVValue vectorValue = emitRValue(context, expression.operands[0]);
                const SPIRVValue matrixValue = emitExpression(context, expression.operands[1]);
                const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(matrixValue.typeName);
                if (!matrixShape.has_value())
                {
                    return {};
                }
                if (matrixShape->rowCount != matrixShape->columnCount)
                {
                    addDiagnostic(expression.sourceLocation,
                                  "non-square vector-matrix multiplication is unsupported by the cross-backend shader ABI.");
                    return {};
                }
                const std::string resultType = hasScalarOrVectorValueType(expression.type)
                                                   ? valueTypeKeyForName(expression.type)
                                                   : vectorKeyForScalar(matrixShape->scalarTypeName, matrixShape->columnCount);
                vectorValue = adaptValueToType(context,
                                               vectorValue,
                                               matrixShape->columnVectorTypeName,
                                               expression.operands[0].sourceLocation);
                std::vector<uint32_t> resultComponents;
                for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                {
                    const SPIRVValue rowValue = emitMatrixRowValue(context, matrixValue, rowIndex, expression.sourceLocation);
                    resultComponents.push_back(emitDotValue(context, vectorValue, rowValue, expression.sourceLocation).id);
                }
                return emitVectorValueFromComponents(context, resultType, resultComponents, expression.sourceLocation);
            }

            /** Emits a square UGL matrix-matrix multiply as right-hand rows dotted with left-hand columns. */
            SPIRVValue emitMatrixMatrixMulCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "mul requires two matrix operands.");
                    return {};
                }
                const SPIRVValue lhsMatrix = emitExpression(context, expression.operands[0]);
                const SPIRVValue rhsMatrix = emitExpression(context, expression.operands[1]);
                const std::optional<SPIRVMatrixShape> lhsMatrixShape = matrixShapeForRegisteredType(lhsMatrix.typeName);
                const std::optional<SPIRVMatrixShape> rhsMatrixShape = matrixShapeForRegisteredType(rhsMatrix.typeName);
                if (!lhsMatrixShape.has_value() || !rhsMatrixShape.has_value())
                {
                    return {};
                }
                const std::string lhsRowTypeName = lhsMatrixShape->rowVectorTypeName;
                const std::string resultRowTypeName = vectorKeyForScalar(lhsMatrixShape->scalarTypeName, rhsMatrixShape->columnCount);
                const uint32_t lhsRowCount = lhsMatrixShape->rowCount;
                const uint32_t rhsColumnCount = rhsMatrixShape->columnCount;

                std::vector<uint32_t> resultRowIds;
                std::vector<SPIRVValue> resultRows;
                for (uint32_t rowIndex = 0; rowIndex < lhsRowCount; ++rowIndex)
                {
                    const SPIRVValue rhsRow = emitMatrixRowValue(context, rhsMatrix, rowIndex, expression.sourceLocation);
                    std::vector<uint32_t> rowComponents;
                    for (uint32_t columnIndex = 0; columnIndex < rhsColumnCount; ++columnIndex)
                    {
                        const SPIRVValue lhsColumn = emitMatrixColumnValue(context, lhsMatrix, columnIndex, expression.sourceLocation);
                        const SPIRVValue adaptedLhsColumn = adaptValueToType(context, lhsColumn, lhsRowTypeName, expression.sourceLocation);
                        rowComponents.push_back(emitDotValue(context, rhsRow, adaptedLhsColumn, expression.sourceLocation).id);
                    }
                    SPIRVValue rowValue = emitVectorValueFromComponents(context, resultRowTypeName, rowComponents, expression.sourceLocation);
                    resultRowIds.push_back(rowValue.id);
                        resultRows.push_back(rowValue);
                }

                if (matrixShapeForRegisteredType(expression.type).has_value())
                {
                    return emitMatrixValueFromRows(context, expression.type, resultRows, expression.sourceLocation);
                }

                uint32_t resultRowArrayFieldIndex = 0;
                const UGLIR::Type *resultRowArrayType = findMatrixRowArrayType(expression.type, resultRowArrayFieldIndex);
                if (resultRowArrayType == nullptr)
                {
                    addDiagnostic(expression.sourceLocation, "matrix-matrix mul requires a lowered matrix result type.");
                    return {};
                }

                const uint32_t rowArrayId = allocateId();
                std::vector<uint32_t> rowArrayOperands{getTypeId(resultRowArrayType->name, expression.sourceLocation), rowArrayId};
                rowArrayOperands.insert(rowArrayOperands.end(), resultRowIds.begin(), resultRowIds.end());
                context.body.appendInstruction(spv::OpCompositeConstruct, rowArrayOperands);

                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpCompositeConstruct, {
                    getTypeId(expression.type, expression.sourceLocation),
                    resultId,
                    rowArrayId,
                });
                return makeValue(resultId, expression.type, expression.sourceLocation);
            }

            /** Dispatches UGL matrix/vector `mul` overloads to explicit row-array SPIR-V code paths. */
            SPIRVValue emitMatrixMulCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "mul requires two operands.");
                    return {};
                }
                const bool lhsIsMatrix = matrixShapeForRegisteredType(expression.operands[0].type).has_value();
                const bool rhsIsMatrix = matrixShapeForRegisteredType(expression.operands[1].type).has_value();
                if (lhsIsMatrix && rhsIsMatrix)
                {
                    return emitMatrixMatrixMulCall(context, expression);
                }
                if (lhsIsMatrix)
                {
                    return emitMatrixVectorMulCall(context, expression);
                }
                if (rhsIsMatrix)
                {
                    return emitVectorMatrixMulCall(context, expression);
                }
                addDiagnostic(expression.sourceLocation, "mul requires at least one matrix operand for direct SPIR-V matrix lowering.");
                return {};
            }

            /** Emits a dot-product intrinsic for scalar/vector floating-point inputs. */
            SPIRVValue emitDotCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "dot requires two operands.");
                    return {};
                }
                const SPIRVValue lhs = emitRValue(context, expression.operands[0]);
                const SPIRVValue rhs = adaptValueToType(context, emitRValue(context, expression.operands[1]), lhs.typeName, expression.operands[1].sourceLocation);
                const std::string resultType = hasScalarOrVectorValueType(expression.type) && valueTypeVectorWidth(expression.type) == 1u
                                                   ? valueTypeKeyForName(expression.type)
                                                   : valueTypeScalarKey(lhs.typeName);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpDot, {getTypeId(resultType, expression.sourceLocation), resultId, lhs.id, rhs.id});
                return makeValue(resultId, resultType, expression.sourceLocation);
            }

            /** Emits a linear interpolation intrinsic as `a + (b - a) * t`. */
            SPIRVValue emitLerpCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "lerp requires three operands.");
                    return {};
                }
                const SPIRVValue a = emitRValue(context, expression.operands[0]);
                const SPIRVValue b = adaptValueToType(context, emitRValue(context, expression.operands[1]), a.typeName, expression.operands[1].sourceLocation);
                const SPIRVValue t = adaptValueToType(context, emitRValue(context, expression.operands[2]), a.typeName, expression.operands[2].sourceLocation);
                return emitGLSLExtInst(context, valueTypeKeyForName(a.typeName), GLSLstd450FMix, {a, b, t}, expression.sourceLocation);
            }

            /** Emits a saturate intrinsic by clamping to the closed zero-to-one floating-point interval. */
            SPIRVValue emitSaturateCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "saturate requires one operand.");
                    return {};
                }
                const SPIRVValue value = emitRValue(context, expression.operands.front());
                const SPIRVValue zero = broadcastScalarValueToVector(context, makeValue(getFloatConstant("0.0"), floatTypeKey(), expression.sourceLocation), value.typeName, expression.sourceLocation);
                const SPIRVValue one = broadcastScalarValueToVector(context, makeValue(getFloatConstant("1.0"), floatTypeKey(), expression.sourceLocation), value.typeName, expression.sourceLocation);
                return emitGLSLExtInst(context, valueTypeKeyForName(value.typeName), GLSLstd450FClamp, {value, zero, one}, expression.sourceLocation);
            }

            /** Emits UGL clip(x) as a conditional fragment kill when any operand lane is negative. */
            SPIRVValue emitClipCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() != 1u)
                {
                    addDiagnostic(expression.sourceLocation, "clip intrinsic requires one operand.");
                    return makeVoidValue();
                }

                SPIRVValue value = emitRValue(context, expression.operands.front());
                const std::string valueType = valueTypeKeyForName(value.typeName);
                if (!isFloatLikeScalarOrVectorValue(valueType))
                {
                    addDiagnostic(expression.sourceLocation, "clip intrinsic only supports half/float scalar or vector operands.");
                    return makeVoidValue();
                }

                value = makeValue(value.id, valueType, expression.operands.front().sourceLocation);
                const uint32_t valueWidth = valueTypeVectorWidth(valueType);
                const std::string scalarType = valueTypeScalarKey(valueType);
                const SPIRVValue zeroScalar = makeValue(isHalfScalarValue(scalarType) ? getHalfConstant("0.0") : getFloatConstant("0.0"),
                                                        scalarType,
                                                        expression.sourceLocation);
                const SPIRVValue zero = valueWidth > 1u
                                            ? broadcastScalarValueToVector(context, zeroScalar, valueType, expression.sourceLocation)
                                            : zeroScalar;

                const std::string compareType = valueWidth > 1u ? vectorKeyForScalar(boolTypeKey(), valueWidth) : boolTypeKey();
                const uint32_t compareId = allocateId();
                context.body.appendInstruction(spv::OpFOrdLessThan, {
                    getTypeId(compareType, expression.sourceLocation),
                    compareId,
                    value.id,
                    zero.id,
                });
                SPIRVValue condition = makeValue(compareId, compareType, expression.sourceLocation);
                if (valueWidth > 1u)
                {
                    const uint32_t anyId = allocateId();
                    context.body.appendInstruction(spv::OpAny, {getBoolTypeId(), anyId, condition.id});
                    condition = makeValue(anyId, boolTypeKey(), expression.sourceLocation);
                }

                const uint32_t killLabel = allocateId();
                const uint32_t mergeLabel = allocateId();
                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, killLabel, mergeLabel});
                context.blockTerminated = true;

                beginBlock(context, killLabel);
                context.body.appendInstruction(spv::OpKill, {});
                context.blockTerminated = true;

                beginBlock(context, mergeLabel);
                return makeVoidValue();
            }

            /** Emits UGL's sincos intrinsic by computing both trigonometric values and storing them into out parameters. */
            SPIRVValue emitSincosCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "sincos requires angle, sine output, and cosine output operands.");
                    return {};
                }
                const SPIRVValue angle = emitRValue(context, expression.operands[0]);
                std::string intrinsicType = valueTypeKeyForName(angle.typeName);
                if (!isFloatLikeScalarOrVectorValue(intrinsicType))
                {
                    intrinsicType = valueTypeVectorWidth(intrinsicType) > 1u
                                        ? vectorKeyForScalar(floatTypeKey(), valueTypeVectorWidth(intrinsicType))
                                        : floatTypeKey();
                }
                const SPIRVValue intrinsicAngle = adaptValueToType(context, angle, intrinsicType, expression.operands[0].sourceLocation);
                const SPIRVValue sine = emitGLSLExtInst(context, intrinsicType, GLSLstd450Sin, {intrinsicAngle}, expression.sourceLocation);
                const SPIRVValue cosine = emitGLSLExtInst(context, intrinsicType, GLSLstd450Cos, {intrinsicAngle}, expression.sourceLocation);
                emitTypedStore(context, emitLValue(context, expression.operands[1]), sine, expression.operands[1].sourceLocation);
                emitTypedStore(context, emitLValue(context, expression.operands[2]), cosine, expression.operands[2].sourceLocation);
                return makeVoidValue();
            }

            /** Emits geometric and interpolation math intrinsics (sincos, dot, distance, cross, lerp, saturate, clamp, smoothstep, step, reflect, mul). Returns true when the intrinsic matched and the emitted value was stored in result. */
            bool emitGeometricMathIntrinsic(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSincos)
                {
                    result = emitSincosCall(context, expression);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathDot)
                {
                    result = emitDotCall(context, expression);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathDistance && expression.operands.size() >= 2u)
                {
                    const SPIRVValue first = emitRValue(context, expression.operands[0]);
                    const SPIRVValue second = adaptValueToType(context, emitRValue(context, expression.operands[1]), first.typeName, expression.operands[1].sourceLocation);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) && valueTypeVectorWidth(expression.type) == 1u
                                                       ? valueTypeKeyForName(expression.type)
                                                       : valueTypeScalarKey(first.typeName);
                    result = emitGLSLExtInst(context, resultType, GLSLstd450Distance, {first, second}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathCross && expression.operands.size() >= 2u)
                {
                    const SPIRVValue first = emitRValue(context, expression.operands[0]);
                    const SPIRVValue second = adaptValueToType(context, emitRValue(context, expression.operands[1]), first.typeName, expression.operands[1].sourceLocation);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(first.typeName);
                    result = emitGLSLExtInst(context, resultType, GLSLstd450Cross, {first, second}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathLerp)
                {
                    result = emitLerpCall(context, expression);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSaturate)
                {
                    result = emitSaturateCall(context, expression);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathClamp && expression.operands.size() >= 3u)
                {
                    const SPIRVValue value = emitRValue(context, expression.operands[0]);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(value.typeName);
                    const SPIRVValue adaptedValue = adaptValueToType(context, value, resultType, expression.operands[0].sourceLocation);
                    const SPIRVValue minimum = adaptValueToType(context, emitRValue(context, expression.operands[1]), resultType, expression.operands[1].sourceLocation);
                    const SPIRVValue maximum = adaptValueToType(context, emitRValue(context, expression.operands[2]), resultType, expression.operands[2].sourceLocation);
                    if (isFloatLikeScalarOrVectorValue(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450FClamp, {adaptedValue, minimum, maximum}, expression.sourceLocation);
                        return true;
                    }
                    if (isSignedIntegerOrVectorTypeName(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450SClamp, {adaptedValue, minimum, maximum}, expression.sourceLocation);
                        return true;
                    }
                    if (isUnsignedIntegerOrVectorTypeName(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450UClamp, {adaptedValue, minimum, maximum}, expression.sourceLocation);
                        return true;
                    }
                    addDiagnostic(expression.sourceLocation, "clamp intrinsic result type \"" + resultType + "\" is not supported by the direct SPIR-V backend.");
                    result = {};
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSmoothstep && expression.operands.size() >= 3u)
                {
                    const SPIRVValue edge0 = emitRValue(context, expression.operands[0]);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(edge0.typeName);
                    const SPIRVValue adaptedEdge0 = adaptValueToType(context, edge0, resultType, expression.operands[0].sourceLocation);
                    const SPIRVValue edge1 = adaptValueToType(context, emitRValue(context, expression.operands[1]), resultType, expression.operands[1].sourceLocation);
                    const SPIRVValue value = adaptValueToType(context, emitRValue(context, expression.operands[2]), resultType, expression.operands[2].sourceLocation);
                    result = emitGLSLExtInst(context, resultType, GLSLstd450SmoothStep, {adaptedEdge0, edge1, value}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathStep && expression.operands.size() >= 2u)
                {
                    const SPIRVValue edge = emitRValue(context, expression.operands[0]);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(edge.typeName);
                    const SPIRVValue adaptedEdge = adaptValueToType(context, edge, resultType, expression.operands[0].sourceLocation);
                    const SPIRVValue value = adaptValueToType(context, emitRValue(context, expression.operands[1]), resultType, expression.operands[1].sourceLocation);
                    result = emitGLSLExtInst(context, resultType, GLSLstd450Step, {adaptedEdge, value}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathReflect && expression.operands.size() >= 2u)
                {
                    const SPIRVValue incident = emitRValue(context, expression.operands[0]);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(incident.typeName);
                    const SPIRVValue adaptedIncident = adaptValueToType(context, incident, resultType, expression.operands[0].sourceLocation);
                    const SPIRVValue normal = adaptValueToType(context, emitRValue(context, expression.operands[1]), resultType, expression.operands[1].sourceLocation);
                    result = emitGLSLExtInst(context, resultType, GLSLstd450Reflect, {adaptedIncident, normal}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathMul)
                {
                    result = emitMatrixMulCall(context, expression);
                    return true;
                }
                return false;
            }

            /** Emits bit-field and vector-length math intrinsics (firstBitLow/High, length). Returns true when the intrinsic matched and the emitted value was stored in result. */
            bool emitReductionMathIntrinsic(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, const SPIRVValue &first)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFirstBitLow ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFirstBitHigh)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : "u32";
                    const uint32_t instruction = expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFirstBitLow
                                                   ? GLSLstd450FindILsb
                                                   : (isSignedIntegerScalarValue(operandType) ? GLSLstd450FindSMsb : GLSLstd450FindUMsb);
                    result = emitGLSLExtInst(context, resultType, instruction, {first}, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathLength)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    const std::string scalarResultType = hasScalarOrVectorValueType(expression.type) && valueTypeVectorWidth(expression.type) == 1u
                                                             ? valueTypeKeyForName(expression.type)
                                                             : valueTypeScalarKey(operandType);
                    if (valueTypeVectorWidth(operandType) <= 1u)
                    {
                        const SPIRVValue scalarValue = convertScalarValueToType(context, first, scalarResultType, expression.operands.front().sourceLocation);
                        result = emitGLSLExtInst(context, scalarResultType, GLSLstd450FAbs, {scalarValue}, expression.sourceLocation);
                        return true;
                    }
                    result = emitGLSLExtInst(context, scalarResultType, GLSLstd450Length, {first}, expression.sourceLocation);
                    return true;
                }
                return false;
            }

            /** Emits derivative and logical-reduction math intrinsics (ddx/ddy/fwidth, all, any, sign, frac). Returns true when the intrinsic matched and the emitted value was stored in result. */
            bool emitDerivativeLogicalMathIntrinsic(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, const SPIRVValue &first, const std::string &resultType)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathDdx ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathDdy)
                {
                    const SPIRVValue value = adaptValueToType(context, first, resultType, expression.operands.front().sourceLocation);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathDdy ? spv::OpDPdy : spv::OpDPdx, {
                        getTypeId(resultType, expression.sourceLocation),
                        resultId,
                        value.id,
                    });
                    result = makeValue(resultId, resultType, expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAll)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    if (valueTypeVectorWidth(operandType) <= 1u)
                    {
                        result = adaptValueToType(context, first, boolTypeKey(), expression.operands.front().sourceLocation);
                        return true;
                    }
                    const SPIRVValue boolVector = adaptValueToType(context,
                                                                    first,
                                                                    vectorKeyForScalar(boolTypeKey(), valueTypeVectorWidth(operandType)),
                                                                    expression.operands.front().sourceLocation);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpAll, {getBoolTypeId(), resultId, boolVector.id});
                    result = makeValue(resultId, boolTypeKey(), expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAny)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    if (valueTypeVectorWidth(operandType) <= 1u)
                    {
                        result = adaptValueToType(context, first, boolTypeKey(), expression.operands.front().sourceLocation);
                        return true;
                    }
                    const SPIRVValue boolVector = adaptValueToType(context,
                                                                    first,
                                                                    vectorKeyForScalar(boolTypeKey(), valueTypeVectorWidth(operandType)),
                                                                    expression.operands.front().sourceLocation);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpAny, {getBoolTypeId(), resultId, boolVector.id});
                    result = makeValue(resultId, boolTypeKey(), expression.sourceLocation);
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSign)
                {
                    const SPIRVValue value = adaptValueToType(context, first, resultType, expression.operands.front().sourceLocation);
                    if (isFloatLikeScalarOrVectorValue(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450FSign, {value}, expression.sourceLocation);
                        return true;
                    }
                    if (isSignedIntegerOrVectorTypeName(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450SSign, {value}, expression.sourceLocation);
                        return true;
                    }
                    result = value;
                    return true;
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFrac ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFloor ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathCeil ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathRound)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    if (!isFloatLikeScalarOrVectorValue(operandType))
                    {
                        if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFrac)
                        {
                            result = makeValue(getDefaultValueId(resultType, expression.sourceLocation), resultType, expression.sourceLocation);
                            return true;
                        }
                        result = adaptValueToType(context, first, resultType, expression.operands.front().sourceLocation);
                        return true;
                    }

                    uint32_t instruction = GLSLstd450Floor;
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFrac)
                    {
                        instruction = GLSLstd450Fract;
                    }
                    else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathCeil)
                    {
                        instruction = GLSLstd450Ceil;
                    }
                    else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathRound)
                    {
                        instruction = GLSLstd450RoundEven;
                    }
                    const SPIRVValue intrinsicValue = emitGLSLExtInst(context, operandType, instruction, {first}, expression.sourceLocation);
                    result = adaptValueToType(context, intrinsicValue, resultType, expression.sourceLocation);
                    return true;
                }
                return false;
            }

            /** Emits float-unary math intrinsics via the GLSL.ext dispatch table (normalize, rsqrt, sqrt, trigonometry, exp, log). Returns true when the intrinsic matched and the emitted value was stored in result. */
            bool emitFloatUnaryMathIntrinsic(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, const SPIRVValue &first, const std::string &resultType)
            {
                uint32_t floatUnaryInstruction = 0;
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathNormalize)
                {
                    floatUnaryInstruction = GLSLstd450Normalize;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathRsqrt)
                {
                    floatUnaryInstruction = GLSLstd450InverseSqrt;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSqrt)
                {
                    floatUnaryInstruction = GLSLstd450Sqrt;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAsin)
                {
                    floatUnaryInstruction = GLSLstd450Asin;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAcos)
                {
                    floatUnaryInstruction = GLSLstd450Acos;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAtan)
                {
                    floatUnaryInstruction = GLSLstd450Atan;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathSin)
                {
                    floatUnaryInstruction = GLSLstd450Sin;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathCos)
                {
                    floatUnaryInstruction = GLSLstd450Cos;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathTan)
                {
                    floatUnaryInstruction = GLSLstd450Tan;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathExp2)
                {
                    floatUnaryInstruction = GLSLstd450Exp2;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathExp)
                {
                    floatUnaryInstruction = GLSLstd450Exp;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathLog2)
                {
                    floatUnaryInstruction = GLSLstd450Log2;
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathLog)
                {
                    floatUnaryInstruction = GLSLstd450Log;
                }
                if (floatUnaryInstruction != 0)
                {
                    const std::string operandType = valueTypeKeyForName(first.typeName);
                    std::string intrinsicType = operandType;
                    if (!isFloatLikeScalarOrVectorValue(intrinsicType))
                    {
                        switch (valueTypeVectorWidth(intrinsicType))
                        {
                        case 2:
                            intrinsicType = "float2";
                            break;
                        case 3:
                            intrinsicType = "float3";
                            break;
                        case 4:
                            intrinsicType = "float4";
                            break;
                        default:
                            intrinsicType = floatTypeKey();
                            break;
                        }
                    }
                    const SPIRVValue intrinsicOperand = adaptValueToType(context, first, intrinsicType, expression.operands.front().sourceLocation);
                    const SPIRVValue intrinsicValue = emitGLSLExtInst(context, intrinsicType, floatUnaryInstruction, {intrinsicOperand}, expression.sourceLocation);
                    result = adaptValueToType(context, intrinsicValue, resultType, expression.sourceLocation);
                    return true;
                }
                return false;
            }

            /** Adapts the first operand and emits abs plus the two-operand math intrinsics (modf, pow, atan2, fmod). Returns true when the intrinsic matched and the emitted value was stored in result. */
            bool emitAbsBinaryMathIntrinsic(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, SPIRVValue &first, const std::string &resultType)
            {
                first = adaptValueToType(context, first, resultType, expression.operands.front().sourceLocation);
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAbs)
                {
                    if (isUnsignedIntegerOrVectorTypeName(resultType))
                    {
                        result = first;
                        return true;
                    }
                    if (isSignedIntegerOrVectorTypeName(resultType))
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450SAbs, {first}, expression.sourceLocation);
                        return true;
                    }
                    result = emitGLSLExtInst(context, resultType, GLSLstd450FAbs, {first}, expression.sourceLocation);
                    return true;
                }
                if (expression.operands.size() >= 2u)
                {
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathModf)
                    {
                        const SPIRVValue integerPart = emitLValue(context, expression.operands[1]);
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpExtInst, {
                            getTypeId(resultType, expression.sourceLocation),
                            resultId,
                            getGLSLStd450ImportId(),
                            GLSLstd450Modf,
                            first.id,
                            integerPart.id,
                        });
                        result = makeValue(resultId, resultType, expression.sourceLocation);
                        return true;
                    }
                    SPIRVValue second = adaptValueToType(context, emitRValue(context, expression.operands[1]), resultType, expression.operands[1].sourceLocation);
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathPow)
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450Pow, {first, second}, expression.sourceLocation);
                        return true;
                    }
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathAtan2)
                    {
                        result = emitGLSLExtInst(context, resultType, GLSLstd450Atan2, {first, second}, expression.sourceLocation);
                        return true;
                    }
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathFmod)
                    {
                        const uint32_t resultId = allocateId();
                        context.body.appendInstruction(spv::OpFRem, {getTypeId(resultType, expression.sourceLocation), resultId, first.id, second.id});
                        result = makeValue(resultId, resultType, expression.sourceLocation);
                        return true;
                    }
                }
                return false;
            }

            /** Emits UGL math intrinsics through the dedicated helper groups, or through GLSL.ext instructions when one applies. */
            SPIRVValue emitMathIntrinsicCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                SPIRVValue result;
                if (emitGeometricMathIntrinsic(context, expression, result))
                {
                    return result;
                }
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "math intrinsic \"" + expression.name + "\" requires at least one operand.");
                    return {};
                }
                SPIRVValue first = emitRValue(context, expression.operands.front());
                if (emitReductionMathIntrinsic(context, expression, result, first))
                {
                    return result;
                }
                std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(first.typeName);
                if (emitDerivativeLogicalMathIntrinsic(context, expression, result, first, resultType))
                {
                    return result;
                }
                if (emitFloatUnaryMathIntrinsic(context, expression, result, first, resultType))
                {
                    return result;
                }
                if (emitAbsBinaryMathIntrinsic(context, expression, result, first, resultType))
                {
                    return result;
                }
                addDiagnostic(expression.sourceLocation, "unsupported math intrinsic \"" + expression.name + "\" for direct SPIR-V emission.");
                return {};
            }

            /** Emits UGL min/max with GLSL numeric semantics for floats and explicit comparison semantics for integers. */
            SPIRVValue emitMinMaxCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "min/max call is missing one or both operands.");
                    return {};
                }
                const bool isMax = expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::MathMax;
                const SPIRVValue lhs = emitRValue(context, expression.operands[0]);
                const SPIRVValue rhs = adaptValueToType(context, emitRValue(context, expression.operands[1]), lhs.typeName, expression.operands[1].sourceLocation);
                const std::string valueTypeName = valueTypeKeyForName(lhs.typeName);
                const std::string valueScalarTypeName = valueTypeScalarKey(valueTypeName);
                if (isFloatLikeScalarOrVectorValue(valueTypeName))
                {
                    return emitGLSLExtInst(context, valueTypeName, isMax ? GLSLstd450NMax : GLSLstd450NMin, {lhs, rhs}, expression.sourceLocation);
                }
                const std::string compareTypeName = boolTypeKeyForValueType(valueTypeName);
                const uint32_t compareId = allocateId();
                spv::Op compareOp = spv::OpULessThan;
                if (isSignedIntegerScalarValue(valueScalarTypeName))
                {
                    compareOp = isMax ? spv::OpSGreaterThan : spv::OpSLessThan;
                }
                else
                {
                    compareOp = isMax ? spv::OpUGreaterThan : spv::OpULessThan;
                }
                context.body.appendInstruction(compareOp, {getTypeId(compareTypeName, expression.sourceLocation), compareId, lhs.id, rhs.id});
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpSelect, {getTypeId(valueTypeName, expression.sourceLocation), resultId, compareId, lhs.id, rhs.id});
                return makeValue(resultId, valueTypeName, expression.sourceLocation);
            }

            /** Loads the shader-visible value stored in a reflected uniform-buffer resource. */
            SPIRVValue emitUniformBufferResourceLoad(SPIRVFunctionContext &context,
                                                     const UGLIR::SourceLocation &location,
                                                     const SPIRVResourceInfo &resource)
            {
                if (resource.binding == nullptr || resource.binding->kind != UGLIR::ResourceKind::UniformBuffer)
                {
                    addDiagnostic(location, "UniformBuffer read requires a uniform-buffer resource.");
                    return {};
                }
                const uint32_t pointerId = allocateId();
                // All Uniform resources use the sole member of their explicit block wrapper.
                context.body.appendInstruction(spv::OpAccessChain, {
                    resource.elementPointerTypeId, pointerId, resource.variableId, getUIntConstant(0),
                });
                return loadIfPointer(context,
                    makePointerValue(pointerId, resource.binding->elementType,
                        spv::StorageClassUniform, location, false), location);
            }

	            /** Loads one field from a reflected uniform-buffer resource through a Uniform access chain. */
            SPIRVValue emitUniformBufferFieldLoad(SPIRVFunctionContext &context,
                                                  const UGLIR::SourceLocation &location,
                                                  const SPIRVResourceInfo &resource,
                                                  const std::string &fieldName)
            {
                if (resource.binding == nullptr || resource.binding->kind != UGLIR::ResourceKind::UniformBuffer)
                {
                    addDiagnostic(location, "UniformBuffer field read requires a uniform-buffer resource.");
                    return {};
                }
                uint32_t fieldIndex = 0;
                const UGLIR::TypeField *field = findStructField(resource.binding->elementType, fieldName, fieldIndex);
                if (field == nullptr)
                {
                    addDiagnostic(location, "UniformBuffer element type \"" + resource.binding->elementType +
                        "\" does not contain field \"" + fieldName + "\".");
                    return {};
                }
                const uint32_t fieldPointerTypeId = getPointerTypeId(
                    getStorageTypeId(field->type, field->sourceLocation), spv::StorageClassUniform);
                const uint32_t pointerId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    fieldPointerTypeId, pointerId, resource.variableId,
                    getUIntConstant(0), getUIntConstant(fieldIndex),
                });
                return loadIfPointer(context,
                    makePointerValue(pointerId, field->type, spv::StorageClassUniform,
                        field->sourceLocation, false), field->sourceLocation);
            }

	            /** Emits a UniformBuffer read call by loading the reflected uniform block value. */
	            SPIRVValue emitUniformBufferReadCall(SPIRVFunctionContext &context,
	                                                 const UGLIR::Expression &expression,
	                                                 const SPIRVResourceInfo &resource)
            {
                return emitUniformBufferResourceLoad(context, expression.sourceLocation, resource);
            }

            /** Emits a sampled texture call using `OpSampledImage` and an image sampling instruction. */
            SPIRVValue emitTextureSampleCall(SPIRVFunctionContext &context,
                                             const UGLIR::Expression &expression,
                                             const SPIRVResourceInfo &textureResource)
            {
                if (textureResource.binding == nullptr || textureResource.binding->kind != UGLIR::ResourceKind::Texture)
                {
                    addDiagnostic(expression.sourceLocation, "texture sampling requires a sampled texture resource.");
                    return {};
                }
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "texture sample call is missing texture, sampler, or coordinate operands.");
                    return {};
                }
                const SPIRVResourceInfo *samplerResource = mModule.reflection.arguments.enabled ?
                    findOpaqueArgumentResource(expression.operands[1].type) : findResourceForExpression(context, expression.operands[1]);
                if (samplerResource == nullptr || samplerResource->binding == nullptr || samplerResource->binding->kind != UGLIR::ResourceKind::Sampler)
                {
                    addDiagnostic(expression.sourceLocation, "texture sample call requires a reflected sampler resource.");
                    return {};
                }

                const uint32_t imageId = loadResourceObjectForExpression(context, expression.operands.front(), textureResource);
                const uint32_t samplerId = loadResourceObjectForExpression(context, expression.operands[1], *samplerResource);
                const uint32_t sampledImageTypeId = getSampledImageTypeId(textureResource.objectTypeId);
                const uint32_t sampledImageId = allocateId();
                context.body.appendInstruction(spv::OpSampledImage, {sampledImageTypeId, sampledImageId, imageId, samplerId});
                if ((!mModule.reflection.arguments.enabled && textureResource.isObjectArray) ||
                    mNonUniformDecoratedIds.count(imageId) != 0 || mNonUniformDecoratedIds.count(samplerId) != 0)
                {
                    mCapabilityTracker.ensureSampledImageArrayNonUniformIndexingCapability();
                    decorateNonUniformId(sampledImageId);
                }
                const bool isArrayTexture = textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const size_t layerOperandIndex = isArrayTexture ? 3u : expression.operands.size();
                const size_t lodOperandIndex = isArrayTexture ? 4u : 3u;
                const SPIRVValue coordinate = emitImageCoordinate(context, expression, textureResource, 2u, layerOperandIndex, UGLIR::ScalarKind::Float);
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(*textureResource.binding,
                                                                                          describeTypeName(textureResource.binding->elementType),
                                                                                          describeTypeName(expression.type));
                if (!payloadType.has_value())
                {
                    addDiagnostic(expression.sourceLocation, "texture sample call requires structured image payload metadata.");
                    return {};
                }
                const std::string resultTypeName = valueTypeKey(payloadType->visibleValueType);
                const std::string imageResultTypeName = valueTypeKey(payloadType->instructionTexelType);
                const uint32_t imageResultId = allocateId();
                const uint32_t imageResultTypeId = getTypeId(imageResultTypeName, expression.sourceLocation);
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureSampleGrad)
                {
                    const size_t gradientXOperandIndex = isArrayTexture ? 4u : 3u;
                    const size_t gradientYOperandIndex = gradientXOperandIndex + 1u;
                    if (expression.operands.size() <= gradientYOperandIndex)
                    {
                        addDiagnostic(expression.sourceLocation, "texture sampleGrad call is missing gradient operands.");
                        return {};
                    }
                    const SPIRVValue gradientX = emitRValue(context, expression.operands[gradientXOperandIndex]);
                    const SPIRVValue gradientY = adaptValueToType(context,
                                                                  emitRValue(context, expression.operands[gradientYOperandIndex]),
                                                                  gradientX.typeName,
                                                                  expression.operands[gradientYOperandIndex].sourceLocation);
                    context.body.appendInstruction(spv::OpImageSampleExplicitLod, {
                        imageResultTypeId,
                        imageResultId,
                        sampledImageId,
                        coordinate.id,
                        static_cast<uint32_t>(spv::ImageOperandsGradMask),
                        gradientX.id,
                        gradientY.id,
                    });
                }
                else if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::TextureSampleLevel &&
                         expression.operands.size() > lodOperandIndex)
                {
                    const SPIRVValue lod = adaptValueToType(context, emitRValue(context, expression.operands[lodOperandIndex]), floatTypeKey(), expression.operands[lodOperandIndex].sourceLocation);
                    context.body.appendInstruction(spv::OpImageSampleExplicitLod, {
                        imageResultTypeId,
                        imageResultId,
                        sampledImageId,
                        coordinate.id,
                        static_cast<uint32_t>(spv::ImageOperandsLodMask),
                        lod.id,
                    });
                }
                else
                {
                    context.body.appendInstruction(spv::OpImageSampleImplicitLod, {
                        imageResultTypeId,
                        imageResultId,
                        sampledImageId,
                        coordinate.id,
                    });
                }
                return adaptValueToType(context, makeValue(imageResultId, imageResultTypeName, expression.sourceLocation), resultTypeName, expression.sourceLocation);
            }

            /** Emits a texture gather call using `OpImageGather`. */
            SPIRVValue emitTextureGatherCall(SPIRVFunctionContext &context,
                                             const UGLIR::Expression &expression,
                                             const SPIRVResourceInfo &textureResource)
            {
                if (textureResource.binding == nullptr || textureResource.binding->kind != UGLIR::ResourceKind::Texture)
                {
                    addDiagnostic(expression.sourceLocation, "texture gather requires a sampled texture resource.");
                    return {};
                }
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "texture gather call is missing texture, sampler, or coordinate operands.");
                    return {};
                }
                const SPIRVResourceInfo *samplerResource = mModule.reflection.arguments.enabled ?
                    findOpaqueArgumentResource(expression.operands[1].type) : findResourceForExpression(context, expression.operands[1]);
                if (samplerResource == nullptr || samplerResource->binding == nullptr || samplerResource->binding->kind != UGLIR::ResourceKind::Sampler)
                {
                    addDiagnostic(expression.sourceLocation, "texture gather call requires a reflected sampler resource.");
                    return {};
                }
                const uint32_t imageId = loadResourceObjectForExpression(context, expression.operands.front(), textureResource);
                const uint32_t samplerId = loadResourceObjectForExpression(context, expression.operands[1], *samplerResource);
                const uint32_t sampledImageTypeId = getSampledImageTypeId(textureResource.objectTypeId);
                const uint32_t sampledImageId = allocateId();
                context.body.appendInstruction(spv::OpSampledImage, {sampledImageTypeId, sampledImageId, imageId, samplerId});
                if ((!mModule.reflection.arguments.enabled && textureResource.isObjectArray) ||
                    mNonUniformDecoratedIds.count(imageId) != 0 || mNonUniformDecoratedIds.count(samplerId) != 0)
                {
                    mCapabilityTracker.ensureSampledImageArrayNonUniformIndexingCapability();
                    decorateNonUniformId(sampledImageId);
                }
                const bool isArrayTexture = textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const SPIRVValue coordinate = emitImageCoordinate(context, expression, textureResource, 2u, isArrayTexture ? 3u : expression.operands.size(), UGLIR::ScalarKind::Float);
                const uint32_t resultId = allocateId();
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(*textureResource.binding,
                                                                                          describeTypeName(textureResource.binding->elementType),
                                                                                          describeTypeName(expression.type));
                if (!payloadType.has_value())
                {
                    addDiagnostic(expression.sourceLocation, "texture gather call requires structured image payload metadata.");
                    return {};
                }
                const std::string resultTypeName = valueTypeKey(payloadType->visibleValueType);
                const std::string imageResultTypeName = valueTypeKey(payloadType->instructionTexelType);
                std::vector<uint32_t> operands{
                    getTypeId(imageResultTypeName, expression.sourceLocation),
                    resultId,
                    sampledImageId,
                    coordinate.id,
                    getIntConstant(static_cast<int32_t>(getGatherComponentIndex(expression.intrinsicCallKind))),
                };
                const size_t offsetOperandIndex = isArrayTexture ? 4u : 3u;
                if (expression.operands.size() > offsetOperandIndex)
                {
                    const UGLIR::Expression &offsetExpression = expression.operands[offsetOperandIndex];
                    if (!isZeroGatherOffsetExpression(offsetExpression))
                    {
                        mCapabilityTracker.ensureImageGatherExtendedCapability();
                        const SPIRVValue offset = emitRValue(context, offsetExpression);
                        operands.push_back(static_cast<uint32_t>(spv::ImageOperandsOffsetMask));
                        operands.push_back(offset.id);
                    }
                }
                context.body.appendInstruction(spv::OpImageGather, operands);
                return adaptValueToType(context, makeValue(resultId, imageResultTypeName, expression.sourceLocation), resultTypeName, expression.sourceLocation);
            }

            /** Emits an image read or fetch call for sampled and storage images. */
            SPIRVValue emitTextureReadCall(SPIRVFunctionContext &context,
                                           const UGLIR::Expression &expression,
                                           const SPIRVResourceInfo &textureResource)
            {
                if (textureResource.binding == nullptr ||
                    (textureResource.binding->kind != UGLIR::ResourceKind::Texture &&
                     textureResource.binding->kind != UGLIR::ResourceKind::StorageTexture))
                {
                    addDiagnostic(expression.sourceLocation, "texture read requires a texture or storage texture resource.");
                    return {};
                }
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "texture read call is missing a coordinate operand.");
                    return {};
                }
                const uint32_t imageId = loadResourceObjectForExpression(context, expression.operands.front(), textureResource);
                const bool isArrayTexture = textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const size_t layerOperandIndex = isArrayTexture && expression.operands.size() >= 3u ? 2u : expression.operands.size();
                const size_t lodOperandIndex = isArrayTexture ? 3u : 2u;
                const SPIRVValue coordinate = emitImageCoordinate(context, expression, textureResource, 1u, layerOperandIndex, UGLIR::ScalarKind::Int);
                const uint32_t resultId = allocateId();
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(*textureResource.binding,
                                                                                          describeTypeName(textureResource.binding->elementType),
                                                                                          describeTypeName(expression.type));
                if (!payloadType.has_value())
                {
                    addDiagnostic(expression.sourceLocation, "texture read call requires structured image payload metadata.");
                    return {};
                }
                const std::string imageResultTypeName = valueTypeKey(payloadType->instructionTexelType);
                const uint32_t resultTypeId = getTypeId(imageResultTypeName, expression.sourceLocation);
                if (textureResource.binding->kind == UGLIR::ResourceKind::StorageTexture)
                {
                    context.body.appendInstruction(spv::OpImageRead, {resultTypeId, resultId, imageId, coordinate.id});
                }
                else
                {
                    context.body.appendInstruction(spv::OpImageFetch, {
                        resultTypeId,
                        resultId,
                        imageId,
                        coordinate.id,
                        static_cast<uint32_t>(spv::ImageOperandsLodMask),
                        expression.operands.size() > lodOperandIndex ? emitRValue(context, expression.operands[lodOperandIndex]).id : getUIntConstant(0),
                    });
                }
                const std::string resultTypeName = valueTypeKey(payloadType->visibleValueType);
                return adaptValueToType(context, makeValue(resultId, imageResultTypeName, expression.sourceLocation), resultTypeName, expression.sourceLocation);
            }

            /** Emits an image query-size call and returns the base-level extent as a uint vector. */
            SPIRVValue emitTextureDimensionsCall(SPIRVFunctionContext &context,
                                                 const UGLIR::Expression &expression,
                                                 const SPIRVResourceInfo &textureResource)
            {
                if (textureResource.binding == nullptr)
                {
                    addDiagnostic(expression.sourceLocation, "getDimensions requires a reflected texture resource.");
                    return {};
                }
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "getDimensions is missing its texture operand.");
                    return {};
                }
                const uint32_t imageId = loadResourceObjectForExpression(context, expression.operands.front(), textureResource);
                const uint32_t sizeVectorWidth = textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture3D ||
                                                         textureResource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray
                                                     ? 3u
                                                     : 2u;
                const std::string sizeType = spvVectorTypeKey(UGLIR::ScalarKind::UInt, sizeVectorWidth);
                const uint32_t resultId = allocateId();
                mCapabilityTracker.ensureImageQueryCapability();
                if (textureResource.binding->kind == UGLIR::ResourceKind::Texture)
                {
                    context.body.appendInstruction(spv::OpImageQuerySizeLod, {
                        getTypeId(sizeType, expression.sourceLocation),
                        resultId,
                        imageId,
                        getUIntConstant(0),
                    });
                }
                else
                {
                    context.body.appendInstruction(spv::OpImageQuerySize, {
                        getTypeId(sizeType, expression.sourceLocation),
                        resultId,
                        imageId,
                    });
                }
                return adaptValueToType(context,
                                        makeValue(resultId, sizeType, expression.sourceLocation),
                                        valueTypeKeyForName(expression.type),
                                        expression.sourceLocation);
            }

            /** Emits a pixel-local read through the Phase-10 Vulkan input-attachment ABI. */
            SPIRVValue emitPixelLocalInputAttachmentReadCall(SPIRVFunctionContext &context,
                                                             const UGLIR::Expression &expression,
                                                             const SPIRVResourceInfo &resource)
            {
                if (resource.binding == nullptr || resource.binding->kind != UGLIR::ResourceKind::InputAttachment)
                {
                    addDiagnostic(expression.sourceLocation, "pixel-local input read requires a reflected input-attachment resource.");
                    return {};
                }
                const std::string resultType = valueTypeKeyForName(expression.type);
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(*resource.binding,
                                                                                          describeTypeName(resource.binding->elementType),
                                                                                          describeTypeName(expression.type));
                if (!payloadType.has_value())
                {
                    addDiagnostic(expression.sourceLocation, "pixel-local input read requires structured image payload metadata.");
                    return {};
                }
                const std::string imageResultTypeName = valueTypeKey(payloadType->instructionTexelType);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpImageRead, {
                    getTypeId(imageResultTypeName, expression.sourceLocation),
                    resultId,
                    loadResourceObject(context, resource),
                    getSubpassCoordinateZeroId(expression.sourceLocation),
                });
                if (valueTypeVectorWidth(resultType) <= 1u)
                {
                    const std::string scalarTypeName = valueTypeScalarKey(imageResultTypeName);
                    const uint32_t componentId = allocateId();
                    context.body.appendInstruction(spv::OpCompositeExtract, {
                        getTypeId(scalarTypeName, expression.sourceLocation),
                        componentId,
                        resultId,
                        0u,
                    });
                    return adaptValueToType(context,
                                            makeValue(componentId, scalarTypeName, expression.sourceLocation),
                                            resultType,
                                            expression.sourceLocation);
                }
                return adaptValueToType(context,
                                        makeValue(resultId, imageResultTypeName, expression.sourceLocation),
                                        resultType,
                                                      expression.sourceLocation);
            }

            /** Emits a logical AND of two boolean SSA values. */
            uint32_t emitBoolAnd(SPIRVFunctionContext &context, uint32_t lhsId, uint32_t rhsId)
            {
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpLogicalAnd, {getBoolTypeId(), resultId, lhsId, rhsId});
                return resultId;
            }

            /** Returns a zero-safe `max(value, 1u)` value. */
            uint32_t emitUIntMaxOne(SPIRVFunctionContext &context, uint32_t valueId)
            {
                const uint32_t hasValueId = allocateId();
                context.body.appendInstruction(spv::OpUGreaterThan, {getBoolTypeId(), hasValueId, valueId, getUIntConstant(0)});
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpSelect, {getUIntTypeId(), resultId, hasValueId, valueId, getUIntConstant(1)});
                return resultId;
            }

            /** Extracts one typed field from a draw-info record value. */
            SPIRVValue emitDrawInfoFieldExtract(SPIRVFunctionContext &context,
                                                const SPIRVValue &drawInfo,
                                                const SPIRVDrawInfoField &field,
                                                const UGLIR::SourceLocation &location)
            {
                const uint32_t fieldValueId = allocateId();
                context.body.appendInstruction(spv::OpCompositeExtract, {
                    getTypeId(field.typeName, location),
                    fieldValueId,
                    drawInfo.id,
                    field.fieldIndex,
                });
                return makeValue(fieldValueId, field.typeName, location);
            }

            /** Emits a representative component-table storage-buffer load for entity metadata intrinsics. */
            uint32_t emitComponentTableIndexLoad(SPIRVFunctionContext &context, const UGLIR::SourceLocation &location)
            {
                const SPIRVResourceInfo *resource = findComponentScalarUIntBufferResource();
                if (resource == nullptr || resource->binding == nullptr)
                {
                    return getUIntConstant(0);
                }
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    resource->elementPointerTypeId,
                    resultId,
                    resource->variableId,
                    getUIntConstant(0),
                    getUIntConstant(0),
                });
                const uint32_t valueId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {
                    getTypeId(resource->binding->elementType, location),
                    valueId,
                    resultId,
                });
                if (UGLIR::scalarKind(mModule, resource->binding->elementType) == UGLIR::ScalarKind::UInt)
                {
                    return valueId;
                }
                return getUIntConstant(0);
            }

            /** Emits an overloaded UGL operator helper as the equivalent SPIR-V arithmetic operation. */
            SPIRVValue emitOperatorFunctionCall(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "operator helper call \"" + expression.name + "\" of type \"" + expression.type + "\" is missing operands.");
                    return {};
                }
                std::string compoundOperatorName;
                const std::string &callName = expression.name;
                if (callName.find("operator+=") != std::string::npos)
                {
                    compoundOperatorName = "+";
                }
                else if (callName.find("operator-=") != std::string::npos)
                {
                    compoundOperatorName = "-";
                }
                else if (callName.find("operator*=") != std::string::npos)
                {
                    compoundOperatorName = "*";
                }
                else if (callName.find("operator/=") != std::string::npos)
                {
                    compoundOperatorName = "/";
                }
                if (!compoundOperatorName.empty())
                {
                    if (expression.operands.size() < 2u)
                    {
                        addDiagnostic(expression.sourceLocation, "compound operator helper call \"" + expression.name + "\" is missing its right-hand operand.");
                        return {};
                    }
                    const SPIRVValue pointer = emitLValue(context, expression.operands[0]);
                    const std::string valueTypeName = storageValueTypeName(pointer.typeName);
                    const SPIRVValue current = loadIfPointer(context, pointer, expression.operands[0].sourceLocation);
                    const SPIRVValue rhs = adaptValueToType(context, emitRValue(context, expression.operands[1]), valueTypeName, expression.operands[1].sourceLocation);
                    UGLIR::Expression binaryExpression;
                    binaryExpression.kind = UGLIR::ExpressionKind::Binary;
                    binaryExpression.type = valueTypeName;
                    binaryExpression.operatorName = compoundOperatorName;
                    binaryExpression.sourceLocation = expression.sourceLocation;
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(getBinaryOpcode(binaryExpression, valueTypeName), {
                        getTypeId(valueTypeName, expression.sourceLocation),
                        resultId,
                        current.id,
                        rhs.id,
                    });
                    const SPIRVValue result = makeValue(resultId, valueTypeName, expression.sourceLocation);
                    emitTypedStore(context, pointer, result, expression.sourceLocation);
                    return result;
                }
                std::string operatorName;
                if (callName.find("operator+") != std::string::npos)
                {
                    operatorName = "+";
                }
                else if (callName.find("operator-") != std::string::npos)
                {
                    operatorName = "-";
                }
                else if (callName.find("operator*") != std::string::npos)
                {
                    operatorName = "*";
                }
                else if (callName.find("operator/") != std::string::npos)
                {
                    operatorName = "/";
                }
                else if (expression.name == "+" || expression.name == "-" || expression.name == "*" || expression.name == "/")
                {
                    operatorName = expression.name;
                }
                if (operatorName.empty() || expression.operands.size() < 2u)
                {
                    return emitRValue(context, expression.operands.front());
                }
                UGLIR::Expression binaryExpression = expression;
                binaryExpression.kind = UGLIR::ExpressionKind::Binary;
                binaryExpression.operatorName = operatorName;
                binaryExpression.type = valueTypeKeyForName(expression.type);
                return emitBinaryExpression(context, binaryExpression);
            }

            /** Emits a native floating-point vector-times-scalar multiply while preserving the shader-visible vector result type. */
            SPIRVValue emitVectorTimesScalarExpression(SPIRVFunctionContext &context,
                                                       const SPIRVValue &rawVector,
                                                       const SPIRVValue &rawScalar,
                                                       const std::string &vectorTypeName,
                                                       const std::string &resultTypeName,
                                                       const UGLIR::SourceLocation &vectorLocation,
                                                       const UGLIR::SourceLocation &scalarLocation,
                                                       const UGLIR::SourceLocation &expressionLocation)
            {
                const std::string canonicalVectorTypeName = valueTypeKeyForName(vectorTypeName);
                const std::string scalarTypeName = valueTypeScalarKey(canonicalVectorTypeName);
                const SPIRVValue vectorValue = adaptValueToType(context, rawVector, canonicalVectorTypeName, vectorLocation);
                const SPIRVValue scalarValue = adaptValueToType(context, rawScalar, scalarTypeName, scalarLocation);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpVectorTimesScalar, {
                    getTypeId(resultTypeName, expressionLocation),
                    resultId,
                    vectorValue.id,
                    scalarValue.id,
                });
                return makeValue(resultId, resultTypeName, expressionLocation);
            }

            /** Loads the object held by a UniformConstant resource variable. */
            uint32_t loadResourceObject(SPIRVFunctionContext &context, const SPIRVResourceInfo &resource)
            {
                if (resource.isObjectArray)
                {
                    return loadResourceObjectAtIndex(context, resource, getUIntConstant(0));
                }
                const uint32_t objectId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {resource.objectTypeId, objectId, resource.variableId});
                return objectId;
            }

            /** Loads one object from a UniformConstant descriptor array resource. */
            uint32_t loadResourceObjectAtIndex(SPIRVFunctionContext &context, const SPIRVResourceInfo &resource, uint32_t indexId, bool nonUniform = true)
            {
                if (!resource.isObjectArray)
                {
                    return loadResourceObject(context, resource);
                }
                const bool storage = resource.binding->kind == UGLIR::ResourceKind::StorageTexture;
                if (nonUniform)
                {
                    if (storage)
                    {
                        mCapabilityTracker.ensureStorageImageArrayNonUniformIndexingCapability();
                    }
                    else
                    {
                        mCapabilityTracker.ensureSampledImageArrayNonUniformIndexingCapability();
                    }
                    decorateNonUniformId(indexId);
                }
                else
                {
                    mCapabilityTracker.ensureImageArrayDynamicIndexingCapability(storage);
                }
                const uint32_t pointerId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    resource.elementPointerTypeId,
                    pointerId,
                    resource.variableId,
                    indexId,
                });
                if (nonUniform)
                {
                    decorateNonUniformId(pointerId);
                }
                const uint32_t objectId = allocateId();
                context.body.appendInstruction(spv::OpLoad, {resource.objectTypeId, objectId, pointerId});
                if (nonUniform)
                {
                    decorateNonUniformId(objectId);
                }
                return objectId;
            }

            /** Finds the immutable descriptor-table prototype matching a first-class Args resource value. */
            const SPIRVResourceInfo *findOpaqueArgumentResource(const std::string &typeName) const
            {
                for (const auto &[name, resource] : mResourcesByUGLIRName)
                {
                    if (resource.isObjectArray && resource.binding->argumentType == typeName)
                    {
                        return &resource;
                    }
                }
                return nullptr;
            }

            /** Recognizes expressions derived solely from uniform root Args and constants; unknown locals remain conservative. */
            bool isUniformArgumentExpression(const SPIRVFunctionContext &context, const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Literal || expression.kind == UGLIR::ExpressionKind::ThisRef)
                {
                    return true;
                }
                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    if (context.localPointers.count(expression.name) != 0 || context.parameterValues.count(expression.name) != 0)
                    {
                        return false;
                    }
                    for (const auto &parameter : mModule.reflection.arguments.parameters)
                    {
                        if (parameter.name == expression.name)
                        {
                            return true;
                        }
                    }
                    return false;
                }
                switch (expression.kind)
                {
                case UGLIR::ExpressionKind::MemberRef:
                case UGLIR::ExpressionKind::Subscript:
                case UGLIR::ExpressionKind::Cast:
                case UGLIR::ExpressionKind::Load:
                case UGLIR::ExpressionKind::Construct:
                case UGLIR::ExpressionKind::Binary:
                case UGLIR::ExpressionKind::Unary:
                case UGLIR::ExpressionKind::Conditional:
                    for (const auto &operand : expression.operands)
                    {
                        if (!isUniformArgumentExpression(context, operand))
                        {
                            return false;
                        }
                    }
                    return !expression.operands.empty();
                default:
                    return false;
                }
            }

            /** Loads a texture/sampler object, honoring any local descriptor-array object alias. */
            uint32_t loadResourceObjectForExpression(SPIRVFunctionContext &context,
                                                     const UGLIR::Expression &resourceExpression,
                                                     const SPIRVResourceInfo &resource)
            {
                if (mModule.reflection.arguments.enabled)
                {
                    const auto index = emitRValue(context, resourceExpression);
                    return loadResourceObjectAtIndex(context, resource, index.id, !isUniformArgumentExpression(context, resourceExpression));
                }
                if ((resourceExpression.kind == UGLIR::ExpressionKind::Cast ||
                     resourceExpression.kind == UGLIR::ExpressionKind::Load ||
                     resourceExpression.kind == UGLIR::ExpressionKind::Construct) &&
                    !resourceExpression.operands.empty())
                {
                    return loadResourceObjectForExpression(context, resourceExpression.operands.front(), resource);
                }
                if (resourceExpression.kind == UGLIR::ExpressionKind::Subscript &&
                    resourceExpression.operands.size() >= 2u)
                {
                    const SPIRVResourceInfo *baseResource = findResourceForExpression(context, resourceExpression.operands.front());
                    if (baseResource == &resource && resource.isObjectArray)
                    {
                        const SPIRVValue index = adaptValueToType(context,
                                                                  emitRValue(context, resourceExpression.operands[1]),
                                                                  uintTypeKey(),
                                                                  resourceExpression.operands[1].sourceLocation);
                        return loadResourceObjectAtIndex(context, resource, index.id);
                    }
                }
                if (resourceExpression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    const auto aliasIter = context.resourceObjectAliases.find(resourceExpression.name);
                    if (aliasIter != context.resourceObjectAliases.end())
                    {
                        return aliasIter->second.id;
                    }
                }
                return loadResourceObject(context, resource);
            }

            /** Returns the SPIR-V gather component index encoded by a symbolized UGLIR gather intrinsic. */
            uint32_t getGatherComponentIndex(UGLIR::IntrinsicCallKind intrinsicCallKind) const
            {
                switch (intrinsicCallKind)
                {
                case UGLIR::IntrinsicCallKind::TextureGatherGreen:
                    return 1;
                case UGLIR::IntrinsicCallKind::TextureGatherBlue:
                    return 2;
                case UGLIR::IntrinsicCallKind::TextureGatherAlpha:
                    return 3;
                case UGLIR::IntrinsicCallKind::TextureGather:
                case UGLIR::IntrinsicCallKind::TextureGatherRed:
                default:
                    return 0;
                }
            }

            /** Emits a storage image write call as `OpImageWrite`. */
            SPIRVValue emitStorageImageWriteCall(SPIRVFunctionContext &context,
                                                 const UGLIR::Expression &expression,
                                                 const SPIRVResourceInfo &resource)
            {
                if (resource.binding == nullptr || resource.binding->kind != UGLIR::ResourceKind::StorageTexture)
                {
                    addDiagnostic(expression.sourceLocation, "texture write is only supported on storage texture resources in direct SPIR-V emission.");
                    return {};
                }
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "storage texture write is missing coordinate or value operands.");
                    return {};
                }
                const uint32_t imageId = loadResourceObjectForExpression(context, expression.operands.front(), resource);
                const bool isArrayTexture = resource.binding->textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const size_t valueOperandIndex = isArrayTexture && expression.operands.size() >= 4u ? 3u : 2u;
                const SPIRVValue coordinate = emitImageCoordinate(context, expression, resource, 1u, isArrayTexture ? 2u : expression.operands.size(), UGLIR::ScalarKind::Int);
                const std::optional<SPIRVImagePayloadType> payloadType = imagePayloadType(*resource.binding,
                                                                                          describeTypeName(resource.binding->elementType),
                                                                                          describeTypeName(expression.operands[valueOperandIndex].type));
                if (!payloadType.has_value())
                {
                    addDiagnostic(expression.sourceLocation, "storage texture write requires structured image payload metadata.");
                    return {};
                }
                const SPIRVValue value = adaptValueToType(context,
                                                          emitRValue(context, expression.operands[valueOperandIndex]),
                                                          valueTypeKey(payloadType->visibleValueType),
                                                          expression.operands[valueOperandIndex].sourceLocation);
                context.body.appendInstruction(spv::OpImageWrite, {imageId, coordinate.id, value.id});
                return makeVoidValue();
            }

            /** Emits a vector or scalar construction expression. */
            /** Emits matrix construction operands into rows and assembles the matrix value. Returns true when the expression is a matrix construct and result was stored. */
            bool emitMatrixConstruct(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, const std::optional<SPIRVMatrixShape> &matrixShape, const std::string &resultType, uint32_t resultId)
            {
                if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(resultType))
                {
                    std::vector<SPIRVValue> rowValues;
                    if (expression.operands.size() == matrixShape->rowCount)
                    {
                        for (const UGLIR::Expression &operand : expression.operands)
                        {
                            rowValues.push_back(adaptValueToType(context,
                                                                 emitRValue(context, operand),
                                                                 matrixShape->rowVectorTypeName,
                                                                 operand.sourceLocation));
                        }
                    }
                    else if (expression.operands.size() == 1u)
                    {
                        const SPIRVValue scalarValue = adaptValueToType(context,
                                                                        emitRValue(context, expression.operands.front()),
                                                                        matrixShape->scalarTypeName,
                                                                        expression.operands.front().sourceLocation);
                        const SPIRVValue zeroValue = makeValue(getDefaultValueId(matrixShape->scalarTypeName, expression.sourceLocation),
                                                               matrixShape->scalarTypeName,
                                                               expression.sourceLocation);
                        for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                        {
                            std::vector<uint32_t> rowComponents;
                            rowComponents.reserve(matrixShape->columnCount);
                            for (uint32_t columnIndex = 0; columnIndex < matrixShape->columnCount; ++columnIndex)
                            {
                                rowComponents.push_back((rowIndex == columnIndex ? scalarValue : zeroValue).id);
                            }
                            rowValues.push_back(emitVectorValueFromComponents(context,
                                                                              matrixShape->rowVectorTypeName,
                                                                              rowComponents,
                                                                              expression.operands.front().sourceLocation));
                        }
                    }
                    else if (expression.operands.size() == matrixShape->rowCount * matrixShape->columnCount)
                    {
                        size_t operandIndex = 0;
                        for (uint32_t rowIndex = 0; rowIndex < matrixShape->rowCount; ++rowIndex)
                        {
                            std::vector<uint32_t> rowComponents;
                            for (uint32_t columnIndex = 0; columnIndex < matrixShape->columnCount; ++columnIndex)
                            {
                                const UGLIR::Expression &operand = expression.operands[operandIndex++];
                                rowComponents.push_back(adaptValueToType(context,
                                                                         emitRValue(context, operand),
                                                                         matrixShape->scalarTypeName,
                                                                         operand.sourceLocation)
                                                            .id);
                            }
                            rowValues.push_back(emitVectorValueFromComponents(context,
                                                                              matrixShape->rowVectorTypeName,
                                                                              rowComponents,
                                                                              expression.sourceLocation));
                        }
                    }
                    else
                    {
                        addDiagnostic(expression.sourceLocation, "matrix construction received an unsupported operand shape.");
                        result = {};
                        return true;
                    }
                    result = emitMatrixValueFromRows(context, expression.type, rowValues, expression.sourceLocation);
                    return true;
                }

                return false;
            }

            /** Emits vector-from-components construction through the emitted operand table. Returns true when the kind matched and result was stored. */
            bool emitVectorComponentsConstruct(SPIRVFunctionContext &context, const UGLIR::Expression &expression, SPIRVValue &result, const std::string &resultType, uint32_t resultId)
            {
                {
                    std::vector<uint32_t> componentIds;
                    componentIds.reserve(expression.constructInfo.components.size());
                    std::vector<SPIRVValue> emittedOperands(expression.operands.size());
                    for (const UGLIR::ConstructComponent &component : expression.constructInfo.components)
                    {
                        if (component.operandIndex >= expression.operands.size())
                        {
                            addDiagnostic(expression.sourceLocation, "construct component references a missing operand.");
                            result = {};
                            return true;
                        }
                        const UGLIR::Expression &operand = expression.operands[component.operandIndex];
                        SPIRVValue &emittedOperand = emittedOperands[component.operandIndex];
                        if (emittedOperand.id == 0)
                        {
                            emittedOperand = emitRValue(context, operand);
                        }
                        SPIRVValue value = emittedOperand;
                        if (component.sourceIsVector)
                        {
                            value = emitVectorComponentValue(context, value, component.sourceComponentIndex, operand.sourceLocation);
                        }
                        value = adaptValueToType(context, value, spvScalarTypeKey(component.targetScalarKind), operand.sourceLocation);
                        componentIds.push_back(value.id);
                    }
                    if (componentIds.size() != expression.constructInfo.targetComponentCount)
                    {
                        addDiagnostic(expression.sourceLocation,
                                      "vector construction received " + std::to_string(componentIds.size()) +
                                          " scalar components for target type \"" + resultType + "\".");
                        result = {};
                        return true;
                    }
                    result = emitVectorValueFromComponents(context, resultType, componentIds, expression.sourceLocation);
                    return true;
                }
                return false;
            }

            SPIRVValue emitConstructExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    const std::string defaultType = valueTypeKeyForName(expression.type);
                    if (!describeTypeName(defaultType).has_value() &&
                        expression.constructInfo.targetScalarKind != UGLIR::ScalarKind::None &&
                        expression.constructInfo.targetComponentCount <= 1u)
                    {
                        return makeValue(getDefaultValueId(spvScalarTypeKey(expression.constructInfo.targetScalarKind), expression.sourceLocation),
                                         spvScalarTypeKey(expression.constructInfo.targetScalarKind),
                                         expression.sourceLocation);
                    }
                    const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(expression.type);
                    return makeValue(getDefaultValueId(defaultType, expression.sourceLocation),
                                     matrixShape.has_value() ? expression.type : defaultType,
                                     expression.sourceLocation);
                }
                if (expression.operands.size() == 1u &&
                    getTypeId(valueTypeKeyForName(expression.operands.front().type), expression.operands.front().sourceLocation) ==
                        getTypeId(valueTypeKeyForName(expression.type), expression.sourceLocation))
                {
                    return emitRValue(context, expression.operands.front());
                }
                std::vector<uint32_t> operands;
                const uint32_t resultId = allocateId();
                std::string resultType = valueTypeKeyForName(expression.type);
                SPIRVValue result;
                if (const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(resultType))
                {
                    if (emitMatrixConstruct(context, expression, result, matrixShape, resultType, resultId))
                    {
                        return result;
                    }
                }
                if (expression.constructInfo.kind == UGLIR::ConstructKind::ScalarConvert)
                {
                    return adaptValueToType(context, emitRValue(context, expression.operands.front()), resultType, expression.operands.front().sourceLocation);
                }
                if (expression.constructInfo.kind == UGLIR::ConstructKind::VectorSplat)
                {
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "vector splat construct is missing its operand.");
                        return {};
                    }
                    SPIRVValue value = emitRValue(context, expression.operands.front());
                    value = adaptValueToType(context, value, spvScalarTypeKey(expression.constructInfo.targetScalarKind), expression.operands.front().sourceLocation);
                    return broadcastScalarValueToVector(context, value, resultType, expression.operands.front().sourceLocation);
                }
                if (expression.constructInfo.kind == UGLIR::ConstructKind::VectorFromComponents &&
                    emitVectorComponentsConstruct(context, expression, result, resultType, resultId))
                {
                    return result;
                }

                const uint32_t constructTypeId = getTypeId(resultType, expression.sourceLocation);
                operands.push_back(constructTypeId);
                operands.push_back(resultId);
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    SPIRVValue value = emitRValue(context, operand);
                    operands.push_back(value.id);
                }
                context.body.appendInstruction(spv::OpCompositeConstruct, operands);
                return makeValue(resultId, resultType, expression.sourceLocation);
            }

            /** Emits an arithmetic or comparison binary expression. */
            SPIRVValue emitBinaryExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "binary expression is missing one or both operands.");
                    return {};
                }
                std::string resultType = valueTypeKeyForName(expression.type);
                if (!expression.isEagerLogical && (expression.operatorName == "||" || expression.operatorName == "&&"))
                {
                    const SPIRVValue lhs = adaptValueToType(context,
                                                            emitRValue(context, expression.operands[0]),
                                                            boolTypeKey(), expression.operands[0].sourceLocation);
                    const uint32_t lhsBlock = context.currentBlockId;
                    const uint32_t rhsLabel = allocateId();
                    const uint32_t mergeLabel = allocateId();
                    context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                    context.body.appendInstruction(spv::OpBranchConditional,
                                                   {lhs.id,
                                                    expression.operatorName == "&&" ? rhsLabel : mergeLabel,
                                                    expression.operatorName == "&&" ? mergeLabel : rhsLabel});
                    context.blockTerminated = true;
                    beginBlock(context, rhsLabel);
                    const SPIRVValue rhs = adaptValueToType(context,
                                                            emitRValue(context, expression.operands[1]),
                                                            boolTypeKey(), expression.operands[1].sourceLocation);
                    const uint32_t rhsBlock = context.currentBlockId;
                    branchTo(context, mergeLabel);
                    beginBlock(context, mergeLabel);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(spv::OpPhi,
                                                   {getBoolTypeId(), resultId, lhs.id, lhsBlock, rhs.id, rhsBlock});
                    return makeValue(resultId, boolTypeKey(), expression.sourceLocation);
                }
                const SPIRVValue rawLhs = emitRValue(context, expression.operands[0]);
                const SPIRVValue rawRhs = emitRValue(context, expression.operands[1]);
                const bool comparisonOperation = expression.operatorName == "<" ||
                                                 expression.operatorName == ">" ||
                                                 expression.operatorName == "<=" ||
                                                 expression.operatorName == ">=" ||
                                                 expression.operatorName == "==" ||
                                                 expression.operatorName == "!=";
                if (!hasScalarOrVectorValueType(resultType))
                {
                    resultType = binaryOperandTypeName(rawLhs, rawRhs);
                }
                std::string operandTypeName;
                if (comparisonOperation)
                {
                    operandTypeName = binaryOperandTypeName(rawLhs, rawRhs);
                }
                else if (valueTypeVectorWidth(resultType) > 1u)
                {
                    operandTypeName = resultType;
                }
                else if (valueTypeScalarKind(resultType) == UGLIR::ScalarKind::Bool)
                {
                    const std::string lhsType = valueTypeKeyForName(rawLhs.typeName);
                    operandTypeName = valueTypeVectorWidth(lhsType) > 1u ? lhsType : valueTypeScalarKey(lhsType);
                }
                else if (isFloatLikeScalarValue(resultType) || isUnsignedIntegerScalarValue(resultType) || isSignedIntegerScalarValue(resultType))
                {
                    operandTypeName = valueTypeScalarKey(resultType);
                }
                else
                {
                    operandTypeName = binaryOperandTypeName(rawLhs, rawRhs);
                }
                const bool operationUsesVector = valueTypeVectorWidth(operandTypeName) > 1u;
                if (expression.operatorName == "*" &&
                    operationUsesVector &&
                    isFloatLikeVectorValue(operandTypeName))
                {
                    const uint32_t rawLhsWidth = valueTypeVectorWidth(valueTypeKeyForName(rawLhs.typeName));
                    const uint32_t rawRhsWidth = valueTypeVectorWidth(valueTypeKeyForName(rawRhs.typeName));
                    if (rawLhsWidth > 1u && rawRhsWidth == 1u)
                    {
                        return emitVectorTimesScalarExpression(context,
                                                               rawLhs,
                                                               rawRhs,
                                                               operandTypeName,
                                                               resultType,
                                                               expression.operands[0].sourceLocation,
                                                               expression.operands[1].sourceLocation,
                                                               expression.sourceLocation);
                    }
                    if (rawLhsWidth == 1u && rawRhsWidth > 1u)
                    {
                        return emitVectorTimesScalarExpression(context,
                                                               rawRhs,
                                                               rawLhs,
                                                               operandTypeName,
                                                               resultType,
                                                               expression.operands[1].sourceLocation,
                                                               expression.operands[0].sourceLocation,
                                                               expression.sourceLocation);
                    }
                }
                const SPIRVValue lhs = operationUsesVector && expression.operands[0].kind == UGLIR::ExpressionKind::Literal
                                           ? broadcastScalarValueToVector(context, rawLhs, operandTypeName, expression.operands[0].sourceLocation)
                                           : adaptValueToType(context, rawLhs, operandTypeName, expression.operands[0].sourceLocation);
                const SPIRVValue rhs = operationUsesVector && expression.operands[1].kind == UGLIR::ExpressionKind::Literal
                                           ? broadcastScalarValueToVector(context, rawRhs, operandTypeName, expression.operands[1].sourceLocation)
                                           : adaptValueToType(context, rawRhs, operandTypeName, expression.operands[1].sourceLocation);
                const uint32_t resultId = allocateId();
                const std::string operationResultType = comparisonOperation ? resultType : operandTypeName;
                const uint32_t resultTypeId = getTypeId(operationResultType, expression.sourceLocation);
                if (expression.operatorName == "/" &&
                    expression.operands[1].kind == UGLIR::ExpressionKind::Literal &&
                    isFloatLikeScalarOrVectorValue(operandTypeName))
                {
                    const float divisor = std::strtof(expression.operands[1].value.c_str(), nullptr);
                    if (divisor != 0.0f)
                    {
                        char reciprocalLiteral[64] = {};
                        std::snprintf(reciprocalLiteral, sizeof(reciprocalLiteral), "%.9g", 1.0f / divisor);
                        const std::string operandScalarType = valueTypeScalarKey(operandTypeName);
                        const SPIRVValue reciprocal{
                            .id = isHalfScalarValue(operandScalarType) ? getHalfConstant(reciprocalLiteral) : getFloatConstant(reciprocalLiteral),
                            .typeName = operandScalarType,
                        };
                        if (operationUsesVector)
                        {
                            return emitVectorTimesScalarExpression(context,
                                                                   rawLhs,
                                                                   reciprocal,
                                                                   operandTypeName,
                                                                   resultType,
                                                                   expression.operands[0].sourceLocation,
                                                                   expression.operands[1].sourceLocation,
                                                                   expression.sourceLocation);
                        }
                        const SPIRVValue reciprocalOperand = operationUsesVector
                                                                 ? broadcastScalarValueToVector(context,
                                                                                                reciprocal,
                                                                                                operandTypeName,
                                                                                                expression.operands[1].sourceLocation)
                                                                 : adaptValueToType(context,
                                                                                    reciprocal,
                                                                                    operandTypeName,
                                                                                    expression.operands[1].sourceLocation);
                        context.body.appendInstruction(spv::OpFMul, {resultTypeId, resultId, lhs.id, reciprocalOperand.id});
                        return adaptValueToType(context, makeValue(resultId, operationResultType, expression.sourceLocation), resultType, expression.sourceLocation);
                    }
                }
                const spv::Op opcode = getBinaryOpcode(expression, operandTypeName);
                context.body.appendInstruction(opcode, {resultTypeId, resultId, lhs.id, rhs.id});
                if (comparisonOperation)
                {
                    return makeValue(resultId, resultType, expression.sourceLocation);
                }
                return adaptValueToType(context, makeValue(resultId, operationResultType, expression.sourceLocation), resultType, expression.sourceLocation);
            }

            /** Returns the SPIR-V opcode for one supported UGLIR binary expression. */
            spv::Op getBinaryOpcode(const UGLIR::Expression &expression, const std::string &operandTypeName)
            {
                if (expression.operatorName == "+")
                {
                    return isFloatLikeScalarOrVectorValue(operandTypeName) ? spv::OpFAdd : spv::OpIAdd;
                }
                if (expression.operatorName == "-")
                {
                    return isFloatLikeScalarOrVectorValue(operandTypeName) ? spv::OpFSub : spv::OpISub;
                }
                if (expression.operatorName == "*")
                {
                    return isFloatLikeScalarOrVectorValue(operandTypeName) ? spv::OpFMul : spv::OpIMul;
                }
                if (expression.operatorName == "/")
                {
                    if (isFloatLikeScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpFDiv;
                    }
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpUDiv : spv::OpSDiv;
                }
                if (expression.operatorName == "%")
                {
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpUMod : spv::OpSRem;
                }
                if (expression.operatorName == "<")
                {
                    if (isFloatLikeScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpFOrdLessThan;
                    }
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpULessThan : spv::OpSLessThan;
                }
                if (expression.operatorName == ">")
                {
                    if (isFloatLikeScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpFOrdGreaterThan;
                    }
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpUGreaterThan : spv::OpSGreaterThan;
                }
                if (expression.operatorName == "<=")
                {
                    if (isFloatLikeScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpFOrdLessThanEqual;
                    }
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpULessThanEqual : spv::OpSLessThanEqual;
                }
                if (expression.operatorName == ">=")
                {
                    if (isFloatLikeScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpFOrdGreaterThanEqual;
                    }
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpUGreaterThanEqual : spv::OpSGreaterThanEqual;
                }
                if (expression.operatorName == "==")
                {
                    if (isBooleanScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpLogicalEqual;
                    }
                    return isFloatLikeScalarOrVectorValue(operandTypeName) ? spv::OpFOrdEqual : spv::OpIEqual;
                }
                if (expression.operatorName == "!=")
                {
                    if (isBooleanScalarOrVectorValue(operandTypeName))
                    {
                        return spv::OpLogicalNotEqual;
                    }
                    return isFloatLikeScalarOrVectorValue(operandTypeName) ? spv::OpFOrdNotEqual : spv::OpINotEqual;
                }
                if (expression.operatorName == "&")
                {
                    return spv::OpBitwiseAnd;
                }
                if (expression.operatorName == "|")
                {
                    return spv::OpBitwiseOr;
                }
                if (expression.operatorName == "^")
                {
                    return spv::OpBitwiseXor;
                }
                if (expression.operatorName == "<<")
                {
                    return spv::OpShiftLeftLogical;
                }
                if (expression.operatorName == ">>")
                {
                    return isUnsignedIntegerOrVectorTypeName(operandTypeName) ? spv::OpShiftRightLogical : spv::OpShiftRightArithmetic;
                }
                if (expression.operatorName == "||")
                {
                    return spv::OpLogicalOr;
                }
                if (expression.operatorName == "&&")
                {
                    return spv::OpLogicalAnd;
                }

                addDiagnostic(expression.sourceLocation, "unsupported binary operator \"" + expression.operatorName + "\" for direct SPIR-V emission.");
                return spv::OpIAdd;
            }

            /** Emits a supported unary expression. */
            SPIRVValue emitUnaryExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "unary expression is missing its operand.");
                    return {};
                }
                if (expression.operatorName == "++" || expression.operatorName == "--")
                {
                    const SPIRVValue pointer = emitLValue(context, expression.operands.front());
                    const std::string valueTypeName = storageValueTypeName(pointer.typeName);
                    const SPIRVValue current = loadIfPointer(context, pointer, expression.sourceLocation);
                    const std::string canonicalValueType = valueTypeKeyForName(valueTypeName);
                    const uint32_t oneId = isFloatScalarValue(canonicalValueType)
                                               ? getFloatConstant("1.0")
                                               : (isHalfScalarValue(canonicalValueType)
                                                      ? getHalfConstant("1.0")
                                                      : (isSignedIntegerScalarValue(canonicalValueType) ? getIntConstant(1) : getUIntConstant(1)));
                    const spv::Op arithmeticOp = isFloatLikeScalarValue(canonicalValueType)
                                                     ? (expression.operatorName == "++" ? spv::OpFAdd : spv::OpFSub)
                                                     : (expression.operatorName == "++" ? spv::OpIAdd : spv::OpISub);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(arithmeticOp, {
                        getTypeId(valueTypeName, expression.sourceLocation),
                        resultId,
                        current.id,
                        oneId,
                    });
                    const SPIRVValue incremented{.id = resultId, .typeName = valueTypeName};
                    emitTypedStore(context, pointer, incremented, expression.sourceLocation);
                    return expression.isPostfix ? current : incremented;
                }

                const SPIRVValue operand = emitRValue(context, expression.operands.front());
                const uint32_t resultId = allocateId();
                if (expression.operatorName == "!")
                {
                    context.body.appendInstruction(spv::OpLogicalNot, {getBoolTypeId(), resultId, operand.id});
                    return makeValue(resultId, boolTypeKey(), expression.sourceLocation);
                }
                if (expression.operatorName == "-")
                {
                    context.body.appendInstruction(isFloatLikeScalarOrVectorValue(operand.typeName) ? spv::OpFNegate : spv::OpSNegate, {
                        getTypeId(valueTypeKeyForName(expression.type), expression.sourceLocation),
                        resultId,
                        operand.id,
                    });
                    return makeValue(resultId, valueTypeKeyForName(expression.type), expression.sourceLocation);
                }
                if (expression.operatorName == "~")
                {
                    const std::string resultType = hasScalarOrVectorValueType(expression.type) ? valueTypeKeyForName(expression.type) : valueTypeKeyForName(operand.typeName);
                    context.body.appendInstruction(spv::OpNot, {
                        getTypeId(resultType, expression.sourceLocation),
                        resultId,
                        operand.id,
                    });
                    return makeValue(resultId, resultType, expression.sourceLocation);
                }
                addDiagnostic(expression.sourceLocation, "unsupported unary operator \"" + expression.operatorName + "\" for direct SPIR-V emission.");
                return operand;
            }

            /** Emits structured selection so only the chosen conditional operand is evaluated. */
            SPIRVValue emitConditionalExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 3u)
                {
                    addDiagnostic(expression.sourceLocation, "conditional expression is missing one or more operands.");
                    return {};
                }
                const SPIRVValue condition = adaptValueToType(context,
                                                              emitRValue(context, expression.operands[0]),
                                                              boolTypeKey(),
                                                              expression.operands[0].sourceLocation);
                const std::string resultType = valueTypeKeyForName(expression.type);
                const uint32_t thenLabel = allocateId();
                const uint32_t elseLabel = allocateId();
                const uint32_t mergeLabel = allocateId();
                context.body.appendInstruction(spv::OpSelectionMerge, {mergeLabel, static_cast<uint32_t>(spv::SelectionControlMaskNone)});
                context.body.appendInstruction(spv::OpBranchConditional, {condition.id, thenLabel, elseLabel});
                context.blockTerminated = true;

                beginBlock(context, thenLabel);
                const SPIRVValue trueValue = emitRValue(context, expression.operands[1]);
                const SPIRVValue selectedTrueValue = adaptValueToType(context, trueValue, resultType, expression.operands[1].sourceLocation);
                const uint32_t thenValueBlock = context.currentBlockId;
                branchTo(context, mergeLabel);

                beginBlock(context, elseLabel);
                const SPIRVValue falseValue = emitRValue(context, expression.operands[2]);
                const SPIRVValue selectedFalseValue = adaptValueToType(context, falseValue, resultType, expression.operands[2].sourceLocation);
                const uint32_t elseValueBlock = context.currentBlockId;
                branchTo(context, mergeLabel);

                beginBlock(context, mergeLabel);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpPhi, {
                    getTypeId(resultType, expression.sourceLocation),
                    resultId,
                    selectedTrueValue.id,
                    thenValueBlock,
                    selectedFalseValue.id,
                    elseValueBlock,
                });
                return makeValue(resultId, resultType, expression.sourceLocation);
            }

            /** Stores a value into a multi-component vector swizzle by rebuilding the base vector. */
            std::optional<SPIRVValue> emitVectorSwizzleStore(SPIRVFunctionContext &context,
                                        const UGLIR::Expression &destination,
                                        const SPIRVValue &incomingValue,
                                        const UGLIR::SourceLocation &location,
                                        const std::string &operatorName,
                                        const std::string &computationType)
            {
                if (destination.kind != UGLIR::ExpressionKind::MemberRef || destination.operands.empty())
                {
                    return std::nullopt;
                }

                const std::vector<uint32_t> componentIndices = getVectorComponentIndices(destination.name);
                if (componentIndices.size() <= 1u)
                {
                    return std::nullopt;
                }
                if (valueTypeVectorWidth(destination.operands.front().type) <= 1u)
                {
                    return std::nullopt;
                }

                const SPIRVValue basePointer = emitLValue(context, destination.operands.front());
                if (!basePointer.isPointer)
                {
                    addDiagnostic(location, "multi-component swizzle assignment requires an assignable vector base.");
                    return SPIRVValue{};
                }

                const std::string baseTypeName = storageValueTypeName(basePointer.typeName);
                const uint32_t baseWidth = valueTypeVectorWidth(baseTypeName);
                if (baseWidth <= 1u)
                {
                    addDiagnostic(location, "multi-component swizzle assignment requires a vector base.");
                    return SPIRVValue{};
                }

                const std::string componentTypeName = valueTypeScalarKey(baseTypeName);
                const std::string incomingTypeName = componentIndices.size() == 1u
                                                         ? componentTypeName
                                                         : vectorKeyForScalar(componentTypeName, static_cast<uint32_t>(componentIndices.size()));
                const SPIRVValue currentValue = loadIfPointer(context, basePointer, destination.sourceLocation);
                SPIRVValue replacementValue = adaptValueToType(context, incomingValue, incomingTypeName, location);

                if (!operatorName.empty() && operatorName != "=")
                {
                    const uint32_t swizzleTypeId = getTypeId(incomingTypeName, location);
                    const uint32_t currentSwizzleId = allocateId();
                    std::vector<uint32_t> shuffleOperands{swizzleTypeId, currentSwizzleId, currentValue.id, currentValue.id};
                    shuffleOperands.insert(shuffleOperands.end(), componentIndices.begin(), componentIndices.end());
                    context.body.appendInstruction(spv::OpVectorShuffle, shuffleOperands);

                    UGLIR::Expression operation;
                    operation.kind = UGLIR::ExpressionKind::Binary;
                    operation.type = computationType.empty() ? incomingTypeName : valueTypeKeyForName(computationType);
                    operation.operatorName = operatorName.substr(0, operatorName.size() - 1u);
                    operation.sourceLocation = location;
                    const SPIRVValue lhs = adaptValueToType(context, makeValue(currentSwizzleId, incomingTypeName, location), operation.type, location);
                    const SPIRVValue rhs = adaptValueToType(context, incomingValue, operation.type, location);
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(getBinaryOpcode(operation, operation.type),
                                                   {getTypeId(operation.type, location), resultId, lhs.id, rhs.id});
                    replacementValue = adaptValueToType(context, makeValue(resultId, operation.type, location), incomingTypeName, location);
                }

                std::vector<uint32_t> constructOperands{getTypeId(baseTypeName, location), allocateId()};
                for (uint32_t componentIndex = 0; componentIndex < baseWidth; ++componentIndex)
                {
                    uint32_t selectedValueId = 0;
                    for (uint32_t swizzleIndex = 0; swizzleIndex < componentIndices.size(); ++swizzleIndex)
                    {
                        if (componentIndices[swizzleIndex] != componentIndex)
                        {
                            continue;
                        }

                        selectedValueId = allocateId();
                        if (componentIndices.size() == 1u)
                        {
                            selectedValueId = replacementValue.id;
                        }
                        else
                        {
                            context.body.appendInstruction(spv::OpCompositeExtract, {
                                getTypeId(componentTypeName, location),
                                selectedValueId,
                                replacementValue.id,
                                swizzleIndex,
                            });
                        }
                        break;
                    }

                    if (selectedValueId == 0)
                    {
                        selectedValueId = allocateId();
                        context.body.appendInstruction(spv::OpCompositeExtract, {
                            getTypeId(componentTypeName, location),
                            selectedValueId,
                            currentValue.id,
                            componentIndex,
                        });
                    }

                    constructOperands.push_back(selectedValueId);
                }

                context.body.appendInstruction(spv::OpCompositeConstruct, constructOperands);
                emitTypedStore(context, basePointer, makeValue(constructOperands[1], baseTypeName, location), location);
                return replacementValue;
            }

            /** Stores or compounds a scalar into a constant-indexed local matrix element by rebuilding the matrix value. */
            std::optional<SPIRVValue> emitMatrixElementStore(SPIRVFunctionContext &context,
                                        const UGLIR::Expression &destination,
                                        const SPIRVValue &incomingValue,
                                        const std::string &operatorName,
                                        const std::string &computationType,
                                        const UGLIR::SourceLocation &location)
            {
                if (destination.kind != UGLIR::ExpressionKind::Subscript || destination.operands.size() < 2u)
                {
                    return std::nullopt;
                }
                const UGLIR::Expression &rowSubscript = destination.operands.front();
                if (rowSubscript.kind != UGLIR::ExpressionKind::Subscript || rowSubscript.operands.size() < 2u)
                {
                    return std::nullopt;
                }

                const std::optional<uint32_t> rowIndex = getSwitchCaseLiteral(rowSubscript.operands[1]);
                const std::optional<uint32_t> columnIndex = getSwitchCaseLiteral(destination.operands[1]);
                if (!rowIndex.has_value() || !columnIndex.has_value())
                {
                    return std::nullopt;
                }

                const std::optional<SPIRVMatrixShape> matrixShape = matrixShapeForRegisteredType(rowSubscript.operands.front().type);
                if (!matrixShape.has_value())
                {
                    return std::nullopt;
                }
                const SPIRVValue matrixPointer = emitLValue(context, rowSubscript.operands.front());
                if (!matrixPointer.isPointer)
                {
                    return std::nullopt;
                }
                if (*rowIndex >= matrixShape->rowCount || *columnIndex >= matrixShape->columnCount)
                {
                    addDiagnostic(location, "matrix element store index is out of range.");
                    return SPIRVValue{};
                }

                const SPIRVValue matrixValue = loadIfPointer(context, matrixPointer, location);
                SPIRVValue replacementScalar;
                if (operatorName.empty() || operatorName == "=")
                {
                    replacementScalar = adaptValueToType(context, incomingValue, matrixShape->scalarTypeName, location);
                }
                else
                {
                    std::string binaryOperator = operatorName;
                    if (!binaryOperator.empty() && binaryOperator.back() == '=')
                    {
                        binaryOperator.pop_back();
                    }
                    if (binaryOperator.empty())
                    {
                        addDiagnostic(location, "compound store operator \"" + operatorName + "\" is not supported by direct SPIR-V emission.");
                        return SPIRVValue{};
                    }
                    const SPIRVValue rowValue = emitMatrixRowValue(context, matrixValue, *rowIndex, location);
                    const SPIRVValue currentScalar = emitVectorComponentValue(context, rowValue, *columnIndex, location);
                    const std::string computationValueType = computationType.empty()
                                                                  ? matrixShape->scalarTypeName
                                                                  : valueTypeKeyForName(computationType);
                    const SPIRVValue adaptedCurrent = adaptValueToType(context, currentScalar, computationValueType, location);
                    const SPIRVValue adaptedIncoming = adaptValueToType(context, incomingValue, computationValueType, location);
                    UGLIR::Expression binaryExpression;
                    binaryExpression.kind = UGLIR::ExpressionKind::Binary;
                    binaryExpression.type = computationValueType;
                    binaryExpression.operatorName = binaryOperator;
                    binaryExpression.sourceLocation = location;
                    const uint32_t resultId = allocateId();
                    context.body.appendInstruction(getBinaryOpcode(binaryExpression, computationValueType), {
                        getTypeId(computationValueType, location),
                        resultId,
                        adaptedCurrent.id,
                        adaptedIncoming.id,
                    });
                    replacementScalar = adaptValueToType(context,
                                                          makeValue(resultId, computationValueType, location),
                                                          matrixShape->scalarTypeName,
                                                          location);
                }
                std::vector<SPIRVValue> rowValues;
                rowValues.reserve(matrixShape->rowCount);
                for (uint32_t currentRow = 0; currentRow < matrixShape->rowCount; ++currentRow)
                {
                    SPIRVValue rowValue = emitMatrixRowValue(context, matrixValue, currentRow, location);
                    if (currentRow == *rowIndex)
                    {
                        std::vector<uint32_t> rowComponents;
                        rowComponents.reserve(matrixShape->columnCount);
                        for (uint32_t currentColumn = 0; currentColumn < matrixShape->columnCount; ++currentColumn)
                        {
                            if (currentColumn == *columnIndex)
                            {
                                rowComponents.push_back(replacementScalar.id);
                                continue;
                            }
                            rowComponents.push_back(emitVectorComponentValue(context, rowValue, currentColumn, location).id);
                        }
                        rowValue = emitVectorValueFromComponents(context, matrixShape->rowVectorTypeName, rowComponents, location);
                    }
                    rowValues.push_back(rowValue);
                }

                const SPIRVValue rebuiltMatrix = emitMatrixValueFromRows(context, matrixPointer.typeName, rowValues, location);
                emitTypedStore(context, matrixPointer, rebuiltMatrix, location);
                return replacementScalar;
            }

            /** Emits an assignment and returns the converted value that was stored, without re-evaluating its destination. */
            SPIRVValue emitStoreExpression(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "store expression is missing its destination or value operand.");
                    return {};
                }
                if (expression.operands[0].kind == UGLIR::ExpressionKind::DeclRef &&
                    isResourceAliasType(expression.operands[0].type))
                {
                    const std::string aliasName = expression.operands[0].name;
                    const SPIRVResourceInfo *resource = findResourceAliasInitializer(context, expression.operands[1]);
                    if (resource == nullptr)
                    {
                        addDiagnostic(expression.sourceLocation, "resource alias assignment cannot resolve a reflected resource.");
                        return {};
                    }
                    assignResourceAlias(context, aliasName, *resource, expression.sourceLocation);
                    if (resource->binding != nullptr && resource->binding->kind == UGLIR::ResourceKind::Texture)
                    {
                        const uint32_t objectId = loadResourceObjectForExpression(context, expression.operands[1], *resource);
                        if (objectId != 0)
                        {
                            context.resourceObjectAliases[aliasName] = makeValue(objectId, resource->binding->elementType, expression.sourceLocation);
                        }
                    }
                    return makeVoidValue();
                }
                SPIRVValue value = emitRValue(context, expression.operands[1]);
                if (const auto stored = emitMatrixElementStore(context,
                                                                expression.operands[0],
                                                                value,
                                                                expression.operatorName,
                                                                expression.computationType,
                                                                expression.sourceLocation))
                {
                    return *stored;
                }
                if (const auto stored = emitVectorSwizzleStore(context, expression.operands[0], value, expression.sourceLocation, expression.operatorName, expression.computationType))
                {
                    return *stored;
                }

                const SPIRVValue pointer = emitLValue(context, expression.operands[0]);
                if (!expression.operatorName.empty() && expression.operatorName != "=")
                {
                    std::string binaryOperator = expression.operatorName;
                    if (!binaryOperator.empty() && binaryOperator.back() == '=')
                    {
                        binaryOperator.pop_back();
                    }
                    if (binaryOperator.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "compound store operator \"" + expression.operatorName + "\" is not supported by direct SPIR-V emission.");
                        return {};
                    }
                    const std::string valueTypeName = expression.computationType.empty()
                                                          ? storageValueTypeName(pointer.typeName)
                                                          : valueTypeKeyForName(expression.computationType);
                    const SPIRVValue current = adaptValueToType(context,
                        loadIfPointer(context, pointer, expression.operands[0].sourceLocation),
                        valueTypeName, expression.operands[0].sourceLocation);
                    value = adaptValueToType(context, value, valueTypeName, expression.operands[1].sourceLocation);
                    UGLIR::Expression binaryExpression;
                    binaryExpression.kind = UGLIR::ExpressionKind::Binary;
                    binaryExpression.type = valueTypeName;
                    binaryExpression.operatorName = binaryOperator;
                    binaryExpression.sourceLocation = expression.sourceLocation;
                    const uint32_t resultId = allocateId();
                    const spv::Op opcode = getBinaryOpcode(binaryExpression, valueTypeName);
                    context.body.appendInstruction(opcode, {getTypeId(valueTypeName, expression.sourceLocation), resultId, current.id, value.id});
                    value = makeValue(resultId, valueTypeName, expression.sourceLocation);
                }
                value = adaptValueToType(context, value, storageValueTypeName(pointer.typeName), expression.sourceLocation);
                emitTypedStore(context, pointer, value, expression.sourceLocation);
                return value;
            }

            /** Emits an expression in lvalue position and returns a pointer. */
            SPIRVValue emitLValue(SPIRVFunctionContext &context, const UGLIR::Expression &expression)
            {
                switch (expression.kind)
                {
                case UGLIR::ExpressionKind::DeclRef:
                {
                    const auto iter = context.localPointers.find(expression.name);
                    if (iter != context.localPointers.end())
                    {
                        return iter->second;
                    }
                    addDiagnostic(expression.sourceLocation, "declaration reference \"" + expression.name + "\" is not assignable in direct SPIR-V emission.");
                    return {};
                }
                case UGLIR::ExpressionKind::ThisRef:
                {
                    const auto iter = context.localPointers.find("this");
                    if (iter != context.localPointers.end())
                    {
                        return iter->second;
                    }
                    addDiagnostic(expression.sourceLocation, "this reference is not assignable in direct SPIR-V emission.");
                    return {};
                }
                case UGLIR::ExpressionKind::Subscript:
                    return emitSubscriptPointer(context, expression);
                case UGLIR::ExpressionKind::MemberRef:
                {
                    if (const SPIRVResourceInfo *resource = findResourceForExpression(context, expression))
                    {
                        SPIRVValue resourcePointer = makePointerValue(resource->variableId,
                                                                       resource->binding->elementType,
                                                                       storageClassForResource(*resource),
                                                                       expression.sourceLocation);
                        resourcePointer.isResourceRoot = true;
                        return resourcePointer;
                    }
                    const SPIRVValue memberPointer = emitStructMemberPointer(context, expression);
                    if (memberPointer.id != 0)
                    {
                        return memberPointer;
                    }
                    addDiagnostic(expression.sourceLocation, "member reference \"" + expression.name + "\" is not assignable in direct SPIR-V emission.");
                    return {};
                }
                case UGLIR::ExpressionKind::Cast:
                case UGLIR::ExpressionKind::Load:
                case UGLIR::ExpressionKind::Construct:
                    if (!expression.operands.empty())
                    {
                        return emitLValue(context, expression.operands.front());
                    }
                    addDiagnostic(expression.sourceLocation, "wrapped lvalue expression is missing its operand.");
                    return {};
                default:
                    if (!expression.operands.empty())
                    {
                        return emitLValue(context, expression.operands.front());
                    }
                    addDiagnostic(expression.sourceLocation, "unsupported lvalue expression in direct SPIR-V emission.");
                    return {};
                }
            }

            /** Emits a pointer to a storage-buffer element or an array/vector component in its original address space. */
            SPIRVValue emitSubscriptPointer(SPIRVFunctionContext &context,
                                             const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2u)
                {
                    addDiagnostic(expression.sourceLocation, "subscript expression is missing its base or index operand.");
                    return {};
                }
                if (mModule.reflection.arguments.enabled && isArgumentBufferExpression(context, expression.operands[0]))
                {
                    const auto index = emitRValue(context, expression.operands[1]);
                    return emitArgumentBufferPointer(context, expression.operands[0], index.id);
                }
                const SPIRVResourceInfo *resource = findResourceForExpression(context, expression.operands[0]);
                if (resource == nullptr)
                {
                    return emitArraySubscriptPointer(context, expression);
                }
                if (resource->binding == nullptr || resource->binding->kind != UGLIR::ResourceKind::StorageBuffer)
                {
                    addDiagnostic(expression.sourceLocation,
                        "direct SPIR-V subscript expressions require a StructuredBuffer or RWStructuredBuffer resource.");
                    return {};
                }
                const SPIRVValue index = emitRValue(context, expression.operands[1]);
                return emitStorageBufferElementPointer(context, *resource, index.id, expression.sourceLocation);
            }

            /** Materializes constructed local arrays or resolves an existing array memory object. */
            SPIRVValue emitSubscriptBasePointer(SPIRVFunctionContext &context,
                                                 const UGLIR::Expression &expression)
            {
                const UGLIR::Type *baseType = findType(expression.type);
                if (baseType == nullptr)
                {
                    baseType = findType(valueTypeKeyForName(expression.type));
                }
                if (expression.kind != UGLIR::ExpressionKind::Construct ||
                    baseType == nullptr || baseType->kind != UGLIR::TypeKind::Array)
                {
                    return emitLValue(context, expression);
                }
                const std::string arrayKey = makeConstantArrayTemporaryKey(expression);
                const auto arrayPointer = context.constantArrayPointers.find(arrayKey);
                if (arrayPointer == context.constantArrayPointers.end())
                {
                    addDiagnostic(expression.sourceLocation,
                        "constructed array subscript is missing its predeclared SPIR-V temporary.");
                    return {};
                }
                const SPIRVValue arrayValue = emitConstructExpression(context, expression);
                emitTypedStore(context, arrayPointer->second, arrayValue, expression.sourceLocation);
                return arrayPointer->second;
            }

            /** Addresses an array element using its parent's physical or computation-only layout. */
            SPIRVValue emitArraySubscriptPointer(SPIRVFunctionContext &context,
                                                  const UGLIR::Expression &expression)
            {
                const SPIRVValue basePointer = emitSubscriptBasePointer(context, expression.operands[0]);
                const UGLIR::Type *baseType = findType(basePointer.typeName);
                if (!basePointer.isPointer || baseType == nullptr)
                {
                    addDiagnostic(expression.sourceLocation,
                        "direct SPIR-V subscript expressions require a storage buffer, array, or vector memory object.");
                    return {};
                }
                const bool isArray = baseType->kind == UGLIR::TypeKind::Array;
                const bool isVector = baseType->kind == UGLIR::TypeKind::Vector;
                if (!isArray && !isVector)
                {
                    addDiagnostic(expression.sourceLocation,
                        "direct SPIR-V subscript expressions require a storage buffer, array, or vector memory object.");
                    return {};
                }
                const SPIRVValue index = emitRValue(context, expression.operands[1]);
                const std::string elementValueType = storageValueTypeName(baseType->elementType);
                const uint32_t elementPointerTypeId = getPointerTypeId(
                    getMemoryValueTypeId(elementValueType, basePointer.storageClass, expression.sourceLocation),
                    basePointer.storageClass);
                const uint32_t resultId = allocateId();
                context.body.appendInstruction(spv::OpAccessChain, {
                    elementPointerTypeId, resultId, basePointer.id, index.id,
                });
                return makePointerValue(resultId, baseType->elementType, basePointer.storageClass,
                    expression.sourceLocation, false);
            }

            /** Returns the reflected resource reached by a member-reference expression or local alias. */
            const SPIRVResourceInfo *findResourceForExpression(const SPIRVFunctionContext &context, const UGLIR::Expression &expression) const
            {
                if ((expression.kind == UGLIR::ExpressionKind::Cast ||
                     expression.kind == UGLIR::ExpressionKind::Load ||
                     expression.kind == UGLIR::ExpressionKind::Construct) &&
                    !expression.operands.empty())
                {
                    return findResourceForExpression(context, expression.operands.front());
                }
                if (expression.kind == UGLIR::ExpressionKind::Subscript &&
                    !expression.operands.empty())
                {
                    const SPIRVResourceInfo *baseResource = findResourceForExpression(context, expression.operands.front());
                    if (baseResource != nullptr &&
                        baseResource->binding != nullptr &&
                        baseResource->binding->kind == UGLIR::ResourceKind::Texture)
                    {
                        return baseResource;
                    }
                }
                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    const auto resourceIter = mResourcesByUGLIRName.find(expression.name);
                    if (resourceIter != mResourcesByUGLIRName.end())
                    {
                        return &resourceIter->second;
                    }
                    const auto aliasIter = context.resourceAliases.find(expression.name);
                    if (aliasIter != context.resourceAliases.end())
                    {
                        return aliasIter->second;
                    }
                }
                return findReflectedResourceForExpression(expression);
            }

            /** Returns the reflected resource reached by a member-reference expression. */
            const SPIRVResourceInfo *findReflectedResourceForExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::ResourceBinding *resource = UGLIR::findReflectedResource(mModule, expression);
                if (resource == nullptr) return nullptr;
                const auto iter = mResourcesByUGLIRName.find(resource->name);
                return iter == mResourcesByUGLIRName.end() ? nullptr : &iter->second;
            }

            /** Returns the first reflected resource with the requested component ABI role. */
            const SPIRVResourceInfo *findFirstComponentABIResource(UGLIR::ResourceRole componentKind) const
            {
                for (const UGLIR::ResourceBinding &binding : mModule.reflection.resources)
                {
                    if (binding.resourceRole != componentKind)
                    {
                        continue;
                    }
                    const auto iter = mResourcesByUGLIRName.find(binding.name);
                    if (iter != mResourcesByUGLIRName.end())
                    {
                        return &iter->second;
                    }
                }
                return nullptr;
            }

            /** Returns the reflected auxiliary resource with the requested component ABI role. */
            const SPIRVResourceInfo *findComponentABIAuxiliaryResource(UGLIR::ResourceRole componentKind) const
            {
                return findFirstComponentABIResource(componentKind);
            }

            /** Returns a component-table index resource for one concrete component index. */
            const SPIRVResourceInfo *findComponentIndexTableResource(UGLIR::ResourceRole componentKind, uint32_t componentIndex) const
            {
                for (const UGLIR::ResourceBinding &binding : mModule.reflection.resources)
                {
                    if (binding.resourceRole != componentKind ||
                        binding.resourceIndex != componentIndex)
                    {
                        continue;
                    }
                    const auto iter = mResourcesByUGLIRName.find(binding.name);
                    if (iter != mResourcesByUGLIRName.end())
                    {
                        return &iter->second;
                    }
                }
                return nullptr;
            }

            /** Returns the first component-table buffer value that can be loaded as a scalar uint value. */
            const SPIRVResourceInfo *findComponentScalarUIntBufferResource() const
            {
                for (const UGLIR::ResourceBinding &binding : mModule.reflection.resources)
                {
                    if (binding.resourceRole != UGLIR::ResourceRole::BufferValue ||
                        UGLIR::scalarKind(mModule, binding.elementType) != UGLIR::ScalarKind::UInt)
                    {
                        continue;
                    }
                    const auto iter = mResourcesByUGLIRName.find(binding.name);
                    if (iter != mResourcesByUGLIRName.end())
                    {
                        return &iter->second;
                    }
                }
                return findFirstComponentABIResource(UGLIR::ResourceRole::BufferValue);
            }


            /** Returns the zero value for a registered scalar, vector, matrix or aggregate type. */
            uint32_t getDefaultValueId(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                if (describeTypeName(typeName).has_value() || findType(typeName) != nullptr)
                {
                    if (const auto existing = mNullConstants.find(typeName); existing != mNullConstants.end())
                    {
                        return existing->second;
                    }
                    const uint32_t constantId = allocateId();
                    mTypesConstantsGlobals.appendInstruction(spv::OpConstantNull, {getTypeId(typeName, location), constantId});
                    mNullConstants.emplace(typeName, constantId);
                    return constantId;
                }
                addDiagnostic(location, "no default value is available for type \"" + typeName + "\".");
                return getUIntConstant(0);
            }

            /** Assembles all SPIR-V sections into a complete module word vector. */
            std::vector<uint32_t> assembleModuleWords() const
            {
                std::vector<uint32_t> words;
                words.reserve(5u + mCapabilityTracker.getCapabilities().words().size() + mCapabilityTracker.getExtensions().words().size() + mExtInstImports.words().size() + mMemoryModel.words().size() + mEntryPoints.words().size() +
                              mExecutionModes.words().size() + mDebugNames.words().size() + mAnnotations.words().size() +
                              mTypesConstantsGlobals.words().size() + mFunctions.words().size());
                words.push_back(spv::MagicNumber);
                words.push_back(RuntimeSPIRVVersionWord);
                words.push_back(0u);
                words.push_back(mNextId);
                words.push_back(0u);
                appendSection(words, mCapabilityTracker.getCapabilities());
                appendSection(words, mCapabilityTracker.getExtensions());
                appendSection(words, mExtInstImports);
                appendSection(words, mMemoryModel);
                appendSection(words, mEntryPoints);
                appendSection(words, mExecutionModes);
                appendSection(words, mDebugNames);
                appendSection(words, mAnnotations);
                appendSection(words, mTypesConstantsGlobals);
                appendSection(words, mFunctions);
                return words;
            }

            /** Appends one SPIR-V section to the final module word vector. */
            static void appendSection(std::vector<uint32_t> &words, const SPIRVInstructionBuilder &section)
            {
                const std::vector<uint32_t> &sectionWords = section.words();
                words.insert(words.end(), sectionWords.begin(), sectionWords.end());
            }
        };
    } // namespace

    SPIRVModuleBuildResult buildSPIRVModuleFromUGLIR(const UGLIR::Module &module)
    {
        UGLIRSPIRVModuleBuilder builder(module);
        return builder.run();
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
