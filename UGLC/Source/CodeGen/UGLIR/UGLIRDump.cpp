#include <CodeGen/UGLIR/UGLIRDump.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>

#include <nlohmann/json.hpp>

#include <sstream>
#include <utility>

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        using OrderedJson = nlohmann::ordered_json;

        /** Returns the stable dump spelling for a shader stage. */
        const char *toString(ShaderStage stage)
        {
            switch (stage)
            {
            case ShaderStage::None:
                return "none";
            case ShaderStage::Vertex:
                return "vertex";
            case ShaderStage::Fragment:
                return "fragment";
            case ShaderStage::Compute:
                return "compute";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a shader entry role. */
        const char *toString(ShaderEntryKind entryKind)
        {
            switch (entryKind)
            {
            case ShaderEntryKind::None:
                return "none";
            case ShaderEntryKind::Compute:
                return "compute";
            case ShaderEntryKind::Vertex:
                return "vertex";
            case ShaderEntryKind::Fragment:
                return "fragment";
            case ShaderEntryKind::PixelLocal:
                return "pixel_local";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a type kind. */
        const char *toString(TypeKind kind)
        {
            switch (kind)
            {
            case TypeKind::Void:
                return "void";
            case TypeKind::Bool:
                return "bool";
            case TypeKind::Int:
                return "int";
            case TypeKind::UInt:
                return "uint";
            case TypeKind::Float:
                return "float";
            case TypeKind::Half:
                return "half";
            case TypeKind::Vector:
                return "vector";
            case TypeKind::Matrix:
                return "matrix";
            case TypeKind::Array:
                return "array";
            case TypeKind::Struct:
                return "struct";
            case TypeKind::Resource:
                return "resource";
            case TypeKind::Sampler:
                return "sampler";
            case TypeKind::Texture:
                return "texture";
            case TypeKind::Buffer:
                return "buffer";
            case TypeKind::Workgroup:
                return "workgroup";
            case TypeKind::ReferenceAlias:
                return "reference_alias";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a compiler-facing type role. */
        const char *toString(TypeRole role)
        {
            switch (role)
            {
            case TypeRole::Value:
                return "value";
            case TypeRole::ImplementationOnly:
                return "implementation_only";
            case TypeRole::SwizzleProxy:
                return "swizzle_proxy";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for an expression kind. */
        const char *toString(ExpressionKind kind)
        {
            switch (kind)
            {
            case ExpressionKind::Literal:
                return "literal";
            case ExpressionKind::DeclRef:
                return "decl_ref";
            case ExpressionKind::ThisRef:
                return "this_ref";
            case ExpressionKind::MemberRef:
                return "member_ref";
            case ExpressionKind::Subscript:
                return "subscript";
            case ExpressionKind::Call:
                return "call";
            case ExpressionKind::Construct:
                return "construct";
            case ExpressionKind::Cast:
                return "cast";
            case ExpressionKind::Binary:
                return "binary";
            case ExpressionKind::Unary:
                return "unary";
            case ExpressionKind::Conditional:
                return "conditional";
            case ExpressionKind::Load:
                return "load";
            case ExpressionKind::Store:
                return "store";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a structured intrinsic kind. */
        const char *toString(IntrinsicCallKind kind)
        {
            switch (kind)
            {
            case IntrinsicCallKind::None:
                return "none";
            case IntrinsicCallKind::DiscardFragment:
                return "discard_fragment";
            case IntrinsicCallKind::Clip:
                return "clip";
            case IntrinsicCallKind::GroupMemoryBarrier:
                return "group_memory_barrier";
            case IntrinsicCallKind::GroupMemoryBarrierWithGroupSync:
                return "group_memory_barrier_with_group_sync";
            case IntrinsicCallKind::DeviceMemoryBarrier:
                return "device_memory_barrier";
            case IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync:
                return "device_memory_barrier_with_group_sync";
            case IntrinsicCallKind::AllMemoryBarrier:
                return "all_memory_barrier";
            case IntrinsicCallKind::AllMemoryBarrierWithGroupSync:
                return "all_memory_barrier_with_group_sync";
            case IntrinsicCallKind::AtomicAdd:
                return "atomic_add";
            case IntrinsicCallKind::AtomicAnd:
                return "atomic_and";
            case IntrinsicCallKind::AtomicOr:
                return "atomic_or";
            case IntrinsicCallKind::AtomicMin:
                return "atomic_min";
            case IntrinsicCallKind::AtomicMax:
                return "atomic_max";
            case IntrinsicCallKind::AtomicLoad:
                return "atomic_load";
            case IntrinsicCallKind::AtomicStore:
                return "atomic_store";
            case IntrinsicCallKind::AtomicCompareExchange:
                return "atomic_compare_exchange";
            case IntrinsicCallKind::WaveGetLaneIndex:
                return "wave_get_lane_index";
            case IntrinsicCallKind::WaveGetLaneCount:
                return "wave_get_lane_count";
            case IntrinsicCallKind::WaveActiveBallot:
                return "wave_active_ballot";
            case IntrinsicCallKind::WaveActiveCountBits:
                return "wave_active_count_bits";
            case IntrinsicCallKind::WavePrefixCountBits:
                return "wave_prefix_count_bits";
            case IntrinsicCallKind::WavePrefixSum:
                return "wave_prefix_sum";
            case IntrinsicCallKind::WaveReadLaneAt:
                return "wave_read_lane_at";
            case IntrinsicCallKind::WaveReadLaneFirst:
                return "wave_read_lane_first";
            case IntrinsicCallKind::WaveMatch:
                return "wave_match";
            case IntrinsicCallKind::QuadReadLaneAt:
                return "quad_read_lane_at";
            case IntrinsicCallKind::QuadReadAcrossX:
                return "quad_read_across_x";
            case IntrinsicCallKind::QuadReadAcrossY:
                return "quad_read_across_y";
            case IntrinsicCallKind::QuadReadAcrossDiagonal:
                return "quad_read_across_diagonal";
            case IntrinsicCallKind::BitcastAsFloat:
                return "bitcast_asfloat";
            case IntrinsicCallKind::BitcastAsUInt:
                return "bitcast_asuint";
            case IntrinsicCallKind::BitcastAsInt:
                return "bitcast_asint";
            case IntrinsicCallKind::MathAbs:
                return "math_abs";
            case IntrinsicCallKind::MathAcos:
                return "math_acos";
            case IntrinsicCallKind::MathAll:
                return "math_all";
            case IntrinsicCallKind::MathAny:
                return "math_any";
            case IntrinsicCallKind::MathAsin:
                return "math_asin";
            case IntrinsicCallKind::MathAtan:
                return "math_atan";
            case IntrinsicCallKind::MathAtan2:
                return "math_atan2";
            case IntrinsicCallKind::MathCeil:
                return "math_ceil";
            case IntrinsicCallKind::MathClamp:
                return "math_clamp";
            case IntrinsicCallKind::MathCos:
                return "math_cos";
            case IntrinsicCallKind::MathCross:
                return "math_cross";
            case IntrinsicCallKind::MathDdx:
                return "math_ddx";
            case IntrinsicCallKind::MathDdy:
                return "math_ddy";
            case IntrinsicCallKind::MathDistance:
                return "math_distance";
            case IntrinsicCallKind::MathDot:
                return "math_dot";
            case IntrinsicCallKind::MathExp:
                return "math_exp";
            case IntrinsicCallKind::MathExp2:
                return "math_exp2";
            case IntrinsicCallKind::MathFloor:
                return "math_floor";
            case IntrinsicCallKind::MathFrac:
                return "math_frac";
            case IntrinsicCallKind::MathFmod:
                return "math_fmod";
            case IntrinsicCallKind::MathFirstBitHigh:
                return "math_firstbit_high";
            case IntrinsicCallKind::MathFirstBitLow:
                return "math_firstbit_low";
            case IntrinsicCallKind::MathLength:
                return "math_length";
            case IntrinsicCallKind::MathLerp:
                return "math_lerp";
            case IntrinsicCallKind::MathLog:
                return "math_log";
            case IntrinsicCallKind::MathLog2:
                return "math_log2";
            case IntrinsicCallKind::MathMax:
                return "math_max";
            case IntrinsicCallKind::MathMin:
                return "math_min";
            case IntrinsicCallKind::MathModf:
                return "math_modf";
            case IntrinsicCallKind::MathMul:
                return "math_mul";
            case IntrinsicCallKind::MathNormalize:
                return "math_normalize";
            case IntrinsicCallKind::MathPow:
                return "math_pow";
            case IntrinsicCallKind::MathReflect:
                return "math_reflect";
            case IntrinsicCallKind::MathRound:
                return "math_round";
            case IntrinsicCallKind::MathRsqrt:
                return "math_rsqrt";
            case IntrinsicCallKind::MathSaturate:
                return "math_saturate";
            case IntrinsicCallKind::MathSign:
                return "math_sign";
            case IntrinsicCallKind::MathSin:
                return "math_sin";
            case IntrinsicCallKind::MathSincos:
                return "math_sincos";
            case IntrinsicCallKind::MathSqrt:
                return "math_sqrt";
            case IntrinsicCallKind::MathStep:
                return "math_step";
            case IntrinsicCallKind::MathSmoothstep:
                return "math_smoothstep";
            case IntrinsicCallKind::MathTan:
                return "math_tan";
            case IntrinsicCallKind::MathTranspose:
                return "math_transpose";
            case IntrinsicCallKind::UniformBufferRead:
                return "uniform_buffer_read";
            case IntrinsicCallKind::InputAttachmentRead:
                return "input_attachment_read";
            case IntrinsicCallKind::TextureRead:
                return "texture_read";
            case IntrinsicCallKind::TextureWrite:
                return "texture_write";
            case IntrinsicCallKind::TextureSample:
                return "texture_sample";
            case IntrinsicCallKind::TextureSampleLevel:
                return "texture_sample_level";
            case IntrinsicCallKind::TextureSampleGrad:
                return "texture_sample_grad";
            case IntrinsicCallKind::TextureGather:
                return "texture_gather";
            case IntrinsicCallKind::TextureGatherRed:
                return "texture_gather_red";
            case IntrinsicCallKind::TextureGatherGreen:
                return "texture_gather_green";
            case IntrinsicCallKind::TextureGatherBlue:
                return "texture_gather_blue";
            case IntrinsicCallKind::TextureGatherAlpha:
                return "texture_gather_alpha";
            case IntrinsicCallKind::TextureGetDimensions:
                return "texture_get_dimensions";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a construct expression plan. */
        const char *toString(ConstructKind kind)
        {
            switch (kind)
            {
            case ConstructKind::None:
                return "none";
            case ConstructKind::ScalarConvert:
                return "scalar_convert";
            case ConstructKind::VectorSplat:
                return "vector_splat";
            case ConstructKind::VectorFromComponents:
                return "vector_from_components";
            case ConstructKind::Aggregate:
                return "aggregate";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a statement kind. */
        const char *toString(StatementKind kind)
        {
            switch (kind)
            {
            case StatementKind::Block:
                return "block";
            case StatementKind::VariableDeclaration:
                return "var";
            case StatementKind::Return:
                return "return";
            case StatementKind::If:
                return "if";
            case StatementKind::Switch:
                return "switch";
            case StatementKind::For:
                return "for";
            case StatementKind::While:
                return "while";
            case StatementKind::Do:
                return "do";
            case StatementKind::Break:
                return "break";
            case StatementKind::Continue:
                return "continue";
            case StatementKind::Expression:
                return "expr";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for function linkage. */
        const char *toString(FunctionLinkage linkage)
        {
            switch (linkage)
            {
            case FunctionLinkage::Internal:
                return "internal";
            case FunctionLinkage::Exported:
                return "exported";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for a resource kind. */
        const char *toString(ResourceKind kind)
        {
            switch (kind)
            {
            case ResourceKind::Unknown:
                return "unknown";
            case ResourceKind::UniformBuffer:
                return "uniform_buffer";
            case ResourceKind::StorageBuffer:
                return "storage_buffer";
            case ResourceKind::Texture:
                return "texture";
            case ResourceKind::StorageTexture:
                return "storage_texture";
            case ResourceKind::Sampler:
                return "sampler";
            case ResourceKind::InputAttachment:
                return "input_attachment";
            case ResourceKind::BindGroup:
                return "bind_group";
            }
            return "unknown";
        }

        /** Returns the stable dump spelling for an access mode. */
        const char *toString(AccessMode mode)
        {
            switch (mode)
            {
            case AccessMode::Read:
                return "read";
            case AccessMode::Write:
                return "write";
            case AccessMode::ReadWrite:
                return "read_write";
            }
            return "unknown";
        }

        /** Appends one indentation level using two spaces per level. */
        void appendIndent(std::ostringstream &stream, int indentLevel)
        {
            for (int i = 0; i < indentLevel; ++i)
            {
                stream << "  ";
            }
        }

        /** Appends one escaped quoted string to the text dump. */
        void appendQuoted(std::ostringstream &stream, const std::string &value)
        {
            stream << '"';
            for (char ch : value)
            {
                switch (ch)
                {
                case '\\':
                    stream << "\\\\";
                    break;
                case '"':
                    stream << "\\\"";
                    break;
                case '\n':
                    stream << "\\n";
                    break;
                case '\r':
                    stream << "\\r";
                    break;
                case '\t':
                    stream << "\\t";
                    break;
                default:
                    stream << ch;
                    break;
                }
            }
            stream << '"';
        }

        /** Appends a source location using the stable text dump spelling. */
        void appendSourceLocationText(std::ostringstream &stream, const SourceLocation &location)
        {
            stream << " source ";
            appendQuoted(stream, location.file);
            stream << ':' << location.line << ':' << location.column;
        }

        /** Appends a shader stage visibility list using the stable text dump spelling. */
        void appendStageListText(std::ostringstream &stream, const std::vector<ShaderStage> &stages)
        {
            stream << " stages";
            if (stages.empty())
            {
                stream << " none";
                return;
            }

            for (ShaderStage stage : stages)
            {
                stream << ' ' << toString(stage);
            }
        }

        /** Appends one expression subtree to the text dump. */
        void appendExpressionText(std::ostringstream &stream, const Expression &expression, int indentLevel)
        {
            appendIndent(stream, indentLevel);
            stream << "expr " << toString(expression.kind) << " type ";
            appendQuoted(stream, expression.type);
            stream << " intrinsic " << toString(expression.intrinsicCallKind);
            stream << " construct_kind " << toString(expression.constructInfo.kind);
            if (expression.constructInfo.targetScalarKind != ScalarKind::None)
            {
                stream << " construct_scalar_kind ";
                appendQuoted(stream, toString(expression.constructInfo.targetScalarKind));
            }
            if (expression.constructInfo.targetComponentCount != 0)
            {
                stream << " construct_components " << expression.constructInfo.targetComponentCount;
            }
            if (!expression.name.empty())
            {
                stream << " name ";
                appendQuoted(stream, expression.name);
            }
            if (!expression.value.empty())
            {
                stream << " value ";
                appendQuoted(stream, expression.value);
            }
            if (!expression.operatorName.empty())
            {
                stream << " op ";
                appendQuoted(stream, expression.operatorName);
                if (expression.isPostfix)
                {
                    stream << " postfix";
                }
            }
            if (!expression.computationType.empty())
            {
                stream << " computation-type ";
                appendQuoted(stream, expression.computationType);
            }
            appendSourceLocationText(stream, expression.sourceLocation);
            stream << '\n';

            for (const ConstructComponent &component : expression.constructInfo.components)
            {
                appendIndent(stream, indentLevel + 1);
                stream << "construct_component operand " << component.operandIndex
                       << " source_component " << component.sourceComponentIndex
                       << " source_scalar_kind ";
                appendQuoted(stream, toString(component.sourceScalarKind));
                stream << " target_scalar_kind ";
                appendQuoted(stream, toString(component.targetScalarKind));
                stream << " source_is_vector " << (component.sourceIsVector ? "true" : "false") << '\n';
            }

            for (const Expression &operand : expression.operands)
            {
                appendExpressionText(stream, operand, indentLevel + 1);
            }
        }

        /** Appends one statement subtree to the text dump. */
        void appendStatementText(std::ostringstream &stream, const Statement &statement, int indentLevel)
        {
            appendIndent(stream, indentLevel);
            stream << "stmt " << toString(statement.kind);
            if (!statement.name.empty())
            {
                stream << " name ";
                appendQuoted(stream, statement.name);
            }
            if (!statement.type.empty())
            {
                stream << " type ";
                appendQuoted(stream, statement.type);
            }
            appendSourceLocationText(stream, statement.sourceLocation);
            stream << '\n';

            for (const Expression &expression : statement.expressions)
            {
                appendExpressionText(stream, expression, indentLevel + 1);
            }
            for (const Statement &child : statement.children)
            {
                appendStatementText(stream, child, indentLevel + 1);
            }
            if (!statement.elseChildren.empty())
            {
                appendIndent(stream, indentLevel + 1);
                stream << "else\n";
                for (const Statement &child : statement.elseChildren)
                {
                    appendStatementText(stream, child, indentLevel + 2);
                }
            }
            for (const SwitchCase &switchCase : statement.switchCases)
            {
                appendIndent(stream, indentLevel + 1);
                stream << (switchCase.isDefault ? "default" : "case");
                appendSourceLocationText(stream, switchCase.sourceLocation);
                stream << '\n';
                for (const Expression &label : switchCase.labels)
                {
                    appendExpressionText(stream, label, indentLevel + 2);
                }
                for (const Statement &child : switchCase.body)
                {
                    appendStatementText(stream, child, indentLevel + 2);
                }
            }
        }

        /** Appends one type to the text dump. */
        void appendTypeText(std::ostringstream &stream, const Type &type)
        {
            appendIndent(stream, 2);
            stream << "type ";
            appendQuoted(stream, type.name);
            stream << " kind " << toString(type.kind);
            stream << " scalar_kind " << toString(type.scalarKind);
            if (type.bitWidth != 0)
            {
                stream << " bit_width " << type.bitWidth;
            }
            if (!type.elementType.empty())
            {
                stream << " element ";
                appendQuoted(stream, type.elementType);
            }
            if (type.vectorWidth != 0)
            {
                stream << " vector_width " << type.vectorWidth;
            }
            if (type.matrixColumns != 0 || type.matrixRows != 0)
            {
                stream << " matrix " << type.matrixColumns << 'x' << type.matrixRows;
            }
            if (type.arrayCount != 0)
            {
                stream << " array_count " << type.arrayCount;
            }
            if (type.resourceKind != ResourceKind::Unknown)
            {
                stream << " resource_kind " << toString(type.resourceKind);
            }
            stream << " access " << toString(type.accessMode);
            if (type.textureDimension != TextureDimension::None)
            {
                stream << " texture_dimension ";
                appendQuoted(stream, toString(type.textureDimension));
            }
            if (type.textureFormat != TextureFormat::Unknown)
            {
                stream << " texture_format ";
                appendQuoted(stream, toString(type.textureFormat));
            }
            if (type.isMultisampled)
            {
                stream << " multisampled true";
            }
            if (type.role != TypeRole::Value)
            {
                stream << " role " << toString(type.role);
            }
            if (type.isImplementationOnly)
            {
                stream << " implementation_only true";
            }
            appendSourceLocationText(stream, type.sourceLocation);
            stream << '\n';

            for (const TypeField &field : type.fields)
            {
                appendIndent(stream, 3);
                stream << "field ";
                appendQuoted(stream, field.name);
                stream << " type ";
                appendQuoted(stream, field.type);
                stream << " semantic ";
                appendQuoted(stream, semanticDisplayName(field.semanticKind, field.semanticIndex));
                stream << " location " << field.location;
                stream << " offset " << field.offset;
                appendSourceLocationText(stream, field.sourceLocation);
                stream << '\n';
            }
        }

        /** Appends one function to the text dump. */
        void appendFunctionText(std::ostringstream &stream, const Function &function)
        {
            appendIndent(stream, 2);
            stream << "function ";
            appendQuoted(stream, function.name);
            stream << " linkage " << toString(function.linkage)
                   << " stage " << toString(function.stage)
                   << " entry_kind " << toString(function.entryKind)
                   << " entry " << (function.isEntryPoint ? "true" : "false")
                   << " return ";
            appendQuoted(stream, function.returnType);
            appendSourceLocationText(stream, function.sourceLocation);
            stream << '\n';

            appendIndent(stream, 3);
            stream << "workgroup_size "
                   << function.workgroupSize[0] << ' '
                   << function.workgroupSize[1] << ' '
                   << function.workgroupSize[2] << '\n';

            appendIndent(stream, 3);
            stream << "params\n";
            for (const FunctionParameter &parameter : function.parameters)
            {
                appendIndent(stream, 4);
                stream << "param ";
                appendQuoted(stream, parameter.name);
                stream << " type ";
                appendQuoted(stream, parameter.type);
                stream << " semantic ";
                appendQuoted(stream, semanticDisplayName(parameter.semanticKind, parameter.semanticIndex));
                stream << " passing_mode " << toString(parameter.passingMode);
                stream << " reference " << (parameter.isReference ? "true" : "false");
                stream << " const_reference " << (parameter.isConstReference ? "true" : "false");
                appendSourceLocationText(stream, parameter.sourceLocation);
                stream << '\n';
            }

            appendIndent(stream, 3);
            stream << "body\n";
            for (const Statement &statement : function.body)
            {
                appendStatementText(stream, statement, 4);
            }
        }

        /** Appends one reflected resource binding to the text dump. */
        void appendResourceBindingText(std::ostringstream &stream, const ResourceBinding &resource)
        {
            appendIndent(stream, 3);
            stream << "resource ";
            appendQuoted(stream, resource.name);
            stream << " kind " << toString(resource.kind)
                   << " set " << resource.bindGroupIndex
                   << " binding " << resource.bindingIndex
                   << " access " << toString(resource.accessMode)
                   << " element ";
            appendQuoted(stream, resource.elementType);
            stream << " array_count " << resource.arrayCount;
            stream << " texture_dimension ";
            appendQuoted(stream, toString(resource.textureDimension));
            stream << " texture_format ";
            appendQuoted(stream, toString(resource.textureFormat));
            stream << " multisampled " << (resource.isMultisampled ? "true" : "false");
            stream << " input_attachment " << resource.inputAttachmentIndex;
            stream << " resource_role ";
            appendQuoted(stream, toString(resource.resourceRole));
            stream << " resource_index " << resource.resourceIndex;
            appendStageListText(stream, resource.visibleStages);
            appendSourceLocationText(stream, resource.sourceLocation);
            stream << '\n';
        }

        /** Appends one stage IO binding to the text dump. */
        void appendStageIOBindingText(std::ostringstream &stream, const StageIOBinding &binding, int indentLevel)
        {
            appendIndent(stream, indentLevel);
            stream << "io ";
            appendQuoted(stream, binding.name);
            stream << " type ";
            appendQuoted(stream, binding.type);
            stream << " semantic ";
            appendQuoted(stream, semanticDisplayName(binding.semanticKind, binding.semanticIndex));
            stream << " location " << binding.location;
            stream << " index " << binding.index;
            appendSourceLocationText(stream, binding.sourceLocation);
            stream << '\n';
        }

        /** Converts one source location into the stable JSON dump shape. */
        OrderedJson makeSourceLocationJson(const SourceLocation &location)
        {
            OrderedJson result;
            result["file"] = location.file;
            result["line"] = location.line;
            result["column"] = location.column;
            return result;
        }

        /** Converts one type field into the stable JSON dump shape. */
        OrderedJson makeTypeFieldJson(const TypeField &field)
        {
            OrderedJson result;
            result["name"] = field.name;
            result["type"] = field.type;
            result["semantic"] = semanticDisplayName(field.semanticKind, field.semanticIndex);
            result["semanticKind"] = toString(field.semanticKind);
            result["semanticIndex"] = field.semanticIndex;
            result["location"] = field.location;
            result["offset"] = field.offset;
            result["sourceLocation"] = makeSourceLocationJson(field.sourceLocation);
            return result;
        }

        /** Converts one type into the stable JSON dump shape. */
        OrderedJson makeTypeJson(const Type &type)
        {
            OrderedJson result;
            result["name"] = type.name;
            result["kind"] = toString(type.kind);
            result["scalarKind"] = toString(type.scalarKind);
            result["bitWidth"] = type.bitWidth;
            result["elementType"] = type.elementType;
            result["vectorWidth"] = type.vectorWidth;
            result["matrixColumns"] = type.matrixColumns;
            result["matrixRows"] = type.matrixRows;
            result["arrayCount"] = type.arrayCount;
            result["resourceKind"] = toString(type.resourceKind);
            result["accessMode"] = toString(type.accessMode);
            result["textureDimension"] = toString(type.textureDimension);
            result["textureFormat"] = toString(type.textureFormat);
            result["isMultisampled"] = type.isMultisampled;
            result["role"] = toString(type.role);
            result["isImplementationOnly"] = type.isImplementationOnly;
            OrderedJson fields = OrderedJson::array();
            for (const TypeField &field : type.fields)
            {
                fields.push_back(makeTypeFieldJson(field));
            }
            result["fields"] = std::move(fields);
            result["sourceLocation"] = makeSourceLocationJson(type.sourceLocation);
            return result;
        }

        /** Converts one expression subtree into the stable JSON dump shape. */
        OrderedJson makeExpressionJson(const Expression &expression)
        {
            OrderedJson result;
            result["kind"] = toString(expression.kind);
            result["intrinsicKind"] = toString(expression.intrinsicCallKind);
            result["type"] = expression.type;
            result["name"] = expression.name;
            result["value"] = expression.value;
            result["operatorName"] = expression.operatorName;
            result["computationType"] = expression.computationType;
            result["isPostfix"] = expression.isPostfix;
            result["isEagerLogical"] = expression.isEagerLogical;
            result["constructKind"] = toString(expression.constructInfo.kind);
            result["constructTargetScalarKind"] = toString(expression.constructInfo.targetScalarKind);
            result["constructTargetComponentCount"] = expression.constructInfo.targetComponentCount;
            OrderedJson constructComponents = OrderedJson::array();
            for (const ConstructComponent &component : expression.constructInfo.components)
            {
                OrderedJson componentJson;
                componentJson["operandIndex"] = component.operandIndex;
                componentJson["sourceComponentIndex"] = component.sourceComponentIndex;
                componentJson["sourceScalarKind"] = toString(component.sourceScalarKind);
                componentJson["targetScalarKind"] = toString(component.targetScalarKind);
                componentJson["sourceIsVector"] = component.sourceIsVector;
                constructComponents.push_back(std::move(componentJson));
            }
            result["constructComponents"] = std::move(constructComponents);
            OrderedJson operands = OrderedJson::array();
            for (const Expression &operand : expression.operands)
            {
                operands.push_back(makeExpressionJson(operand));
            }
            result["operands"] = std::move(operands);
            result["sourceLocation"] = makeSourceLocationJson(expression.sourceLocation);
            return result;
        }

        /** Converts one statement subtree into the stable JSON dump shape. */
        OrderedJson makeStatementJson(const Statement &statement)
        {
            OrderedJson result;
            result["kind"] = toString(statement.kind);
            result["name"] = statement.name;
            result["type"] = statement.type;
            OrderedJson expressions = OrderedJson::array();
            for (const Expression &expression : statement.expressions)
            {
                expressions.push_back(makeExpressionJson(expression));
            }
            result["expressions"] = std::move(expressions);

            OrderedJson children = OrderedJson::array();
            for (const Statement &child : statement.children)
            {
                children.push_back(makeStatementJson(child));
            }
            result["children"] = std::move(children);

            OrderedJson elseChildren = OrderedJson::array();
            for (const Statement &child : statement.elseChildren)
            {
                elseChildren.push_back(makeStatementJson(child));
            }
            result["elseChildren"] = std::move(elseChildren);

            OrderedJson switchCases = OrderedJson::array();
            for (const SwitchCase &switchCase : statement.switchCases)
            {
                OrderedJson caseJson;
                caseJson["isDefault"] = switchCase.isDefault;
                OrderedJson labels = OrderedJson::array();
                for (const Expression &label : switchCase.labels)
                {
                    labels.push_back(makeExpressionJson(label));
                }
                caseJson["labels"] = std::move(labels);
                OrderedJson body = OrderedJson::array();
                for (const Statement &child : switchCase.body)
                {
                    body.push_back(makeStatementJson(child));
                }
                caseJson["body"] = std::move(body);
                caseJson["sourceLocation"] = makeSourceLocationJson(switchCase.sourceLocation);
                switchCases.push_back(std::move(caseJson));
            }
            result["switchCases"] = std::move(switchCases);
            result["sourceLocation"] = makeSourceLocationJson(statement.sourceLocation);
            return result;
        }

        /** Converts one function parameter into the stable JSON dump shape. */
        OrderedJson makeFunctionParameterJson(const FunctionParameter &parameter)
        {
            OrderedJson result;
            result["name"] = parameter.name;
            result["type"] = parameter.type;
            result["semantic"] = semanticDisplayName(parameter.semanticKind, parameter.semanticIndex);
            result["semanticKind"] = toString(parameter.semanticKind);
            result["semanticIndex"] = parameter.semanticIndex;
            result["passingMode"] = toString(parameter.passingMode);
            result["isReference"] = parameter.isReference;
            result["isConstReference"] = parameter.isConstReference;
            result["sourceLocation"] = makeSourceLocationJson(parameter.sourceLocation);
            return result;
        }

        /** Converts one workgroup-size array into the stable JSON dump shape. */
        OrderedJson makeWorkgroupSizeJson(const std::array<uint32_t, 3> &workgroupSize)
        {
            OrderedJson result = OrderedJson::array();
            result.push_back(workgroupSize[0]);
            result.push_back(workgroupSize[1]);
            result.push_back(workgroupSize[2]);
            return result;
        }

        /** Converts one function into the stable JSON dump shape. */
        OrderedJson makeFunctionJson(const Function &function)
        {
            OrderedJson result;
            result["name"] = function.name;
            result["returnType"] = function.returnType;
            result["linkage"] = toString(function.linkage);
            result["stage"] = toString(function.stage);
            result["entryKind"] = toString(function.entryKind);
            result["isEntryPoint"] = function.isEntryPoint;
            result["workgroupSize"] = makeWorkgroupSizeJson(function.workgroupSize);

            OrderedJson parameters = OrderedJson::array();
            for (const FunctionParameter &parameter : function.parameters)
            {
                parameters.push_back(makeFunctionParameterJson(parameter));
            }
            result["parameters"] = std::move(parameters);

            OrderedJson body = OrderedJson::array();
            for (const Statement &statement : function.body)
            {
                body.push_back(makeStatementJson(statement));
            }
            result["body"] = std::move(body);
            result["sourceLocation"] = makeSourceLocationJson(function.sourceLocation);
            return result;
        }

        /** Converts one stage IO binding into the stable JSON dump shape. */
        OrderedJson makeStageIOBindingJson(const StageIOBinding &binding)
        {
            OrderedJson result;
            result["name"] = binding.name;
            result["type"] = binding.type;
            result["semantic"] = semanticDisplayName(binding.semanticKind, binding.semanticIndex);
            result["semanticKind"] = toString(binding.semanticKind);
            result["semanticIndex"] = binding.semanticIndex;
            result["location"] = binding.location;
            result["index"] = binding.index;
            result["sourceLocation"] = makeSourceLocationJson(binding.sourceLocation);
            return result;
        }

        /** Converts a shader stage list into the stable JSON dump shape. */
        OrderedJson makeStageListJson(const std::vector<ShaderStage> &stages)
        {
            OrderedJson result = OrderedJson::array();
            for (ShaderStage stage : stages)
            {
                result.push_back(toString(stage));
            }
            return result;
        }

        /** Converts one reflected resource binding into the stable JSON dump shape. */
        OrderedJson makeResourceBindingJson(const ResourceBinding &resource)
        {
            OrderedJson result;
            result["name"] = resource.name;
            result["kind"] = toString(resource.kind);
            result["bindGroupIndex"] = resource.bindGroupIndex;
            result["bindingIndex"] = resource.bindingIndex;
            result["accessMode"] = toString(resource.accessMode);
            result["elementType"] = resource.elementType;
            result["arrayCount"] = resource.arrayCount;
            result["textureDimension"] = toString(resource.textureDimension);
            result["textureFormat"] = toString(resource.textureFormat);
            result["isMultisampled"] = resource.isMultisampled;
            result["inputAttachmentIndex"] = resource.inputAttachmentIndex;
            result["resourceRole"] = toString(resource.resourceRole);
            result["resourceIndex"] = resource.resourceIndex;
            result["visibleStages"] = makeStageListJson(resource.visibleStages);
            result["sourceLocation"] = makeSourceLocationJson(resource.sourceLocation);
            return result;
        }

        /** Converts module reflection into the stable JSON dump shape. */
        OrderedJson makeReflectionJson(const Reflection &reflection)
        {
            OrderedJson result;
            result["shaderClassName"] = reflection.shaderClassName;
            result["metalBindGroupBufferOffset"] = reflection.metalBindGroupBufferOffset;
            OrderedJson groups = OrderedJson::array();
            for (const auto &group : reflection.bindGroups)
            {
                groups.push_back({{"name", group.name}, {"typeName", group.typeName},
                                  {"bindGroupIndex", group.bindGroupIndex}, {"isRenderSet", group.isRenderSet}});
            }
            result["bindGroups"] = std::move(groups);
            result["entryName"] = reflection.entryName;
            result["stage"] = toString(reflection.stage);
            result["entryKind"] = toString(reflection.entryKind);
            result["workgroupSize"] = makeWorkgroupSizeJson(reflection.workgroupSize);

            OrderedJson stageInputs = OrderedJson::array();
            for (const StageIOBinding &binding : reflection.stageInputs)
            {
                stageInputs.push_back(makeStageIOBindingJson(binding));
            }
            result["stageInputs"] = std::move(stageInputs);

            OrderedJson stageOutputs = OrderedJson::array();
            for (const StageIOBinding &binding : reflection.stageOutputs)
            {
                stageOutputs.push_back(makeStageIOBindingJson(binding));
            }
            result["stageOutputs"] = std::move(stageOutputs);

            OrderedJson resources = OrderedJson::array();
            for (const ResourceBinding &resource : reflection.resources)
            {
                resources.push_back(makeResourceBindingJson(resource));
            }
            result["resources"] = std::move(resources);
            result["artifactHash"] = reflection.artifactHash;
            return result;
        }
    } // namespace

    /** Returns a deterministic human-readable dump for a complete UGLIR module. */
    std::string dumpModuleAsText(const Module &module)
    {
        std::ostringstream stream;
        stream << "module ";
        appendQuoted(stream, module.name);
        appendSourceLocationText(stream, module.sourceLocation);
        stream << '\n';

        appendIndent(stream, 1);
        stream << "types\n";
        for (const Type &type : module.types)
        {
            appendTypeText(stream, type);
        }

        appendIndent(stream, 1);
        stream << "functions\n";
        for (const Function &function : module.functions)
        {
            appendFunctionText(stream, function);
        }

        appendIndent(stream, 1);
        stream << "reflection\n";
        appendIndent(stream, 2);
        stream << "entry ";
        appendQuoted(stream, module.reflection.entryName);
        stream << " stage " << toString(module.reflection.stage)
               << " entry_kind " << toString(module.reflection.entryKind) << '\n';
        appendIndent(stream, 2);
        stream << "workgroup_size "
               << module.reflection.workgroupSize[0] << ' '
               << module.reflection.workgroupSize[1] << ' '
               << module.reflection.workgroupSize[2] << '\n';
        appendIndent(stream, 2);
        stream << "artifact_hash ";
        appendQuoted(stream, module.reflection.artifactHash);
        stream << '\n';
        appendIndent(stream, 2);
        stream << "stage_inputs\n";
        for (const StageIOBinding &binding : module.reflection.stageInputs)
        {
            appendStageIOBindingText(stream, binding, 3);
        }
        appendIndent(stream, 2);
        stream << "stage_outputs\n";
        for (const StageIOBinding &binding : module.reflection.stageOutputs)
        {
            appendStageIOBindingText(stream, binding, 3);
        }
        appendIndent(stream, 2);
        stream << "resources\n";
        for (const ResourceBinding &resource : module.reflection.resources)
        {
            appendResourceBindingText(stream, resource);
        }

        return stream.str();
    }

    /** Returns a deterministic JSON dump for a complete UGLIR module. */
    std::string dumpModuleAsJson(const Module &module)
    {
        OrderedJson document;
        document["schemaVersion"] = 1;
        document["name"] = module.name;
        document["sourceLocation"] = makeSourceLocationJson(module.sourceLocation);

        OrderedJson types = OrderedJson::array();
        for (const Type &type : module.types)
        {
            types.push_back(makeTypeJson(type));
        }
        document["types"] = std::move(types);

        OrderedJson functions = OrderedJson::array();
        for (const Function &function : module.functions)
        {
            functions.push_back(makeFunctionJson(function));
        }
        document["functions"] = std::move(functions);
        document["reflection"] = makeReflectionJson(module.reflection);
        return document.dump(2) + "\n";
    }
} // namespace UGLC::CodeGen::UGLIR
