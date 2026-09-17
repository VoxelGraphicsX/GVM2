#include "UGLIRToHLSLEmitter.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/HLSL/HLSLPrelude.hpp>
#include <CodeGen/HLSL/HLSLTextureTypes.hpp>
#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace UGLC::CodeGen::HLSLEmitter
{
    namespace
    {
        /** Stores the HLSL spelling and binding metadata for one reflected UGLIR resource. */
        struct HLSLResourceDeclaration
        {
            std::string uglirName;
            std::string hlslName;
            std::string declaration;
            UGLIR::ResourceKind kind = UGLIR::ResourceKind::Unknown;
        };

        /** Stores one HLSL subpass input declaration generated from a pixel-local input attachment. */
        struct HLSLPixelLocalInputAttachment
        {
            std::string fieldName;
            std::string hlslResourceName;
            std::string hlslValueTypeName;
            uint32_t bindGroupIndex = 0;
            uint32_t bindingIndex = 0;
            uint32_t inputAttachmentIndex = 0;
            UGLIR::SourceLocation sourceLocation;
        };

        /** Stores the entry ABI expansion for one UGLIR PixelLocalInput parameter. */
        struct HLSLPixelLocalInputParameter
        {
            std::string parameterName;
            std::string parameterTypeName;
            std::string inputStructName;
            std::vector<HLSLPixelLocalInputAttachment> attachments;
            UGLIR::SourceLocation sourceLocation;
        };

        /** Returns true when the character can appear in a generated HLSL identifier after the first character. */
        bool isHLSLIdentifierBodyCharacter(char ch)
        {
            return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        }

        /** Returns true when the character can appear as the first character in a generated HLSL identifier. */
        bool isHLSLIdentifierHeadCharacter(char ch)
        {
            return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
        }

        /** Returns true when the identifier is an HLSL or C++ keyword that should not be emitted directly. */
        bool isReservedHLSLIdentifier(const std::string &identifier)
        {
            static const std::unordered_set<std::string> reservedIdentifiers = {
                "AppendStructuredBuffer", "BlendState", "Bool", "Buffer", "ByteAddressBuffer", "ConsumeStructuredBuffer",
                "DepthStencilState", "DepthStencilView", "DomainShader", "GeometryShader", "InputPatch", "LineStream",
                "OutputPatch", "PixelShader", "PointStream", "RasterizerState", "RenderTargetView", "RWBuffer",
                "RWByteAddressBuffer", "RWStructuredBuffer", "RWTexture1D", "RWTexture1DArray", "RWTexture2D",
                "RWTexture2DArray", "RWTexture3D", "SamplerComparisonState", "SamplerState", "StructuredBuffer",
                "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS", "Texture2DMSArray",
                "Texture3D", "TextureCube", "TextureCubeArray", "TriangleStream", "VertexShader", "asm", "asm_fragment",
                "auto", "bool", "break", "case", "catch", "cbuffer", "char", "class", "column_major", "compile",
                "compile_fragment", "const", "continue", "default", "delete", "discard", "do", "double", "else",
                "enum", "export", "extern", "false", "float", "for", "fxgroup", "groupshared", "half", "if", "in",
                "inline", "inout", "int", "interface", "line", "lineadj", "linear", "matrix", "namespace", "new",
                "nointerpolation", "out", "packoffset", "pass", "pixelfragment", "point", "precise", "register",
                "return", "row_major", "sampler", "shared", "snorm", "stateblock", "stateblock_state", "static",
                "string", "struct", "switch", "tbuffer", "technique", "technique10", "technique11", "texture",
                "true", "try", "typedef", "triangle", "triangleadj", "uint", "uniform", "unorm", "unsigned",
                "vector", "vertexfragment", "void", "volatile", "while",
            };
            return reservedIdentifiers.contains(identifier);
        }

        /** Converts an arbitrary UGLIR symbol into a stable HLSL identifier. */
        std::string sanitizeHLSLIdentifier(const std::string &name)
        {
            std::string result;
            result.reserve(name.size() + 1);
            for (char ch : name)
            {
                result.push_back(isHLSLIdentifierBodyCharacter(ch) ? ch : '_');
            }
            if (result.empty())
            {
                result = "unnamed";
            }
            if (!isHLSLIdentifierHeadCharacter(result.front()))
            {
                result.insert(result.begin(), '_');
            }
            if (isReservedHLSLIdentifier(result))
            {
                result += "_";
            }
            return result;
        }

        /** Joins member-path segments using UGLIR reflection's dotted resource spelling. */
        std::string joinResourcePath(const std::vector<std::string> &path)
        {
            std::string result;
            for (const std::string &segment : path)
            {
                if (segment.empty())
                {
                    continue;
                }
                if (!result.empty())
                {
                    result += ".";
                }
                result += segment;
            }
            return result;
        }

        /** Emits one indentation level using four spaces per generated HLSL block level. */
        std::string makeIndent(int indentLevel)
        {
            std::string result;
            for (int i = 0; i < indentLevel; ++i)
            {
                result += "    ";
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

        /** Returns the HLSL semantic suffix for a stage IO field. */
        std::string hlslStageFieldSemantic(const UGLIR::TypeField &field, const UGLIR::StageIOBinding *activeOutput = nullptr)
        {
            const uint32_t semanticLocation = activeOutput != nullptr ? activeOutput->location : field.location;
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Position)
            {
                return " : SV_Position";
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Color ||
                field.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalColor)
            {
                return " : SV_Target" + std::to_string(semanticLocation);
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Depth)
            {
                return " : SV_Depth";
            }
            if (field.semanticKind == UGLIR::BuiltinSemanticKind::Attribute ||
                field.semanticKind == UGLIR::BuiltinSemanticKind::Field)
            {
                return " : TEXCOORD" + std::to_string(semanticLocation);
            }
            return {};
        }

        /** Returns true when a UGLIR struct is already declared by the shared HLSL prelude. */
        bool isHLSLPreludeStructType(const std::string &typeName)
        {
            return typeName == "UGL_DrawInfo_";
        }

        /** Returns true when the UGLIR type name is an unsigned integer type that should use HLSL's `u` literal suffix. */
        bool isUnsignedIntegerTypeName(const std::string &typeName)
        {
            return typeName == "u32" || typeName == "uint" || typeName == "uint32_t" || typeName == "unsigned int";
        }

        /** Emits one source location through UGLC's shared diagnostic formatting helpers. */
        std::string formatDiagnostic(const UGLIRToHLSLEmissionDiagnostic &diagnostic)
        {
            const std::string message = "UGLIR HLSL emitter: " + diagnostic.message;
            if (!diagnostic.sourceLocation.file.empty() && diagnostic.sourceLocation.line != 0 && diagnostic.sourceLocation.column != 0)
            {
                return UGLC::CodeGen::formatClangStyleDiagnostic(diagnostic.sourceLocation.file,
                                                                  diagnostic.sourceLocation.line,
                                                                  diagnostic.sourceLocation.column,
                                                                  message);
            }
            return UGLC::CodeGen::formatUnlocatedDiagnostic(message);
        }

        /** Emits the Phase-5 HLSL source body for one UGLIR module. */
        class UGLIRToHLSLEmitter
        {
        public:
            /** Creates an emitter that owns no module data and reports diagnostics into the supplied result. */
            explicit UGLIRToHLSLEmitter(const UGLIR::Module &module)
                : mModule(module)
            {
                for (const UGLIR::Type &type : module.types)
                {
                    mTypesByName.emplace(type.name, &type);
                }
                for (const UGLIR::ResourceBinding &resource : module.reflection.resources)
                {
                    if (resource.resourceRole == UGLIR::ResourceRole::AccessBounds ||
                        resource.resourceRole == UGLIR::ResourceRole::TextureIndexTable ||
                        resource.resourceRole == UGLIR::ResourceRole::BufferIndexTable ||
                        resource.resourceRole == UGLIR::ResourceRole::DrawInfo ||
                        resource.resourceRole == UGLIR::ResourceRole::CommandParams)
                    {
                        continue;
                    }
                    HLSLResourceDeclaration declaration;
                    declaration.uglirName = resource.name;
                    declaration.hlslName = sanitizeHLSLIdentifier(resource.name);
                    declaration.kind = resource.kind;
                    mResourcesByUGLIRName.emplace(resource.name, std::move(declaration));
                }
            }

            /** Emits the module and returns either source text or accumulated diagnostics. */
            UGLIRToHLSLEmissionResult run()
            {
                UGLIRToHLSLEmissionResult result;
                const UGLIR::Function *entryFunction = findEntryFunction();
                if (entryFunction == nullptr)
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                mPixelLocalInputParameters = collectPixelLocalInputParameters(*entryFunction);
                buildResourceDeclarations();

                std::ostringstream stream;
                emitStructDeclarations(stream, *entryFunction);
                emitPixelLocalInputStructDeclarations(stream);
                emitResourceDeclarations(stream);
                emitInternalHelperFunctions(stream);
                emitEntryFunction(stream, *entryFunction);

                if (!mDiagnostics.empty())
                {
                    result.diagnostics = std::move(mDiagnostics);
                    return result;
                }

                UGLC::CodeGen::EmittedShaderSource source;
                source.backend = UGLC::CodeGen::ShaderBackendKind::HLSLSPIRV;
                source.stage = toShaderStageKind(entryFunction->stage);
                source.backendName = UGLC::CodeGen::getHLSLSPIRVShaderBackendCapabilities().backendName;
                source.stageName = UGLC::CodeGen::getShaderStageDisplayName(source.stage);
                source.entryPoint = entryPointNameForEntryKind(entryFunction->entryKind);
                source.debugName = mModule.name + "::" + entryFunction->name;
                source.sourceName = mModule.sourceLocation.file;
                source.preludeText = UGLC::CodeGen::HLSL::MakeHLSLPreludeSource();
                source.sourceText = stream.str();
                source.enableLineDirectives = false;
                result.source = std::move(source);
                return result;
            }

        private:
            const UGLIR::Module &mModule;
            std::unordered_map<std::string, const UGLIR::Type *> mTypesByName;
            std::unordered_map<std::string, HLSLResourceDeclaration> mResourcesByUGLIRName;
            std::vector<HLSLPixelLocalInputParameter> mPixelLocalInputParameters;
            std::vector<UGLIRToHLSLEmissionDiagnostic> mDiagnostics;
            const UGLIR::Function *mCurrentFunction = nullptr;

            /** Records one HLSL emission diagnostic against the most precise UGLIR location available. */
            void addDiagnostic(const UGLIR::SourceLocation &location, std::string message)
            {
                mDiagnostics.push_back({location, std::move(message)});
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

            /** Returns the canonical UGLIR type record for a name when the lowering phase registered one. */
            const UGLIR::Type *findType(const std::string &typeName) const
            {
                const auto iter = mTypesByName.find(typeName);
                return iter == mTypesByName.end() ? nullptr : iter->second;
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

            /** Converts one UGLIR type name into the HLSL type spelling supported by Phase 5. */
            std::string emitTypeName(const std::string &typeName, const UGLIR::SourceLocation &location)
            {
                if (typeName == "void")
                {
                    return "void";
                }
                if (typeName == "bool")
                {
                    return "bool";
                }
                if (typeName == "i32" || typeName == "int" || typeName == "int32_t")
                {
                    return "int";
                }
                if (isUnsignedIntegerTypeName(typeName))
                {
                    return "uint";
                }
                if (typeName == "f32" || typeName == "float")
                {
                    return "float";
                }
                if (typeName == "f16" || typeName == "half")
                {
                    return "half";
                }
                if (typeName == "int2" || typeName == "int3" || typeName == "int4" ||
                    typeName == "uint2" || typeName == "uint3" || typeName == "uint4" ||
                    typeName == "float2" || typeName == "float3" || typeName == "float4" ||
                    typeName == "bool2" || typeName == "bool3" || typeName == "bool4" ||
                    typeName == "half2" || typeName == "half3" || typeName == "half4")
                {
                    return typeName;
                }

                const UGLIR::Type *type = findType(typeName);
                if (type == nullptr)
                {
                    addDiagnostic(location, "unsupported UGLIR type \"" + typeName + "\".");
                    return sanitizeHLSLIdentifier(typeName);
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
                    return sanitizeHLSLIdentifier(type->name);
                case UGLIR::TypeKind::Array:
                    return emitTypeName(type->elementType, location);
                case UGLIR::TypeKind::Workgroup:
                    return emitTypeName(type->elementType, location);
                case UGLIR::TypeKind::Texture:
                {
                    std::string textureType = type->accessMode == UGLIR::AccessMode::ReadWrite ? "RWTexture2D" : "Texture2D";
                    if (type->textureDimension == UGLIR::TextureDimension::Texture2DArray)
                    {
                        textureType = type->accessMode == UGLIR::AccessMode::ReadWrite ? "RWTexture2DArray" : "Texture2DArray";
                    }
                    else if (type->textureDimension == UGLIR::TextureDimension::Texture3D)
                    {
                        textureType = type->accessMode == UGLIR::AccessMode::ReadWrite ? "RWTexture3D" : "Texture3D";
                    }
                    return textureType + "<" + emitTypeName(type->elementType, location) + ">";
                }
                case UGLIR::TypeKind::Sampler:
                    return "SamplerState";
                default:
                    break;
                }

                addDiagnostic(location, "unsupported UGLIR type kind for \"" + typeName + "\".");
                return sanitizeHLSLIdentifier(typeName);
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
                    if (fieldType != nullptr &&
                        (fieldType->kind == UGLIR::TypeKind::Resource || fieldType->kind == UGLIR::TypeKind::Buffer ||
                         fieldType->kind == UGLIR::TypeKind::Texture || fieldType->kind == UGLIR::TypeKind::Sampler))
                    {
                        return true;
                    }
                }
                return false;
            }

            /** Finds the active reflected output ABI binding for a framebuffer struct field. */
            const UGLIR::StageIOBinding *findActiveFramebufferOutputField(const UGLIR::TypeField &field) const
            {
                for (const UGLIR::StageIOBinding &output : mModule.reflection.stageOutputs)
                {
                    if (output.name == field.name)
                    {
                        return &output;
                    }
                }
                return nullptr;
            }

            /** Returns true when a struct field should be emitted for an HLSL output ABI struct. */
            bool shouldEmitOutputStructField(const UGLIR::TypeField &field) const
            {
                return field.semanticKind != UGLIR::BuiltinSemanticKind::PixelLocalDepth && findActiveFramebufferOutputField(field) != nullptr;
            }

            /** Returns the HLSL global resource name used for one pixel-local input attachment. */
            std::string pixelLocalInputResourceName(const std::string &resourceName) const
            {
                const size_t separator = resourceName.find('.');
                if (separator == std::string::npos)
                {
                    return sanitizeHLSLIdentifier(resourceName);
                }
                return "_UGLC_PixelLocalInput_" + sanitizeHLSLIdentifier(resourceName.substr(0, separator)) + "_" + sanitizeHLSLIdentifier(resourceName.substr(separator + 1));
            }

            /** Returns the HLSL value type carried by one pixel-local input attachment. */
            std::string pixelLocalInputAttachmentTypeName(const UGLIR::ResourceBinding &resource)
            {
                if (resource.textureFormat != UGLIR::TextureFormat::Unknown)
                {
                    try
                    {
                        return UGLC::CodeGen::HLSL::MakeFormatToVectorTypeForFrameBuffer(UGLIR::textureFormatToken(resource.textureFormat));
                    }
                    catch (const std::runtime_error &error)
                    {
                        addDiagnostic(resource.sourceLocation, error.what());
                    }
                }
                return emitTypeName(resource.elementType, resource.sourceLocation);
            }

            /** Collects the HLSL ABI expansion required for all UGLIR PixelLocalInput parameters. */
            std::vector<HLSLPixelLocalInputParameter> collectPixelLocalInputParameters(const UGLIR::Function &function)
            {
                std::vector<HLSLPixelLocalInputParameter> result;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (parameter.semanticKind != UGLIR::BuiltinSemanticKind::PixelLocalInput)
                    {
                        continue;
                    }

                    HLSLPixelLocalInputParameter inputParameter;
                    inputParameter.parameterName = sanitizeHLSLIdentifier(parameter.name);
                    inputParameter.parameterTypeName = parameter.type;
                    inputParameter.inputStructName = sanitizeHLSLIdentifier(parameter.type) + "_PixelLocalInput";
                    inputParameter.sourceLocation = parameter.sourceLocation;

                    const std::string resourcePrefix = parameter.name + ".";
                    for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                    {
                        if (resource.kind != UGLIR::ResourceKind::InputAttachment ||
                            resource.name.rfind(resourcePrefix, 0) != 0)
                        {
                            continue;
                        }
                        inputParameter.attachments.push_back(HLSLPixelLocalInputAttachment{
                            .fieldName = resource.name.substr(resourcePrefix.size()),
                            .hlslResourceName = pixelLocalInputResourceName(resource.name),
                            .hlslValueTypeName = pixelLocalInputAttachmentTypeName(resource),
                            .bindGroupIndex = resource.bindGroupIndex,
                            .bindingIndex = resource.bindingIndex,
                            .inputAttachmentIndex = resource.inputAttachmentIndex,
                            .sourceLocation = resource.sourceLocation,
                        });
                    }

                    std::sort(inputParameter.attachments.begin(),
                              inputParameter.attachments.end(),
                              [](const HLSLPixelLocalInputAttachment &left, const HLSLPixelLocalInputAttachment &right) {
                                  if (left.inputAttachmentIndex != right.inputAttachmentIndex)
                                  {
                                      return left.inputAttachmentIndex < right.inputAttachmentIndex;
                                  }
                                  return left.fieldName < right.fieldName;
                              });
                    result.push_back(std::move(inputParameter));
                }
                return result;
            }

            /** Finds a previously collected PixelLocalInput expansion plan by sanitized parameter name. */
            const HLSLPixelLocalInputParameter *findPixelLocalInputParameter(const UGLIR::FunctionParameter &parameter) const
            {
                const std::string parameterName = sanitizeHLSLIdentifier(parameter.name);
                for (const HLSLPixelLocalInputParameter &inputParameter : mPixelLocalInputParameters)
                {
                    if (inputParameter.parameterName == parameterName)
                    {
                        return &inputParameter;
                    }
                }
                return nullptr;
            }

            /** Emits all user struct declarations required by helper signatures and local variables. */
            void emitStructDeclarations(std::ostringstream &stream, const UGLIR::Function &entryFunction)
            {
                for (const UGLIR::Type &type : mModule.types)
                {
                    if (type.kind != UGLIR::TypeKind::Struct || type.fields.empty() || hasResourceField(type) || isHLSLPreludeStructType(type.name))
                    {
                        continue;
                    }
                    stream << "struct " << sanitizeHLSLIdentifier(type.name) << "\n{\n";
                    for (const UGLIR::TypeField &field : type.fields)
                    {
                        const bool outputStruct = type.name == entryFunction.returnType;
                        const UGLIR::StageIOBinding *activeOutput = outputStruct ? findActiveFramebufferOutputField(field) : nullptr;
                        if (outputStruct && (field.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalDepth || activeOutput == nullptr))
                        {
                            continue;
                        }
                        stream << "    " << emitTypeName(field.type, field.sourceLocation) << " "
                               << sanitizeHLSLIdentifier(field.name) << hlslStageFieldSemantic(field, activeOutput) << ";\n";
                    }
                    stream << "};\n\n";
                }
            }

            /** Emits plain pixel-local input structs used to materialize HLSL SubpassInput values. */
            void emitPixelLocalInputStructDeclarations(std::ostringstream &stream)
            {
                std::unordered_set<std::string> emittedStructNames;
                for (const HLSLPixelLocalInputParameter &inputParameter : mPixelLocalInputParameters)
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
                               << sanitizeHLSLIdentifier(field.name) << ";\n";
                    }
                    stream << "};\n\n";
                }
            }

            /** Builds all HLSL resource declarations from UGLIR reflection. */
            void buildResourceDeclarations()
            {
                for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                {
                    auto iter = mResourcesByUGLIRName.find(resource.name);
                    if (iter == mResourcesByUGLIRName.end())
                    {
                        continue;
                    }

                    const std::string bindingAttribute = "[[vk::binding(" + std::to_string(resource.bindingIndex) + ", " + std::to_string(resource.bindGroupIndex) + ")]] ";
                    if (resource.kind == UGLIR::ResourceKind::StorageBuffer)
                    {
                        const std::string elementType = emitTypeName(resource.elementType, resource.sourceLocation);
                        const bool writable = resource.accessMode != UGLIR::AccessMode::Read;
                        const char registerClass = writable ? 'u' : 't';
                        iter->second.declaration = bindingAttribute + std::string(writable ? "RWStructuredBuffer<" : "StructuredBuffer<")
                                                    + elementType + "> " + iter->second.hlslName
                                                    + " : register(" + registerClass + std::to_string(resource.bindingIndex)
                                                    + ", space" + std::to_string(resource.bindGroupIndex) + ");";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::UniformBuffer)
                    {
                        const std::string elementType = emitTypeName(resource.elementType, resource.sourceLocation);
                        iter->second.declaration = bindingAttribute + "ConstantBuffer<" + elementType + "> " + iter->second.hlslName
                                                    + " : register(b" + std::to_string(resource.bindingIndex)
                                                    + ", space" + std::to_string(resource.bindGroupIndex) + ");";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::InputAttachment)
                    {
                        iter->second.hlslName = pixelLocalInputResourceName(resource.name);
                        iter->second.declaration = bindingAttribute + "[[vk::input_attachment_index(" + std::to_string(resource.inputAttachmentIndex)
                                                    + ")]] SubpassInput<" + pixelLocalInputAttachmentTypeName(resource) + "> "
                                                    + iter->second.hlslName + ";";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::Texture || resource.kind == UGLIR::ResourceKind::StorageTexture)
                    {
                        const bool writable = resource.kind == UGLIR::ResourceKind::StorageTexture;
                        std::string textureType = writable ? "RWTexture2D" : "Texture2D";
                        if (resource.textureDimension == UGLIR::TextureDimension::Texture2DArray)
                        {
                            textureType = writable ? "RWTexture2DArray" : "Texture2DArray";
                        }
                        else if (resource.textureDimension == UGLIR::TextureDimension::Texture3D)
                        {
                            textureType = writable ? "RWTexture3D" : "Texture3D";
                        }
                        const char registerClass = writable ? 'u' : 't';
                        std::string imageFormatAttribute;
                        if (writable && resource.textureFormat != UGLIR::TextureFormat::Unknown)
                        {
                            imageFormatAttribute = "[[vk::image_format(\"" + UGLC::CodeGen::HLSL::MakeVulkanImageFormatForStorageTexture(UGLIR::textureFormatToken(resource.textureFormat)) + "\")]] ";
                        }
                        iter->second.declaration = bindingAttribute + imageFormatAttribute + textureType + "<" + emitTypeName(resource.elementType, resource.sourceLocation) + "> "
                                                    + iter->second.hlslName
                                                    + " : register(" + registerClass + std::to_string(resource.bindingIndex)
                                                    + ", space" + std::to_string(resource.bindGroupIndex) + ");";
                        continue;
                    }
                    if (resource.kind == UGLIR::ResourceKind::Sampler)
                    {
                        iter->second.declaration = bindingAttribute + "SamplerState " + iter->second.hlslName
                                                    + " : register(s" + std::to_string(resource.bindingIndex)
                                                    + ", space" + std::to_string(resource.bindGroupIndex) + ");";
                        continue;
                    }

                    addDiagnostic(resource.sourceLocation, "unsupported resource kind for Phase 8 HLSL emission.");
                }
            }

            /** Emits the global HLSL resource declarations that DXC will compile into descriptor bindings. */
            void emitResourceDeclarations(std::ostringstream &stream)
            {
                bool emittedAnyResource = false;
                for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                {
                    const auto iter = mResourcesByUGLIRName.find(resource.name);
                    if (iter != mResourcesByUGLIRName.end() && !iter->second.declaration.empty())
                    {
                        stream << iter->second.declaration << "\n";
                        emittedAnyResource = true;
                    }
                }
                if (emittedAnyResource)
                {
                    stream << "\n";
                }
            }

            /** Emits all internal helper functions before the entry function so calls resolve without prototypes. */
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

            /** Emits an HLSL shader entry with reflected stage metadata. */
            void emitEntryFunction(std::ostringstream &stream, const UGLIR::Function &function)
            {
                const UGLIR::Function *previousFunction = mCurrentFunction;
                mCurrentFunction = &function;
                const bool isCompute = function.entryKind == UGLIR::ShaderEntryKind::Compute;
                if (isCompute)
                {
                    stream << "[numthreads(" << function.workgroupSize[0] << ", " << function.workgroupSize[1] << ", " << function.workgroupSize[2] << ")]\n";
                }
                const std::string returnType = isCompute ? "void" : emitTypeName(function.returnType, function.sourceLocation);
                stream << returnType << " " << entryPointNameForEntryKind(function.entryKind) << "(";
                std::vector<std::string> parameters;
                std::vector<const HLSLPixelLocalInputParameter *> pixelLocalInputParameters;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (isResourceAliasType(parameter.type))
                    {
                        continue;
                    }
                    if (parameter.semanticKind == UGLIR::BuiltinSemanticKind::PixelLocalInput)
                    {
                        const HLSLPixelLocalInputParameter *inputParameter = findPixelLocalInputParameter(parameter);
                        if (inputParameter == nullptr)
                        {
                            addDiagnostic(parameter.sourceLocation, "PixelLocalInput parameter \"" + parameter.name + "\" could not be mapped to HLSL SubpassInput declarations.");
                            continue;
                        }
                        pixelLocalInputParameters.push_back(inputParameter);
                        continue;
                    }
                    parameters.push_back(emitEntryParameter(parameter));
                }
                for (size_t index = 0; index < parameters.size(); ++index)
                {
                    stream << parameters[index];
                    if (index + 1 < parameters.size())
                    {
                        stream << ", ";
                    }
                }
                stream << ")\n";
                if (pixelLocalInputParameters.empty())
                {
                    emitStatementListAsBlock(stream, function.body, 0);
                    mCurrentFunction = previousFunction;
                    return;
                }

                stream << "{\n";
                for (const HLSLPixelLocalInputParameter *inputParameter : pixelLocalInputParameters)
                {
                    stream << "    " << inputParameter->inputStructName << " " << inputParameter->parameterName << ";\n";
                    for (const HLSLPixelLocalInputAttachment &attachment : inputParameter->attachments)
                    {
                        stream << "    " << inputParameter->parameterName << "." << sanitizeHLSLIdentifier(attachment.fieldName)
                               << " = " << attachment.hlslResourceName << ".SubpassLoad();\n";
                    }
                }
                for (const UGLIR::Statement &statement : function.body)
                {
                    emitStatement(stream, statement, 1);
                }
                stream << "}\n";
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

                stream << emitTypeName(function.returnType, function.sourceLocation) << " "
                       << sanitizeHLSLIdentifier(function.name) << "(";
                std::vector<std::string> parameters;
                for (const UGLIR::FunctionParameter &parameter : function.parameters)
                {
                    if (isResourceAliasType(parameter.type))
                    {
                        continue;
                    }
                    parameters.push_back(emitTypeName(parameter.type, parameter.sourceLocation) + " " + sanitizeHLSLIdentifier(parameter.name));
                }
                for (size_t index = 0; index < parameters.size(); ++index)
                {
                    stream << parameters[index];
                    if (index + 1 < parameters.size())
                    {
                        stream << ", ";
                    }
                }
                stream << ")\n";
                const UGLIR::Function *previousFunction = mCurrentFunction;
                mCurrentFunction = &function;
                emitStatementListAsBlock(stream, function.body, 0);
                mCurrentFunction = previousFunction;
            }

            /** Emits one entry parameter and maps supported UGLIR semantics to HLSL semantics. */
            std::string emitEntryParameter(const UGLIR::FunctionParameter &parameter)
            {
                const std::string typeName = emitTypeName(parameter.type, parameter.sourceLocation);
                const std::string parameterName = sanitizeHLSLIdentifier(parameter.name);
                switch (parameter.semanticKind)
                {
                case UGLIR::BuiltinSemanticKind::DispatchThreadID:
                    return typeName + " " + parameterName + " : SV_DispatchThreadID";
                case UGLIR::BuiltinSemanticKind::GroupThreadID:
                    return typeName + " " + parameterName + " : SV_GroupThreadID";
                case UGLIR::BuiltinSemanticKind::GroupID:
                    return typeName + " " + parameterName + " : SV_GroupID";
                case UGLIR::BuiltinSemanticKind::GroupIndex:
                    return "uint " + parameterName + " : SV_GroupIndex";
                case UGLIR::BuiltinSemanticKind::VertexID:
                    return "uint " + parameterName + " : SV_VertexID";
                case UGLIR::BuiltinSemanticKind::InstanceID:
                    return "uint " + parameterName + " : SV_InstanceID";
                case UGLIR::BuiltinSemanticKind::DrawEntityID:
                    return "uint " + parameterName;
                case UGLIR::BuiltinSemanticKind::DrawEntityInstanceID:
                    return "uint " + parameterName + " : SV_InstanceID";
                case UGLIR::BuiltinSemanticKind::PixelCoord:
                    return typeName + " " + parameterName + " : SV_Position";
                case UGLIR::BuiltinSemanticKind::SampleIndex:
                    return "uint " + parameterName + " : SV_SampleIndex";
                case UGLIR::BuiltinSemanticKind::StageInput:
                case UGLIR::BuiltinSemanticKind::PixelLocalInput:
                case UGLIR::BuiltinSemanticKind::VertexInput:
                    return typeName + " " + parameterName;
                case UGLIR::BuiltinSemanticKind::None:
                    return typeName + " " + parameterName;
                default:
                    addDiagnostic(parameter.sourceLocation,
                                  "unsupported entry semantic \"" + UGLIR::semanticDisplayName(parameter.semanticKind, parameter.semanticIndex) + "\".");
                    return typeName + " " + parameterName;
                }
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

            /** Emits a single structured UGLIR statement as HLSL source. */
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
                    addDiagnostic(statement.sourceLocation, "unsupported UGLIR statement kind in Phase 5 HLSL emission.");
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
                    result = std::string(isWorkgroupArray ? "groupshared " : "") + emitTypeName(valueTypeName, statement.sourceLocation) + " "
                           + sanitizeHLSLIdentifier(statement.name) + "[" + std::to_string(type->arrayCount) + "]";
                }
                else if (isWorkgroupTypeName(statement.type))
                {
                    result = "groupshared " + emitTypeName(workgroupElementTypeName(statement.type, statement.sourceLocation), statement.sourceLocation) + " "
                           + sanitizeHLSLIdentifier(statement.name);
                }
                else
                {
                    result = emitTypeName(statement.type, statement.sourceLocation) + " " + sanitizeHLSLIdentifier(statement.name);
                }
                if (!statement.expressions.empty())
                {
                    result += " = " + emitExpression(statement.expressions.front());
                }
                return result;
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
                            return sanitizeHLSLIdentifier(parameter.name);
                        }
                    }
                }
                addDiagnostic(location, "`this` expression is only valid inside lowered non-static method helpers.");
                return {};
            }

            /** Emits one UGLIR expression as an HLSL expression fragment. */
            std::string emitExpression(const UGLIR::Expression &expression)
            {
                switch (expression.kind)
                {
                case UGLIR::ExpressionKind::Literal:
                    return emitLiteralExpression(expression);
                case UGLIR::ExpressionKind::ThisRef:
                    return emitThisRefExpression(expression.sourceLocation);
                case UGLIR::ExpressionKind::DeclRef:
                    return sanitizeHLSLIdentifier(expression.name);
                case UGLIR::ExpressionKind::MemberRef:
                    return emitMemberRefExpression(expression);
                case UGLIR::ExpressionKind::Subscript:
                    return emitSubscriptExpression(expression);
                case UGLIR::ExpressionKind::Call:
                    return emitCallExpression(expression);
                case UGLIR::ExpressionKind::Construct:
                    return emitConstructExpression(expression);
                case UGLIR::ExpressionKind::Cast:
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
                addDiagnostic(expression.sourceLocation, "unsupported UGLIR expression kind in Phase 5 HLSL emission.");
                return {};
            }

            /** Emits a typed literal with HLSL suffixes where UGLIR carries enough information. */
            std::string emitLiteralExpression(const UGLIR::Expression &expression)
            {
                if (expression.type == "bool")
                {
                    return expression.value == "0" ? "false" : expression.value == "1" ? "true" : expression.value;
                }
                if (isUnsignedIntegerTypeName(expression.type))
                {
                    if (!expression.value.empty() && expression.value.back() != 'u' && expression.value.back() != 'U')
                    {
                        return expression.value + "u";
                    }
                }
                if (expression.type == "f32" || expression.type == "float")
                {
                    if (expression.value.find('.') == std::string::npos && expression.value.find('e') == std::string::npos && expression.value.find('E') == std::string::npos)
                    {
                        return expression.value + ".0f";
                    }
                }
                return expression.value;
            }

            /** Emits a member reference, collapsing reflected resource member paths into resource names. */
            std::string emitMemberRefExpression(const UGLIR::Expression &expression)
            {
                if (const HLSLResourceDeclaration *resource = findResourceDeclarationForExpression(expression))
                {
                    return resource->hlslName;
                }
                if (expression.operands.empty())
                {
                    return sanitizeHLSLIdentifier(expression.name);
                }

                const std::string base = emitExpression(expression.operands.front());
                if (expression.name.empty())
                {
                    return base;
                }
                return base + "." + sanitizeHLSLIdentifier(expression.name);
            }

            /** Emits an array or resource subscript expression. */
            std::string emitSubscriptExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2)
                {
                    addDiagnostic(expression.sourceLocation, "subscript expression is missing its base or index operand.");
                    return {};
                }
                return emitExpression(expression.operands[0]) + "[" + emitExpression(expression.operands[1]) + "]";
            }

            /** Returns the texture DSL operation represented by a structured UGLIR intrinsic. */
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

            /** Emits a direct helper call expression. */
            std::string emitCallExpression(const UGLIR::Expression &expression)
            {
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::DiscardFragment)
                {
                    return "discard";
                }
                if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::Clip)
                {
                    if (expression.operands.empty())
                    {
                        addDiagnostic(expression.sourceLocation, "clip intrinsic requires one operand.");
                        return "clip(0.0)";
                    }
                    return "clip(" + emitExpression(expression.operands.front()) + ")";
                }

                if (!expression.operands.empty())
                {
                    const HLSLResourceDeclaration *resource = findResourceDeclarationForExpression(expression.operands.front());
                    if (resource != nullptr)
                    {
                        const UGLC::CodeGen::TextureMemberCallKind methodKind = textureMemberCallKindForExpression(expression);
                        if (methodKind != UGLC::CodeGen::TextureMemberCallKind::Unknown)
                        {
                            return emitTextureMemberCallExpression(expression, *resource, methodKind);
                        }
                    }
                    if (expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::UniformBufferRead ||
                        expression.intrinsicCallKind == UGLIR::IntrinsicCallKind::InputAttachmentRead)
                    {
                        return emitExpression(expression.operands.front());
                    }
                }

                std::string result = sanitizeHLSLIdentifier(expression.name) + "(";
                std::vector<std::string> arguments;
                for (const UGLIR::Expression &operand : expression.operands)
                {
                    if (isResourceAliasType(operand.type))
                    {
                        continue;
                    }
                    arguments.push_back(emitExpression(operand));
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

            /** Emits an HLSL texture or storage-texture method call from a UGLIR intrinsic-shaped call. */
            std::string emitTextureMemberCallExpression(const UGLIR::Expression &expression,
                                                        const HLSLResourceDeclaration &resource,
                                                        UGLC::CodeGen::TextureMemberCallKind methodKind)
            {
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Sample)
                {
                    return resource.hlslName + ".Sample(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleLevel)
                {
                    return resource.hlslName + ".SampleLevel(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::SampleGrad)
                {
                    return resource.hlslName + ".SampleGrad(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ", " + emitCallOperand(expression, 3) + ", " + emitCallOperand(expression, 4) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Read)
                {
                    return resource.hlslName + ".Load(int3(" + emitCallOperand(expression, 1) + ", 0))";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Write)
                {
                    return resource.hlslName + "[" + emitCallOperand(expression, 1) + "] = " + emitCallOperand(expression, 2);
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::Gather ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherRed ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherGreen ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherBlue ||
                    methodKind == UGLC::CodeGen::TextureMemberCallKind::GatherAlpha)
                {
                    return resource.hlslName + "." + std::string(UGLC::CodeGen::getHLSLGatherIntrinsic(methodKind)) + "(" + emitCallOperand(expression, 1) + ", " + emitCallOperand(expression, 2) + ")";
                }
                if (methodKind == UGLC::CodeGen::TextureMemberCallKind::GetDimensions)
                {
                    return "uint2(0u, 0u)";
                }
                addDiagnostic(expression.sourceLocation, "unsupported texture member call \"" + expression.name + "\" for Phase 8 HLSL emission.");
                return resource.hlslName;
            }

            /** Emits one operand from a call expression or returns an empty string when it is absent. */
            std::string emitCallOperand(const UGLIR::Expression &expression, size_t index)
            {
                return index < expression.operands.size() ? emitExpression(expression.operands[index]) : std::string();
            }

            /** Emits a constructor-style expression for supported scalar and vector types. */
            std::string emitConstructExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() == 1 && expression.operands.front().type == expression.type)
                {
                    return emitExpression(expression.operands.front());
                }
                std::string result = emitTypeName(expression.type, expression.sourceLocation) + "(";
                for (size_t index = 0; index < expression.operands.size(); ++index)
                {
                    result += emitExpression(expression.operands[index]);
                    if (index + 1 < expression.operands.size())
                    {
                        result += ", ";
                    }
                }
                result += ")";
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
                return "(" + emitExpression(expression.operands[0]) + " " + expression.operatorName + " " + emitExpression(expression.operands[1]) + ")";
            }

            /** Emits a unary expression. */
            std::string emitUnaryExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.empty())
                {
                    addDiagnostic(expression.sourceLocation, "unary expression is missing its operand.");
                    return {};
                }
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

            /** Emits a store expression as an assignment fragment. */
            std::string emitStoreExpression(const UGLIR::Expression &expression)
            {
                if (expression.operands.size() < 2)
                {
                    addDiagnostic(expression.sourceLocation, "store expression is missing its destination or value operand.");
                    return {};
                }
                const std::string op = expression.operatorName.empty() ? "=" : expression.operatorName;
                return emitExpression(expression.operands[0]) + " " + op + " " + emitExpression(expression.operands[1]);
            }

            /** Returns the resource declaration reached by a member-reference expression when it names a reflected resource. */
            const HLSLResourceDeclaration *findResourceDeclarationForExpression(const UGLIR::Expression &expression) const
            {
                std::vector<std::string> memberPath;
                if (!collectResourceMemberPath(expression, memberPath))
                {
                    return nullptr;
                }
                const std::string resourceName = joinResourcePath(memberPath);
                const auto iter = mResourcesByUGLIRName.find(resourceName);
                if (iter == mResourcesByUGLIRName.end())
                {
                    return nullptr;
                }
                if (iter->second.kind == UGLIR::ResourceKind::InputAttachment)
                {
                    return nullptr;
                }
                return &iter->second;
            }

            /** Collects the member path of a `this.bindGroup.resource` UGLIR expression. */
            bool collectResourceMemberPath(const UGLIR::Expression &expression, std::vector<std::string> &memberPath) const
            {
                if (expression.kind == UGLIR::ExpressionKind::ThisRef)
                {
                    return true;
                }
                if (expression.kind == UGLIR::ExpressionKind::DeclRef)
                {
                    const std::string prefix = expression.name + ".";
                    for (const UGLIR::ResourceBinding &resource : mModule.reflection.resources)
                    {
                        if (resource.name.rfind(prefix, 0) == 0)
                        {
                            memberPath.push_back(expression.name);
                            return true;
                        }
                    }
                    return false;
                }
                if (expression.kind == UGLIR::ExpressionKind::Cast ||
                    expression.kind == UGLIR::ExpressionKind::Load ||
                    expression.kind == UGLIR::ExpressionKind::Construct)
                {
                    return !expression.operands.empty() && collectResourceMemberPath(expression.operands.front(), memberPath);
                }
                if (expression.kind != UGLIR::ExpressionKind::MemberRef || expression.operands.empty())
                {
                    return false;
                }
                if (!collectResourceMemberPath(expression.operands.front(), memberPath))
                {
                    return false;
                }
                if (!expression.name.empty())
                {
                    memberPath.push_back(expression.name);
                }
                return true;
            }
        };
    } // namespace

    UGLIRToHLSLEmissionResult emitModuleAsHLSL(const UGLIR::Module &module)
    {
        UGLIRToHLSLEmitter emitter(module);
        return emitter.run();
    }

    std::string formatUGLIRToHLSLEmissionDiagnostics(const UGLIRToHLSLEmissionResult &result)
    {
        std::ostringstream stream;
        for (const UGLIRToHLSLEmissionDiagnostic &diagnostic : result.diagnostics)
        {
            stream << formatDiagnostic(diagnostic) << '\n';
        }
        return stream.str();
    }
} // namespace UGLC::CodeGen::HLSLEmitter
