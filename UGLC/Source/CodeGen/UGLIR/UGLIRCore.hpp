#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    /** Describes a shader stage attached to an entry function or reflection record. */
    enum class ShaderStage
    {
        None,
        Vertex,
        Fragment,
        Compute,
    };

    /** Describes the DSL entry role for a shader artifact while keeping backend stage mapping separate. */
    enum class ShaderEntryKind
    {
        None,
        Compute,
        Vertex,
        Fragment,
        PixelLocal,
    };

    /** Describes one shader ABI semantic without relying on public annotation spelling. */
    enum class BuiltinSemanticKind
    {
        None,
        DispatchThreadID,
        GroupThreadID,
        GroupID,
        GroupIndex,
        VertexID,
        InstanceID,
        PrimitiveID,
        PixelCoord,
        SampleIndex,
        Barycentrics,
        StageInput,
        VertexInput,
        PixelLocalInput,
        Position,
        Attribute,
        Color,
        PixelLocalColor,
        Depth,
        PixelLocalDepth,
        Field,
        DrawEntityID,
        DrawEntityInstanceID,
    };

    /** Describes one indexed or non-indexed builtin semantic attached to an IR entity. */
    struct BuiltinSemantic
    {
        BuiltinSemanticKind kind = BuiltinSemanticKind::None;
        uint32_t index = 0;
    };

    /** Describes the shader ABI passing mode for a non-entry helper parameter. */
    enum class ParameterPassingMode
    {
        Value,
        In,
        Out,
        InOut,
    };

    /** Describes the stable category of an UGLIR type node. */
    enum class TypeKind
    {
        Void,
        Bool,
        Int,
        UInt,
        Float,
        Half,
        Vector,
        Matrix,
        Array,
        Struct,
        Resource,
        Sampler,
        Texture,
        Buffer,
        Workgroup,
        ReferenceAlias,
    };

    /** Describes the compiler-facing role of a type when it is not a plain shader value payload. */
    enum class TypeRole
    {
        Value,
        ImplementationOnly,
        SwizzleProxy,
    };

    /** Describes the scalar lane category for scalar, vector, and matrix value types. */
    enum class ScalarKind
    {
        None,
        Bool,
        Int,
        UInt,
        Float,
        Half,
    };

    /** Describes the stable category of an UGLIR expression node. */
    enum class ExpressionKind
    {
        Literal,
        DeclRef,
        ThisRef,
        MemberRef,
        Subscript,
        Call,
        Construct,
        Cast,
        Binary,
        Unary,
        Conditional,
        Load,
        Store,
    };

    /** Describes a structured shader intrinsic carried by a call expression when the DSL operation is not an ordinary helper call. */
    enum class IntrinsicCallKind
    {
        None,
        DiscardFragment,
        Clip,
        GroupMemoryBarrier,
        GroupMemoryBarrierWithGroupSync,
        DeviceMemoryBarrier,
        DeviceMemoryBarrierWithGroupSync,
        AllMemoryBarrier,
        AllMemoryBarrierWithGroupSync,
        AtomicAdd,
        AtomicAnd,
        AtomicOr,
        AtomicMin,
        AtomicMax,
        AtomicLoad,
        AtomicStore,
        AtomicCompareExchange,
        WaveGetLaneIndex,
        WaveGetLaneCount,
        WaveActiveBallot,
        WaveActiveCountBits,
        WavePrefixCountBits,
        WavePrefixSum,
        WaveReadLaneAt,
        WaveReadLaneFirst,
        WaveMatch,
        QuadReadLaneAt,
        QuadReadAcrossX,
        QuadReadAcrossY,
        QuadReadAcrossDiagonal,
        BitcastAsFloat,
        BitcastAsUInt,
        BitcastAsInt,
        MathAbs,
        MathAcos,
        MathAll,
        MathAny,
        MathAsin,
        MathAtan,
        MathAtan2,
        MathCeil,
        MathClamp,
        MathCos,
        MathCross,
        MathDdx,
        MathDdy,
        MathDistance,
        MathDot,
        MathExp,
        MathExp2,
        MathFloor,
        MathFrac,
        MathFmod,
        MathFirstBitHigh,
        MathFirstBitLow,
        MathLength,
        MathLerp,
        MathLog,
        MathLog2,
        MathMax,
        MathMin,
        MathModf,
        MathMul,
        MathNormalize,
        MathPow,
        MathReflect,
        MathRound,
        MathRsqrt,
        MathSaturate,
        MathSign,
        MathSin,
        MathSincos,
        MathSqrt,
        MathStep,
        MathSmoothstep,
        MathTan,
        MathTranspose,
        UniformBufferRead,
        InputAttachmentRead,
        TextureRead,
        TextureWrite,
        TextureSample,
        TextureSampleLevel,
        TextureSampleGrad,
        TextureGather,
        TextureGatherRed,
        TextureGatherGreen,
        TextureGatherBlue,
        TextureGatherAlpha,
        TextureGetDimensions,
    };

    /** Describes how a construct expression should be consumed by backends. */
    enum class ConstructKind
    {
        None,
        ScalarConvert,
        VectorSplat,
        VectorFromComponents,
        Aggregate,
    };

    /** Maps one construct operand or operand component to one target constructor component. */
    struct ConstructComponent
    {
        uint32_t operandIndex = 0;
        uint32_t sourceComponentIndex = 0;
        ScalarKind sourceScalarKind = ScalarKind::None;
        ScalarKind targetScalarKind = ScalarKind::None;
        bool sourceIsVector = false;
    };

    /** Stores the backend-neutral component plan for a construct expression. */
    struct ConstructInfo
    {
        ConstructKind kind = ConstructKind::None;
        ScalarKind targetScalarKind = ScalarKind::None;
        uint32_t targetComponentCount = 0;
        std::vector<ConstructComponent> components;
    };

    /** Describes the stable category of an UGLIR statement node. */
    enum class StatementKind
    {
        Block,
        VariableDeclaration,
        Return,
        If,
        Switch,
        For,
        While,
        Do,
        Break,
        Continue,
        Expression,
    };

    /** Describes whether a function is module-local or externally visible as an entry/helper symbol. */
    enum class FunctionLinkage
    {
        Internal,
        Exported,
    };

    /** Describes one shader-visible resource category in backend-neutral reflection. */
    enum class ResourceKind
    {
        Unknown,
        UniformBuffer,
        StorageBuffer,
        Texture,
        StorageTexture,
        Sampler,
        InputAttachment,
        BindGroup,
    };

    /** Describes the dimensionality of a shader-visible texture or input attachment. */
    enum class TextureDimension
    {
        None,
        Texture2D,
        Texture2DArray,
        Texture3D,
        Subpass,
    };

    /** Describes a shader-visible texture or framebuffer format without relying on source spelling. */
    enum class TextureFormat
    {
        Unknown,
        R8Unorm,
        R8Snorm,
        R8Uint,
        R8Sint,
        R16Unorm,
        R16Snorm,
        R16Uint,
        R16Sint,
        R16Float,
        R32Float,
        R32Uint,
        R32Sint,
        RG8Unorm,
        RG8Snorm,
        RG8Uint,
        RG8Sint,
        RG16Unorm,
        RG16Snorm,
        RG16Uint,
        RG16Sint,
        RG16Float,
        RG32Float,
        RG32Uint,
        RG32Sint,
        RG11B10Ufloat,
        RGB9E5Ufloat,
        RGB10A2Uint,
        RGB10A2Unorm,
        RGBA8Unorm,
        RGBA8UnormSrgb,
        RGBA8Snorm,
        RGBA8Uint,
        RGBA8Sint,
        BGRA8Unorm,
        BGRA8UnormSrgb,
        RGBA16Uint,
        RGBA16Sint,
        RGBA16Float,
        RGBA32Float,
        RGBA32Uint,
        RGBA32Sint,
        ASTC4x4Unorm,
        Depth16Unorm,
        Depth32Float,
        PreferredSwapchain,
    };

    /** Describes the ordinary resource-table role used by lowered shader ABI resources. */
    enum class ResourceRole
    {
        None,
        AccessBounds,
        BufferIndexTable,
        TextureIndexTable,
        BufferValue,
        TextureValue,
        DrawInfo,
        CommandParams,
        PixelLocalInput,
    };

    /** Describes the permitted access mode for a reflected resource binding. */
    enum class AccessMode
    {
        Read,
        Write,
        ReadWrite,
    };

    /** Stores a compact source position for diagnostics and deterministic IR dumps. */
    struct SourceLocation
    {
        std::string file;
        uint32_t line = 0;
        uint32_t column = 0;
    };

    /** Stores one field in a structured UGLIR type while preserving declaration order. */
    struct TypeField
    {
        std::string name;
        std::string type;
        std::string semantic;
        BuiltinSemanticKind semanticKind = BuiltinSemanticKind::None;
        uint32_t semanticIndex = 0;
        uint32_t location = 0;
        uint32_t offset = 0;
        SourceLocation sourceLocation;
    };

    /** Stores one canonical backend-neutral type used by functions, expressions, and reflection. */
    struct Type
    {
        std::string name;
        TypeKind kind = TypeKind::Void;
        ScalarKind scalarKind = ScalarKind::None;
        uint32_t bitWidth = 0;
        std::string elementType;
        uint32_t vectorWidth = 0;
        uint32_t matrixColumns = 0;
        uint32_t matrixRows = 0;
        uint32_t arrayCount = 0;
        ResourceKind resourceKind = ResourceKind::Unknown;
        AccessMode accessMode = AccessMode::Read;
        TextureDimension textureDimension = TextureDimension::None;
        TextureFormat textureFormat = TextureFormat::Unknown;
        bool isMultisampled = false;
        TypeRole role = TypeRole::Value;
        bool isImplementationOnly = false;
        std::vector<TypeField> fields;
        SourceLocation sourceLocation;
    };

    /** Stores one typed expression tree node for structured shader IR. */
    struct Expression
    {
        ExpressionKind kind = ExpressionKind::Literal;
        IntrinsicCallKind intrinsicCallKind = IntrinsicCallKind::None;
        std::string type;
        std::string name;
        std::string value;
        /** Records Clang's arithmetic result type before a compound assignment converts back to storage. */
        std::string computationType;
        std::string operatorName;
        bool isPostfix = false;
        bool isEagerLogical = false;
        std::vector<Expression> operands;
        ConstructInfo constructInfo;
        SourceLocation sourceLocation;
    };

    struct Statement;

    /** Stores one case or default arm in a structured switch statement. */
    struct SwitchCase
    {
        std::vector<Expression> labels;
        std::vector<Statement> body;
        bool isDefault = false;
        SourceLocation sourceLocation;
    };

    /** Stores one structured statement node and its nested child statements or expressions. */
    struct Statement
    {
        StatementKind kind = StatementKind::Block;
        std::string name;
        std::string type;
        std::vector<Expression> expressions;
        std::vector<Statement> children;
        std::vector<Statement> elseChildren;
        std::vector<SwitchCase> switchCases;
        SourceLocation sourceLocation;
    };

    /** Stores one function parameter in declaration order. */
    struct FunctionParameter
    {
        std::string name;
        std::string type;
        std::string semantic;
        BuiltinSemanticKind semanticKind = BuiltinSemanticKind::None;
        uint32_t semanticIndex = 0;
        ParameterPassingMode passingMode = ParameterPassingMode::Value;
        bool isReference = false;
        bool isConstReference = false;
        SourceLocation sourceLocation;
    };

    /** Stores one entry or helper function in the UGLIR module. */
    struct Function
    {
        std::string name;
        std::string returnType;
        FunctionLinkage linkage = FunctionLinkage::Internal;
        ShaderStage stage = ShaderStage::None;
        ShaderEntryKind entryKind = ShaderEntryKind::None;
        bool isEntryPoint = false;
        std::array<uint32_t, 3> workgroupSize{1, 1, 1};
        std::vector<FunctionParameter> parameters;
        std::vector<Statement> body;
        SourceLocation sourceLocation;
    };

    /** Stores one shader entry input or output binding in backend-neutral form. */
    struct StageIOBinding
    {
        std::string name;
        std::string type;
        std::string semantic;
        BuiltinSemanticKind semanticKind = BuiltinSemanticKind::None;
        uint32_t semanticIndex = 0;
        uint32_t location = 0;
        uint32_t index = 0;
        SourceLocation sourceLocation;
    };

    /** Stores one backend-neutral resource binding entry for runtime reflection. */
    struct ResourceBinding
    {
        std::string name;
        ResourceKind kind = ResourceKind::Unknown;
        uint32_t bindGroupIndex = 0;
        uint32_t bindingIndex = 0;
        AccessMode accessMode = AccessMode::Read;
        std::string elementType;
        uint32_t arrayCount = 1;
        TextureDimension textureDimension = TextureDimension::None;
        TextureFormat textureFormat = TextureFormat::Unknown;
        bool isMultisampled = false;
        uint32_t inputAttachmentIndex = 0;
        ResourceRole resourceRole = ResourceRole::None;
        uint32_t resourceIndex = 0;
        std::vector<ShaderStage> visibleStages;
        SourceLocation sourceLocation;
    };

    /** Describes one complete logical bind group shared by all entries of a shader class. */
    struct BindGroupBinding
    {
        std::string name;
        std::string typeName;
        uint32_t bindGroupIndex = 0;
        bool isRenderSet = false;
    };

    /** Stores the shader artifact reflection that later host code and backends will consume. */
    struct Reflection
    {
        std::string shaderClassName;
        std::vector<BindGroupBinding> bindGroups;
        uint32_t metalBindGroupBufferOffset = 0;
        std::string entryName;
        ShaderStage stage = ShaderStage::None;
        ShaderEntryKind entryKind = ShaderEntryKind::None;
        std::array<uint32_t, 3> workgroupSize{1, 1, 1};
        std::vector<StageIOBinding> stageInputs;
        std::vector<StageIOBinding> stageOutputs;
        std::vector<ResourceBinding> resources;
        std::string artifactHash;
    };

    /** Stores one complete UGLIR shader artifact module. */
    struct Module
    {
        std::string name;
        std::vector<Type> types;
        std::vector<Function> functions;
        Reflection reflection;
        SourceLocation sourceLocation;
    };
} // namespace UGLC::CodeGen::UGLIR
