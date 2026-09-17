#include "UGLIRToMSLEmitter.hpp"
#include <CodeGen/UGLIR/UGLIRVerifier.hpp>

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/UGLIR/UGLIRResourceUsage.hpp>
#include <CodeGen/MSL/MSLTextureTypes.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace UGLC::CodeGen::MSLEmitter
{
    namespace
    {
        /** Stores the Metal spelling and binding metadata for one reflected UGLIR resource. */
        struct MSLResourceParameter
        {
            std::string uglirName;
            std::string metalName;
            std::string entryExpression;
            std::string declaration;
            std::string helperDeclaration;
            std::string argumentBufferFieldDeclaration;
            std::string bindGroupName;
            std::string bindGroupMetalName;
            std::string bindGroupTypeName;
            std::string resourceFieldName;
            UGLIR::ResourceKind kind = UGLIR::ResourceKind::StorageBuffer;
            std::string elementType;
            UGLIR::TextureFormat textureFormat = UGLIR::TextureFormat::Unknown;
            UGLIR::TextureDimension textureDimension = UGLIR::TextureDimension::None;
            UGLIR::ResourceRole resourceRole = UGLIR::ResourceRole::None;
            uint32_t bindingIndex = 0;
            uint32_t resourceIndex = 0;
            uint32_t arrayCount = 1;
            bool usesAtomicAccess = false;
        };

        /** Stores one Metal argument-buffer bind group used by a UGLIR module. */
        struct MSLBindGroupParameter
        {
            std::string bindGroupName;
            std::string metalName;
            std::string typeName;
            uint32_t metalBufferIndex = 0;
        };

        /** Records how a reflected resource is used by reachable UGLIR expressions. */
        struct MSLResourceUsage
        {
            bool readsTexture = false;
            bool writesTexture = false;
            bool usesAtomicAccess = false;
        };

        /** Identifies one struct field that must be emitted as a Metal atomic field. */
        struct MSLAtomicFieldKey
        {
            std::string typeName;
            std::string fieldName;
        };

        /** Stores one Metal framebuffer-fetch parameter generated from a pixel-local input attachment. */
        struct MSLPixelLocalInputAttachment
        {
            std::string fieldName;
            std::string metalParameterName;
            std::string metalTypeName;
            uint32_t colorAttachmentIndex = 0;
            UGLIR::SourceLocation sourceLocation;
        };

        /** Stores the entry ABI expansion for one UGLIR PixelLocalInput parameter. */
        struct MSLPixelLocalInputParameter
        {
            std::string parameterName;
            std::string parameterTypeName;
            std::string inputStructName;
            std::vector<MSLPixelLocalInputAttachment> attachments;
            UGLIR::SourceLocation sourceLocation;
        };

        /** Classifies why a struct appears in a Metal shader ABI. */
        enum class MSLStructRole
        {
            Value,
            VertexInput,
            StageVarying,
            Framebuffer
        };

        /** Returns true when the character can appear in a generated Metal identifier after the first character. */
        bool isMSLIdentifierBodyCharacter(char ch)
        {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        }

        /** Returns true when the character can appear as the first character in a generated Metal identifier. */
        bool isMSLIdentifierHeadCharacter(char ch)
        {
            return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
        }

        /** Returns true when the identifier is a Metal or C++ keyword that should not be emitted directly. */
        bool isReservedMSLIdentifier(const std::string &identifier)
        {
            static const std::unordered_set<std::string> reservedIdentifiers = {
                "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break",
                "case", "catch", "char", "class", "compl", "const", "constexpr", "const_cast", "continue",
                "decltype", "default", "delete", "device", "do", "double", "dynamic_cast", "else", "enum",
                "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "half", "if",
                "inline", "int", "kernel", "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
                "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "reinterpret_cast",
                "return", "sampler", "short", "signed", "sizeof", "static", "static_assert", "static_cast",
                "struct", "switch", "template", "this", "thread", "threadgroup", "throw", "true", "try",
                "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile",
                "while", "xor", "xor_eq",
            };
            return reservedIdentifiers.contains(identifier);
        }

        /** Converts an arbitrary UGLIR symbol into a stable Metal identifier. */
        std::string sanitizeMSLIdentifier(const std::string &name)
        {
            std::string result;
            result.reserve(name.size() + 1);
            for (char ch : name)
            {
                result.push_back(isMSLIdentifierBodyCharacter(ch) ? ch : '_');
            }
            if (result.empty())
            {
                result = "unnamed";
            }
            if (!isMSLIdentifierHeadCharacter(result.front()))
            {
                result.insert(result.begin(), '_');
            }
            if (isReservedMSLIdentifier(result))
            {
                result += "_";
            }
            return result;
        }


        /** Emits one indentation level using four spaces per generated Metal block level. */
        std::string makeIndent(int indentLevel)
        {
            std::string result;
            for (int i = 0; i < indentLevel; ++i)
            {
                result += "    ";
            }
            return result;
        }

        /** Joins generated Metal expression fragments with a comma separator. */
        std::string joinCommaSeparated(const std::vector<std::string> &items)
        {
            std::string result;
            for (size_t index = 0; index < items.size(); ++index)
            {
                result += items[index];
                if (index + 1 < items.size())
                {
                    result += ", ";
                }
            }
            return result;
        }

        /** Maps a UGLIR stage to the public shader stage enum used by shader artifacts. */
        UGLC::CodeGen::ShaderStageKind toShaderStageKind(UGLIR::ShaderStage stage)
        {
            switch (stage)
            {
            case UGLIR::ShaderStage::Vertex:
                return UGLC::CodeGen::ShaderStageKind::Vertex;
            case UGLIR::ShaderStage::Fragment:
                return UGLC::CodeGen::ShaderStageKind::Fragment;
            case UGLIR::ShaderStage::Compute:
                return UGLC::CodeGen::ShaderStageKind::Compute;
            default:
                return UGLC::CodeGen::ShaderStageKind::Compute;
            }
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

        /** Returns the Metal function qualifier for a UGLIR entry kind. */
        std::string metalEntryQualifierForEntryKind(UGLIR::ShaderEntryKind entryKind)
        {
            switch (entryKind)
            {
            case UGLIR::ShaderEntryKind::Vertex:
                return "vertex";
            case UGLIR::ShaderEntryKind::Fragment:
            case UGLIR::ShaderEntryKind::PixelLocal:
                return "fragment";
            case UGLIR::ShaderEntryKind::Compute:
                return "kernel";
            default:
                return "kernel";
            }
        }

        /** Returns the Metal attribute suffix for a stage IO field in the requested ABI role. */
        std::string metalStageFieldAttribute(const UGLIR::TypeField &field, MSLStructRole role, uint32_t location)
        {
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Position)
            {
                return " [[position]]";
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Color ||
                field.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalColor)
            {
                return " [[color(" + std::to_string(location) + ")]]";
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Depth)
            {
                return " [[depth(any)]]";
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Attribute)
            {
                if (role == MSLStructRole::VertexInput)
                {
                    return " [[attribute(" + std::to_string(location) + ")]]";
                }
                return " [[user(locn" + std::to_string(location) + ")]]";
            }
            return {};
        }

        /** Returns true when a UGLIR struct is already declared by the shared Metal prelude. */
        bool isMSLPreludeStructType(const std::string &typeName)
        {
            (void)typeName;
            return false;
        }

        /** Returns true when the string starts with a fixed ASCII prefix. */
        bool startsWith(const std::string &text, const std::string &prefix)
        {
            return text.rfind(prefix, 0) == 0;
        }

        /** Returns a Metal swizzle component name for a zero-based vector component index. */
        std::string metalVectorComponentName(uint32_t componentIndex)
        {
            static constexpr const char *componentNames[] = {"x", "y", "z", "w"};
            return componentIndex < 4u ? componentNames[componentIndex] : std::string();
        }

        /** Returns the scalar Metal texture element type for a UGLIR resource value type. */
        std::string metalTextureElementType(const std::string &typeName)
        {
            switch (UGLIR::scalarKindFromTypeName(typeName))
            {
            case UGLIR::ScalarKind::UInt:
                return "uint";
            case UGLIR::ScalarKind::Int:
                return "int";
            case UGLIR::ScalarKind::Half:
                return "half";
            case UGLIR::ScalarKind::Bool:
                return "bool";
            case UGLIR::ScalarKind::Float:
            case UGLIR::ScalarKind::None:
                return "float";
            }
            return "float";
        }

        /** Returns true when a reflected resource is represented by a Metal texture object. */
        bool isTextureResourceKind(UGLIR::ResourceKind kind)
        {
            return kind == UGLIR::ResourceKind::Texture || kind == UGLIR::ResourceKind::StorageTexture;
        }

        /** Returns the Metal texture object spelling for one structured UGLIR texture dimension. */
        std::string metalTextureObjectTypeName(UGLIR::TextureDimension dimension)
        {
            switch (dimension)
            {
            case UGLIR::TextureDimension::Texture2DArray:
                return "texture2d_array";
            case UGLIR::TextureDimension::Texture3D:
                return "texture3d";
            case UGLIR::TextureDimension::Texture2D:
            case UGLIR::TextureDimension::Subpass:
            case UGLIR::TextureDimension::None:
                return "texture2d";
            }
            return "texture2d";
        }

        /** Returns the four-component Metal value type produced by texture read operations. */
        std::string metalTextureReadVectorType(const MSLResourceParameter &resource)
        {
            std::string scalarType;
            if (resource.textureFormat != UGLIR::TextureFormat::Unknown && resource.kind == UGLIR::ResourceKind::StorageTexture)
            {
                scalarType = UGLC::CodeGen::MSL::MakeFormatToVectorTypeForStorageTexture(UGLIR::textureFormatToken(resource.textureFormat));
            }
            else if (resource.textureFormat != UGLIR::TextureFormat::Unknown && resource.kind == UGLIR::ResourceKind::Texture)
            {
                scalarType = UGLC::CodeGen::MSL::MakeFormatToVectorTypeForTexture(UGLIR::textureFormatToken(resource.textureFormat));
            }
            else
            {
                scalarType = metalTextureElementType(resource.elementType);
            }
            return scalarType + "4";
        }

        /** Returns the Metal access qualifier suffix required by a storage texture's reflected resource contract. */
        std::string metalTextureAccessSuffix(const UGLIR::ResourceBinding &resource, const MSLResourceUsage &usage)
        {
            if (resource.kind != UGLIR::ResourceKind::StorageTexture)
            {
                return {};
            }
            if (resource.accessMode == UGLIR::AccessMode::ReadWrite)
            {
                return ", access::read_write";
            }
            if (usage.readsTexture)
            {
                return ", access::read";
            }
            if (usage.writesTexture)
            {
                return ", access::write";
            }
            return resource.accessMode == UGLIR::AccessMode::ReadWrite ? ", access::read_write" : ", access::read";
        }

        /** Returns a Metal intrinsic or prelude function name for a structured UGLIR intrinsic. */
        std::string mapMetalIntrinsicName(UGLIR::IntrinsicCallKind intrinsicCallKind)
        {
            switch (intrinsicCallKind)
            {
            case UGLIR::IntrinsicCallKind::DiscardFragment:
                return "discard_fragment";
            case UGLIR::IntrinsicCallKind::Clip:
                return "UGLC_clip";
            case UGLIR::IntrinsicCallKind::GroupMemoryBarrier:
                return "GroupMemoryBarrier";
            case UGLIR::IntrinsicCallKind::GroupMemoryBarrierWithGroupSync:
                return "GroupMemoryBarrierWithGroupSync";
            case UGLIR::IntrinsicCallKind::DeviceMemoryBarrier:
                return "DeviceMemoryBarrier";
            case UGLIR::IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync:
                return "DeviceMemoryBarrierWithGroupSync";
            case UGLIR::IntrinsicCallKind::AllMemoryBarrier:
                return "AllMemoryBarrier";
            case UGLIR::IntrinsicCallKind::AllMemoryBarrierWithGroupSync:
                return "AllMemoryBarrierWithGroupSync";
            case UGLIR::IntrinsicCallKind::AtomicAdd:
                return "atomicAdd";
            case UGLIR::IntrinsicCallKind::AtomicAnd:
                return "atomicAnd";
            case UGLIR::IntrinsicCallKind::AtomicCompareExchange:
                return "atomicCompareExchange";
            case UGLIR::IntrinsicCallKind::AtomicLoad:
                return "atomicLoad";
            case UGLIR::IntrinsicCallKind::AtomicMax:
                return "atomicMax";
            case UGLIR::IntrinsicCallKind::AtomicMin:
                return "atomicMin";
            case UGLIR::IntrinsicCallKind::AtomicOr:
                return "atomicOr";
            case UGLIR::IntrinsicCallKind::AtomicStore:
                return "atomicStore";
            case UGLIR::IntrinsicCallKind::MathAbs:
                return "abs";
            case UGLIR::IntrinsicCallKind::MathAcos:
                return "acos";
            case UGLIR::IntrinsicCallKind::MathAll:
                return "all";
            case UGLIR::IntrinsicCallKind::MathAny:
                return "any";
            case UGLIR::IntrinsicCallKind::MathAsin:
                return "asin";
            case UGLIR::IntrinsicCallKind::MathAtan:
                return "atan";
            case UGLIR::IntrinsicCallKind::MathAtan2:
                return "atan2";
            case UGLIR::IntrinsicCallKind::MathCeil:
                return "ceil";
            case UGLIR::IntrinsicCallKind::MathClamp:
                return "clamp";
            case UGLIR::IntrinsicCallKind::MathCos:
                return "cos";
            case UGLIR::IntrinsicCallKind::MathCross:
                return "cross";
            case UGLIR::IntrinsicCallKind::MathDdx:
                return "dfdx";
            case UGLIR::IntrinsicCallKind::MathDdy:
                return "dfdy";
            case UGLIR::IntrinsicCallKind::MathDistance:
                return "distance";
            case UGLIR::IntrinsicCallKind::MathDot:
                return "dot";
            case UGLIR::IntrinsicCallKind::MathExp:
                return "exp";
            case UGLIR::IntrinsicCallKind::MathExp2:
                return "exp2";
            case UGLIR::IntrinsicCallKind::MathFloor:
                return "floor";
            case UGLIR::IntrinsicCallKind::MathFrac:
                return "frac";
            case UGLIR::IntrinsicCallKind::MathFmod:
                return "fmod";
            case UGLIR::IntrinsicCallKind::MathFirstBitHigh:
                return "firstbithigh";
            case UGLIR::IntrinsicCallKind::MathFirstBitLow:
                return "firstbitlow";
            case UGLIR::IntrinsicCallKind::MathLength:
                return "length";
            case UGLIR::IntrinsicCallKind::MathLerp:
                return "lerp";
            case UGLIR::IntrinsicCallKind::MathLog:
                return "log";
            case UGLIR::IntrinsicCallKind::MathLog2:
                return "log2";
            case UGLIR::IntrinsicCallKind::MathMax:
                return "max";
            case UGLIR::IntrinsicCallKind::MathMin:
                return "min";
            case UGLIR::IntrinsicCallKind::MathModf:
                return "modf";
            case UGLIR::IntrinsicCallKind::MathMul:
                return "mul";
            case UGLIR::IntrinsicCallKind::MathNormalize:
                return "normalize";
            case UGLIR::IntrinsicCallKind::MathPow:
                return "pow";
            case UGLIR::IntrinsicCallKind::MathReflect:
                return "reflect";
            case UGLIR::IntrinsicCallKind::MathRound:
                return "rint";
            case UGLIR::IntrinsicCallKind::MathRsqrt:
                return "rsqrt";
            case UGLIR::IntrinsicCallKind::MathSaturate:
                return "saturate";
            case UGLIR::IntrinsicCallKind::MathSign:
                return "sign";
            case UGLIR::IntrinsicCallKind::MathSin:
                return "sin";
            case UGLIR::IntrinsicCallKind::MathSincos:
                return "sincos";
            case UGLIR::IntrinsicCallKind::MathSqrt:
                return "sqrt";
            case UGLIR::IntrinsicCallKind::MathStep:
                return "step";
            case UGLIR::IntrinsicCallKind::MathSmoothstep:
                return "smoothstep";
            case UGLIR::IntrinsicCallKind::MathTan:
                return "tan";
            case UGLIR::IntrinsicCallKind::MathTranspose:
                return "transpose";
            case UGLIR::IntrinsicCallKind::WaveActiveCountBits:
                return "WaveActiveCountBits";
            case UGLIR::IntrinsicCallKind::WavePrefixCountBits:
                return "WavePrefixCountBits";
            case UGLIR::IntrinsicCallKind::WavePrefixSum:
                return "WavePrefixSum";
            case UGLIR::IntrinsicCallKind::WaveReadLaneAt:
                return "WaveReadLaneAt";
            case UGLIR::IntrinsicCallKind::WaveReadLaneFirst:
                return "WaveReadLaneFirst";
            default:
                return {};
            }
        }

        /** Returns the operator token encoded in a lowered UGL overloaded-operator call name. */
        std::string mapOperatorToken(const std::string &functionName)
        {
            if (functionName == "operator_")
            {
                return "^";
            }
            const size_t operatorMarker = functionName.find("operator");
            if (operatorMarker == std::string::npos)
            {
                return {};
            }
            const std::string suffix = functionName.substr(operatorMarker + std::string("operator").size());
            static const std::vector<std::string> operators = {
                "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "|=", "&=", "^=", "==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "+", "-", "*", "/", "%", "<", ">", "|", "&", "^", "!", "~",
            };
            for (const std::string &op : operators)
            {
                if (startsWith(suffix, op))
                {
                    return op;
                }
            }
            return {};
        }

        /** Emits one source location through UGLC's shared diagnostic formatting helpers. */
        std::string formatDiagnostic(const UGLIRToMSLEmissionDiagnostic &diagnostic)
        {
            const std::string message = "UGLIR MSL emitter: " + diagnostic.message;
            if (!diagnostic.sourceLocation.file.empty() && diagnostic.sourceLocation.line != 0 && diagnostic.sourceLocation.column != 0)
            {
                return UGLC::CodeGen::formatClangStyleDiagnostic(diagnostic.sourceLocation.file,
                                                                  diagnostic.sourceLocation.line,
                                                                  diagnostic.sourceLocation.column,
                                                                  message);
            }
            return UGLC::CodeGen::formatUnlocatedDiagnostic(message);
        }

        /** Emits the Phase-4 Metal source body for one UGLIR module. */
        class UGLIRToMSLEmitter
        {
        public:
            /** Creates an emitter that owns no module data and reports diagnostics into the supplied result. */
            UGLIRToMSLEmitter(const UGLIR::Module &module, MSLResourceLayout resourceLayout)
                : mModule(module)
                , mResourceLayout(std::move(resourceLayout))
            {
                for (const UGLIR::Type &type : module.types)
                {
                    mTypesByName.emplace(type.name, &type);
                }
                buildBindGroupLayoutIndexes();
                for (const UGLIR::ResourceBinding &resource : module.reflection.resources)
                {
                    if (resource.kind == UGLIR::ResourceKind::InputAttachment)
                    {
                        continue;
                    }
                    const MSLBindGroupLayout *bindGroupLayout = findBindGroupLayoutForResource(resource);
                    const MSLResourceBindingLayout *resourceLayoutBinding = bindGroupLayout == nullptr ? nullptr : findResourceLayoutForResource(*bindGroupLayout, resource);
                    if (bindGroupLayout == nullptr || resourceLayoutBinding == nullptr)
                    {
                        addDiagnostic(resource.sourceLocation, "Metal resource layout is missing a bind-group entry for reflected resource \"" + resource.name + "\".");
                        continue;
                    }
                    MSLResourceParameter parameter;
                    parameter.uglirName = resource.name;
                    parameter.metalName = sanitizeMSLIdentifier(resource.name);
                    parameter.bindGroupName = bindGroupLayout->bindGroupName;
                    parameter.bindGroupMetalName = sanitizeMSLIdentifier(bindGroupLayout->bindGroupName);
                    parameter.bindGroupTypeName = sanitizeMSLIdentifier(bindGroupLayout->bindGroupTypeName.empty() ? bindGroupLayout->bindGroupName + "BindGroup" : bindGroupLayout->bindGroupTypeName);
                    parameter.resourceFieldName = resourceLayoutBinding->resourceFieldName;
                    parameter.entryExpression = parameter.bindGroupMetalName + "->" + sanitizeMSLIdentifier(resourceLayoutBinding->resourceFieldName);
                    parameter.kind = resource.kind;
                    parameter.elementType = resource.elementType;
                    parameter.textureFormat = resource.textureFormat;
                    parameter.textureDimension = resource.textureDimension;
                    parameter.resourceRole = resource.resourceRole;
                    parameter.bindingIndex = resourceLayoutBinding->bindingIndex;
                    parameter.resourceIndex = resource.resourceIndex;
                    parameter.arrayCount = std::max<uint32_t>(resource.arrayCount, 1u);
                    mResourcesByUGLIRName.emplace(resource.name, std::move(parameter));
                }
                for (const UGLIR::Function &function : module.functions)
                {
                    mFunctionsByName.emplace(function.name, &function);
                }
            }

            /** Emits the module and returns either source text or accumulated diagnostics. */
            UGLIRToMSLEmissionResult run()
            {
                UGLIRToMSLEmissionResult result;
                const UGLIR::Function *entryFunction = findEntryFunction();
                if (entryFunction == nullptr)
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                classifyStructRoles(*entryFunction);
                const std::unordered_map<std::string, MSLResourceUsage> resourceUsage = collectResourceUsage();
                mAtomicWorkgroupVariableNames = collectAtomicWorkgroupVariableNames();
                buildResourceParameterDeclarations(resourceUsage);
                mFunctionResourceUsage = UGLIR::collectFunctionResourceRequirements(mModule);
                mPixelLocalInputParameters = collectPixelLocalInputParameters(*entryFunction);

                std::ostringstream stream;
                emitHalfArithmeticFunctions(stream);
                emitStructDeclarations(stream);
                emitPixelLocalInputStructDeclarations(stream);
                emitBindGroupArgumentStructDeclarations(stream);
                emitInternalHelperFunctionPrototypes(stream);
                emitInternalHelperFunctions(stream);
                emitEntryFunction(stream, *entryFunction);

                if (!mDiagnostics.empty())
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                UGLC::CodeGen::EmittedShaderSource source;
                source.backend = UGLC::CodeGen::ShaderBackendKind::MSL;
                source.stage = toShaderStageKind(entryFunction->stage);
                source.backendName = "MSL";
                source.stageName = UGLC::CodeGen::getShaderStageDisplayName(source.stage);
                source.entryPoint = entryPointNameForEntryKind(entryFunction->entryKind);
                source.debugName = mModule.name + "::" + entryFunction->name;
                source.sourceName = mModule.sourceLocation.file;
                source.sourceText = stream.str();
                source.enableLineDirectives = false;
                result.source = std::move(source);
                return result;
            }

        private:
            const UGLIR::Module &mModule;
            MSLResourceLayout mResourceLayout;
            std::unordered_map<std::string, const UGLIR::Type *> mTypesByName;
            std::unordered_map<std::string, MSLResourceParameter> mResourcesByUGLIRName;
            std::unordered_map<std::string, const UGLIR::Function *> mFunctionsByName;
            std::unordered_map<std::string, const MSLBindGroupLayout *> mBindGroupsByName;
            std::vector<MSLBindGroupParameter> mBindGroupParameters;
            std::unordered_set<std::string> mVertexInputStructNames;
            std::unordered_set<std::string> mStageVaryingStructNames;
            std::unordered_set<std::string> mFramebufferStructNames;
            std::unordered_set<std::string> mAtomicWorkgroupVariableNames;
            std::unordered_set<std::string> mAtomicPlainFieldKeys;
            std::vector<MSLPixelLocalInputParameter> mPixelLocalInputParameters;
            UGLIR::FunctionResourceUsageMap mFunctionResourceUsage;
            const UGLIR::Function *mCurrentFunction = nullptr;
            bool mEmittingEntryBody = false;
            bool mEmittingAtomicRawLValue = false;
            std::vector<UGLIRToMSLEmissionDiagnostic> mDiagnostics;

            /** Records one MSL emission diagnostic against the most precise UGLIR location available. */
            void addDiagnostic(const UGLIR::SourceLocation &location, std::string message)
            {
                mDiagnostics.push_back({location, std::move(message)});
            }

            /** Returns the reflected type record for a lowered UGLIR type name when it exists. */
            const UGLIR::Type *findType(const std::string &typeName) const
            {
                const auto iter = mTypesByName.find(typeName);
                return iter == mTypesByName.end() ? nullptr : iter->second;
            }

            /** Returns the structured scalar/vector/matrix description for one lowered type name. */
            std::optional<UGLIR::ValueTypeDescription> describeValueType(const std::string &typeName) const
            {
                return UGLIR::describeValueType(mModule, typeName);
            }

            /** Returns the Metal scalar spelling for one structured scalar lane category. */
            std::string metalScalarTypeName(UGLIR::ScalarKind scalarKind) const
            {
                switch (scalarKind)
                {
                case UGLIR::ScalarKind::Bool:
                    return "bool";
                case UGLIR::ScalarKind::Int:
                    return "int";
                case UGLIR::ScalarKind::UInt:
                    return "uint";
                case UGLIR::ScalarKind::Float:
                    return "float";
                case UGLIR::ScalarKind::Half:
                    return "half";
                case UGLIR::ScalarKind::None:
                    return {};
                }
                return {};
            }

            /** Returns the Metal value spelling for one structured scalar, vector, or matrix description. */
            std::string metalValueTypeName(const UGLIR::ValueTypeDescription &valueType) const
            {
                const std::string scalarName = metalScalarTypeName(valueType.scalarKind);
                if (scalarName.empty())
                {
                    return {};
                }
                if (valueType.matrixRows != 0 && valueType.matrixColumns != 0)
                {
                    return scalarName + std::to_string(valueType.matrixRows) + "x" + std::to_string(valueType.matrixColumns);
                }
                if (valueType.vectorWidth > 1u)
                {
                    return scalarName + std::to_string(valueType.vectorWidth);
                }
                return scalarName;
            }

            /** Returns the native Metal spelling for a lowered scalar, vector, matrix, or void type. */
            std::string canonicalMetalValueTypeName(const std::string &typeName) const
            {
                if (const UGLIR::Type *type = findType(typeName); type != nullptr && type->kind == UGLIR::TypeKind::Void)
                {
                    return "void";
                }
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() ? metalValueTypeName(*valueType) : std::string();
            }

            /** Returns true when one lowered type is a scalar unsigned integer value. */
            bool isUnsignedIntegerValueType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() && valueType->scalarKind == UGLIR::ScalarKind::UInt &&
                       valueType->vectorWidth == 1u && valueType->matrixColumns == 0;
            }

            /** Returns true when one lowered type is a scalar signed integer value. */
            bool isSignedIntegerValueType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() && valueType->scalarKind == UGLIR::ScalarKind::Int &&
                       valueType->vectorWidth == 1u && valueType->matrixColumns == 0;
            }

            /** Returns true when one lowered type is a scalar 32-bit float value. */
            bool isFloatValueType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() && valueType->scalarKind == UGLIR::ScalarKind::Float &&
                       valueType->vectorWidth == 1u && valueType->matrixColumns == 0;
            }

            /** Returns true when one lowered type is a scalar boolean value. */
            bool isBooleanValueType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() && valueType->scalarKind == UGLIR::ScalarKind::Bool &&
                       valueType->vectorWidth == 1u && valueType->matrixColumns == 0;
            }

            /** Returns true when one lowered type is the void type registered by lowering. */
            bool isVoidType(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                return type != nullptr && type->kind == UGLIR::TypeKind::Void;
            }

            /** Returns the scalar Metal texture element spelling for one lowered resource element type. */
            std::string metalTextureElementType(const std::string &typeName) const
            {
                const std::optional<UGLIR::ValueTypeDescription> valueType = describeValueType(typeName);
                return valueType.has_value() ? metalScalarTypeName(valueType->scalarKind) : UGLC::CodeGen::MSLEmitter::metalTextureElementType(typeName);
            }

            /** Returns the scalar Metal texture element spelling for one reflected resource contract. */
            std::string metalTextureElementType(const UGLIR::ResourceBinding &resource) const
            {
                if (resource.kind == UGLIR::ResourceKind::StorageTexture && resource.textureFormat != UGLIR::TextureFormat::Unknown)
                {
                    return UGLC::CodeGen::MSL::MakeFormatToVectorTypeForStorageTexture(UGLIR::textureFormatToken(resource.textureFormat));
                }
                if (resource.kind == UGLIR::ResourceKind::Texture && resource.textureFormat != UGLIR::TextureFormat::Unknown)
                {
                    return UGLC::CodeGen::MSL::MakeFormatToVectorTypeForTexture(UGLIR::textureFormatToken(resource.textureFormat));
                }
                return metalTextureElementType(resource.elementType);
            }

            /** Builds quick lookup tables for the host-computed Metal bind-group layout. */
            void buildBindGroupLayoutIndexes()
            {
                std::unordered_set<std::string> seenBindGroupNames;
                for (const MSLBindGroupLayout &layout : mResourceLayout.bindGroups)
                {
                    if (layout.bindGroupName.empty())
                    {
                        addDiagnostic(mModule.sourceLocation, "Metal resource layout contains an unnamed bind group.");
                        continue;
                    }
                    if (!seenBindGroupNames.insert(layout.bindGroupName).second)
                    {
                        addDiagnostic(mModule.sourceLocation, "Metal resource layout contains duplicate bind group \"" + layout.bindGroupName + "\".");
                        continue;
                    }

                    std::unordered_set<uint32_t> seenResourceBindings;
                    for (const MSLResourceBindingLayout &resource : layout.resources)
                    {
                        if (!seenResourceBindings.insert(resource.bindingIndex).second)
                        {
                            addDiagnostic(mModule.sourceLocation,
                                          "Metal resource layout for bind group \"" + layout.bindGroupName
                                              + "\" contains duplicate resource binding " + std::to_string(resource.bindingIndex) + ".");
                        }
                    }

                    mBindGroupsByName.emplace(layout.bindGroupName, &layout);
                }
            }

            /** Finds the host-computed Metal bind group that owns one reflected UGLIR resource. */
            const MSLBindGroupLayout *findBindGroupLayoutForResource(const UGLIR::ResourceBinding &resource) const
            {
                const size_t separator = resource.name.find('.');
                if (separator == std::string::npos)
                {
                    return nullptr;
                }
                const std::string bindGroupName = resource.name.substr(0, separator);
                const auto iter = mBindGroupsByName.find(bindGroupName);
                return iter == mBindGroupsByName.end() ? nullptr : iter->second;
            }

            /** Finds the Metal argument-buffer field layout that corresponds to one reflected UGLIR resource. */
            const MSLResourceBindingLayout *findResourceLayoutForResource(const MSLBindGroupLayout &layout, const UGLIR::ResourceBinding &resource) const
            {
                for (const MSLResourceBindingLayout &resourceLayout : layout.resources)
                {
                    if (resourceLayout.resourceName == resource.name)
                    {
                        return &resourceLayout;
                    }
                }
                for (const MSLResourceBindingLayout &resourceLayout : layout.resources)
                {
                    if (resourceLayout.bindingIndex == resource.bindingIndex)
                    {
                        return &resourceLayout;
                    }
                }
                return nullptr;
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

            /** Returns true when a type name refers to a user-visible record type rather than a scalar, vector, or resource. */
            bool isStructTypeName(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                return type != nullptr && type->kind == UGLIR::TypeKind::Struct;
            }

            /** Records how entry parameters and return values use struct types in Metal shader ABI. */
            void classifyStructRoles(const UGLIR::Function &entryFunction)
            {
                if (entryFunction.entryKind == UGLIR::ShaderEntryKind::Vertex)
                {
                    for (const UGLIR::FunctionParameter &parameter : entryFunction.parameters)
                    {
                        if (isStructTypeName(parameter.type))
                        {
                            mVertexInputStructNames.insert(parameter.type);
                        }
                    }
                    if (isStructTypeName(entryFunction.returnType))
                    {
                        mStageVaryingStructNames.insert(entryFunction.returnType);
                    }
                    return;
                }

                if (entryFunction.entryKind == UGLIR::ShaderEntryKind::Fragment ||
                    entryFunction.entryKind == UGLIR::ShaderEntryKind::PixelLocal)
                {
                    for (const UGLIR::FunctionParameter &parameter : entryFunction.parameters)
                    {
                        if (isStructTypeName(parameter.type))
                        {
                            mStageVaryingStructNames.insert(parameter.type);
                        }
                    }
                    if (isStructTypeName(entryFunction.returnType))
                    {
                        mFramebufferStructNames.insert(entryFunction.returnType);
                    }
                }
            }

            /** Returns the Metal ABI role for a struct type name. */
            MSLStructRole structRoleForTypeName(const std::string &typeName) const
            {
                if (mFramebufferStructNames.contains(typeName))
                {
                    return MSLStructRole::Framebuffer;
                }
                if (mVertexInputStructNames.contains(typeName))
                {
                    return MSLStructRole::VertexInput;
                }
                if (mStageVaryingStructNames.contains(typeName))
                {
                    return MSLStructRole::StageVarying;
                }
                return MSLStructRole::Value;
            }

            /** Returns the stage-output location for a framebuffer field when reflection carries a more precise value. */
            uint32_t stageOutputLocationForField(const UGLIR::TypeField &field) const
            {
                for (const UGLIR::StageIOBinding &output : mModule.reflection.stageOutputs)
                {
                    if (output.name == field.name)
                    {
                        return output.location;
                    }
                }
                return field.location;
            }

            /** Returns true when a framebuffer struct field is part of the active reflected output ABI. */
            bool isActiveFramebufferOutputField(const UGLIR::TypeField &field) const
            {
                for (const UGLIR::StageIOBinding &output : mModule.reflection.stageOutputs)
                {
                    if (output.name == field.name)
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns the field metadata for a named field inside a lowered struct type. */
            const UGLIR::TypeField *findTypeField(const std::string &typeName, const std::string &fieldName) const
            {
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    return nullptr;
                }
                for (const UGLIR::TypeField &field : type->fields)
                {
                    if (field.name == fieldName)
                    {
                        return &field;
                    }
                }
                return nullptr;
            }

            /** Returns the framebuffer color location used by Metal framebuffer-fetch parameters. */
            uint32_t pixelLocalInputColorAttachmentIndex(const UGLIR::FunctionParameter &parameter,
                                                         const UGLIR::ResourceBinding &resource,
                                                         const std::string &fieldName) const
            {
                for (const UGLIR::StageIOBinding &input : mModule.reflection.stageInputs)
                {
                    if (input.name == fieldName &&
                        (input.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalColor ||
                         input.semanticKind == UGLIR::BuiltinSemanticKind::Color))
                    {
                        return input.location;
                    }
                }
                if (const UGLIR::TypeField *field = findTypeField(parameter.type, fieldName))
                {
                    return field->location;
                }
                return resource.inputAttachmentIndex;
            }

            /** Returns the Metal parameter type for one pixel-local framebuffer-fetch attachment. */
            std::string pixelLocalInputAttachmentTypeName(const UGLIR::ResourceBinding &resource)
            {
                if (resource.textureFormat != UGLIR::TextureFormat::Unknown)
                {
                    return UGLC::CodeGen::MSL::MakeFormatToVectorTypeForFrameBuffer(UGLIR::textureFormatToken(resource.textureFormat));
                }
                return emitTypeName(resource.elementType, resource.sourceLocation);
            }

            /** Returns a stable key for one atomic-backed plain field inside a shader value struct. */
            std::string makeAtomicPlainFieldKey(const std::string &typeName, const std::string &fieldName) const
            {
                return typeName + "." + fieldName;
            }

            /** Returns the atomic field key referenced by one member expression, or an empty string for ordinary fields. */
            std::string atomicPlainFieldKeyForExpression(const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Cast ||
                    expression.kind == UGLIR::ExpressionKind::Load ||
                    expression.kind == UGLIR::ExpressionKind::Construct)
                {
                    return expression.operands.empty() ? std::string() : atomicPlainFieldKeyForExpression(expression.operands.front());
                }
                if (expression.kind != UGLIR::ExpressionKind::MemberRef || expression.operands.empty() || expression.name.empty())
                {
                    return {};
                }
                if (!isUnsignedIntegerValueType(expression.type) && !isSignedIntegerValueType(expression.type))
                {
                    return {};
                }
                return makeAtomicPlainFieldKey(expression.operands.front().type, expression.name);
            }

            /** Returns true when one expression names a scalar storage-buffer element emitted as a Metal atomic pointer. */
            bool isAtomicScalarStorageBufferExpression(const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Cast ||
                    expression.kind == UGLIR::ExpressionKind::Load ||
                    expression.kind == UGLIR::ExpressionKind::Construct)
                {
                    return !expression.operands.empty() && isAtomicScalarStorageBufferExpression(expression.operands.front());
                }
                const std::string resourceName = resourceNameForAccessExpression(expression);
                if (resourceName.empty())
                {
                    return false;
                }
                const auto iter = mResourcesByUGLIRName.find(resourceName);
                if (iter == mResourcesByUGLIRName.end() || !iter->second.usesAtomicAccess)
                {
                    return false;
                }
                return isUnsignedIntegerValueType(expression.type) || isSignedIntegerValueType(expression.type);
            }

            /** Returns true when one lvalue must use Metal atomic load/store helper calls. */
            bool isAtomicBackedLValueExpression(const UGLIR::Expression &expression) const
            {
                const std::string workgroupName = atomicWorkgroupVariableNameForExpression(expression);
                if (!workgroupName.empty() && mAtomicWorkgroupVariableNames.contains(workgroupName))
                {
                    const UGLIR::Type *type = findType(expression.type);
                    return type == nullptr || type->kind != UGLIR::TypeKind::Array;
                }
                const std::string fieldKey = atomicPlainFieldKeyForExpression(expression);
                if (!fieldKey.empty() && mAtomicPlainFieldKeys.contains(fieldKey))
                {
                    return true;
                }
                return isAtomicScalarStorageBufferExpression(expression);
            }

            /** Emits one atomic-backed lvalue without wrapping it in an implicit atomic load. */
            std::string emitAtomicRawLValueExpression(const UGLIR::Expression &expression)
            {
                const bool previousMode = mEmittingAtomicRawLValue;
                mEmittingAtomicRawLValue = true;
                const std::string result = emitExpression(expression);
                mEmittingAtomicRawLValue = previousMode;
                return result;
            }

            /** Collects the Metal ABI expansion required for all UGLIR PixelLocalInput parameters. */
            std::vector<MSLPixelLocalInputParameter> collectPixelLocalInputParameters(const UGLIR::Function &function)
            {
                std::vector<MSLPixelLocalInputParameter> result;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind != UGLIR::BuiltinSemanticKind::PixelLocalInput)
                    {
                        continue;
                    }
                    if (findType(parameter.type) == nullptr)
                    {
                        addDiagnostic(parameter.sourceLocation, "PixelLocalInput parameter \"" + parameter.name + "\" uses an unknown framebuffer type \"" + parameter.type + "\".");
                        continue;
                    }

                    MSLPixelLocalInputParameter inputParameter;
                    inputParameter.parameterName = sanitizeMSLIdentifier(parameter.name);
                    inputParameter.parameterTypeName = parameter.type;
                    inputParameter.inputStructName = sanitizeMSLIdentifier(parameter.type) + "_PixelLocalInput";
                    inputParameter.sourceLocation = parameter.sourceLocation;

                    const std::string resourcePrefix = parameter.name + ".";
                    for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                    {
                        if (resource.kind != UGLIR::ResourceKind::InputAttachment ||
                            resource.name.rfind(resourcePrefix, 0) != 0)
                        {
                            continue;
                        }
                        const std::string fieldName = resource.name.substr(resourcePrefix.size());
                        inputParameter.attachments.push_back(MSLPixelLocalInputAttachment{
                            .fieldName = fieldName,
                            .metalParameterName = "_UGLC_PixelLocalInput_" + inputParameter.parameterName + "_" + sanitizeMSLIdentifier(fieldName),
                            .metalTypeName = pixelLocalInputAttachmentTypeName(resource),
                            .colorAttachmentIndex = pixelLocalInputColorAttachmentIndex(parameter, resource, fieldName),
                            .sourceLocation = resource.sourceLocation,
                        });
                    }

                    std::sort(inputParameter.attachments.begin(),
                              inputParameter.attachments.end(),
                              [](const MSLPixelLocalInputAttachment &left, const MSLPixelLocalInputAttachment &right) {
                                  if (left.colorAttachmentIndex != right.colorAttachmentIndex)
                                  {
                                      return left.colorAttachmentIndex < right.colorAttachmentIndex;
                                  }
                                  return left.fieldName < right.fieldName;
                              });
                    result.push_back(std::move(inputParameter));
                }
                return result;
            }

            /** Finds a previously collected PixelLocalInput expansion plan by sanitized parameter name. */
            const MSLPixelLocalInputParameter *findPixelLocalInputParameter(const UGLIR::FunctionParameter &parameter) const
            {
                const std::string parameterName = sanitizeMSLIdentifier(parameter.name);
                for (const MSLPixelLocalInputParameter &inputParameter : mPixelLocalInputParameters)
                {
                    if (inputParameter.parameterName == parameterName)
                    {
                        return &inputParameter;
                    }
                }
                return nullptr;
            }

            /** Returns true when a struct field should be emitted for the requested Metal ABI role. */
            bool shouldEmitStructField(const UGLIR::TypeField &field, MSLStructRole role) const
            {
                if (role != MSLStructRole::Framebuffer)
                {
                    return true;
                }
                return field.semanticKind != UGLIR::BuiltinSemanticKind::PixelLocalDepth && isActiveFramebufferOutputField(field);
            }

            /** Returns true when a type name denotes UGL workgroup storage. */
            bool isWorkgroupTypeName(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                return type != nullptr && type->kind == UGLIR::TypeKind::Workgroup;
            }

            /** Returns the shader value type stored inside a workgroup-storage wrapper. */
            std::string workgroupElementTypeName(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr || type->kind != UGLIR::TypeKind::Workgroup || type->elementType.empty())
                {
                    addDiagnostic(location, "workgroup type \"" + typeName + "\" is missing its element type.");
                    return "uint";
                }
                return type->elementType;
            }

            /** Converts one UGLIR type name into the Metal type spelling supported by Phase 4. */
            std::string emitTypeName(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                if (const std::string canonicalTypeName = canonicalMetalValueTypeName(typeName); !canonicalTypeName.empty())
                {
                    return canonicalTypeName;
                }

                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    addDiagnostic(location, "unsupported UGLIR type \"" + typeName + "\".");
                    return sanitizeMSLIdentifier(typeName);
                }

                switch (type->kind)
                {
                case UGLIR::TypeKind::Void:
                    return "void";
                case UGLIR::TypeKind::Bool:
                    return "bool";
                case UGLIR::TypeKind::Int:
                    if (type->bitWidth == 32)
                    {
                        return "int";
                    }
                    break;
                case UGLIR::TypeKind::UInt:
                    if (type->bitWidth == 32)
                    {
                        return "uint";
                    }
                    break;
                case UGLIR::TypeKind::Float:
                    if (type->bitWidth == 32)
                    {
                        return "float";
                    }
                    break;
                case UGLIR::TypeKind::Half:
                    return "half";
                case UGLIR::TypeKind::Vector:
                    return emitTypeName(type->elementType, location) + std::to_string(type->vectorWidth);
                case UGLIR::TypeKind::Struct:
                    return sanitizeMSLIdentifier(type->name);
                case UGLIR::TypeKind::Array:
                    return emitTypeName(type->elementType, location);
                case UGLIR::TypeKind::Workgroup:
                    return emitTypeName(type->elementType, location);
                case UGLIR::TypeKind::Buffer:
                    return std::string(type->accessMode == UGLIR::AccessMode::Read ? "device const " : "device ") + emitTypeName(type->elementType, location) + "*";
                case UGLIR::TypeKind::Texture:
                {
                    const std::string textureType = metalTextureObjectTypeName(type->textureDimension);
                    const std::string accessSuffix = type->accessMode == UGLIR::AccessMode::ReadWrite ? ", access::read_write" : "";
                    std::string elementType = metalTextureElementType(type->elementType);
                    if (type->textureFormat != UGLIR::TextureFormat::Unknown)
                    {
                        elementType = type->resourceKind == UGLIR::ResourceKind::StorageTexture
                                          ? UGLC::CodeGen::MSL::MakeFormatToVectorTypeForStorageTexture(UGLIR::textureFormatToken(type->textureFormat))
                                          : UGLC::CodeGen::MSL::MakeFormatToVectorTypeForTexture(UGLIR::textureFormatToken(type->textureFormat));
                    }
                    return textureType + "<" + elementType + accessSuffix + ">";
                }
                case UGLIR::TypeKind::Sampler:
                    return "sampler";
                default:
                    break;
                }

                addDiagnostic(location, "unsupported UGLIR type kind for \"" + typeName + "\".");
                return sanitizeMSLIdentifier(typeName);
            }

            /** Returns true when a UGLIR type is a shader resource alias that should not appear in helper ABI. */
            bool isResourceAliasType(const std::string &typeName) const
            {
                const UGLIR::Type *type = findType(typeName);
                return type != nullptr && type->kind == UGLIR::TypeKind::Resource;
            }

            /** Returns true when a struct is a DSL resource holder rather than a shader-value struct. */
            bool hasResourceField(const UGLIR::Type &type) const
            {
                for (const UGLIR::TypeField &field : type.fields)
                {
                    const UGLIR::Type *fieldType = findType(field.type);
                    if (fieldType != nullptr && fieldType->isImplementationOnly)
                    {
                        return true;
                    }
                    if (fieldType != nullptr &&
                        (fieldType->kind == UGLIR::TypeKind::Resource || fieldType->kind == UGLIR::TypeKind::Buffer ||
                         fieldType->kind == UGLIR::TypeKind::Texture || fieldType->kind == UGLIR::TypeKind::Sampler))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Emits every fixed-size array dimension, from the outermost array to its element type. */
            std::string emitArrayDimensions(const UGLIR::Type &type) const
            {
                std::string dimensions;
                const UGLIR::Type *arrayType = &type;
                while (arrayType != nullptr && arrayType->kind == UGLIR::TypeKind::Array)
                {
                    dimensions += "[" + std::to_string(arrayType->arrayCount) + "]";
                    arrayType = findType(arrayType->elementType);
                }
                return dimensions;
            }

            /** Emits a Metal struct field declaration while preserving fixed-size array fields. */
            std::string emitStructFieldDeclaration(const UGLIR::TypeField &field,
                                                   const std::string &fieldType,
                                                   const std::string &fieldAttribute) const
            {
                const UGLIR::Type *type = findType(field.type);
                if (type != nullptr && type->kind == UGLIR::TypeKind::Array)
                {
                    return fieldType + " " + sanitizeMSLIdentifier(field.name) + emitArrayDimensions(*type) + fieldAttribute;
                }
                return fieldType + " " + sanitizeMSLIdentifier(field.name) + fieldAttribute;
            }

            /** Emits all user struct declarations required by helper signatures and local variables. */
            void emitStructDeclarations(std::ostringstream &stream)
            {
                for (const UGLIR::Type &type : mModule.types)
                {
                    if (type.kind != UGLIR::TypeKind::Struct || type.fields.empty() || hasResourceField(type) ||
                        isMSLPreludeStructType(type.name) || type.isImplementationOnly)
                    {
                        continue;
                    }
                    const MSLStructRole role = structRoleForTypeName(type.name);
                    stream << "struct " << sanitizeMSLIdentifier(type.name) << "\n{\n";
                    for (const UGLIR::TypeField &field : type.fields)
                    {
                        if (!shouldEmitStructField(field, role))
                        {
                            continue;
                        }
                        const uint32_t location = role == MSLStructRole::Framebuffer ? stageOutputLocationForField(field) : field.location;
                        std::string fieldType = emitTypeName(field.type, field.sourceLocation);
                        const std::string atomicFieldKey = makeAtomicPlainFieldKey(type.name, field.name);
                        if (mAtomicPlainFieldKeys.contains(atomicFieldKey))
                        {
                            if (isUnsignedIntegerValueType(field.type))
                            {
                                fieldType = "atomic_uint";
                            }
                            else if (isSignedIntegerValueType(field.type))
                            {
                                fieldType = "atomic_int";
                            }
                            else
                        {
                            addDiagnostic(field.sourceLocation, "Metal atomic struct fields are only supported for int and uint values.");
                        }
                    }
                        stream << "    " << emitStructFieldDeclaration(field, fieldType, metalStageFieldAttribute(field, role, location)) << ";\n";
                    }
                    stream << "};\n\n";
                }
            }

            /** Emits plain pixel-local input structs used to materialize Metal framebuffer-fetch parameters. */
            void emitPixelLocalInputStructDeclarations(std::ostringstream &stream)
            {
                std::unordered_set<std::string> emittedStructNames;
                for (const MSLPixelLocalInputParameter &inputParameter : mPixelLocalInputParameters)
                {
                    if (!emittedStructNames.insert(inputParameter.inputStructName).second)
                    {
                        continue;
                    }
                    const UGLIR::Type *inputType = findType(inputParameter.parameterTypeName);
                    if (inputType == nullptr)
                    {
                        addDiagnostic(inputParameter.sourceLocation, "PixelLocalInput struct type \"" + inputParameter.parameterTypeName + "\" was not registered in UGLIR.");
                        continue;
                    }

                    stream << "struct " << inputParameter.inputStructName << "\n{\n";
                    for (const UGLIR::TypeField &field : inputType->fields)
                    {
                        stream << "    " << emitTypeName(field.type, field.sourceLocation) << " "
                               << sanitizeMSLIdentifier(field.name) << ";\n";
                    }
                    stream << "};\n\n";
                }
            }

            /** Emits the Metal argument-buffer structs that mirror host bind-group layouts. */
            void emitBindGroupArgumentStructDeclarations(std::ostringstream &stream)
            {
                std::unordered_set<std::string> emittedTypeNames;
                for (const MSLBindGroupParameter &bindGroup : mBindGroupParameters)
                {
                    if (!emittedTypeNames.insert(bindGroup.typeName).second)
                    {
                        continue;
                    }
                    stream << "struct " << bindGroup.typeName << "\n{\n";
                    for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                    {
                        const auto iter = mResourcesByUGLIRName.find(resource.name);
                        if (iter == mResourcesByUGLIRName.end() || iter->second.bindGroupName != bindGroup.bindGroupName)
                        {
                            continue;
                        }
                        stream << "    " << iter->second.argumentBufferFieldDeclaration << ";\n";
                    }
                    stream << "};\n\n";
                }
            }

            /** Builds all Metal resource parameter declarations from UGLIR reflection. */
            void buildResourceParameterDeclarations(const std::unordered_map<std::string, MSLResourceUsage> &resourceUsage)
            {
                std::unordered_set<std::string> usedBindGroups;
                for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                {
                    auto iter = mResourcesByUGLIRName.find(resource.name);
                    if (iter == mResourcesByUGLIRName.end())
                    {
                        continue;
                    }
                    if (usedBindGroups.insert(iter->second.bindGroupName).second)
                    {
                        const MSLBindGroupLayout *bindGroupLayout = findBindGroupLayoutForResource(resource);
                        if (bindGroupLayout != nullptr)
                        {
                            mBindGroupParameters.push_back(MSLBindGroupParameter{
                                .bindGroupName = bindGroupLayout->bindGroupName,
                                .metalName = sanitizeMSLIdentifier(bindGroupLayout->bindGroupName),
                                .typeName = sanitizeMSLIdentifier(bindGroupLayout->bindGroupTypeName.empty() ? bindGroupLayout->bindGroupName + "BindGroup" : bindGroupLayout->bindGroupTypeName),
                                .metalBufferIndex = bindGroupLayout->metalBufferIndex,
                            });
                        }
                    }

                    if (resource.kind == UGLIR::ResourceKind::StorageBuffer)
                    {
                        const std::string elementType = emitTypeName(resource.elementType, resource.sourceLocation);
                        const bool readOnly = resource.accessMode == UGLIR::AccessMode::Read;
                        const auto usageIter = resourceUsage.find(resource.name);
                        const bool usesAtomicAccess = usageIter != resourceUsage.end() && usageIter->second.usesAtomicAccess;
                        iter->second.usesAtomicAccess = usesAtomicAccess;
                        std::string pointerElementType = elementType;
                        if (usesAtomicAccess)
                        {
                            if (isUnsignedIntegerValueType(resource.elementType))
                            {
                                pointerElementType = "atomic_uint";
                            }
                            else if (isSignedIntegerValueType(resource.elementType))
                            {
                                pointerElementType = "atomic_int";
                            }
                            else
                            {
                                addDiagnostic(resource.sourceLocation, "Metal atomics are only supported for int and uint storage-buffer elements.");
                            }
                        }
                        const std::string addressSpace = readOnly ? "const device " : "device ";
                        iter->second.helperDeclaration = addressSpace + pointerElementType + "* " + iter->second.metalName;
                        iter->second.argumentBufferFieldDeclaration = addressSpace + pointerElementType + "* "
                                                                      + sanitizeMSLIdentifier(iter->second.resourceFieldName)
                                                                      + " [[id(" + std::to_string(iter->second.bindingIndex) + ")]]";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::UniformBuffer)
                    {
                        const std::string elementType = emitTypeName(resource.elementType, resource.sourceLocation);
                        iter->second.helperDeclaration = "constant " + elementType + "* " + iter->second.metalName;
                        iter->second.argumentBufferFieldDeclaration = "constant " + elementType + "* "
                                                                      + sanitizeMSLIdentifier(iter->second.resourceFieldName)
                                                                      + " [[id(" + std::to_string(iter->second.bindingIndex) + ")]]";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::Texture || resource.kind == UGLIR::ResourceKind::StorageTexture)
                    {
                        const std::string textureType = metalTextureObjectTypeName(resource.textureDimension);
                        const auto usageIter = resourceUsage.find(resource.name);
                        const MSLResourceUsage usage = usageIter == resourceUsage.end() ? MSLResourceUsage{} : usageIter->second;
                        const std::string accessSuffix = metalTextureAccessSuffix(resource, usage);
                        const std::string textureObjectType = textureType + "<" + metalTextureElementType(resource) + accessSuffix + ">";
                        if (resource.resourceRole == UGLIR::ResourceRole::TextureValue && resource.arrayCount > 1u)
                        {
                            const bool writable = resource.accessMode == UGLIR::AccessMode::ReadWrite;
                            const std::string wrapperType = UGLC::CodeGen::MSL::getBindlessTextureWrapperFromTemplateType(writable, metalTextureElementType(resource));
                            if (wrapperType.empty())
                            {
                                addDiagnostic(resource.sourceLocation, "Metal texture component array requires a supported scalar element type.");
                                continue;
                            }
                            iter->second.helperDeclaration = "device " + wrapperType + "* " + iter->second.metalName;
                            iter->second.argumentBufferFieldDeclaration = "device " + wrapperType + "* "
                                                                          + sanitizeMSLIdentifier(iter->second.resourceFieldName)
                                                                          + " [[id(" + std::to_string(iter->second.bindingIndex) + ")]]";
                            continue;
                        }
                        const std::string textureParameterType = textureObjectType;
                        iter->second.helperDeclaration = textureParameterType + " " + iter->second.metalName;
                        iter->second.argumentBufferFieldDeclaration = textureParameterType + " "
                                                                      + sanitizeMSLIdentifier(iter->second.resourceFieldName)
                                                                      + " [[id(" + std::to_string(iter->second.bindingIndex) + ")]]";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::Sampler)
                    {
                        iter->second.helperDeclaration = "sampler " + iter->second.metalName;
                        iter->second.argumentBufferFieldDeclaration = "sampler " + sanitizeMSLIdentifier(iter->second.resourceFieldName)
                                                                      + " [[id(" + std::to_string(iter->second.bindingIndex) + ")]]";
                        continue;
                    }

                    addDiagnostic(resource.sourceLocation, "unsupported resource kind for Phase 8 MSL emission.");
                }
            }

            /** Scans reachable UGLIR expressions to classify texture and atomic resource usage before emitting parameters. */
            std::unordered_map<std::string, MSLResourceUsage> collectResourceUsage()
            {
                std::unordered_map<std::string, MSLResourceUsage> usage;
                mAtomicPlainFieldKeys.clear();
                for (const UGLIR::Function &function : mModule.functions)
                {
                    collectStatementListResourceUsage(function.body, usage);
                }
                return usage;
            }

            /** Returns the texture DSL operation represented by a UGLIR call expression. */
            UGLC::CodeGen::TextureMemberCallKind textureMemberCallKindForExpression(const UGLIR::Expression &expression) const
            {
                switch (expression.intrinsicCallKind)
                {
                case UGLIR::IntrinsicCallKind::TextureRead:
                    return UGLC::CodeGen::TextureMemberCallKind::Read;
                case UGLIR::IntrinsicCallKind::TextureWrite:
                    return UGLC::CodeGen::TextureMemberCallKind::Write;
                case UGLIR::IntrinsicCallKind::TextureSample:
                    return UGLC::CodeGen::TextureMemberCallKind::Sample;
                case UGLIR::IntrinsicCallKind::TextureSampleLevel:
                    return UGLC::CodeGen::TextureMemberCallKind::SampleLevel;
                case UGLIR::IntrinsicCallKind::TextureSampleGrad:
                    return UGLC::CodeGen::TextureMemberCallKind::SampleGrad;
                case UGLIR::IntrinsicCallKind::TextureGather:
                    return UGLC::CodeGen::TextureMemberCallKind::Gather;
                case UGLIR::IntrinsicCallKind::TextureGatherRed:
                    return UGLC::CodeGen::TextureMemberCallKind::GatherRed;
                case UGLIR::IntrinsicCallKind::TextureGatherGreen:
                    return UGLC::CodeGen::TextureMemberCallKind::GatherGreen;
                case UGLIR::IntrinsicCallKind::TextureGatherBlue:
                    return UGLC::CodeGen::TextureMemberCallKind::GatherBlue;
                case UGLIR::IntrinsicCallKind::TextureGatherAlpha:
                    return UGLC::CodeGen::TextureMemberCallKind::GatherAlpha;
                case UGLIR::IntrinsicCallKind::TextureGetDimensions:
                    return UGLC::CodeGen::TextureMemberCallKind::GetDimensions;
                default:
                    return UGLC::CodeGen::TextureMemberCallKind::Unknown;
                }
            }

            /** Scans a statement list for reflected resource operations. */
            void collectStatementListResourceUsage(const std::vector<UGLIR::Statement> &statements,
                                                   std::unordered_map<std::string, MSLResourceUsage> &usage)
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        collectExpressionResourceUsage(expression, usage);
                    }
                    collectStatementListResourceUsage(statement.children, usage);
                    collectStatementListResourceUsage(statement.elseChildren, usage);
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        collectStatementListResourceUsage(switchCase.body, usage);
                    }
                }
            }

            /** Scans one expression tree for reflected resource operations. */
            void collectExpressionResourceUsage(const UGLIR::Expression &expression,
                                                std::unordered_map<std::string, MSLResourceUsage> &usage)
            {
                if (expression.kind == UGLIR::ExpressionKind::Call && !expression.operands.empty())
                {
                    const std::string resourceName = resourceNameForAccessExpression(expression.operands.front());
                    if (!resourceName.empty())
                    {
                        const UGLC::CodeGen::TextureMemberCallKind methodKind = textureMemberCallKindForExpression(expression);
                        if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Read ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::Sample ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleLevel ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleGrad ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::Gather ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherRed ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherGreen ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherBlue ||
                            methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherAlpha)
                        {
                            usage[resourceName].readsTexture = true;
                        }
                        else if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Write)
                        {
                            usage[resourceName].writesTexture = true;
                        }
                    }
                    if (isAtomicIntrinsicCall(expression.intrinsicCallKind))
                    {
                        const std::string atomicFieldKey = atomicPlainFieldKeyForExpression(expression.operands.front());
                        if (!atomicFieldKey.empty())
                        {
                            mAtomicPlainFieldKeys.insert(atomicFieldKey);
                        }
                        const std::string atomicResourceName = resourceNameForAccessExpression(expression.operands.front());
                        if (!atomicResourceName.empty())
                        {
                            usage[atomicResourceName].usesAtomicAccess = true;
                        }
                    }
                }

                for (const UGLIR::Expression &operand : expression.operands)
                {
                    collectExpressionResourceUsage(operand, usage);
                }
            }

            /** Emits helper function declarations so mutually ordered helper calls resolve in Metal. */
            void emitInternalHelperFunctionPrototypes(std::ostringstream &stream)
            {
                bool emittedAnyPrototype = false;
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (function.isEntryPoint)
                    {
                        continue;
                    }
                    stream << emitFunctionSignature(function) << ";\n";
                    emittedAnyPrototype = true;
                }
                if (emittedAnyPrototype)
                {
                    stream << "\n";
                }
            }

            /** Emits all internal helper function definitions before the entry function body. */
            void emitInternalHelperFunctions(std::ostringstream &stream)
            {
                for (const UGLIR::Function &function : mModule.functions)
                {
                    if (function.isEntryPoint)
                    {
                        continue;
                    }
                    emitFunction(stream, function, false);
                    stream << "\n";
                }
            }

            /** Returns the local thread-copy variable name used when a constant resource value must bind to a helper reference. */
            std::string uniformThreadCopyName(const MSLResourceParameter &resource) const
            {
                return "__uglc_thread_" + resource.metalName;
            }

            /** Returns the UniformBuffer resource whose value must be copied before passing the expression to a thread reference. */
            const MSLResourceParameter *findUniformThreadReferenceResource(const UGLIR::Expression &expression) const
            {
                if (const MSLResourceParameter *resource = findResourceParameterForExpression(expression))
                {
                    return resource->kind == UGLIR::ResourceKind::UniformBuffer ? resource : nullptr;
                }
                if (expression.kind == UGLIR::ExpressionKind::Call &&
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::UniformBufferRead &&
                    !expression.operands.empty())
                {
                    const MSLResourceParameter *resource = findResourceParameterForExpression(expression.operands.front());
                    return resource != nullptr && resource->kind == UGLIR::ResourceKind::UniformBuffer ? resource : nullptr;
                }
                return nullptr;
            }

            /** Adds any UniformBuffer resources that a call expression passes to helper thread-reference parameters. */
            void collectUniformThreadCopyResourcesFromExpression(const UGLIR::Expression &expression,
                                                                 std::unordered_set<std::string> &resourceNames) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Call)
                {
                    const auto functionIter = mFunctionsByName.find(expression.name);
                    if (functionIter != mFunctionsByName.end())
                    {
                        const UGLIR::Function &callee = *functionIter->second;
                        size_t parameterIndex = 0;
                        for (const UGLIR::Expression &operand : expression.operands)
                        {
                            if (isResourceAliasType(operand.type))
                            {
                                continue;
                            }
                            while (parameterIndex < callee.parameters.size() &&
                                   isResourceAliasType(callee.parameters[parameterIndex].type))
                            {
                                ++parameterIndex;
                            }
                            if (parameterIndex >= callee.parameters.size())
                            {
                                break;
                            }
                            const UGLIR::FunctionParameter &parameter = callee.parameters[parameterIndex++];
                            if (!parameter.isReference)
                            {
                                continue;
                            }
                            if (const MSLResourceParameter *resource = findUniformThreadReferenceResource(operand))
                            {
                                resourceNames.insert(resource->uglirName);
                            }
                        }
                    }
                }

                for (const UGLIR::Expression &operand : expression.operands)
                {
                    collectUniformThreadCopyResourcesFromExpression(operand, resourceNames);
                }
            }

            /** Adds any UniformBuffer resources that statements pass to helper thread-reference parameters. */
            void collectUniformThreadCopyResourcesFromStatements(const std::vector<UGLIR::Statement> &statements,
                                                                 std::unordered_set<std::string> &resourceNames) const
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        collectUniformThreadCopyResourcesFromExpression(expression, resourceNames);
                    }
                    collectUniformThreadCopyResourcesFromStatements(statement.children, resourceNames);
                    collectUniformThreadCopyResourcesFromStatements(statement.elseChildren, resourceNames);
                    for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                    {
                        collectUniformThreadCopyResourcesFromStatements(switchCase.body, resourceNames);
                    }
                }
            }

            /** Emits local thread copies for UniformBuffer records that must bind to helper reference parameters. */
            void emitUniformThreadCopyDeclarations(std::ostringstream &stream,
                                                   const UGLIR::Function &function,
                                                   int indentLevel)
            {
                std::unordered_set<std::string> resourceNames;
                collectUniformThreadCopyResourcesFromStatements(function.body, resourceNames);
                if (resourceNames.empty())
                {
                    return;
                }

                std::vector<std::string> orderedResourceNames(resourceNames.begin(), resourceNames.end());
                std::sort(orderedResourceNames.begin(), orderedResourceNames.end());
                const std::string indent = makeIndent(indentLevel);
                for (const std::string &resourceName : orderedResourceNames)
                {
                    const auto resourceIter = mResourcesByUGLIRName.find(resourceName);
                    if (resourceIter == mResourcesByUGLIRName.end())
                    {
                        continue;
                    }
                    const MSLResourceParameter &resource = resourceIter->second;
                    stream << indent << emitTypeName(resource.elementType, mModule.sourceLocation)
                           << " " << uniformThreadCopyName(resource)
                           << " = (*" << resourceAccessExpression(resource) << ");\n";
                }
            }

            /** Emits local variables for RenderEntity entry parameters decoded from command-parameter metadata. */
            void emitDrawEntityParameterMaterialization(std::ostringstream &stream,
                                                        const UGLIR::Function &function,
                                                        int indentLevel)
            {
                if (!functionUsesDrawEntityParameters(function))
                {
                    return;
                }

                const std::string indent = makeIndent(indentLevel);
                const std::string commandParamsName = "__uglc_draw_command_params";
                const MSLResourceParameter *commandParamsResource = findFirstResourceByRole(UGLIR::ResourceRole::CommandParams);
                const MSLResourceParameter *accessBoundsResource = findFirstResourceByRole(UGLIR::ResourceRole::AccessBounds);
                if (commandParamsResource == nullptr || accessBoundsResource == nullptr)
                {
                    addDiagnostic(function.sourceLocation, "DrawEntity entry parameters require command-parameter and access-bounds resources.");
                    stream << indent << "uint2 " << commandParamsName << " = uint2(0u);\n";
                }
                else
                {
                    const std::string commandIndex = drawEntityInstanceIDSourceExpression(function);
                    const std::string commandCount = resourceAccessExpression(*accessBoundsResource) + "[0].y";
                    const std::string safeCommandIndex = "min(" + commandIndex + ", (max(" + commandCount + ", 1u) - 1u))";
                    stream << indent << "uint2 " << commandParamsName << " = "
                           << resourceAccessExpression(*commandParamsResource) << "[" << safeCommandIndex << "];\n";
                }
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID)
                    {
                        stream << indent << "uint " << sanitizeMSLIdentifier(parameter.name) << " = " << commandParamsName << ".x;\n";
                    }
                    else if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID)
                    {
                        stream << indent << "uint " << sanitizeMSLIdentifier(parameter.name) << " = " << commandParamsName << ".y;\n";
                    }
                }
            }

            /** Emits a Metal shader entry with reflected resources and stage parameters. */
            void emitEntryFunction(std::ostringstream &stream, const UGLIR::Function &function)
            {
                const bool isCompute = function.entryKind == UGLIR::ShaderEntryKind::Compute;
                const std::string returnType = isCompute ? "void" : emitTypeName(function.returnType, function.sourceLocation);
                stream << metalEntryQualifierForEntryKind(function.entryKind) << " " << returnType << " "
                       << entryPointNameForEntryKind(function.entryKind) << "(\n";
                std::vector<std::string> parameters;
                for (const MSLBindGroupParameter &bindGroup : mBindGroupParameters)
                {
                    parameters.push_back("const constant " + bindGroup.typeName + "* " + bindGroup.metalName
                                         + " [[buffer(" + std::to_string(bindGroup.metalBufferIndex) + ")]]");
                }
                if (isCompute)
                {
                    parameters.push_back("uint __uglc_hidden_wave_lane_index [[thread_index_in_simdgroup]]");
                    parameters.push_back("uint __uglc_hidden_wave_lane_count [[threads_per_simdgroup]]");
                }
                if (functionUsesDrawEntityParameters(function) && !functionHasInstanceIDParameter(function))
                {
                    parameters.push_back("uint __uglc_draw_instance_id [[instance_id]]");
                }
                std::vector<const MSLPixelLocalInputParameter *> pixelLocalInputParameters;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (isResourceAliasType(parameter.type))
                    {
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID)
                    {
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalInput)
                    {
                        const MSLPixelLocalInputParameter *inputParameter = findPixelLocalInputParameter(parameter);
                        if (inputParameter == nullptr)
                        {
                            addDiagnostic(parameter.sourceLocation, "PixelLocalInput parameter \"" + parameter.name + "\" could not be mapped to Metal framebuffer-fetch parameters.");
                            continue;
                        }
                        pixelLocalInputParameters.push_back(inputParameter);
                        for (const MSLPixelLocalInputAttachment &attachment : inputParameter->attachments)
                        {
                            parameters.push_back(attachment.metalTypeName + " " + attachment.metalParameterName
                                                 + " [[color(" + std::to_string(attachment.colorAttachmentIndex) + ")]]");
                        }
                        continue;
                    }
                    parameters.push_back(emitEntryParameter(parameter, function.entryKind));
                }
                for (size_t index = 0; index < parameters.size(); ++index)
                {
                    stream << "    " << parameters[index];
                    if (index + 1 < parameters.size())
                    {
                        stream << ",";
                    }
                    stream << "\n";
                }
                stream << ")\n";
                const UGLIR::Function *previousFunction = mCurrentFunction;
                mCurrentFunction = &function;
                mEmittingEntryBody = true;
                stream << "{\n";
                emitUniformThreadCopyDeclarations(stream, function, 1);
                if (!pixelLocalInputParameters.empty())
                {
                    for (const MSLPixelLocalInputParameter *inputParameter : pixelLocalInputParameters)
                    {
                        stream << "    " << inputParameter->inputStructName << " " << inputParameter->parameterName << ";\n";
                        for (const MSLPixelLocalInputAttachment &attachment : inputParameter->attachments)
                        {
                            stream << "    " << inputParameter->parameterName << "." << sanitizeMSLIdentifier(attachment.fieldName)
                                   << " = " << attachment.metalParameterName << ";\n";
                        }
                    }
                }
                emitDrawEntityParameterMaterialization(stream, function, 1);
                for (const UGLIR::Statement &statement : function.body)
                {
                    emitStatement(stream, statement, 1);
                }
                stream << "}\n";
                mEmittingEntryBody = false;
                mCurrentFunction = previousFunction;
            }

            /** Emits one helper function using the function name preserved by UGLIR lowering. */
            void emitFunction(std::ostringstream &stream, const UGLIR::Function &function, bool forceComputeEntry)
            {
                if (forceComputeEntry)
                {
                    emitEntryFunction(stream, function);
                    return;
                }

                const UGLIR::Function *previousFunction = mCurrentFunction;
                mCurrentFunction = &function;
                stream << emitFunctionSignature(function) << "\n";
                stream << "{\n";
                emitUniformThreadCopyDeclarations(stream, function, 1);
                for (const UGLIR::Statement &statement : function.body)
                {
                    emitStatement(stream, statement, 1);
                }
                stream << "}\n";
                mCurrentFunction = previousFunction;
            }

            /** Emits one non-entry helper parameter while preserving C++ reference semantics for Metal. */
            std::string emitHelperParameterDeclaration(const UGLIR::FunctionParameter &parameter)
            {
                const std::string parameterName = sanitizeMSLIdentifier(parameter.name);
                const UGLIR::Type *parameterType = findType(parameter.type);
                if (parameterType != nullptr && parameterType->kind == UGLIR::TypeKind::Array)
                {
                    const std::string elementTypeName = emitTypeName(parameterType->elementType, parameter.sourceLocation);
                    return std::string(parameter.isConstReference ? "const thread " : "thread ") + elementTypeName + " (&" + parameterName + ")"
                           + emitArrayDimensions(*parameterType);
                }

                const std::string typeName = emitTypeName(parameter.type, parameter.sourceLocation);
                if (parameter.isReference)
                {
                    return std::string(parameter.isConstReference ? "const thread " : "thread ") + typeName + "& " + parameterName;
                }
                return typeName + " " + parameterName;
            }

            /** Emits an always-inline GPU function signature so Metal can eliminate call-boundary copies and unused arguments. */
            std::string emitFunctionSignature(const UGLIR::Function &function)
            {
                std::string result = "inline __attribute__((always_inline)) " + emitTypeName(function.returnType, function.sourceLocation) + " " + sanitizeMSLIdentifier(function.name) + "(";
                std::vector<std::string> parameters;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (isResourceAliasType(parameter.type))
                    {
                        continue;
                    }
                    parameters.push_back(emitHelperParameterDeclaration(parameter));
                }
                for (size_t resourceIndex = 0; resourceIndex < mModule.reflection.resources.size(); ++resourceIndex)
                {
                    if (!mFunctionResourceUsage.at(&function).requiredResources[resourceIndex]) continue;
                    const UGLIR::ResourceBinding &resource = mModule.reflection.resources[resourceIndex];
                    const auto iter = mResourcesByUGLIRName.find(resource.name);
                    if (iter != mResourcesByUGLIRName.end() && !iter->second.helperDeclaration.empty())
                    {
                        parameters.push_back(iter->second.helperDeclaration);
                    }
                }
                for (size_t index = 0; index < parameters.size(); ++index)
                {
                    result += parameters[index];
                    if (index + 1 < parameters.size())
                    {
                        result += ", ";
                    }
                }
                result += ")";
                return result;
            }

            /** Emits one entry parameter and maps supported UGLIR semantics to Metal attributes. */
            std::string emitEntryParameter(const UGLIR::FunctionParameter &parameter, UGLIR::ShaderEntryKind entryKind)
            {
                const std::string typeName = emitTypeName(parameter.type, parameter.sourceLocation);
                const std::string parameterName = sanitizeMSLIdentifier(parameter.name);
                switch (parameter.semanticKind)
                {
                case UGLIR::BuiltinSemanticKind::DispatchThreadID:
                    return typeName + " " + parameterName + " [[thread_position_in_grid]]";
                case UGLIR::BuiltinSemanticKind::GroupThreadID:
                    return typeName + " " + parameterName + " [[thread_position_in_threadgroup]]";
                case UGLIR::BuiltinSemanticKind::GroupID:
                    return typeName + " " + parameterName + " [[threadgroup_position_in_grid]]";
                case UGLIR::BuiltinSemanticKind::GroupIndex:
                    return "uint " + parameterName + " [[thread_index_in_threadgroup]]";
                case UGLIR::BuiltinSemanticKind::VertexID:
                    return "uint " + parameterName + " [[vertex_id]]";
                case UGLIR::BuiltinSemanticKind::InstanceID:
                    return "uint " + parameterName + " [[instance_id]]";
                case UGLIR::BuiltinSemanticKind::PixelCoord:
                    return typeName + " " + parameterName + " [[position]]";
                case UGLIR::BuiltinSemanticKind::SampleIndex:
                    return "uint " + parameterName + " [[sample_id]]";
                case UGLIR::BuiltinSemanticKind::Barycentrics:
                    return typeName + " " + parameterName + " [[barycentric_coord]]";
                case UGLIR::BuiltinSemanticKind::PrimitiveID:
                    return "uint " + parameterName + " [[primitive_id]]";
                case UGLIR::BuiltinSemanticKind::StageInput:
                case UGLIR::BuiltinSemanticKind::VertexInput:
                    return typeName + " " + parameterName + " [[stage_in]]";
                case UGLIR::BuiltinSemanticKind::None:
                    break;
                default:
                    addDiagnostic(parameter.sourceLocation,
                                  "unsupported entry semantic \"" + UGLIR::semanticDisplayName(parameter.semanticKind, parameter.semanticIndex) + "\".");
                    return typeName + " " + parameterName;
                }
                if ((entryKind == UGLIR::ShaderEntryKind::Vertex && mVertexInputStructNames.contains(parameter.type)) ||
                    ((entryKind == UGLIR::ShaderEntryKind::Fragment || entryKind == UGLIR::ShaderEntryKind::PixelLocal) && mStageVaryingStructNames.contains(parameter.type)))
                {
                    return typeName + " " + parameterName + " [[stage_in]]";
                }
                if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::None)
                {
                    return typeName + " " + parameterName;
                }

                addDiagnostic(parameter.sourceLocation,
                              "unsupported entry semantic \"" + UGLIR::semanticDisplayName(parameter.semanticKind, parameter.semanticIndex) + "\".");
                return typeName + " " + parameterName;
            }

            /** Returns true when one entry function needs draw command parameter decoding for RenderEntity parameters. */
            bool functionUsesDrawEntityParameters(const UGLIR::Function &function) const
            {
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                        parameter.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID)
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns true when one entry function already exposes the raw Metal instance_id parameter. */
            bool functionHasInstanceIDParameter(const UGLIR::Function &function) const
            {
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::InstanceID)
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Returns the local expression that carries the Metal instance_id value for draw entity decoding. */
            std::string drawEntityInstanceIDSourceExpression(const UGLIR::Function &function) const
            {
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::InstanceID)
                    {
                        return sanitizeMSLIdentifier(parameter.name);
                    }
                }
                return "__uglc_draw_instance_id";
            }

            /** Emits a statement list wrapped in braces at the requested indentation level. */
            void emitStatementListAsBlock(std::ostringstream &stream, const std::vector<UGLIR::Statement> &statements, int indentLevel)
            {
                stream << makeIndent(indentLevel) << "{\n";
                for (const UGLIR::Statement &statement : statements)
                {
                    emitStatement(stream, statement, indentLevel + 1);
                }
                stream << makeIndent(indentLevel) << "}\n";
            }

            /** Emits a single structured UGLIR statement as Metal source. */
            void emitStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                const std::string indent = makeIndent(indentLevel);
                switch (statement.kind)
                {
                case UGLIR::StatementKind::Block:
                    emitStatementListAsBlock(stream, statement.children, indentLevel);
                    return;
                case UGLIR::StatementKind::VariableDeclaration:
                    stream << indent << emitVariableDeclaration(statement) << ";\n";
                    return;
                case UGLIR::StatementKind::Return:
                    if (statement.expressions.empty())
                    {
                        stream << indent << "return;\n";
                    }
                    else if (mCurrentFunction != nullptr && mCurrentFunction->isEntryPoint && mCurrentFunction->entryKind == UGLIR::ShaderEntryKind::Vertex)
                    {
                        emitVertexEntryReturnStatement(stream, statement, indentLevel);
                    }
                    else
                    {
                        stream << indent << "return " << emitExpression(statement.expressions.front()) << ";\n";
                    }
                    return;
                case UGLIR::StatementKind::If:
                    emitIfStatement(stream, statement, indentLevel);
                    return;
                case UGLIR::StatementKind::For:
                    emitForStatement(stream, statement, indentLevel);
                    return;
                case UGLIR::StatementKind::While:
                    emitWhileStatement(stream, statement, indentLevel);
                    return;
                case UGLIR::StatementKind::Do:
                    emitDoStatement(stream, statement, indentLevel);
                    return;
                case UGLIR::StatementKind::Switch:
                    emitSwitchStatement(stream, statement, indentLevel);
                    return;
                case UGLIR::StatementKind::Break:
                    stream << indent << "break;\n";
                    return;
                case UGLIR::StatementKind::Continue:
                    stream << indent << "continue;\n";
                    return;
                case UGLIR::StatementKind::Expression:
                    if (statement.expressions.empty())
                    {
                        addDiagnostic(statement.sourceLocation, "expression statement is missing its expression.");
                        return;
                    }
                    stream << indent << emitExpression(statement.expressions.front()) << ";\n";
                    return;
                default:
                    addDiagnostic(statement.sourceLocation, "unsupported UGLIR statement kind in Phase 4 MSL emission.");
                    return;
                }
            }

            /** Emits a UGLIR variable declaration without its trailing semicolon. */
            std::string emitVariableDeclaration(const UGLIR::Statement &statement)
            {
                std::string result;
                const UGLIR::Type *type = findType(statement.type);
                if (type != nullptr && type->kind == UGLIR::TypeKind::Array)
                {
                    const UGLIR::Type *elementType = findType(type->elementType);
                    const bool isWorkgroupArray = elementType != nullptr && elementType->kind == UGLIR::TypeKind::Workgroup;
                    const std::string valueTypeName = isWorkgroupArray ? workgroupElementTypeName(type->elementType, statement.sourceLocation) : type->elementType;
                    result = std::string(isWorkgroupArray ? "threadgroup " : "") + emitWorkgroupStorageTypeName(statement.name, valueTypeName, statement.sourceLocation) + " "
                           + sanitizeMSLIdentifier(statement.name) + emitArrayDimensions(*type);
                }
                else if (isWorkgroupTypeName(statement.type))
                {
                    const std::string valueTypeName = workgroupElementTypeName(statement.type, statement.sourceLocation);
                    result = "threadgroup " + emitWorkgroupStorageTypeName(statement.name, valueTypeName, statement.sourceLocation) + " "
                           + sanitizeMSLIdentifier(statement.name);
                }
                else
                {
                    result = emitTypeName(statement.type, statement.sourceLocation) + " " + sanitizeMSLIdentifier(statement.name);
                }
                if (!statement.expressions.empty())
                {
                    if (type != nullptr && type->kind == UGLIR::TypeKind::Array)
                    {
                        result += " = " + emitArrayInitializerExpression(type->arrayCount, statement.expressions.front());
                    }
                    else
                    {
                        result += " = " + emitExpression(statement.expressions.front());
                    }
                }
                return result;
            }

            /** Emits a Metal initializer list for a local fixed-size array declaration. */
            std::string emitArrayInitializerExpression(uint32_t arrayCount, const UGLIR::Expression &expression)
            {
                if (expression.kind != UGLIR::ExpressionKind::Construct)
                {
                    return emitExpression(expression);
                }
                if (expression.operands.empty())
                {
                    return "{}";
                }
                if (expression.operands.size() != arrayCount)
                {
                    addDiagnostic(expression.sourceLocation, "array constructor operand count does not match the fixed array size.");
                    return "{}";
                }
                std::vector<std::string> elements;
                elements.reserve(expression.operands.size());
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    elements.push_back(emitExpression(operand));
                }
                return "{" + joinCommaSeparated(elements) + "}";
            }

            /** Emits a legacy-compatible Metal vertex return with clip-space Y correction. */
            void emitVertexEntryReturnStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                if (mCurrentFunction == nullptr || statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "vertex return emission requires a current function and return expression.");
                    return;
                }
                const UGLIR::Type *returnType = findType(mCurrentFunction->returnType);
                if (returnType == nullptr || returnType->kind != UGLIR::TypeKind::Struct)
                {
                    addDiagnostic(statement.sourceLocation, "MSL vertex entry \"" + mCurrentFunction->name + "\" must return a record type with a [[Position]] field so UGLC can apply Metal clip-space Y correction.");
                    stream << makeIndent(indentLevel) << "return " << emitExpression(statement.expressions.front()) << ";\n";
                    return;
                }

                const UGLIR::TypeField *positionField = nullptr;
                for (const UGLIR::TypeField &field : returnType->fields)
                {
                    if (field.semanticKind == UGLIR::BuiltinSemanticKind::Position)
                    {
                        positionField = &field;
                        break;
                    }
                }
                if (positionField == nullptr)
                {
                    addDiagnostic(statement.sourceLocation, "MSL vertex output record \"" + mCurrentFunction->returnType + "\" is missing required [[Position]] field for Metal clip-space Y correction.");
                    stream << makeIndent(indentLevel) << "return " << emitExpression(statement.expressions.front()) << ";\n";
                    return;
                }

                const std::string indent = makeIndent(indentLevel);
                const std::string returnTypeName = emitTypeName(mCurrentFunction->returnType, statement.sourceLocation);
                const std::string positionFieldName = sanitizeMSLIdentifier(positionField->name);
                stream << indent << returnTypeName << " __REVERSED_VERTEX__OUTPUT__ = " << emitExpression(statement.expressions.front()) << ";\n";
                stream << indent << "__REVERSED_VERTEX__OUTPUT__." << positionFieldName << ".y = -__REVERSED_VERTEX__OUTPUT__." << positionFieldName << ".y;\n";
                stream << indent << "return __REVERSED_VERTEX__OUTPUT__;\n";
            }

            /** Returns the Metal storage type for a workgroup variable, preserving atomic qualification only when required. */
            std::string emitWorkgroupStorageTypeName(const std::string &variableName,
                                                     const std::string &valueTypeName,
                                                     const UGLIR::SourceLocation &location)
            {
                const std::string metalValueType = emitTypeName(valueTypeName, location);
                if (!mAtomicWorkgroupVariableNames.contains(variableName))
                {
                    return metalValueType;
                }
                if (isUnsignedIntegerValueType(valueTypeName))
                {
                    return "atomic_uint";
                }
                if (isSignedIntegerValueType(valueTypeName))
                {
                    return "atomic_int";
                }
                addDiagnostic(location, "Metal threadgroup atomics are only supported for int and uint GroupShared variables.");
                return metalValueType;
            }

            /** Emits a UGLIR if statement with optional else branch. */
            void emitIfStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "if statement is missing its condition.");
                    return;
                }

                stream << makeIndent(indentLevel) << "if (" << emitExpression(statement.expressions.front()) << ")\n";
                emitStatementListAsBlock(stream, statement.children, indentLevel);
                if (!statement.elseChildren.empty())
                {
                    stream << makeIndent(indentLevel) << "else\n";
                    emitStatementListAsBlock(stream, statement.elseChildren, indentLevel);
                }
            }

            /** Emits a UGLIR for loop using the Phase-3 statement encoding contract. */
            void emitForStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                const bool hasInit = !statement.children.empty();
                const std::string init = hasInit ? emitVariableDeclaration(statement.children.front()) : std::string();
                const std::string condition = !statement.expressions.empty() ? emitExpression(statement.expressions.front()) : std::string();
                const std::string increment = statement.expressions.size() > 1 ? emitExpression(statement.expressions[1]) : std::string();

                stream << makeIndent(indentLevel) << "for (" << init << "; " << condition << "; " << increment << ")\n";
                stream << makeIndent(indentLevel) << "{\n";
                for (size_t index = hasInit ? 1u : 0u; index < statement.children.size(); ++index)
                {
                    emitStatement(stream, statement.children[index], indentLevel + 1);
                }
                stream << makeIndent(indentLevel) << "}\n";
            }

            /** Emits a UGLIR while loop. */
            void emitWhileStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "while statement is missing its condition.");
                    return;
                }

                stream << makeIndent(indentLevel) << "while (" << emitExpression(statement.expressions.front()) << ")\n";
                emitStatementListAsBlock(stream, statement.children, indentLevel);
            }

            /** Emits a UGLIR do-while loop. */
            void emitDoStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "do statement is missing its condition.");
                    return;
                }

                stream << makeIndent(indentLevel) << "do\n";
                emitStatementListAsBlock(stream, statement.children, indentLevel);
                stream << makeIndent(indentLevel) << "while (" << emitExpression(statement.expressions.front()) << ");\n";
            }

            /** Emits a UGLIR switch statement with explicit case/default arms. */
            void emitSwitchStatement(std::ostringstream &stream, const UGLIR::Statement &statement, int indentLevel)
            {
                if (statement.expressions.empty())
                {
                    addDiagnostic(statement.sourceLocation, "switch statement is missing its selector.");
                    return;
                }

                stream << makeIndent(indentLevel) << "switch (" << emitExpression(statement.expressions.front()) << ")\n";
                stream << makeIndent(indentLevel) << "{\n";
                for (const UGLIR::SwitchCase &switchCase : statement.switchCases)
                {
                    if (switchCase.isDefault)
                    {
                        stream << makeIndent(indentLevel + 1) << "default:\n";
                    }
                    for (const UGLIR::Expression &label : switchCase.labels)
                    {
                        stream << makeIndent(indentLevel + 1) << "case " << emitExpression(label) << ":\n";
                    }
                    for (const UGLIR::Statement &child : switchCase.body)
                    {
                        emitStatement(stream, child, indentLevel + 2);
                    }
                }
                stream << makeIndent(indentLevel) << "}\n";
            }

            /** Emits the current method receiver expression using the lowered helper ABI parameter. */
            std::string emitThisRefExpression(const UGLIR::SourceLocation &location)
            {
                if (mCurrentFunction != nullptr)
                {
                    for (const UGLIR::FunctionParameter &parameter : mCurrentFunction->parameters)
                    {
                        if (parameter.name == "this")
                        {
                            return sanitizeMSLIdentifier(parameter.name);
                        }
                    }
                }
                addDiagnostic(location, "`this` expression is only valid inside lowered non-static method helpers.");
                return {};
            }

            /** Emits one UGLIR expression as a Metal expression fragment. */
            std::string emitExpression(const UGLIR::Expression &expression)
            {
                switch (expression.kind)
                {
                case UGLIR::ExpressionKind::Literal:
                    return emitLiteralExpression(expression);
                case UGLIR::ExpressionKind::ThisRef:
                    return emitThisRefExpression(expression.sourceLocation);
	                case UGLIR::ExpressionKind::DeclRef:
	                    if (!mEmittingAtomicRawLValue && isAtomicBackedLValueExpression(expression))
	                    {
	                        return "atomicLoad(" + emitAtomicRawLValueExpression(expression) + ")";
	                    }
	                    if (const MSLResourceParameter *resource = findResourceParameterForExpression(expression))
	                    {
	                        return resourceAccessExpression(*resource);
	                    }
	                    return sanitizeMSLIdentifier(expression.name);
                case UGLIR::ExpressionKind::MemberRef:
                    return emitMemberRefExpression(expression);
                case UGLIR::ExpressionKind::Subscript:
                    return emitSubscriptExpression(expression);
                case UGLIR::ExpressionKind::Call:
                    return emitCallExpression(expression);
                case UGLIR::ExpressionKind::Construct:
                    return emitConstructExpression(expression);
                case UGLIR::ExpressionKind::Cast:
                    return emitCastExpression(expression);
                case UGLIR::ExpressionKind::Load:
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "cast/load expression is missing its operand.");
                        return {};
                    }
                    return emitExpression(expression.operands.front());
                case UGLIR::ExpressionKind::Binary:
                    return emitBinaryExpression(expression);
                case UGLIR::ExpressionKind::Unary:
                    return emitUnaryExpression(expression);
                case UGLIR::ExpressionKind::Conditional:
                    return emitConditionalExpression(expression);
                case UGLIR::ExpressionKind::Store:
                    return emitStoreExpression(expression);
                }
                addDiagnostic(expression.sourceLocation, "unsupported UGLIR expression kind in Phase 4 MSL emission.");
                return {};
            }

            /** Emits a typed literal with Metal suffixes where UGLIR carries enough information. */
            std::string emitLiteralExpression(const UGLIR::Expression &expression)
            {
                if (const auto valueType = describeValueType(expression.type);
                    valueType.has_value() && valueType->scalarKind == UGLIR::ScalarKind::Half &&
                    valueType->vectorWidth == 1u && valueType->matrixColumns == 0)
                {
                    UGLIR::Expression floatLiteral = expression;
                    floatLiteral.type = "f32";
                    return "half(" + emitLiteralExpression(floatLiteral) + ")";
                }
                if (isBooleanValueType(expression.type))
                {
                    return expression.value == "0" ? "false" : expression.value == "1" ? "true" : expression.value;
                }
                if (isUnsignedIntegerValueType(expression.type))
                {
                    if (!expression.value.empty() && expression.value.back() != 'u' && expression.value.back() != 'U')
                    {
                        return expression.value + "u";
                    }
                }
                if (isFloatValueType(expression.type))
                {
                    if (expression.value.find('.') == std::string::npos && expression.value.find('e') == std::string::npos && expression.value.find('E') == std::string::npos)
                    {
                        return expression.value + ".0f";
                    }
                }
                return expression.value;
            }

            /** Emits a member reference, collapsing reflected resource member paths into resource parameter names. */
            std::string emitMemberRefExpression(const UGLIR::Expression &expression)
            {
                if (!mEmittingAtomicRawLValue && isAtomicBackedLValueExpression(expression))
                {
                    return "atomicLoad(" + emitAtomicRawLValueExpression(expression) + ")";
                }
                if (const MSLResourceParameter *resource = findResourceParameterForExpression(expression))
                {
                    return resourceAccessExpression(*resource);
                }
                if (expression.operands.empty())
                {
                    return sanitizeMSLIdentifier(expression.name);
                }

                const MSLResourceParameter *baseResource = findResourceParameterForExpression(expression.operands.front());
                const std::string base = emitExpression(expression.operands.front());
                if (expression.name.empty())
                {
                    return base;
                }
                if (baseResource != nullptr && baseResource->kind == UGLIR::ResourceKind::UniformBuffer)
                {
                    return base + "->" + sanitizeMSLIdentifier(expression.name);
                }
                return base + "." + sanitizeMSLIdentifier(expression.name);
            }

            /** Emits a dynamic subscript over an inline array construct as a nested conditional expression. */
            std::string emitInlineArrayConstructSubscript(const UGLIR::Expression &arrayExpression,
                                                          const UGLIR::Expression &indexExpression)
            {
                const UGLIR::Type *arrayType = findType(arrayExpression.type);
                if (arrayType == nullptr || arrayType->kind != UGLIR::TypeKind::Array || arrayExpression.operands.empty())
                {
                    return {};
                }

                const std::string indexText = emitExpression(indexExpression);
                if (indexExpression.kind == UGLIR::ExpressionKind::Literal)
                {
                    char *endPointer = nullptr;
                    const unsigned long literalIndex = std::strtoul(indexExpression.value.c_str(), &endPointer, 10);
                    if (endPointer != indexExpression.value.c_str() && literalIndex < arrayExpression.operands.size())
                    {
                        return emitExpression(arrayExpression.operands[literalIndex]);
                    }
                }

                std::string result = emitExpression(arrayExpression.operands.back());
                for (size_t reverseIndex = arrayExpression.operands.size() - 1u; reverseIndex > 0u; --reverseIndex)
                {
                    const size_t operandIndex = reverseIndex - 1u;
                    result = "((" + indexText + " == " + std::to_string(operandIndex) + "u) ? "
                           + emitExpression(arrayExpression.operands[operandIndex]) + " : " + result + ")";
                }
                return result;
            }

            /** Emits an array or resource subscript expression. */
            std::string emitSubscriptExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2)
                {
                    addDiagnostic(expression.sourceLocation, "subscript expression is missing its base or index operand.");
                    return {};
                }
                if (!mEmittingAtomicRawLValue && isAtomicBackedLValueExpression(expression))
                {
                    return "atomicLoad(" + emitAtomicRawLValueExpression(expression) + ")";
                }
                if (expression.operands[0].kind == UGLIR::ExpressionKind::Construct)
                {
                    if (std::string inlineArraySubscript = emitInlineArrayConstructSubscript(expression.operands[0], expression.operands[1]);
                        !inlineArraySubscript.empty())
                    {
                        return inlineArraySubscript;
                    }
                }
                if (const MSLResourceParameter *baseResource = findResourceParameterForExpression(expression.operands[0]))
                {
                    if (isTextureResourceKind(baseResource->kind) && baseResource->resourceRole == UGLIR::ResourceRole::TextureValue)
                    {
                        return resourceAccessExpression(*baseResource) + "[" + emitExpression(expression.operands[1]) + "].texture";
                    }
                }
                const bool rawTarget = mEmittingAtomicRawLValue;
                mEmittingAtomicRawLValue = false;
                const std::string index = emitExpression(expression.operands[1]);
                mEmittingAtomicRawLValue = rawTarget;
                return emitExpression(expression.operands[0]) + "[" + index + "]";
            }

            /** Emits one helper-call argument while preserving Metal address-space rules for resource-backed reference values. */
            std::string emitHelperCallArgument(const UGLIR::FunctionParameter *parameter,
                                               const UGLIR::Expression &operand)
            {
                if (parameter != nullptr && parameter->isReference)
                {
                    if (const MSLResourceParameter *resource = findUniformThreadReferenceResource(operand))
                    {
                        if (!parameter->isConstReference)
                        {
                            addDiagnostic(operand.sourceLocation, "UniformBuffer resource \"" + resource->uglirName + "\" cannot bind to a mutable helper reference.");
                            return emitExpression(operand);
                        }
                        return uniformThreadCopyName(*resource);
                    }
                }
                return emitExpression(operand);
            }

            /** Returns the non-resource-alias helper parameter corresponding to one emitted call argument. */
            const UGLIR::FunctionParameter *nextEmittedHelperParameter(const UGLIR::Function &callee,
                                                                       size_t &parameterIndex) const
            {
                while (parameterIndex < callee.parameters.size() &&
                       isResourceAliasType(callee.parameters[parameterIndex].type))
                {
                    ++parameterIndex;
                }
                if (parameterIndex >= callee.parameters.size())
                {
                    return nullptr;
                }
                return &callee.parameters[parameterIndex++];
            }

            /** Emits a direct helper call expression. */
            std::string emitCallExpression(const UGLIR::Expression &expression)
            {
                if (!expression.operands.empty())
                {
                    const MSLResourceParameter *resource = findResourceParameterForExpression(expression.operands.front());
                    const UGLC::CodeGen::TextureMemberCallKind methodKind = textureMemberCallKindForExpression(expression);
                    if (resource != nullptr)
                    {
                        if (isTextureResourceKind(resource->kind) && methodKind != UGLC::CodeGen::TextureMemberCallKind::Unknown)
                        {
                            return emitTextureMemberCallExpression(expression, *resource, methodKind);
                        }
                    }
                    if (methodKind != UGLC::CodeGen::TextureMemberCallKind::Unknown &&
                        isTextureValueExpression(expression.operands.front()))
                    {
                        return emitTextureValueMemberCallExpression(expression, methodKind);
                    }
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::UniformBufferRead)
                    {
                        const MSLResourceParameter *resource = findResourceParameterForExpression(expression.operands.front());
                        if (resource != nullptr && resource->kind == UGLIR::ResourceKind::UniformBuffer)
                        {
                            return "(*" + resourceAccessExpression(*resource) + ")";
                        }
                        return emitExpression(expression.operands.front());
                    }
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::InputAttachmentRead)
                    {
                        return emitInputAttachmentReadExpression(expression.operands.front());
                    }
                }

                if (std::string mappedCall = emitMappedIntrinsicOrOperatorCall(expression); !mappedCall.empty())
                {
                    return mappedCall;
                }

                std::string result = sanitizeMSLIdentifier(expression.name) + "(";
                std::vector<std::string> arguments;
                const UGLIR::Function *calleeFunction = nullptr;
                const auto calleeIter = mFunctionsByName.find(expression.name);
                if (calleeIter != mFunctionsByName.end())
                {
                    calleeFunction = calleeIter->second;
                }
                size_t parameterIndex = 0;
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (isResourceAliasType(operand.type))
                    {
                        continue;
                    }
                    const UGLIR::FunctionParameter *parameter = calleeFunction == nullptr
                                                                    ? nullptr
                                                                    : nextEmittedHelperParameter(*calleeFunction, parameterIndex);
                    arguments.push_back(emitHelperCallArgument(parameter, operand));
                }
                if (calleeFunction != nullptr && !calleeFunction->isEntryPoint)
                {
                    for (size_t resourceIndex = 0; resourceIndex < mModule.reflection.resources.size(); ++resourceIndex)
                    {
                        if (!mFunctionResourceUsage.at(calleeFunction).requiredResources[resourceIndex]) continue;
                        const UGLIR::ResourceBinding &resource = mModule.reflection.resources[resourceIndex];
                        const auto iter = mResourcesByUGLIRName.find(resource.name);
                        if (iter != mResourcesByUGLIRName.end() && !iter->second.metalName.empty())
                        {
                            arguments.push_back(resourceAccessExpression(iter->second));
                        }
                    }
                }
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    result += arguments[index];
                    if (index + 1 < arguments.size())
                    {
                        result += ", ";
                    }
                }
                result += ")";
                return result;
            }

            /** Emits the already materialized local value for a pixel-local input attachment read. */
            std::string emitInputAttachmentReadExpression(const UGLIR::Expression &operand)
            {
                if (operand.kind == UGLIR::ExpressionKind::DeclRef && operand.name.find('.') != std::string::npos)
                {
                    std::string result;
                    size_t segmentBegin = 0;
                    while (segmentBegin <= operand.name.size())
                    {
                        const size_t segmentEnd = operand.name.find('.', segmentBegin);
                        const std::string segment = segmentEnd == std::string::npos
                                                        ? operand.name.substr(segmentBegin)
                                                        : operand.name.substr(segmentBegin, segmentEnd - segmentBegin);
                        if (!result.empty())
                        {
                            result += ".";
                        }
                        result += sanitizeMSLIdentifier(segment);
                        if (segmentEnd == std::string::npos)
                        {
                            break;
                        }
                        segmentBegin = segmentEnd + 1;
                    }
                    return result;
                }
                return emitExpression(operand);
            }

            /** Returns the expression used to access one resource in the active function emission context. */
            std::string resourceAccessExpression(const MSLResourceParameter &resource) const
            {
                return mEmittingEntryBody ? resource.entryExpression : resource.metalName;
            }

            /** Finds the first reflected resource with one ordinary ABI role. */
            const MSLResourceParameter *findFirstResourceByRole(UGLIR::ResourceRole role) const
            {
                for (const auto &[unusedName, resource] : mResourcesByUGLIRName)
                {
                    (void)unusedName;
                    if (resource.resourceRole == role)
                    {
                        return &resource;
                    }
                }
                return nullptr;
            }

            /** Emits a UGL intrinsic or overloaded operator call as native Metal syntax when possible. */
            std::string emitMappedIntrinsicOrOperatorCall(const UGLIR::Expression &expression)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::None && mFunctionsByName.contains(expression.name))
                    return {};
                std::vector<std::string> arguments;
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (!isResourceAliasType(operand.type))
                    {
                        arguments.push_back(emitExpression(operand));
                    }
                }

                const std::string op = mapOperatorToken(expression.name);
                if (!op.empty())
                {
                    if ((op == "!" || op == "~" || op == "+" || op == "-") && arguments.size() == 1)
                    {
                        return "(" + op + arguments.front() + ")";
                    }
                    if (arguments.size() == 2)
                    {
                        return "(" + arguments[0] + " " + op + " " + arguments[1] + ")";
                    }
                    addDiagnostic(expression.sourceLocation, "unsupported overloaded operator arity for \"" + expression.name + "\".");
                    return {};
                }

                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::BitcastAsFloat ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::BitcastAsUInt ||
                    expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::BitcastAsInt)
                {
                    return emitBitcastIntrinsicCall(expression);
                }
                if (std::string waveCall = emitWaveIntrinsicCall(expression); !waveCall.empty())
                {
                    return waveCall;
                }

                const std::string intrinsicName = mapMetalIntrinsicName(expression.intrinsicCallKind);
                if (intrinsicName.empty())
                {
                    return {};
                }
                if (isAtomicIntrinsicCall(expression.intrinsicCallKind) && !expression.operands.empty() && !arguments.empty())
                {
                    arguments.front() = emitAtomicRawLValueExpression(expression.operands.front());
                }
                if (intrinsicName == "min" || intrinsicName == "max" || intrinsicName == "clamp" || intrinsicName == "smoothstep")
                {
                    return emitTypeAdaptedIntrinsicCall(expression, intrinsicName);
                }
                if (intrinsicName == "saturate" && arguments.size() == 1)
                {
                    return "saturate(" + arguments.front() + ")";
                }
                std::string result = intrinsicName + "(";
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    result += arguments[index];
                    if (index + 1 < arguments.size())
                    {
                        result += ", ";
                    }
                }
                result += ")";
                return result;
            }

            /** Emits a Metal bitcast intrinsic for UGL asfloat/asuint/asint calls. */
            std::string emitBitcastIntrinsicCall(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() != 1)
                {
                    addDiagnostic(expression.sourceLocation, "bitcast intrinsic \"" + expression.name + "\" requires exactly one operand.");
                    return {};
                }
                const std::string targetType = emitTypeName(expression.type, expression.sourceLocation);
                return "as_type<" + targetType + ">(" + emitExpression(expression.operands.front()) + ")";
            }

            /** Emits a Metal wave intrinsic or injected wave-lane builtin access. */
            std::string emitWaveIntrinsicCall(const UGLIR::Expression &expression)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneIndex)
                {
                    return "__uglc_hidden_wave_lane_index";
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveGetLaneCount)
                {
                    return "__uglc_hidden_wave_lane_count";
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveActiveBallot)
                {
                    if (expression.operands.size() != 1)
                    {
                        addDiagnostic(expression.sourceLocation, "WaveActiveBallot requires exactly one predicate operand.");
                        return {};
                    }
                    return "UGLC_WaveActiveBallot(" + emitExpression(expression.operands.front()) + ", __uglc_hidden_wave_lane_count)";
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::WaveMatch)
                {
                    if (expression.operands.size() != 1)
                    {
                        addDiagnostic(expression.sourceLocation, "WaveMatch requires exactly one value operand.");
                        return {};
                    }
                    return "UGLC_WaveMatch(" + emitExpression(expression.operands.front()) + ", __uglc_hidden_wave_lane_index, __uglc_hidden_wave_lane_count)";
                }
                return {};
            }

            /** Emits a Metal math intrinsic after converting every operand to the UGLIR result type. */
            std::string emitTypeAdaptedIntrinsicCall(const UGLIR::Expression &expression, const std::string &intrinsicName)
            {
                const std::string resultType = canonicalMetalValueTypeName(expression.type);
                const std::string resultTypeName = resultType.empty() ? std::string() : emitTypeName(expression.type, expression.sourceLocation);
                std::string result = intrinsicName + "(";
                size_t emittedCount = 0;
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (isResourceAliasType(operand.type))
                    {
                        continue;
                    }
                    if (emittedCount != 0)
                    {
                        result += ", ";
                    }
                    result += emitOperandValueConvertedToType(operand, resultType, resultTypeName);
                    ++emittedCount;
                }
                result += ")";
                return result;
            }

            /** Emits a Metal value expression converted to the requested canonical type when needed. */
            std::string emitValueConvertedToType(const std::string &expressionText,
                                                 const std::string &sourceCanonicalType,
                                                 const std::string &targetCanonicalType,
                                                 const std::string &targetTypeName) const
            {
                if (targetCanonicalType.empty() || targetTypeName.empty() ||
                    sourceCanonicalType.empty() || sourceCanonicalType == targetCanonicalType)
                {
                    return expressionText;
                }
                return targetTypeName + "(" + expressionText + ")";
            }

            /** Emits an operand expression converted to the requested canonical Metal type when needed. */
            std::string emitOperandValueConvertedToType(const UGLIR::Expression &expression,
                                                        const std::string &targetCanonicalType,
                                                        const std::string &targetTypeName)
            {
                return emitValueConvertedToType(emitExpression(expression),
                                                canonicalMetalValueTypeName(expression.type),
                                                targetCanonicalType,
                                                targetTypeName);
            }

            /** Emits one normalized construct component from a UGLIR component mapping. */
            std::string emitConstructComponentExpression(const UGLIR::Expression &expression,
                                                         const UGLIR::ConstructComponent &component)
            {
                if (component.operandIndex >= expression.operands.size())
                {
                    addDiagnostic(expression.sourceLocation, "construct component references a missing operand.");
                    return {};
                }
                std::string componentText = emitExpression(expression.operands[component.operandIndex]);
                if (component.sourceIsVector)
                {
                    componentText += "." + metalVectorComponentName(component.sourceComponentIndex);
                }
                const std::string sourceScalarTypeName = UGLIR::scalarTypeName(component.sourceScalarKind);
                const std::string targetScalarTypeName = UGLIR::scalarTypeName(component.targetScalarKind);
                return emitValueConvertedToType(componentText,
                                                sourceScalarTypeName,
                                                targetScalarTypeName,
                                                targetScalarTypeName);
            }

            /** Emits a Metal texture or storage-texture method call from a UGLIR intrinsic-shaped call. */
            std::string emitTextureMemberCallExpression(const UGLIR::Expression &expression,
                                                        const MSLResourceParameter &resource,
                                                        UGLC::CodeGen::TextureMemberCallKind methodKind)
            {
                const bool isTexture2DArray = resource.textureDimension == UGLIR::TextureDimension::Texture2DArray;
                const std::string resourceExpression = resourceAccessExpression(resource);
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Sample)
                {
                    if (isTexture2DArray)
                    {
                        return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ")";
                    }
                    return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleLevel)
                {
                    if (isTexture2DArray)
                    {
                        return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", level(" + emitCallOperand(expression, 4) + "))";
                    }
                    return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", level(" + emitCallOperand(expression, 3) + "))";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleGrad)
                {
                    if (isTexture2DArray)
                    {
                        return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", gradient2d(" + emitCallOperand(expression, 4) + ", " + emitCallOperand(expression, 5) + "))";
                    }
                    return resourceExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", gradient2d(" + emitCallOperand(expression, 3) + ", " + emitCallOperand(expression, 4) + "))";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Read)
                {
                    std::string readExpression;
                    if (isTexture2DArray)
                    {
                        readExpression = resourceExpression + ".read(uint2(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ", " + emitOptionalCallOperand(expression, 3, "0") + ")";
                    }
                    else if (resource.textureDimension == UGLIR::TextureDimension::Texture3D && resource.kind == UGLIR::ResourceKind::StorageTexture)
                    {
                        readExpression = resourceExpression + ".read(uint3(" + emitCallOperand(expression, 1) + "))";
                    }
                    else if (resource.textureDimension == UGLIR::TextureDimension::Texture3D)
                    {
                        readExpression = resourceExpression + ".read(uint3(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ")";
                    }
                    else
                    {
                        readExpression = resourceExpression + ".read(uint2(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ")";
                    }
                    return castTextureReadIfNeeded(expression, resource, readExpression);
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Write)
                {
                    if (isTexture2DArray)
                    {
                        if (expression.operands.size() > 3)
                        {
                            return resourceExpression + ".write(" + emitCallOperand(expression, 3) + ", " + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                        }
                        addDiagnostic(expression.sourceLocation, "Texture2DArray::write requires coordinate, layer, and value operands.");
                        return resourceExpression;
                    }
                    return resourceExpression + ".write(" + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 1) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Gather ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherRed ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherGreen ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherBlue ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherAlpha)
                {
                    if (isTexture2DArray)
                    {
                        return resourceExpression + ".gather(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", " + emitOptionalCallOperand(expression, 4, "int2(0)") + ", " + std::string(UGLC::CodeGen::getMSLGatherComponent(methodKind)) + ")";
                    }
                    return resourceExpression + ".gather(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitOptionalCallOperand(expression, 3, "int2(0)") + ", " + std::string(UGLC::CodeGen::getMSLGatherComponent(methodKind)) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::GetDimensions)
                {
                    if (expression.operands.size() >= 4)
                    {
                        const std::string thirdDimensionAccessor = isTexture2DArray ? "get_array_size" : "get_depth";
                        std::string result = "(" + emitCallOperand(expression, 1) + " = uint(" + resourceExpression + ".get_width()), "
                                             + emitCallOperand(expression, 2) + " = uint(" + resourceExpression + ".get_height()), "
                                             + emitCallOperand(expression, 3) + " = uint(" + resourceExpression + "." + thirdDimensionAccessor + "())";
                        result += ")";
                        return result;
                    }
                    if (expression.operands.size() >= 3)
                    {
                        std::string result = "(" + emitCallOperand(expression, 1) + " = uint(" + resourceExpression + ".get_width()), "
                                             + emitCallOperand(expression, 2) + " = uint(" + resourceExpression + ".get_height())";
                        result += ")";
                        return result;
                    }
                    if (resource.textureDimension == UGLIR::TextureDimension::Texture3D)
                    {
                        return "uint3(" + resourceExpression + ".get_width(), " + resourceExpression + ".get_height(), " + resourceExpression + ".get_depth())";
                    }
                    return "uint2(" + resourceExpression + ".get_width(), " + resourceExpression + ".get_height())";
                }
                addDiagnostic(expression.sourceLocation, "unsupported texture member call \"" + expression.name + "\" for Phase 8 MSL emission.");
                return resourceExpression;
            }

            /** Returns true when an expression carries a Metal texture object value instead of a reflected resource access. */
            bool isTextureValueExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::Type *type = findType(expression.type);
                return type != nullptr && type->kind == UGLIR::TypeKind::Texture;
            }

            /** Returns true when a texture value expression refers to a 2D array texture object. */
            bool isTextureValue2DArrayExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::Type *type = findType(expression.type);
                return type != nullptr && type->textureDimension == UGLIR::TextureDimension::Texture2DArray;
            }

            /** Returns true when a texture value expression refers to a 3D texture object. */
            bool isTextureValue3DExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::Type *type = findType(expression.type);
                return type != nullptr && type->textureDimension == UGLIR::TextureDimension::Texture3D;
            }

            /** Emits a Metal texture intrinsic when the texture operand is already a first-class texture value. */
            std::string emitTextureValueMemberCallExpression(const UGLIR::Expression &expression,
                                                             UGLC::CodeGen::TextureMemberCallKind methodKind)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "texture value intrinsic is missing its texture operand.");
                    return {};
                }

                const std::string textureExpression = emitExpression(expression.operands.front());
                const bool isTexture2DArray = isTextureValue2DArrayExpression(expression.operands.front());
                const bool isTexture3D = isTextureValue3DExpression(expression.operands.front());

                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Sample)
                {
                    if (isTexture2DArray)
                    {
                        return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ")";
                    }
                    return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleLevel)
                {
                    if (isTexture2DArray)
                    {
                        return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", level(" + emitCallOperand(expression, 4) + "))";
                    }
                    return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", level(" + emitCallOperand(expression, 3) + "))";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleGrad)
                {
                    if (isTexture2DArray)
                    {
                        return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", gradient2d(" + emitCallOperand(expression, 4) + ", " + emitCallOperand(expression, 5) + "))";
                    }
                    return textureExpression + ".sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", gradient2d(" + emitCallOperand(expression, 3) + ", " + emitCallOperand(expression, 4) + "))";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Read)
                {
                    if (isTexture2DArray)
                    {
                        return textureExpression + ".read(uint2(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ", " + emitOptionalCallOperand(expression, 3, "0") + ")";
                    }
                    if (isTexture3D)
                    {
                        return textureExpression + ".read(uint3(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ")";
                    }
                    return textureExpression + ".read(uint2(" + emitCallOperand(expression, 1) + "), " + emitOptionalCallOperand(expression, 2, "0") + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Write)
                {
                    if (isTexture2DArray)
                    {
                        if (expression.operands.size() > 3)
                        {
                            return textureExpression + ".write(" + emitCallOperand(expression, 3) + ", " + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                        }
                        addDiagnostic(expression.sourceLocation, "Texture2DArray::write requires coordinate, layer, and value operands.");
                        return textureExpression;
                    }
                    return textureExpression + ".write(" + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 1) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Gather ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherRed ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherGreen ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherBlue ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherAlpha)
                {
                    if (isTexture2DArray)
                    {
                        return textureExpression + ".gather(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", " + emitOptionalCallOperand(expression, 4, "int2(0)") + ", " + std::string(UGLC::CodeGen::getMSLGatherComponent(methodKind)) + ")";
                    }
                    return textureExpression + ".gather(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitOptionalCallOperand(expression, 3, "int2(0)") + ", " + std::string(UGLC::CodeGen::getMSLGatherComponent(methodKind)) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::GetDimensions)
                {
                    if (expression.operands.size() >= 4)
                    {
                        const std::string thirdDimensionAccessor = isTexture2DArray ? "get_array_size" : "get_depth";
                        return "(" + emitCallOperand(expression, 1) + " = uint(" + textureExpression + ".get_width()), "
                             + emitCallOperand(expression, 2) + " = uint(" + textureExpression + ".get_height()), "
                             + emitCallOperand(expression, 3) + " = uint(" + textureExpression + "." + thirdDimensionAccessor + "()))";
                    }
                    if (expression.operands.size() >= 3)
                    {
                        return "(" + emitCallOperand(expression, 1) + " = uint(" + textureExpression + ".get_width()), "
                             + emitCallOperand(expression, 2) + " = uint(" + textureExpression + ".get_height()))";
                    }
                    if (isTexture3D)
                    {
                        return "uint3(" + textureExpression + ".get_width(), " + textureExpression + ".get_height(), " + textureExpression + ".get_depth())";
                    }
                    return "uint2(" + textureExpression + ".get_width(), " + textureExpression + ".get_height())";
                }
                addDiagnostic(expression.sourceLocation, "unsupported texture value member call \"" + expression.name + "\" for Metal emission.");
                return textureExpression;
            }

            /** Casts a Metal texture read expression when the DSL result type is narrower than the Metal texture scalar. */
            std::string castTextureReadIfNeeded(const UGLIR::Expression &expression,
                                                const MSLResourceParameter &resource,
                                                const std::string &readExpression)
            {
                const std::string expectedType = canonicalMetalValueTypeName(expression.type);
                if (expectedType.empty() || expectedType == metalTextureReadVectorType(resource))
                {
                    return readExpression;
                }
                return emitValueConvertedToType(readExpression,
                                                metalTextureReadVectorType(resource),
                                                expectedType,
                                                emitTypeName(expression.type, expression.sourceLocation));
            }

            /** Emits one operand from a call expression or returns an empty string when it is absent. */
            std::string emitCallOperand(const UGLIR::Expression &expression, size_t index)
            {
                return index < expression.operands.size() ? emitExpression(expression.operands[index]) : std::string();
            }

            /** Emits one optional call operand or a legacy-compatible default value when it is absent. */
            std::string emitOptionalCallOperand(const UGLIR::Expression &expression, size_t index, const std::string &defaultValue)
            {
                return index < expression.operands.size() ? emitExpression(expression.operands[index]) : defaultValue;
            }

            /** Returns the generated arithmetic function that preserves a half result boundary. */
            std::string halfArithmeticFunctionName(const std::string &op) const
            {
                if (op == "+") return "__uglc_half_add";
                if (op == "-") return "__uglc_half_subtract";
                if (op == "*") return "__uglc_half_multiply";
                if (op == "/") return "__uglc_half_divide";
                return {};
            }

            /** Emits inline half arithmetic with contraction and reassociation disabled only inside these operations. */
            void emitHalfArithmeticFunctions(std::ostringstream &stream)
            {
                for (const char *op : {"+", "-", "*", "/"})
                {
                    stream << "/** Evaluates float arithmetic and rounds its result to the requested half type. */\n"
                           << "template<typename HalfType, typename FloatType> HalfType " << halfArithmeticFunctionName(op)
                           << "(FloatType lhs, FloatType rhs) {\n"
                           << "#pragma clang fp contract(off)\n#pragma clang fp reassociate(off)\n"
                           << "    return HalfType(lhs " << op << " rhs);\n}\n";
                }
            }

            /** Emits arithmetic at a half conversion boundary without changing unrelated float expressions. */
            std::string emitRoundedHalfBinary(const UGLIR::Expression &conversion, const UGLIR::Expression &operand)
            {
                const auto target = describeValueType(conversion.type);
                if (!target.has_value() || target->scalarKind != UGLIR::ScalarKind::Half || target->matrixColumns != 0u ||
                    operand.kind != UGLIR::ExpressionKind::Binary || operand.operands.size() != 2u)
                    return {};
                const auto source = describeValueType(operand.type);
                if (!source.has_value() || source->scalarKind != UGLIR::ScalarKind::Float || source->matrixColumns != 0u ||
                    (source->vectorWidth > 1u && source->vectorWidth != target->vectorWidth))
                    return {};
                const std::string functionName = halfArithmeticFunctionName(operand.operatorName);
                if (functionName.empty()) return {};
                const std::string floatType = target->vectorWidth <= 1u ? "float" : "float" + std::to_string(target->vectorWidth);
                return functionName + "<" + canonicalMetalValueTypeName(conversion.type) + ">(" +
                       emitOperandValueConvertedToType(operand.operands[0], floatType, floatType) + ", " +
                       emitOperandValueConvertedToType(operand.operands[1], floatType, floatType) + ")";
            }

            /** Emits an explicit value conversion while preserving required DSL numeric casts. */
            std::string emitCastExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "cast expression is missing its operand.");
                    return {};
                }
                const UGLIR::Expression &operand = expression.operands.front();
                if (const std::string arithmetic = emitRoundedHalfBinary(expression, operand); !arithmetic.empty())
                    return arithmetic;
                const std::string operandExpression = emitExpression(operand);
                if (isVoidType(expression.type))
                {
                    return "((void)" + operandExpression + ")";
                }
                const std::string targetType = canonicalMetalValueTypeName(expression.type);
                const std::string sourceType = canonicalMetalValueTypeName(operand.type);
                return emitValueConvertedToType(operandExpression,
                                                sourceType,
                                                targetType,
                                                emitTypeName(expression.type, expression.sourceLocation));
            }

            /** Emits a constructor-style expression for supported scalar and vector types. */
            std::string emitConstructExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() == 1u)
                    if (const std::string arithmetic = emitRoundedHalfBinary(expression, expression.operands.front()); !arithmetic.empty())
                        return arithmetic;
                const UGLIR::Type *constructType = findType(expression.type);
                if (constructType != nullptr && constructType->role == UGLIR::TypeRole::SwizzleProxy && expression.operands.size() == 1)
                {
                    return emitExpression(expression.operands.front());
                }
                if (expression.operands.size() == 1 && expression.operands.front().type == expression.type)
                {
                    return emitExpression(expression.operands.front());
                }
                if (expression.operands.size() == 1 &&
                    canonicalMetalValueTypeName(expression.type) == canonicalMetalValueTypeName(expression.operands.front().type))
                {
                    return emitExpression(expression.operands.front());
                }
                const std::string targetTypeName = emitTypeName(expression.type, expression.sourceLocation);
                switch (expression.constructInfo.kind)
                {
                case UGLIR::ConstructKind::ScalarConvert:
                {
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "scalar construct is missing its operand.");
                        return {};
                    }
                    const std::string sourceScalarTypeName = expression.constructInfo.components.empty()
                                                                 ? canonicalMetalValueTypeName(expression.operands.front().type)
                                                                 : UGLIR::scalarTypeName(expression.constructInfo.components.front().sourceScalarKind);
                    const std::string targetScalarTypeName = UGLIR::scalarTypeName(expression.constructInfo.targetScalarKind);
                    return emitValueConvertedToType(emitExpression(expression.operands.front()),
                                                    sourceScalarTypeName,
                                                    targetScalarTypeName,
                                                    targetTypeName);
                }
                case UGLIR::ConstructKind::VectorSplat:
                    if (expression.constructInfo.components.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "vector splat construct is missing its component mapping.");
                        return targetTypeName + "()";
                    }
                    return targetTypeName + "(" + emitConstructComponentExpression(expression, expression.constructInfo.components.front()) + ")";
                case UGLIR::ConstructKind::VectorFromComponents:
                {
                    if (expression.constructInfo.components.size() != expression.constructInfo.targetComponentCount)
                    {
                        addDiagnostic(expression.sourceLocation,
                                      "vector construction received " + std::to_string(expression.constructInfo.components.size()) +
                                          " scalar components for target type \"" + expression.type + "\".");
                        return targetTypeName + "()";
                    }
                    std::string result = targetTypeName + "(";
                    size_t componentIndex = 0;
                    for (size_t operandIndex = 0; operandIndex < expression.operands.size(); ++operandIndex)
                    {
                        const size_t firstComponent = componentIndex;
                        while (componentIndex < expression.constructInfo.components.size() &&
                               expression.constructInfo.components[componentIndex].operandIndex == operandIndex)
                        {
                            if (expression.constructInfo.components[componentIndex].sourceComponentIndex != componentIndex - firstComponent)
                            {
                                addDiagnostic(expression.sourceLocation, "vector construction requires consecutive operand components.");
                                return {};
                            }
                            ++componentIndex;
                        }
                        const size_t componentCount = componentIndex - firstComponent;
                        if (componentCount == 0)
                        {
                            addDiagnostic(expression.sourceLocation, "vector construction is missing an operand component mapping.");
                            return {};
                        }
                        const std::string scalarType = UGLIR::scalarTypeName(expression.constructInfo.targetScalarKind);
                        const std::string operandTargetType = scalarType + (componentCount > 1 ? std::to_string(componentCount) : "");
                        if (expression.operands.size() == 1 && componentIndex == expression.constructInfo.components.size())
                        {
                            return emitOperandValueConvertedToType(expression.operands.front(), operandTargetType, targetTypeName);
                        }
                        result += emitOperandValueConvertedToType(expression.operands[operandIndex], operandTargetType, operandTargetType);
                        if (operandIndex + 1u < expression.operands.size())
                        {
                            result += ", ";
                        }
                    }
                    if (componentIndex != expression.constructInfo.components.size())
                    {
                        addDiagnostic(expression.sourceLocation, "vector construction references an invalid operand.");
                        return {};
                    }
                    result += ")";
                    return result;
                }
                case UGLIR::ConstructKind::Aggregate:
                case UGLIR::ConstructKind::None:
                    break;
                }
                const std::string canonicalConstructType = canonicalMetalValueTypeName(expression.type);
                const UGLIR::Type *constructedType = findType(expression.type);
                const bool useAggregateBraces = canonicalConstructType.empty() &&
                                                constructedType != nullptr &&
                                                constructedType->kind == UGLIR::TypeKind::Struct &&
                                                !expression.operands.empty();
                std::string result = targetTypeName + (useAggregateBraces ? "{" : "(");
                for (size_t index = 0; index < expression.operands.size(); ++index)
                {
                    result += emitExpression(expression.operands[index]);
                    if (index + 1 < expression.operands.size())
                    {
                        result += ", ";
                    }
                }
                result += useAggregateBraces ? "}" : ")";
                return result;
            }

            /** Emits a parenthesized binary expression. */
            std::string emitBinaryExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2)
                {
                    addDiagnostic(expression.sourceLocation, "binary expression is missing one or both operands.");
                    return {};
                }
                if (expression.isEagerLogical)
                {
                    return canonicalMetalValueTypeName(expression.type) + "((" + emitExpression(expression.operands[0]) + ") " +
                           (expression.operatorName == "&&" ? "&" : "|") + " (" + emitExpression(expression.operands[1]) + "))";
                }
                return "(" + emitExpression(expression.operands[0]) + " " + expression.operatorName + " " + emitExpression(expression.operands[1]) + ")";
            }

            /** Emits an ordinary atomic-backed update with one destination evaluation and C++ assignment sequencing. */
            std::string emitAtomicBackedUpdate(const UGLIR::Expression &destination,
                                               const UGLIR::Expression *rightOperand,
                                               const std::string &operation, bool postfix)
            {
                // An immediately invoked MSL expression scope keeps updates valid inside branches and arguments.
                std::string result = "([&]() { ";
                if (rightOperand != nullptr)
                    result += "auto __uglc_rhs = " + emitExpression(*rightOperand) + "; ";
                result += "auto __uglc_target = &(" + emitAtomicRawLValueExpression(destination) + "); ";
                result += "auto __uglc_value = atomicLoad(*__uglc_target); ";
                if (postfix)
                    result += "auto __uglc_previous = __uglc_value; ";
                result += "__uglc_value " + operation;
                if (rightOperand != nullptr)
                    result += " __uglc_rhs";
                result += "; ";
                if (postfix)
                    result += "atomicStore(*__uglc_target, __uglc_value); return __uglc_previous; ";
                else
                    result += "return atomicStore(*__uglc_target, __uglc_value); ";
                return result + "}())";
            }

            /** Emits a unary expression, preserving ordinary updates of atomic-backed storage. */
            std::string emitUnaryExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "unary expression is missing its operand.");
                    return {};
                }
                if ((expression.operatorName == "++" || expression.operatorName == "--") &&
                    isAtomicBackedLValueExpression(expression.operands.front()))
                    return emitAtomicBackedUpdate(expression.operands.front(), nullptr,
                                                 expression.operatorName, expression.isPostfix);
                const std::string operand = emitExpression(expression.operands.front());
                return expression.isPostfix ? "(" + operand + expression.operatorName + ")"
                                            : "(" + expression.operatorName + operand + ")";
            }

            /** Emits a ternary conditional expression. */
            std::string emitConditionalExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 3)
                {
                    addDiagnostic(expression.sourceLocation, "conditional expression is missing one or more operands.");
                    return {};
                }
                return "(" + emitExpression(expression.operands[0]) + " ? " + emitExpression(expression.operands[1]) + " : " + emitExpression(expression.operands[2]) + ")";
            }

            /** Emits promoted half compound arithmetic with one destination evaluation and one rounded writeback. */
            std::string emitHalfCompoundStore(const UGLIR::Expression &expression)
            {
                const std::string floatType = canonicalMetalValueTypeName(expression.computationType);
                const std::string halfType = canonicalMetalValueTypeName(expression.type);
                std::string result = "([&]() { auto __uglc_rhs = " + floatType + "(" + emitExpression(expression.operands[1]) + "); ";
                const UGLIR::Expression *base = &expression.operands[0];
                std::vector<const UGLIR::Expression *> selectors;
                while (!base->operands.empty() &&
                       (base->kind == UGLIR::ExpressionKind::MemberRef || base->kind == UGLIR::ExpressionKind::Subscript))
                {
                    const auto baseType = describeValueType(base->operands[0].type);
                    if (!baseType.has_value() || baseType->vectorWidth <= 1u || baseType->matrixColumns != 0u)
                        break;
                    selectors.push_back(base);
                    base = &base->operands[0];
                }
                result += "auto __uglc_target = &(" + emitExpression(*base) + "); ";
                std::string destination = "(*__uglc_target)";
                for (size_t index = selectors.size(); index > 0; --index)
                {
                    const UGLIR::Expression &selector = *selectors[index - 1u];
                    if (selector.kind == UGLIR::ExpressionKind::MemberRef)
                        destination += "." + selector.name;
                    else
                    {
                        const std::string indexName = "__uglc_index" + std::to_string(index);
                        result += "auto " + indexName + " = " + emitExpression(selector.operands[1]) + "; ";
                        destination += "[" + indexName + "]";
                    }
                }
                result += "auto __uglc_value = " + halfArithmeticFunctionName(expression.operatorName.substr(0, expression.operatorName.size() - 1u)) +
                          "<" + halfType + ">(" + floatType + "(" + destination + "), __uglc_rhs); ";
                return result + destination + " = __uglc_value; return __uglc_value; }())";
            }

            /** Emits a store expression as an assignment fragment. */
            std::string emitStoreExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2)
                {
                    addDiagnostic(expression.sourceLocation, "store expression is missing its destination or value operand.");
                    return {};
                }
                const std::string op = expression.operatorName.empty() ? "=" : expression.operatorName;
                if (const auto valueType = describeValueType(expression.type);
                    op != "=" && !expression.computationType.empty() && valueType.has_value() &&
                    valueType->scalarKind == UGLIR::ScalarKind::Half)
                    return emitHalfCompoundStore(expression);
                if (op != "=" && isAtomicBackedLValueExpression(expression.operands[0]))
                    return emitAtomicBackedUpdate(expression.operands[0], &expression.operands[1], op, false);
                std::string value = emitExpression(expression.operands[1]);
                if (op == "=" && isAtomicBackedLValueExpression(expression.operands[0]))
                {
                    return "atomicStore(" + emitAtomicRawLValueExpression(expression.operands[0]) + ", " + value + ")";
                }
                return "(" + emitExpression(expression.operands[0]) + " " + op + " " + value + ")";
            }

            /** Returns the resource parameter reached by a member-reference expression when it names a reflected resource. */
            const MSLResourceParameter *findResourceParameterForExpression(const UGLIR::Expression &expression) const
            {
                const UGLIR::ResourceBinding *resource = UGLIR::findReflectedResource(mModule, expression);
                if (resource == nullptr) return nullptr;
                const auto iter = mResourcesByUGLIRName.find(resource->name);
                return iter == mResourcesByUGLIRName.end() ? nullptr : &iter->second;
            }

            /** Returns the reflected resource name referenced by an expression or an access into that expression. */
            std::string resourceNameForAccessExpression(const UGLIR::Expression &expression) const
            {
                if (const MSLResourceParameter *resource = findResourceParameterForExpression(expression))
                {
                    return resource->uglirName;
                }
                if (expression.kind == UGLIR::ExpressionKind::Subscript && !expression.operands.empty())
                {
                    return resourceNameForAccessExpression(expression.operands.front());
                }
                if ((expression.kind == UGLIR::ExpressionKind::Cast ||
                     expression.kind == UGLIR::ExpressionKind::Load ||
                     expression.kind == UGLIR::ExpressionKind::Construct) &&
                    !expression.operands.empty())
                {
                    return resourceNameForAccessExpression(expression.operands.front());
                }
                return {};
            }

            /** Returns true when the call name denotes an atomic intrinsic that requires a Metal atomic resource view. */
            bool isAtomicIntrinsicCall(UGLIR::IntrinsicCallKind intrinsicCallKind) const
            {
                return intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicAdd ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicAnd ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicCompareExchange ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicLoad ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicMax ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicMin ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicOr ||
                       intrinsicCallKind == UGLIR::IntrinsicCallKind::AtomicStore;
            }

            /** Collects local workgroup variable names that are passed to atomic intrinsics. */
            std::unordered_set<std::string> collectAtomicWorkgroupVariableNames() const
            {
                std::unordered_set<std::string> variableNames;
                for (const UGLIR::Function &function : mModule.functions)
                {
                    collectStatementListAtomicWorkgroupVariableNames(function.body, variableNames);
                }
                return variableNames;
            }

            /** Scans a statement list for atomic intrinsic operands that reference workgroup locals. */
            void collectStatementListAtomicWorkgroupVariableNames(const std::vector<UGLIR::Statement> &statements,
                                                                  std::unordered_set<std::string> &variableNames) const
            {
                for (const UGLIR::Statement &statement : statements)
                {
                    for (const UGLIR::Expression &expression : statement.expressions)
                    {
                        collectExpressionAtomicWorkgroupVariableNames(expression, variableNames);
                    }
                    collectStatementListAtomicWorkgroupVariableNames(statement.children, variableNames);
                    collectStatementListAtomicWorkgroupVariableNames(statement.elseChildren, variableNames);
                }
            }

            /** Scans one expression tree for atomic intrinsic operands that reference workgroup locals. */
            void collectExpressionAtomicWorkgroupVariableNames(const UGLIR::Expression &expression,
                                                               std::unordered_set<std::string> &variableNames) const
            {
                if (expression.kind == UGLIR::ExpressionKind::Call &&
                    isAtomicIntrinsicCall(expression.intrinsicCallKind) &&
                    !expression.operands.empty())
                {
                    if (const std::string variableName = atomicWorkgroupVariableNameForExpression(expression.operands.front()); !variableName.empty())
                    {
                        variableNames.insert(variableName);
                    }
                }
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    collectExpressionAtomicWorkgroupVariableNames(operand, variableNames);
                }
            }

            /** Returns the local variable name when an expression accesses a GroupShared value. */
            std::string atomicWorkgroupVariableNameForExpression(const UGLIR::Expression &expression) const
            {
                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    if (isWorkgroupTypeName(expression.type))
                    {
                        return expression.name;
                    }
                    const UGLIR::Type *type = findType(expression.type);
                    if (type != nullptr && type->kind == UGLIR::TypeKind::Array && isWorkgroupTypeName(type->elementType))
                    {
                        return expression.name;
                    }
                }
                if (expression.kind == UGLIR::ExpressionKind::Subscript && !expression.operands.empty())
                {
                    return atomicWorkgroupVariableNameForExpression(expression.operands.front());
                }
                if ((expression.kind == UGLIR::ExpressionKind::Cast ||
                     expression.kind == UGLIR::ExpressionKind::Load ||
                     expression.kind == UGLIR::ExpressionKind::Construct) &&
                    !expression.operands.empty())
                {
                    return atomicWorkgroupVariableNameForExpression(expression.operands.front());
                }
                return {};
            }

        };
    } // namespace

    UGLIRToMSLEmissionResult emitModuleAsMSL(const UGLIR::Module &module, const MSLResourceLayout &resourceLayout)
    {
        UGLIRToMSLEmissionResult validationFailure;
        for (const UGLIR::UGLIRValidationDiagnostic &diagnostic : UGLIR::validateModule(module))
        {
            validationFailure.diagnostics.push_back({diagnostic.sourceLocation, diagnostic.message});
        }
        if (!validationFailure.diagnostics.empty()) { return validationFailure; }
        UGLIRToMSLEmitter emitter(module, resourceLayout);
        return emitter.run();
    }

    UGLIRToMSLEmissionResult emitModuleAsMSL(const UGLIR::Module &module)
    {
        return emitModuleAsMSL(module, MSLResourceLayout{});
    }

    std::string formatUGLIRToMSLEmissionDiagnostics(const UGLIRToMSLEmissionResult &result)
    {
        std::ostringstream stream;
        for (const UGLIRToMSLEmissionDiagnostic &diagnostic : result.diagnostics)
        {
            stream << formatDiagnostic(diagnostic) << '\n';
        }
        return stream.str();
    }
} // namespace UGLC::CodeGen::MSLEmitter
