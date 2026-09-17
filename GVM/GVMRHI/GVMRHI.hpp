#pragma once
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <Utils/ResourcePool.hpp>
// #include <EASTL/shared_ptr.h>
#include <EASTL/intrusive_ptr.h>
#include <EASTL/optional.h>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>

#ifndef GVM_LOGGING_ENABLED
#define GVM_LOGGING_ENABLED 0
#endif

#ifndef GVM_CPU_PROBE_ENABLED
#define GVM_CPU_PROBE_ENABLED 0
#endif

namespace GVM::RHI
{

    using Flags = uint32_t;
    using Bool = uint32_t;
    static constexpr Bool True = 1;
    static constexpr Bool False = 0;
    static constexpr uint64_t WholeSize = eastl::numeric_limits<uint64_t>::max();
    static constexpr uint64_t WholeMapSize = eastl::numeric_limits<uint64_t>::max();

    namespace Detail
    {
        template <class R>
        using ArrayDataValue = std::remove_cv_t<std::remove_reference_t<decltype(*std::data(std::declval<const R &>()))>>;
    } // namespace Detail

    template <class R, class T>
    concept ArrayCompatible = requires(const R &r) {
        { std::data(r) };
        { std::size(r) };
    } && std::is_convertible_v<decltype(std::size(std::declval<const R &>())), std::size_t> &&
        std::is_same_v<Detail::ArrayDataValue<R>, T>;

    template <typename T>
    class MultipleElements
    {
        eastl::vector<T> storage; // 真正的数据

    public:
        MultipleElements(const T &value) // lvalue
            : storage{value}
        {
        }

        MultipleElements(T &&value) // rvalue
            : storage{eastl::move(value)}
        {
        }

        MultipleElements(std::initializer_list<T> il)
            : storage(il.begin(), il.end())
        {
        }

        /* 其余成员保持不变 … */
        /* ----------- 构造函数 ----------- */
        MultipleElements() = default; // 空容器
                                      /*  explicit MultipleElements(uint64_t n) : storage(n) {} // 指定长度
                                      MultipleElements(uint64_t n, const T &value) : storage(n, value) {}
                               */
        template <ArrayCompatible<T> R>
        MultipleElements(const R &r)
            : storage(std::data(r), std::data(r) + std::size(r))
        {
        }

        /* ------ 新增：接收任意数量的单个 T ------ */
        template <typename... U>
        // requires(sizeof...(U) > 1) // && (std::same_as<T, std::decay_t<U>> && ...))
        MultipleElements(U &&...elems) // a,b,c…
            : storage{eastl::forward<U>(elems)...}
        {
        } // 直接列表初始化 vector

        /* ----------- 赋值：标量 ----------- */
        MultipleElements &operator=(const T &value)
        {
            // std::fill(storage.begin(), storage.end(), value);
            storage = {value};
            return *this;
        }

        /* ----------- 赋值：范围 ----------- */
        template <ArrayCompatible<T> R>
        MultipleElements &operator=(const R &r)
        {
            storage.assign(std::data(r), std::data(r) + std::size(r));
            return *this;
        }

        /* -------- 可选：几条便捷转发，保持和 eastl::vector 的用法一致 -------- */
        operator eastl::vector<T> &() noexcept
        {
            return storage;
        }
        operator const eastl::vector<T> &() const noexcept
        {
            return storage;
        }

        /* ----------- 类 array 的访问 ----------- */
        T &operator[](uint64_t i)
        {
            return storage[i];
        }
        const T &operator[](uint64_t i) const
        {
            return storage[i];
        }

        /* ----------- 迭代器（range‑based for） ----------- */
        auto begin() noexcept
        {
            return storage.begin();
        }
        auto end() noexcept
        {
            return storage.end();
        }
        auto begin() const noexcept
        {
            return storage.begin();
        }
        auto end() const noexcept
        {
            return storage.end();
        }

        /* ----------- 常用辅助 ----------- */
        uint64_t size() const noexcept
        {
            return storage.size();
        }
        T *data() noexcept
        {
            return storage.data();
        }
        const T *data() const noexcept
        {
            return storage.data();
        }
        bool empty() const noexcept
        {
            return storage.empty();
        }
    };

    enum class SwapchainNextTextureQueryStatus
    {
        Success = 0x00000000,
        Timeout = 0x00000001,
        Outdated = 0x00000002,
        Lost = 0x00000003,
        OutOfMemory = 0x00000004,
        DeviceLost = 0x00000005,
        Force32 = 0x7FFFFFFF
    };
    enum class VertexFormat
    {
        Undefined = 0x00000000,
        Uint8x2 = 0x00000001,
        Uint8x4 = 0x00000002,
        Sint8x2 = 0x00000003,
        Sint8x4 = 0x00000004,
        Unorm8x2 = 0x00000005,
        Unorm8x4 = 0x00000006,
        Snorm8x2 = 0x00000007,
        Snorm8x4 = 0x00000008,
        Uint16x2 = 0x00000009,
        Uint16x4 = 0x0000000A,
        Sint16x2 = 0x0000000B,
        Sint16x4 = 0x0000000C,
        Unorm16x2 = 0x0000000D,
        Unorm16x4 = 0x0000000E,
        Snorm16x2 = 0x0000000F,
        Snorm16x4 = 0x00000010,
        Float16x2 = 0x00000011,
        Float16x4 = 0x00000012,
        Float32 = 0x00000013,
        Float32x2 = 0x00000014,
        Float32x3 = 0x00000015,
        Float32x4 = 0x00000016,
        Uint32 = 0x00000017,
        Uint32x2 = 0x00000018,
        Uint32x3 = 0x00000019,
        Uint32x4 = 0x0000001A,
        Sint32 = 0x0000001B,
        Sint32x2 = 0x0000001C,
        Sint32x3 = 0x0000001D,
        Sint32x4 = 0x0000001E,
        Force32 = 0x7FFFFFFF
    };

    enum class IndexFormat
    {
        Undefined = 0x00000000,
        Uint16 = 0x00000001,
        Uint32 = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class AddressMode
    {
        Repeat = 0x00000000,
        MirrorRepeat = 0x00000001,
        ClampToEdge = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class VertexStepMode
    {
        Vertex = 0x00000000,
        Instance = 0x00000001,
        VertexBufferNotUsed = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    typedef Flags ColorWriteMaskFlags;
    namespace ColorWriteMask
    {
        static constexpr ColorWriteMaskFlags None = 0x00000000;
        static constexpr ColorWriteMaskFlags Red = 0x00000001;
        static constexpr ColorWriteMaskFlags Green = 0x00000002;
        static constexpr ColorWriteMaskFlags Blue = 0x00000004;
        static constexpr ColorWriteMaskFlags Alpha = 0x00000008;
        static constexpr ColorWriteMaskFlags All = 0x0000000F;
        static constexpr ColorWriteMaskFlags Force32 = 0x7FFFFFFF;
    } // namespace ColorWriteMask

    typedef Flags BufferUsageFlags;
    namespace BufferUsage
    {
        static constexpr Flags None = 0x00000000;
        static constexpr Flags MapRead = 0x00000001;
        static constexpr Flags MapWrite = 0x00000002;
        static constexpr Flags CopySrc = 0x00000004;
        static constexpr Flags CopyDst = 0x00000008;
        static constexpr Flags Index = 0x00000010;
        static constexpr Flags Vertex = 0x00000020;
        static constexpr Flags Uniform = 0x00000040;
        static constexpr Flags Storage = 0x00000080;
        static constexpr Flags Indirect = 0x00000100;
        static constexpr Flags QueryResolve = 0x00000200;
        static constexpr Flags Force32 = 0x7FFFFFFF;
    } // namespace BufferUsage
    enum class TextureFormat
    {
        Undefined = 0x00000000,
        R8Unorm = 0x00000001,
        R8Snorm = 0x00000002,
        R8Uint = 0x00000003,
        R8Sint = 0x00000004,
        R16Uint = 0x00000005,
        R16Sint = 0x00000006,
        R16Float = 0x00000007,
        RG8Unorm = 0x00000008,
        RG8Snorm = 0x00000009,
        RG8Uint = 0x0000000A,
        RG8Sint = 0x0000000B,
        R32Float = 0x0000000C,
        R32Uint = 0x0000000D,
        R32Sint = 0x0000000E,
        RG16Uint = 0x0000000F,
        RG16Sint = 0x00000010,
        RG16Float = 0x00000011,
        RGBA8Unorm = 0x00000012,
        RGBA8UnormSrgb = 0x00000013,
        RGBA8Snorm = 0x00000014,
        RGBA8Uint = 0x00000015,
        RGBA8Sint = 0x00000016,
        BGRA8Unorm = 0x00000017,
        BGRA8UnormSrgb = 0x00000018,
        RGB10A2Uint = 0x00000019,
        RGB10A2Unorm = 0x0000001A,
        RG11B10Ufloat = 0x0000001B,
        RGB9E5Ufloat = 0x0000001C,
        RG32Float = 0x0000001D,
        RG32Uint = 0x0000001E,
        RG32Sint = 0x0000001F,
        RGBA16Uint = 0x00000020,
        RGBA16Sint = 0x00000021,
        RGBA16Float = 0x00000022,
        RGBA32Float = 0x00000023,
        RGBA32Uint = 0x00000024,
        RGBA32Sint = 0x00000025,
        Stencil8 = 0x00000026,
        Depth16Unorm = 0x00000027,
        Depth24Plus = 0x00000028,
        Depth24PlusStencil8 = 0x00000029,
        Depth32Float = 0x0000002A,
        Depth32FloatStencil8 = 0x0000002B,
        BC1RGBAUnorm = 0x0000002C,
        BC1RGBAUnormSrgb = 0x0000002D,
        BC2RGBAUnorm = 0x0000002E,
        BC2RGBAUnormSrgb = 0x0000002F,
        BC3RGBAUnorm = 0x00000030,
        BC3RGBAUnormSrgb = 0x00000031,
        BC4RUnorm = 0x00000032,
        BC4RSnorm = 0x00000033,
        BC5RGUnorm = 0x00000034,
        BC5RGSnorm = 0x00000035,
        BC6HRGBUfloat = 0x00000036,
        BC6HRGBFloat = 0x00000037,
        BC7RGBAUnorm = 0x00000038,
        BC7RGBAUnormSrgb = 0x00000039,
        ETC2RGB8Unorm = 0x0000003A,
        ETC2RGB8UnormSrgb = 0x0000003B,
        ETC2RGB8A1Unorm = 0x0000003C,
        ETC2RGB8A1UnormSrgb = 0x0000003D,
        ETC2RGBA8Unorm = 0x0000003E,
        ETC2RGBA8UnormSrgb = 0x0000003F,
        EACR11Unorm = 0x00000040,
        EACR11Snorm = 0x00000041,
        EACRG11Unorm = 0x00000042,
        EACRG11Snorm = 0x00000043,
        ASTC4x4Unorm = 0x00000044,
        ASTC4x4UnormSrgb = 0x00000045,
        ASTC5x4Unorm = 0x00000046,
        ASTC5x4UnormSrgb = 0x00000047,
        ASTC5x5Unorm = 0x00000048,
        ASTC5x5UnormSrgb = 0x00000049,
        ASTC6x5Unorm = 0x0000004A,
        ASTC6x5UnormSrgb = 0x0000004B,
        ASTC6x6Unorm = 0x0000004C,
        ASTC6x6UnormSrgb = 0x0000004D,
        ASTC8x5Unorm = 0x0000004E,
        ASTC8x5UnormSrgb = 0x0000004F,
        ASTC8x6Unorm = 0x00000050,
        ASTC8x6UnormSrgb = 0x00000051,
        ASTC8x8Unorm = 0x00000052,
        ASTC8x8UnormSrgb = 0x00000053,
        ASTC10x5Unorm = 0x00000054,
        ASTC10x5UnormSrgb = 0x00000055,
        ASTC10x6Unorm = 0x00000056,
        ASTC10x6UnormSrgb = 0x00000057,
        ASTC10x8Unorm = 0x00000058,
        ASTC10x8UnormSrgb = 0x00000059,
        ASTC10x10Unorm = 0x0000005A,
        ASTC10x10UnormSrgb = 0x0000005B,
        ASTC12x10Unorm = 0x0000005C,
        ASTC12x10UnormSrgb = 0x0000005D,
        ASTC12x12Unorm = 0x0000005E,
        ASTC12x12UnormSrgb = 0x0000005F,
        Force32 = 0x7FFFFFFF
    };

    enum class GraphicsBackend
    {
        Undefined = 0x00000000,
        Metal = 0x00000001,
        Vulkan = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class FrontFace
    {
        CCW = 0x00000000,
        CW = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    enum class CullMode
    {
        None = 0x00000000,
        Front = 0x00000001,
        Back = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class LoadOp
    {
        Undefined = 0x00000000,
        Clear = 0x00000001,
        Load = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class StoreOp
    {
        Undefined = 0x00000000,
        Store = 0x00000001,
        Discard = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class BlendFactor
    {
        Zero = 0x00000000,
        One = 0x00000001,
        Src = 0x00000002,
        OneMinusSrc = 0x00000003,
        SrcAlpha = 0x00000004,
        OneMinusSrcAlpha = 0x00000005,
        Dst = 0x00000006,
        OneMinusDst = 0x00000007,
        DstAlpha = 0x00000008,
        OneMinusDstAlpha = 0x00000009,
        SrcAlphaSaturated = 0x0000000A,
        Constant = 0x0000000B,
        OneMinusConstant = 0x0000000C,
        Force32 = 0x7FFFFFFF
    };

    enum class BlendOperation
    {
        Add = 0x00000000,
        Subtract = 0x00000001,
        ReverseSubtract = 0x00000002,
        Min = 0x00000003,
        Max = 0x00000004,
        Force32 = 0x7FFFFFFF
    };

    enum class BufferBindingType
    {
        Undefined = 0x00000000,
        Uniform = 0x00000001,
        Storage = 0x00000002,
        ReadOnlyStorage = 0x00000003,
        Force32 = 0x7FFFFFFF
    };

    enum class PrimitiveTopology
    {
        PointList = 0x00000000,
        LineList = 0x00000001,
        LineStrip = 0x00000002,
        TriangleList = 0x00000003,
        TriangleStrip = 0x00000004,
        PatchList = 0x00000005,
        Force32 = 0x7FFFFFFF
    };

    enum class FilterMode
    {
        Nearest = 0x00000000,
        Linear = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    enum class MipmapFilterMode
    {
        Nearest = 0x00000000,
        Linear = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    struct Color
    {
        double r;
        double g;
        double b;
        double a;
    };

    typedef Flags ShaderStageFlags;

    namespace ShaderStage
    {
        static constexpr ShaderStageFlags None = 0x00000000;
        static constexpr ShaderStageFlags Vertex = 0x00000001;
        static constexpr ShaderStageFlags Fragment = 0x00000002;
        static constexpr ShaderStageFlags Compute = 0x00000004;
        static constexpr ShaderStageFlags Hull = 0x00000008;
        static constexpr ShaderStageFlags Domain = 0x00000010;
        static constexpr ShaderStageFlags Force32 = 0x7FFFFFFF;
    }; // namespace ShaderStage

    enum class SamplerBindingType
    {
        Undefined = 0x00000000,
        Filtering = 0x00000001,
        NonFiltering = 0x00000002,
        Comparison = 0x00000003,
        Force32 = 0x7FFFFFFF
    };

    typedef Flags TextureUsageFlags;

    namespace TextureUsage
    {
        static constexpr TextureUsageFlags None = 0x00000000;
        static constexpr TextureUsageFlags CopySrc = 0x00000001;
        static constexpr TextureUsageFlags CopyDst = 0x00000002;
        static constexpr TextureUsageFlags TextureBinding = 0x00000004;
        static constexpr TextureUsageFlags StorageBinding = 0x00000008;
        static constexpr TextureUsageFlags RenderAttachment = 0x00000010;
        static constexpr TextureUsageFlags PixelLocalAttachment = 0x00000020;
        static constexpr TextureUsageFlags Force32 = 0x7FFFFFFF;
    }; // namespace TextureUsage

    enum class TextureSampleType
    {
        Undefined = 0x00000000,
        Float = 0x00000001,
        UnfilterableFloat = 0x00000002,
        Depth = 0x00000003,
        Sint = 0x00000004,
        Uint = 0x00000005,
        Force32 = 0x7FFFFFFF
    };

    enum class StorageBufferAccess
    {
        Undefined = 0x00000000,
        WriteOnly = 0x00000001,
        ReadOnly = 0x00000002,
        ReadWrite = 0x00000003,
        Force32 = 0x7FFFFFFF
    };

    enum class StorageTextureAccess
    {
        Undefined = 0x00000000,
        WriteOnly = 0x00000001,
        ReadOnly = 0x00000002,
        ReadWrite = 0x00000003,
        Force32 = 0x7FFFFFFF
    };

    enum class TextureDimension
    {
        e1D = 0x00000000,
        e2D = 0x00000001,
        e3D = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class TextureViewDimension
    {
        Undefined = 0x00000000,
        e1D = 0x00000001,
        e2D = 0x00000002,
        e2DArray = 0x00000003,
        Cube = 0x00000004,
        CubeArray = 0x00000005,
        e3D = 0x00000006,
        Force32 = 0x7FFFFFFF
    };

    enum class TextureStorageMode
    {
        Persistent = 0x00000000,
        TransientAttachment = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    typedef Flags TextureAspectFlags;
    namespace TextureAspect
    {
        static constexpr TextureAspectFlags All = 0x00000000;
        static constexpr TextureAspectFlags StencilOnly = 0x00000001;
        static constexpr TextureAspectFlags DepthOnly = 0x00000002;
        static constexpr TextureAspectFlags Force32 = 0x7FFFFFFF;
    } // namespace TextureAspect

    enum class CompareFunction
    {
        Undefined = 0x00000000,
        Never = 0x00000001,
        Less = 0x00000002,
        LessEqual = 0x00000003,
        Greater = 0x00000004,
        GreaterEqual = 0x00000005,
        Equal = 0x00000006,
        NotEqual = 0x00000007,
        Always = 0x00000008,
        Force32 = 0x7FFFFFFF
    };
    enum class TessellationPartitionMode
    {
        TessellationPartitionMode_Integer = 0, /* Vulkan: vk::TessellationDomainOrigin::UpperLeft  EqualSpacing */
        TessellationPartitionMode_FractionalEven,
        TessellationPartitionMode_FractionalOdd,
        TessellationPartitionMode_Pow2,
        TessellationPartitionMode_Force32 = 0x7FFFFFFF
    };

    enum class TessellationOutputTopology
    {
        TessellationOutputTopology_TriangleCW = 0,
        TessellationOutputTopology_TriangleCCW,
        TessellationOutputTopology_Quad,
        TessellationOutputTopology_Isoline,
        TessellationOutputTopology_Force32 = 0x7FFFFFFF
    };

    struct Extent3D
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
    };
    struct Origin3D
    {
        uint32_t x;
        uint32_t y;
        uint32_t z;
    };
    class InstanceImpl;
    class DeviceImpl;
    class QueueImpl;
    class SwapchainImpl;
    class BufferImpl;
    struct BufferRange;
    class QuerySetImpl;
    class SamplerImpl;
    class TextureImpl;
    class TextureViewImpl;
    class CommandEncoderImpl;
    class BlitPassEncoderImpl;
    class ComputePassEncoderImpl;
    class RenderPassEncoderImpl;

    class ShaderModuleImpl;
    class BindGroupLayoutImpl;
    class PipelineLayoutImpl;
    class ComputePipelineImpl;
    class RenderPipelineImpl;
    class BindGroupImpl;
    class LoggerImpl;

    using Instance = InstanceImpl *;
    using Device = DeviceImpl *;
    using Queue = QueueImpl *;
    using Swapchain = SwapchainImpl *;
    using Buffer = xGE::Utils::ResourceHandle<BufferImpl>;
    using BufferPool = xGE::Utils::ResourcePool<BufferImpl>;
    using Texture = xGE::Utils::ResourceHandle<TextureImpl>;
    using TexturePool = xGE::Utils::ResourcePool<TextureImpl>;
    using TextureView = xGE::Utils::ResourceHandle<TextureViewImpl>;
    using TextureViewPool = xGE::Utils::ResourcePool<TextureViewImpl>;
    using Sampler = xGE::Utils::ResourceHandle<SamplerImpl>;
    using SamplerPool = xGE::Utils::ResourcePool<SamplerImpl>;
    using QuerySet = eastl::intrusive_ptr<QuerySetImpl>;

    using CommandEncoder = eastl::intrusive_ptr<CommandEncoderImpl>;
    using ComputePassEncoder = eastl::intrusive_ptr<ComputePassEncoderImpl>;
    using RenderPassEncoder = eastl::intrusive_ptr<RenderPassEncoderImpl>;
    using BlitPassEncoder = eastl::intrusive_ptr<BlitPassEncoderImpl>;
    using ShaderModule = eastl::intrusive_ptr<ShaderModuleImpl>;
    using BindGroupLayout = eastl::intrusive_ptr<BindGroupLayoutImpl>;
    using BindGroup = eastl::intrusive_ptr<BindGroupImpl>;

    using PipelineLayout = eastl::intrusive_ptr<PipelineLayoutImpl>;
    using ComputePipeline = eastl::intrusive_ptr<ComputePipelineImpl>;
    using RenderPipeline = eastl::intrusive_ptr<RenderPipelineImpl>;

    enum class LogLevel : uint32_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Off
    };

    enum class LogChannel : uint32_t
    {
        General = 0,
        CpuProbe
    };

    struct LogRecord
    {
        GraphicsBackend backend = GraphicsBackend::Undefined;
        LogLevel level = LogLevel::Info;
        LogChannel channel = LogChannel::General;
        eastl::string category;
        eastl::string message;
        uint64_t timestampNs = 0;
        uint64_t threadId = 0;
        uint32_t processId = 0;
    };

    using LogCallback = void (*)(const LogRecord &record, void *userData);

    struct CallbackLoggerConfig
    {
        LogCallback callback = nullptr;
        void *userData = nullptr;
    };

    enum class LoggerMode : uint32_t
    {
        DefaultProvider = 0,
        Callback,
        RegisteredProvider
    };

    enum class LoggerOptionValueType : uint32_t
    {
        Bool = 0,
        Int64,
        UInt64,
        Float64,
        String
    };

    struct LoggerOptionValue
    {
        LoggerOptionValueType type = LoggerOptionValueType::String;
        Bool boolValue = False;
        int64_t int64Value = 0;
        uint64_t uint64Value = 0;
        double float64Value = 0.0;
        eastl::string stringValue;
    };

    struct LoggerOption
    {
        eastl::string key;
        LoggerOptionValue value;
    };

    struct RegisteredLoggerConfig
    {
        eastl::string providerName;
        eastl::vector<LoggerOption> options;
    };

    enum class CpuProbeScopeEmitMode : uint32_t
    {
        EndOnly = 0,
        BeginEnd
    };

    struct CpuProbeConfig
    {
        Bool enabled = False;
        LogLevel level = LogLevel::Trace;
        LogLevel flushLevel = LogLevel::Off;
        CpuProbeScopeEmitMode scopeEmitMode = CpuProbeScopeEmitMode::EndOnly;
        uint64_t minDurationUs = 0;
        Bool emitInstant = True;
        Bool emitValue = True;
    };

    struct LoggingConfig
    {
        Bool enabled = False;
        LogLevel level = LogLevel::Info;
        LogLevel flushLevel = LogLevel::Warn;
        LoggerMode mode = LoggerMode::DefaultProvider;
        CallbackLoggerConfig callback = {};
        RegisteredLoggerConfig registeredProvider = {};
        CpuProbeConfig cpuProbe = {};
    };

    /// Configures the built-in runtime diagnostics overlay that RHI queues can append to submitted frames.
    struct RuntimeDiagnosticsOverlayConfig
    {
        Bool enabled = False;
        Bool showFps = True;
        Bool showPassTimes = True;
        Bool showResources = True;
        uint32_t maxPassScopes = 128u;
        uint32_t maxResourceRows = 256u;
        uint32_t maxTextGlyphs = 4096u;
        uint32_t timestampReadbackLatencyFrames = 2u;
    };

    /// Identifies the coarse resource type reported by a diagnostics resource snapshot.
    enum class DiagnosticsResourceKind
    {
        Buffer,
        Texture
    };

    /// Describes one live GPU resource row for low-overhead runtime diagnostics UI.
    struct DiagnosticsResourceSnapshotEntry
    {
        DiagnosticsResourceKind kind = DiagnosticsResourceKind::Buffer;
        eastl::string label;
        uint64_t estimatedBytes = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t depth = 0u;
        uint32_t mipLevelCount = 0u;
        uint32_t arrayLayerCount = 0u;
        TextureFormat format = TextureFormat::Undefined;
    };

    /// Captures the current backend live-resource list for presentation in diagnostics tooling.
    struct DiagnosticsResourceSnapshot
    {
        eastl::vector<DiagnosticsResourceSnapshotEntry> entries;
        uint64_t totalEstimatedBytes = 0u;
    };

    struct InstanceDescriptor
    {
        GraphicsBackend preferredBackend = GraphicsBackend::Undefined;
        Bool hasLoggingConfig = False;
        LoggingConfig logging = {};
        RuntimeDiagnosticsOverlayConfig diagnosticsOverlay = {};
    };

    /// Identifies which native window-system object is carried by a NativeSurfaceDescriptor.
    enum class NativeSurfaceKind
    {
        Undefined = 0,
        MetalLayer,
        AndroidWindow,
        OhosWindow,
        Win32Window,
        WaylandSurface,
        XcbWindow,
        XlibWindow,
    };

    /// Describes a platform-native presentation surface without relying on positional void pointer slots.
    struct NativeSurfaceDescriptor
    {
        NativeSurfaceKind kind = NativeSurfaceKind::Undefined;
        void *surface = nullptr;
        void *displayOrInstance = nullptr;
        Extent3D extentHint = {};
        Bool hasExtentHint = False;
    };

    /// Describes the native surface used to create a presentation swapchain.
    struct SwapchainDescriptor
    {
        NativeSurfaceDescriptor nativeSurface = {};
        void *A = nullptr;
        void *B = nullptr;
    };

    struct SwapchainQueryResult
    {
        Texture texture;
        SwapchainNextTextureQueryStatus status;
    };

    struct RenderToSwapchainDescriptor
    {
        // Flip the sampled source texture vertically before presenting it to the swapchain.
        Bool flipYAxis = False;
    };

    enum class QueryType
    {
        Timestamp = 0,
        PassCounterStageUtilization = 1,
        PassCounterStatistic = 2,
        Force32 = 0x7FFFFFFF
    };

    static constexpr uint32_t QuerySetIndexUndefined = 0xffffffffu;

    struct QuerySetDescriptor
    {
        eastl::string label;
        QueryType type = QueryType::Timestamp;
        uint32_t count = 0;
    };

    struct TimestampQuerySupport
    {
        Bool supported = False;
        uint32_t validBits = 0;
        double tickPeriodNs = 1.0;
        Bool passTimestampWritesSupported = False;
    };

    /// Describes whether the selected backend can capture pass-scoped hardware counters.
    struct PassCounterQuerySupport
    {
        Bool supported = False;
        const char *backendName = "";
        Bool requiresExclusiveProfilingLock = False;
        Bool supportsSingleSubmitCounterPass = False;
        const char *unsupportedReason = "";
    };

    /**
     * Describes memory topology reported by the selected physical device.
     * Use this for runtime upload strategy decisions instead of platform-name heuristics.
     */
    struct DeviceMemoryProperties
    {
        Bool unifiedMemory = False;
    };

    struct PassTimestampWrites
    {
        QuerySet querySet = nullptr;
        uint32_t beginningOfPassWriteIndex = QuerySetIndexUndefined;
        uint32_t endOfPassWriteIndex = QuerySetIndexUndefined;
    };

    /// Describes pass-scoped hardware counter sample indices for one profiled pass.
    struct PassCounterWrites
    {
        QuerySet stageUtilizationQuerySet = nullptr;
        uint32_t stageUtilizationBeginIndex = QuerySetIndexUndefined;
        uint32_t stageUtilizationEndIndex = QuerySetIndexUndefined;
        QuerySet statisticQuerySet = nullptr;
        uint32_t statisticBeginIndex = QuerySetIndexUndefined;
        uint32_t statisticEndIndex = QuerySetIndexUndefined;
    };

    struct TimestampRawResult
    {
        uint64_t value = 0;
    };

    struct TimestampRangeResult
    {
        eastl::string label;
        uint32_t beginIndex = 0;
        uint32_t endIndex = 0;
        uint64_t beginRaw = 0;
        uint64_t endRaw = 0;
        double durationNs = 0.0;
    };

    /// Raw Metal-compatible stage utilization counter payload.
    struct PassCounterStageUtilizationRawResult
    {
        uint64_t totalCycles = 0;
        uint64_t vertexCycles = 0;
        uint64_t tessellationCycles = 0;
        uint64_t postTessellationVertexCycles = 0;
        uint64_t fragmentCycles = 0;
        uint64_t renderTargetCycles = 0;
    };

    /// Raw Metal-compatible pass statistic counter payload.
    struct PassCounterStatisticRawResult
    {
        uint64_t tessellationInputPatches = 0;
        uint64_t vertexInvocations = 0;
        uint64_t postTessellationVertexInvocations = 0;
        uint64_t clipperInvocations = 0;
        uint64_t clipperPrimitivesOut = 0;
        uint64_t fragmentInvocations = 0;
        uint64_t fragmentsPassed = 0;
        uint64_t computeKernelInvocations = 0;
    };

    /// Pass-scoped hardware counter deltas for one label.
    struct PassCounterRangeResult
    {
        eastl::string label;
        uint32_t stageBeginIndex = 0;
        uint32_t stageEndIndex = 0;
        uint32_t statisticBeginIndex = 0;
        uint32_t statisticEndIndex = 0;
        PassCounterStageUtilizationRawResult stage = {};
        PassCounterStatisticRawResult statistic = {};
    };

    inline bool isTimestampQueryIndexEnabled(uint32_t index)
    {
        return index != QuerySetIndexUndefined;
    }

    inline uint64_t calculateTimestampDeltaRaw(const TimestampQuerySupport &support, uint64_t beginRaw, uint64_t endRaw)
    {
        if (support.validBits == 0u || support.validBits >= 64u)
        {
            return endRaw - beginRaw;
        }

        const uint64_t mask = (uint64_t{1} << support.validBits) - uint64_t{1};
        return (endRaw - beginRaw) & mask;
    }

    inline double calculateTimestampDurationNs(const TimestampQuerySupport &support, uint64_t beginRaw, uint64_t endRaw)
    {
        return static_cast<double>(calculateTimestampDeltaRaw(support, beginRaw, endRaw)) * support.tickPeriodNs;
    }

    struct TextureDataLayout
    {
        uint64_t offset = 0;
        uint32_t bytesPerRow = 0;
        uint32_t rowsPerImage = 0;
    };

    struct ImageCopyBuffer
    {
        TextureDataLayout layout = {};
        Buffer buffer;
    };

    struct ImageCopyTexture
    {
        Texture texture;
        uint32_t mipLevel = 0;
        Origin3D origin = {0, 0, 0};
        TextureAspectFlags aspect = TextureAspect::All;
    };

    struct RenderPassColorAttachment
    {
        TextureView view;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        Color clearValue = {0, 0, 0.2, 1};
        Bool pixelLocal = false;
    };

    struct RenderPassDepthStencilAttachment
    {
        TextureView view;
        LoadOp depthLoadOp;
        StoreOp depthStoreOp;
        float depthClearValue;
        Bool pixelLocal = false;
    };

    /// Describes which framebuffer attachments one pixel-local pass reads and writes.
    struct PixelLocalPassAttachmentAccess
    {
        uint64_t colorReadMask = 0u;
        uint64_t colorWriteMask = 0u;
        Bool depthWrite = false;

        bool operator==(const PixelLocalPassAttachmentAccess &) const = default;
    };

    struct PixelLocalRenderPassState
    {
        Bool enabled = false;
        uint32_t passCount = 1u;
        /// Per-pass attachment access metadata consumed by backends that need render-pass-local scheduling.
        eastl::vector<PixelLocalPassAttachmentAccess> attachmentAccesses;
    };

    struct BlitPassDescriptor
    {
        eastl::string label;
        PassTimestampWrites timestampWrites = {};
        PassCounterWrites counterWrites = {};
    };

    struct ComputePassDescriptor
    {
        eastl::string label;
        PassTimestampWrites timestampWrites = {};
        PassCounterWrites counterWrites = {};
    };

    struct RenderPassDescriptor
    {
        eastl::string label;
        eastl::vector<RenderPassColorAttachment> colorAttachments;
        RenderPassDepthStencilAttachment depthStencilAttachment;
        PassTimestampWrites timestampWrites = {};
        PassCounterWrites counterWrites = {};
        PixelLocalRenderPassState pixelLocal = {};
    };

    struct BufferDescriptor
    {
        eastl::string label;
        BufferUsageFlags usage;
        uint64_t size;
    };

    struct IndirectRenderCommand
    {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    struct IndirectIndexedRenderCommand
    {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t vertexOffset;
        uint32_t firstInstance;
    };

    struct SamplerDescriptor
    {
        eastl::string label;
        AddressMode addressModeU;
        AddressMode addressModeV;
        AddressMode addressModeW;
        FilterMode magFilter;
        FilterMode minFilter;
        MipmapFilterMode mipmapFilter;
        float lodMinClamp;
        float lodMaxClamp;
        CompareFunction compare;
        uint16_t maxAnisotropy = 0;
    };

    struct TextureDescriptor
    {
        eastl::string label;
        TextureUsageFlags usage;
        TextureDimension dimension = TextureDimension::e2D;
        Extent3D size = {0, 0, 1};
        TextureFormat format;
        uint32_t mipLevelCount = 1;
        uint32_t arrayLayerCount = 1;
        TextureStorageMode storageMode = TextureStorageMode::Persistent;
    };
    struct TextureViewDescriptor
    {
        eastl::string label;
        TextureFormat format = TextureFormat::Undefined;
        TextureViewDimension dimension = TextureViewDimension::Undefined;
        uint32_t baseMipLevel = 0;
        uint32_t mipLevelCount = 1;
        uint32_t baseArrayLayer = 0;
        uint32_t arrayLayerCount = 1;
        TextureAspectFlags aspect = TextureAspect::All;
    };
    struct VertexAttribute
    {
        VertexFormat format;
        uint64_t offset;
        uint32_t shaderLocation;
    };

    struct ShaderModuleDescriptor
    {
        eastl::string label;
        eastl::string code;
        eastl::vector<uint32_t> spirv;
    };

    struct VertexBufferLayout
    {
        uint64_t arrayStride;
        VertexStepMode stepMode = VertexStepMode::Vertex;
        eastl::vector<VertexAttribute> attributes;
    };

    struct BlendComponent
    {
        BlendOperation operation = BlendOperation::Add;
        BlendFactor srcFactor = BlendFactor::One;
        BlendFactor dstFactor = BlendFactor::One;
    };

    struct BlendState
    {
        BlendComponent color;
        BlendComponent alpha;
    };

    struct ColorTargetState
    {
        TextureFormat format;
        BlendState blend;
        Bool blendEnabled;
        ColorWriteMaskFlags writeMask = ColorWriteMask::All;
        Bool pixelLocal = false;
    };

    struct DepthStencilState
    {
        TextureFormat format = TextureFormat::Undefined;
        Bool depthTestEnabled = false;
        Bool depthWriteEnabled = false;
        CompareFunction depthCompare = CompareFunction::Always;
        int32_t depthBias = 0;
        float depthBiasSlopeScale = 0.0f;
        float depthBiasClamp = 0.0f;
        Bool pixelLocal = false;
    };

    struct VertexState
    {
        ShaderModule module;
        eastl::string entryPoint;
        eastl::vector<VertexBufferLayout> buffers;
    };
    struct TessellationStageDescriptor
    {
        ShaderModule module;
        eastl::string entryPoint;
    };

    struct TessellationState
    {
        TessellationStageDescriptor control;    /* TCS / HS */
        TessellationStageDescriptor evaluation; /* TES / DS */

        uint32_t patchControlPoints; /* 1-32 (Vulkan), 1-32 (DX12), 1-32 (Metal) */
        TessellationPartitionMode partitionMode;
        TessellationOutputTopology outputTopology;

        /* Metal 需显式指定最大 factor；Vulkan / DX12 由 impl 截断 */
        uint32_t maxTessellationFactor = 0; /* 1-64，默认 64 */
    };

    struct FragmentState
    {
        ShaderModule module;
        eastl::string entryPoint;
        eastl::vector<ColorTargetState> targets;
    };

    struct PrimitiveState
    {
        PrimitiveTopology topology;
        IndexFormat stripIndexFormat;
        FrontFace frontFace;
        CullMode cullMode;
    };
    struct BufferBindingLayout
    {
        BufferBindingType type = BufferBindingType::Undefined;
        StorageBufferAccess access = StorageBufferAccess::Undefined;
        // Bool hasDynamicOffset;
        // uint64_t minBindingSize;
    };

    struct SamplerBindingLayout
    {
        SamplerBindingType type = SamplerBindingType::Undefined;
    };

    struct TextureBindingLayout
    {
        TextureSampleType sampleType = TextureSampleType::Undefined;
        TextureViewDimension viewDimension = TextureViewDimension::Undefined;
    };

    struct StorageTextureBindingLayout
    {
        StorageTextureAccess access = StorageTextureAccess::Undefined;
        TextureFormat format = TextureFormat::Undefined;
        TextureViewDimension viewDimension = TextureViewDimension::Undefined;
    };

    struct BindGroupLayoutEntry
    {
        uint32_t binding;
        uint32_t maxCount = 1;
        ShaderStageFlags visibility;
        BufferBindingLayout buffer;
        SamplerBindingLayout sampler;
        TextureBindingLayout texture;
        StorageTextureBindingLayout storageTexture;
    };

    struct BindGroupLayoutDescriptor
    {
        eastl::string label;
        eastl::vector<BindGroupLayoutEntry> entries;
    };

    struct PipelineLayoutDescriptor
    {
        eastl::string label;
        eastl::vector<BindGroupLayout> bindGroupLayouts;
    };

    struct BufferRange
    {
        Buffer buffer;
        uint64_t offset = 0;
        uint64_t size = WholeSize;
        BufferRange() = default;
        BufferRange(Buffer buffer, uint64_t offset = 0, uint64_t size = WholeSize);


        void reset()
        {
            buffer.reset();
            offset = 0;
            size = WholeSize;
        }
    };

    struct BufferCopyRegion
    {
        uint32_t srcOffset = 0;
        uint32_t dstOffset = 0;
        uint32_t size = 0;
    };

    struct BindGroupEntry
    {
        uint32_t binding;
        MultipleElements<BufferRange> buffer;
        MultipleElements<Sampler> sampler;
        MultipleElements<TextureView> textureView;
    };

    struct BindGroupDescriptor
    {
        eastl::string label;
        BindGroupLayout layout;
        MultipleElements<BindGroupEntry> entries;
    };

    struct ComputeStageDescriptor
    {
        ShaderModule module;
        eastl::string entryPoint;
        uint32_t workgroupX = 0;
        uint32_t workgroupY = 0;
        uint32_t workgroupZ = 0;
        // eastl::vector<ConstantEntry> constants;
    };

    struct ComputePipelineDescriptor
    {
        eastl::string label;
        PipelineLayout layout;
        ComputeStageDescriptor compute;
    };

    struct RenderPipelineDescriptor
    {
        eastl::string label;
        PipelineLayout layout;
        VertexState vertex;
        eastl::optional<TessellationState> tessellation;
        PrimitiveState primitive;
        DepthStencilState depthStencil;
        FragmentState fragment;
        /// Attachment access generated from source-level pixel-local reads and writes for this pipeline.
        PixelLocalPassAttachmentAccess pixelLocalAttachmentAccess = {};
    };

    struct ResourceObject
    {
    protected:
        eastl::string mLabelName;

    public:
        const eastl::string &getLabelName() const
        {
            return mLabelName;
        }
        virtual ~ResourceObject() = default;
    };

    class RefCountedObject
    {
        eastl::atomic<int> mRefCount = 0;

    public:
        virtual void AddRef()
        {
            mRefCount++;
        };
        virtual void Release()
        {
            mRefCount -= 1;
            if (mRefCount == 0)
            {
                delete this;
            }
        };
        virtual ~RefCountedObject() = default;
    };

    class LoggerImpl : public RefCountedObject
    {
    public:
        virtual GraphicsBackend getBackend() const = 0;
        virtual LoggingConfig getConfig() const = 0;
        virtual bool shouldLogChannel(LogChannel channel, LogLevel level) const = 0;
        virtual void logChannel(LogChannel channel, LogLevel level, eastl::string_view category, eastl::string_view message) = 0;
        bool shouldLog(LogLevel level) const
        {
            return shouldLogChannel(LogChannel::General, level);
        }
        void log(LogLevel level, eastl::string_view category, eastl::string_view message)
        {
            logChannel(LogChannel::General, level, category, message);
        }
        virtual void flush() = 0;
        virtual ~LoggerImpl() = default;
    };

    using Logger = eastl::intrusive_ptr<LoggerImpl>;

    class InstanceImpl
    {
    public:
        virtual GraphicsBackend getBackend() const = 0;
        virtual Device createDevice() = 0;
        virtual Swapchain createSwapchain(const SwapchainDescriptor &descriptor) = 0;
        virtual void setLoggingConfig(const LoggingConfig &config) = 0;
        virtual LoggingConfig getLoggingConfig() const = 0;
        virtual Logger getLogger() const = 0;
        virtual void destroy() = 0;
        virtual ~InstanceImpl() = default;
    };

    class DeviceImpl
    {
    public:
        // virtual createQueue() = 0;
        virtual Queue getMainQueue() const = 0;
        virtual TimestampQuerySupport getTimestampQuerySupport() const = 0;
        virtual PassCounterQuerySupport getPassCounterQuerySupport() const
        {
            PassCounterQuerySupport support = {};
            support.supported = False;
            support.backendName = "unknown";
            support.unsupportedReason = "Pass counter queries are not implemented by this device.";
            return support;
        }
        /// Returns memory topology for the selected device so upper layers can choose upload strategies.
        virtual DeviceMemoryProperties getMemoryProperties() const = 0;
        /// Returns the runtime diagnostics overlay configuration captured when the instance was created.
        virtual RuntimeDiagnosticsOverlayConfig getDiagnosticsOverlayConfig() const = 0;
        /// Returns a live-resource snapshot for the runtime diagnostics overlay without forcing GPU synchronization.
        virtual DiagnosticsResourceSnapshot getDiagnosticsResourceSnapshot() const = 0;
        virtual Buffer createBuffer(const BufferDescriptor &descriptor) = 0;
        virtual QuerySet createQuerySet(const QuerySetDescriptor &descriptor) = 0;
        virtual Texture createTexture(const TextureDescriptor &descriptor) = 0;
        virtual ComputePipeline createComputePipeline(const ComputePipelineDescriptor &descriptor) = 0;
        virtual RenderPipeline createRenderPipeline(const RenderPipelineDescriptor &descriptor) = 0;
        virtual ShaderModule createShaderModule(const ShaderModuleDescriptor &descriptor) = 0;
        virtual BindGroupLayout createBindGroupLayout(const BindGroupLayoutDescriptor &descriptor) = 0;
        virtual PipelineLayout createPipelineLayout(const PipelineLayoutDescriptor &descriptor) = 0;
        virtual BindGroup createBindGroup(const BindGroupDescriptor &descriptor) = 0;
        virtual Sampler createSampler(const SamplerDescriptor &descriptor) = 0;
        virtual Logger getLogger() const = 0;

        // Lifetime contract:
        // GVMRHI allows user-side code to end a resource's logical lifetime immediately via
        // freeBuffer/freeTexture/freeSampler as soon as that resource is no longer needed by
        // future API calls. Backend implementations must treat these calls as retirement
        // requests, not as permission to physically destroy the native object synchronously
        // while in-flight GPU work may still reference it.
        //
        // Backends that do not get automatic driver/runtime retention must implement explicit
        // deferred destruction (for example submit serials, fences, frame retirement queues,
        // or equivalent hazard tracking) behind these entry points.
        virtual void freeBuffer(Buffer buffer) = 0;
        virtual void freeTexture(Texture texture) = 0;
        virtual void freeSampler(Sampler sampler) = 0;

        virtual void destroy() = 0;
        virtual ~DeviceImpl() = default;
    };

    class QueueImpl
    {
    public:
        virtual CommandEncoder createCommandEncoder() = 0;
        virtual void writeBuffer(BufferRange buffer, void const *data, uint64_t size) = 0;
        virtual void readBuffer(BufferRange buffer, void *data, uint64_t size) = 0;
        virtual void writeTexture(const ImageCopyTexture &destination, void const *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &writeSize) = 0;
        virtual void readTexture(const ImageCopyTexture &source, void *data, uint64_t dataSize, const TextureDataLayout &dataLayout, const Extent3D &readSize) = 0;
        virtual void uploadTexture(Texture destination, void const *data, uint64_t dataStorageBytes, const eastl::vector<uint64_t> &mipmapOffsetBytes) = 0;
        virtual void uploadTexture(Texture destination, GVM::RHI::BufferRange bufferRange, const eastl::vector<uint64_t> &mipmapOffsetBytes) = 0;
        virtual void copyBufferToBuffer(BufferRange source, BufferRange destination) = 0;
        virtual void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) = 0;
        virtual void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) = 0;
        virtual void copyBufferToBufferMultipleRegion(Buffer source, Buffer destination, Buffer regions, uint32_t regionCount) = 0;
        virtual void fillBuffer(BufferRange source, uint32_t data) = 0;
        virtual void submit(const eastl::vector<CommandEncoder> &encoders) = 0;
        virtual void destroy() = 0;
        virtual ~QueueImpl() = default;
    };

    class SwapchainImpl
    {
    public:
        virtual MultipleElements<TextureFormat> getSupportedFormats() const = 0;
        virtual TextureFormat getPreferredFormat() const = 0;
        virtual SwapchainQueryResult queryNextTexture() = 0;
        virtual void present() = 0;
        virtual void destroy() = 0;
        virtual ~SwapchainImpl() = default;
    };
    class BlitPassEncoderImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual void copyBufferToBuffer(BufferRange source, BufferRange destination) = 0;

        virtual void copyBufferToTexture(const ImageCopyBuffer &source, const ImageCopyTexture &destination, const Extent3D &copySize) = 0;
        virtual void copyTextureToBuffer(const ImageCopyTexture &source, const ImageCopyBuffer &destination, const Extent3D &copySize) = 0;
        virtual void fillBuffer(BufferRange source, uint32_t data) = 0;
        virtual void end() = 0;
        virtual ~BlitPassEncoderImpl() = default;
    };

    class ComputePassEncoderImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual void setPipeline(ComputePipeline pipeline) = 0;
        virtual void setBindGroup(BindGroup group, uint32_t groupIndex) = 0;
        virtual void dispatchWorkgroups(uint32_t x, uint32_t y, uint32_t z) = 0;
        virtual void dispatchWorkgroupsIndirect(BufferRange indirectBuffer) = 0;
        virtual void end() = 0;
        virtual ~ComputePassEncoderImpl() = default;
    };

    class RenderPassEncoderImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void setViewport(float x, float y, float width, float height, float minDepth, float maxDepth) = 0;
        virtual void drawFullscreenTexture(Texture source, const RenderToSwapchainDescriptor &descriptor = {}) = 0;
        virtual void setPipeline(RenderPipeline pipeline) = 0;
        virtual void setBindGroup(BindGroup group, uint32_t groupIndex) = 0;
        virtual void setVertexBuffer(BufferRange buffer, uint32_t slot) = 0;
        virtual void setIndexBuffer(BufferRange buffer, IndexFormat format) = 0;
        virtual void draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) = 0;
        virtual void drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) = 0;
        virtual void drawIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) = 0;
        virtual void drawIndexedIndirect(BufferRange indirectBuffer, uint32_t indirectCommandCount, uint32_t stride) = 0;
        virtual void drawPixels() = 0;
        virtual void nextPixelLocalPass() = 0;
        virtual void end() = 0;
        virtual ~RenderPassEncoderImpl() = default;
    };

    class CommandEncoderImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual void begin() = 0;
        virtual RenderPassEncoder beginRenderPass(const RenderPassDescriptor &pass) = 0;
        virtual BlitPassEncoder beginBlitPass(const BlitPassDescriptor &pass) = 0;
        virtual ComputePassEncoder beginComputePass(const ComputePassDescriptor &pass) = 0;
        virtual void resolveQuerySet(QuerySet querySet, uint32_t firstQuery, uint32_t queryCount, BufferRange destination) = 0;
        virtual void end() = 0;
        // virtual void destroy() = 0;
        virtual ~CommandEncoderImpl() = default;
    };

    class QuerySetImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual QueryType getType() const = 0;
        virtual uint32_t getCount() const = 0;
        virtual uint32_t getResultStrideBytes() const { return sizeof(uint64_t); }
        virtual ~QuerySetImpl() = default;
    };

    class ShaderModuleImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~ShaderModuleImpl() = default;
    };

    class BufferImpl : public ResourceObject
    {
    public:
        virtual uint64_t getStorageSize() const = 0;
        // Buffers that expose CPU-visible memory must follow a strict map -> access range -> unmap
        // sequence. Backends are allowed to throw on getMappedRange/getConstMappedRange when the
        // buffer is not currently mapped.
        virtual void map() = 0;
        virtual void const *getConstMappedRange(uint64_t offset, uint64_t size) const = 0;
        virtual void *getMappedRange(uint64_t offset, uint64_t size) const = 0;
        virtual void unmap() = 0;
        // Backend teardown hook. This is expected to run only when the backend/device has
        // decided the native buffer is safe to retire. User-facing code should request
        // retirement through Device::freeBuffer rather than assuming immediate physical free.
        virtual void destroy() = 0;
        virtual ~BufferImpl() = default;
    };

    inline GVM::RHI::BufferRange::BufferRange(Buffer buffer, uint64_t offset, uint64_t size)
        : buffer(buffer)
        , offset(offset)
        , size(size)
    {
        const uint64_t bufferSize = buffer->getStorageSize();
        const uint64_t remainingSize = offset >= bufferSize ? 0u : (bufferSize - offset);
        if (this->size == WholeSize || this->size > remainingSize)
        {
            this->size = remainingSize;
        }
    }

    class GpuTimestampFrameProfiler
    {
    public:
        struct Scope
        {
            eastl::string label;
            PassTimestampWrites timestampWrites = {};
            PassCounterWrites counterWrites = {};
        };

        void init(Device device, uint32_t maxScopes, const eastl::string &label = "GpuTimestampFrameProfiler")
        {
            if (device == nullptr)
            {
                throw std::invalid_argument("GpuTimestampFrameProfiler::init requires a valid device.");
            }
            if (maxScopes == 0u)
            {
                throw std::invalid_argument("GpuTimestampFrameProfiler::init requires at least one scope.");
            }
            if (maxScopes > QuerySetIndexUndefined / 2u)
            {
                throw std::out_of_range("GpuTimestampFrameProfiler::init maxScopes is too large.");
            }

            mDevice = device;
            mSupport = device->getTimestampQuerySupport();
            if (mSupport.supported == False || mSupport.passTimestampWritesSupported == False)
            {
                throw std::runtime_error("GpuTimestampFrameProfiler::init requires pass timestamp query support.");
            }

            mMaxScopes = maxScopes;
            mNextQuery = 0u;
            mScopes.clear();
            mScopes.reserve(maxScopes);

            QuerySetDescriptor querySetDescriptor = {};
            querySetDescriptor.label = label + ".queries";
            querySetDescriptor.type = QueryType::Timestamp;
            querySetDescriptor.count = maxScopes * 2u;
            mQuerySet = device->createQuerySet(querySetDescriptor);

            BufferDescriptor bufferDescriptor = {};
            bufferDescriptor.label = label + ".resolve";
            bufferDescriptor.usage = BufferUsage::QueryResolve | BufferUsage::CopySrc;
            bufferDescriptor.size = static_cast<uint64_t>(querySetDescriptor.count) * sizeof(uint64_t);
            mResolveBuffer = device->createBuffer(bufferDescriptor);
        }

        void reset()
        {
            mNextQuery = 0u;
            mScopes.clear();
        }

        Scope writePass(const eastl::string &label)
        {
            if (mQuerySet == nullptr)
            {
                throw std::logic_error("GpuTimestampFrameProfiler::writePass was called before init.");
            }
            if (mScopes.size() >= mMaxScopes)
            {
                throw std::out_of_range("GpuTimestampFrameProfiler::writePass exceeded maxScopes.");
            }

            Scope scope = {};
            scope.label = label;
            scope.timestampWrites.querySet = mQuerySet;
            scope.timestampWrites.beginningOfPassWriteIndex = mNextQuery++;
            scope.timestampWrites.endOfPassWriteIndex = mNextQuery++;
            mScopes.push_back(scope);
            return scope;
        }

        void resolve(CommandEncoder encoder)
        {
            if (encoder == nullptr)
            {
                throw std::invalid_argument("GpuTimestampFrameProfiler::resolve requires a valid command encoder.");
            }
            if (mQuerySet == nullptr || mResolveBuffer.isNull())
            {
                throw std::logic_error("GpuTimestampFrameProfiler::resolve was called before init.");
            }
            if (mNextQuery == 0u)
            {
                return;
            }

            encoder->resolveQuerySet(mQuerySet, 0u, mNextQuery, BufferRange(mResolveBuffer, 0u, static_cast<uint64_t>(mNextQuery) * sizeof(uint64_t)));
        }

        void readbackBlocking(Queue queue, eastl::vector<TimestampRawResult> &outResults)
        {
            if (queue == nullptr)
            {
                throw std::invalid_argument("GpuTimestampFrameProfiler::readbackBlocking requires a valid queue.");
            }
            if (mResolveBuffer.isNull())
            {
                throw std::logic_error("GpuTimestampFrameProfiler::readbackBlocking was called before init.");
            }

            outResults.clear();
            outResults.resize(mNextQuery);
            if (mNextQuery == 0u)
            {
                return;
            }

            static_assert(sizeof(TimestampRawResult) == sizeof(uint64_t));
            const uint64_t readbackBytes = static_cast<uint64_t>(mNextQuery) * sizeof(TimestampRawResult);
            queue->readBuffer(BufferRange(mResolveBuffer, 0u, readbackBytes), outResults.data(), readbackBytes);
            eastl::vector<CommandEncoder> noUserEncoders;
            queue->submit(noUserEncoders);
        }

        eastl::vector<TimestampRangeResult> buildRangeResults(const eastl::vector<TimestampRawResult> &rawResults) const
        {
            eastl::vector<TimestampRangeResult> results;
            results.reserve(mScopes.size());
            for (const Scope &scope : mScopes)
            {
                const uint32_t beginIndex = scope.timestampWrites.beginningOfPassWriteIndex;
                const uint32_t endIndex = scope.timestampWrites.endOfPassWriteIndex;
                if (beginIndex >= rawResults.size() || endIndex >= rawResults.size())
                {
                    continue;
                }

                TimestampRangeResult result = {};
                result.label = scope.label;
                result.beginIndex = beginIndex;
                result.endIndex = endIndex;
                result.beginRaw = rawResults[beginIndex].value;
                result.endRaw = rawResults[endIndex].value;
                result.durationNs = calculateTimestampDurationNs(mSupport, result.beginRaw, result.endRaw);
                results.push_back(result);
            }
            return results;
        }

        QuerySet getQuerySet() const
        {
            return mQuerySet;
        }

        Buffer getResolveBuffer() const
        {
            return mResolveBuffer;
        }

        uint32_t getUsedQueryCount() const
        {
            return mNextQuery;
        }

        const TimestampQuerySupport &getSupport() const
        {
            return mSupport;
        }

        const eastl::vector<Scope> &getScopes() const
        {
            return mScopes;
        }

    private:
        Device mDevice = nullptr;
        QuerySet mQuerySet = nullptr;
        Buffer mResolveBuffer;
        TimestampQuerySupport mSupport = {};
        eastl::vector<Scope> mScopes;
        uint32_t mMaxScopes = 0u;
        uint32_t mNextQuery = 0u;
    };

    class GpuPassCounterFrameProfiler
    {
    public:
        struct Scope
        {
            eastl::string label;
            PassCounterWrites counterWrites = {};
        };

        void init(Device device, uint32_t maxScopes, const eastl::string &label = "GpuPassCounterFrameProfiler")
        {
            if (device == nullptr)
            {
                throw std::invalid_argument("GpuPassCounterFrameProfiler::init requires a valid device.");
            }
            if (maxScopes == 0u)
            {
                throw std::invalid_argument("GpuPassCounterFrameProfiler::init requires at least one scope.");
            }
            if (maxScopes > QuerySetIndexUndefined / 2u)
            {
                throw std::out_of_range("GpuPassCounterFrameProfiler::init maxScopes is too large.");
            }

            mDevice = device;
            mSupport = device->getPassCounterQuerySupport();
            if (mSupport.supported == False)
            {
                eastl::string message = "GpuPassCounterFrameProfiler::init requires pass counter query support.";
                if (mSupport.unsupportedReason != nullptr && mSupport.unsupportedReason[0] != '\0')
                {
                    message += " ";
                    message += mSupport.unsupportedReason;
                }
                throw std::runtime_error(message.c_str());
            }

            mMaxScopes = maxScopes;
            mNextQuery = 0u;
            mScopes.clear();
            mScopes.reserve(maxScopes);

            QuerySetDescriptor stageDescriptor = {};
            stageDescriptor.label = label + ".stage_utilization";
            stageDescriptor.type = QueryType::PassCounterStageUtilization;
            stageDescriptor.count = maxScopes * 2u;
            mStageUtilizationQuerySet = device->createQuerySet(stageDescriptor);

            QuerySetDescriptor statisticDescriptor = {};
            statisticDescriptor.label = label + ".statistics";
            statisticDescriptor.type = QueryType::PassCounterStatistic;
            statisticDescriptor.count = maxScopes * 2u;
            mStatisticQuerySet = device->createQuerySet(statisticDescriptor);

            BufferDescriptor stageBufferDescriptor = {};
            stageBufferDescriptor.label = label + ".stage_utilization.resolve";
            stageBufferDescriptor.usage = BufferUsage::QueryResolve | BufferUsage::CopySrc;
            stageBufferDescriptor.size = static_cast<uint64_t>(stageDescriptor.count) * sizeof(PassCounterStageUtilizationRawResult);
            mStageUtilizationResolveBuffer = device->createBuffer(stageBufferDescriptor);

            BufferDescriptor statisticBufferDescriptor = {};
            statisticBufferDescriptor.label = label + ".statistics.resolve";
            statisticBufferDescriptor.usage = BufferUsage::QueryResolve | BufferUsage::CopySrc;
            statisticBufferDescriptor.size = static_cast<uint64_t>(statisticDescriptor.count) * sizeof(PassCounterStatisticRawResult);
            mStatisticResolveBuffer = device->createBuffer(statisticBufferDescriptor);
        }

        void reset()
        {
            mNextQuery = 0u;
            mScopes.clear();
        }

        Scope writePass(const eastl::string &label)
        {
            if (mStageUtilizationQuerySet == nullptr || mStatisticQuerySet == nullptr)
            {
                throw std::logic_error("GpuPassCounterFrameProfiler::writePass was called before init.");
            }
            if (mScopes.size() >= mMaxScopes)
            {
                throw std::out_of_range("GpuPassCounterFrameProfiler::writePass exceeded maxScopes.");
            }

            Scope scope = {};
            scope.label = label;
            scope.counterWrites.stageUtilizationQuerySet = mStageUtilizationQuerySet;
            scope.counterWrites.stageUtilizationBeginIndex = mNextQuery++;
            scope.counterWrites.stageUtilizationEndIndex = mNextQuery++;
            scope.counterWrites.statisticQuerySet = mStatisticQuerySet;
            scope.counterWrites.statisticBeginIndex = scope.counterWrites.stageUtilizationBeginIndex;
            scope.counterWrites.statisticEndIndex = scope.counterWrites.stageUtilizationEndIndex;
            mScopes.push_back(scope);
            return scope;
        }

        void resolve(CommandEncoder encoder)
        {
            if (encoder == nullptr)
            {
                throw std::invalid_argument("GpuPassCounterFrameProfiler::resolve requires a valid command encoder.");
            }
            if (mStageUtilizationQuerySet == nullptr || mStatisticQuerySet == nullptr ||
                mStageUtilizationResolveBuffer.isNull() || mStatisticResolveBuffer.isNull())
            {
                throw std::logic_error("GpuPassCounterFrameProfiler::resolve was called before init.");
            }
            if (mNextQuery == 0u)
            {
                return;
            }

            encoder->resolveQuerySet(
                mStageUtilizationQuerySet,
                0u,
                mNextQuery,
                BufferRange(
                    mStageUtilizationResolveBuffer,
                    0u,
                    static_cast<uint64_t>(mNextQuery) * sizeof(PassCounterStageUtilizationRawResult)));
            encoder->resolveQuerySet(
                mStatisticQuerySet,
                0u,
                mNextQuery,
                BufferRange(
                    mStatisticResolveBuffer,
                    0u,
                    static_cast<uint64_t>(mNextQuery) * sizeof(PassCounterStatisticRawResult)));
        }

        void readbackBlocking(
            Queue queue,
            eastl::vector<PassCounterStageUtilizationRawResult> &outStageResults,
            eastl::vector<PassCounterStatisticRawResult> &outStatisticResults)
        {
            if (queue == nullptr)
            {
                throw std::invalid_argument("GpuPassCounterFrameProfiler::readbackBlocking requires a valid queue.");
            }
            if (mStageUtilizationResolveBuffer.isNull() || mStatisticResolveBuffer.isNull())
            {
                throw std::logic_error("GpuPassCounterFrameProfiler::readbackBlocking was called before init.");
            }

            outStageResults.clear();
            outStatisticResults.clear();
            outStageResults.resize(mNextQuery);
            outStatisticResults.resize(mNextQuery);
            if (mNextQuery == 0u)
            {
                return;
            }

            const uint64_t stageReadbackBytes =
                static_cast<uint64_t>(mNextQuery) * sizeof(PassCounterStageUtilizationRawResult);
            const uint64_t statisticReadbackBytes =
                static_cast<uint64_t>(mNextQuery) * sizeof(PassCounterStatisticRawResult);
            queue->readBuffer(BufferRange(mStageUtilizationResolveBuffer, 0u, stageReadbackBytes), outStageResults.data(), stageReadbackBytes);
            queue->readBuffer(BufferRange(mStatisticResolveBuffer, 0u, statisticReadbackBytes), outStatisticResults.data(), statisticReadbackBytes);
            eastl::vector<CommandEncoder> noUserEncoders;
            queue->submit(noUserEncoders);
        }

        eastl::vector<PassCounterRangeResult> buildRangeResults(
            const eastl::vector<PassCounterStageUtilizationRawResult> &stageRawResults,
            const eastl::vector<PassCounterStatisticRawResult> &statisticRawResults) const
        {
            eastl::vector<PassCounterRangeResult> results;
            results.reserve(mScopes.size());
            for (const Scope &scope : mScopes)
            {
                const uint32_t stageBeginIndex = scope.counterWrites.stageUtilizationBeginIndex;
                const uint32_t stageEndIndex = scope.counterWrites.stageUtilizationEndIndex;
                const uint32_t statisticBeginIndex = scope.counterWrites.statisticBeginIndex;
                const uint32_t statisticEndIndex = scope.counterWrites.statisticEndIndex;
                if (stageBeginIndex >= stageRawResults.size() || stageEndIndex >= stageRawResults.size() ||
                    statisticBeginIndex >= statisticRawResults.size() || statisticEndIndex >= statisticRawResults.size())
                {
                    continue;
                }

                PassCounterRangeResult result = {};
                result.label = scope.label;
                result.stageBeginIndex = stageBeginIndex;
                result.stageEndIndex = stageEndIndex;
                result.statisticBeginIndex = statisticBeginIndex;
                result.statisticEndIndex = statisticEndIndex;

#define GVM_PASS_COUNTER_DELTA(fieldName) stageRawResults[stageEndIndex].fieldName - stageRawResults[stageBeginIndex].fieldName
                result.stage.totalCycles = GVM_PASS_COUNTER_DELTA(totalCycles);
                result.stage.vertexCycles = GVM_PASS_COUNTER_DELTA(vertexCycles);
                result.stage.tessellationCycles = GVM_PASS_COUNTER_DELTA(tessellationCycles);
                result.stage.postTessellationVertexCycles = GVM_PASS_COUNTER_DELTA(postTessellationVertexCycles);
                result.stage.fragmentCycles = GVM_PASS_COUNTER_DELTA(fragmentCycles);
                result.stage.renderTargetCycles = GVM_PASS_COUNTER_DELTA(renderTargetCycles);
#undef GVM_PASS_COUNTER_DELTA

#define GVM_PASS_COUNTER_DELTA(fieldName) statisticRawResults[statisticEndIndex].fieldName - statisticRawResults[statisticBeginIndex].fieldName
                result.statistic.tessellationInputPatches = GVM_PASS_COUNTER_DELTA(tessellationInputPatches);
                result.statistic.vertexInvocations = GVM_PASS_COUNTER_DELTA(vertexInvocations);
                result.statistic.postTessellationVertexInvocations = GVM_PASS_COUNTER_DELTA(postTessellationVertexInvocations);
                result.statistic.clipperInvocations = GVM_PASS_COUNTER_DELTA(clipperInvocations);
                result.statistic.clipperPrimitivesOut = GVM_PASS_COUNTER_DELTA(clipperPrimitivesOut);
                result.statistic.fragmentInvocations = GVM_PASS_COUNTER_DELTA(fragmentInvocations);
                result.statistic.fragmentsPassed = GVM_PASS_COUNTER_DELTA(fragmentsPassed);
                result.statistic.computeKernelInvocations = GVM_PASS_COUNTER_DELTA(computeKernelInvocations);
#undef GVM_PASS_COUNTER_DELTA

                results.push_back(result);
            }
            return results;
        }

        QuerySet getStageUtilizationQuerySet() const
        {
            return mStageUtilizationQuerySet;
        }

        QuerySet getStatisticQuerySet() const
        {
            return mStatisticQuerySet;
        }

        Buffer getStageUtilizationResolveBuffer() const
        {
            return mStageUtilizationResolveBuffer;
        }

        Buffer getStatisticResolveBuffer() const
        {
            return mStatisticResolveBuffer;
        }

        uint32_t getUsedQueryCount() const
        {
            return mNextQuery;
        }

        const PassCounterQuerySupport &getSupport() const
        {
            return mSupport;
        }

        const eastl::vector<Scope> &getScopes() const
        {
            return mScopes;
        }

    private:
        Device mDevice = nullptr;
        QuerySet mStageUtilizationQuerySet = nullptr;
        QuerySet mStatisticQuerySet = nullptr;
        Buffer mStageUtilizationResolveBuffer;
        Buffer mStatisticResolveBuffer;
        PassCounterQuerySupport mSupport = {};
        eastl::vector<Scope> mScopes;
        uint32_t mMaxScopes = 0u;
        uint32_t mNextQuery = 0u;
    };

    class BindGroupLayoutImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~BindGroupLayoutImpl() = default;
    };

    class BindGroupImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~BindGroupImpl() = default;
    };

    class PipelineLayoutImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~PipelineLayoutImpl() = default;
    };

    class ComputePipelineImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~ComputePipelineImpl() = default;
    };

    class RenderPipelineImpl : public ResourceObject, public RefCountedObject
    {
    public:
        virtual ~RenderPipelineImpl() = default;
    };

    class SamplerImpl : public ResourceObject
    {
    public:
        virtual ~SamplerImpl() = default;
    };

    class TextureImpl : public ResourceObject
    {
    public:
        virtual TextureView createView(const TextureViewDescriptor &descriptor) = 0;
        virtual TextureView createView() = 0;
        [[nodiscard]]
        virtual uint32_t getWidth() const = 0;
        [[nodiscard]]
        virtual uint32_t getHeight() const = 0;
        [[nodiscard]]
        virtual uint32_t getDepth() const = 0;
        [[nodiscard]]
        virtual uint32_t getMipLevelCount() const = 0;
        [[nodiscard]]
        virtual uint32_t getArrayLayerCount() const = 0;
        [[nodiscard]]
        virtual TextureFormat getFormat() const = 0;
        // Backend teardown hook. As with BufferImpl::destroy, this is part of the backend's
        // retirement path after Device::freeTexture has determined the native texture can be
        // released safely.
        virtual void destroy() = 0;
        virtual ~TextureImpl() = default;
    };

    class TextureViewImpl : public ResourceObject
    {
    public:
        /// Returns the concrete pixel format used by this view for pipeline attachment compatibility.
        [[nodiscard]]
        virtual TextureFormat getFormat() const = 0;
        /// Returns the mip-adjusted width exposed by this view for attachment layout decisions.
        [[nodiscard]]
        virtual uint32_t getWidth() const = 0;
        /// Returns the mip-adjusted height exposed by this view for attachment layout decisions.
        [[nodiscard]]
        virtual uint32_t getHeight() const = 0;
        // Backend teardown hook for view-side native objects owned by a retired texture/view.
        virtual void destroy() = 0;
        virtual ~TextureViewImpl() = default;
    };
    Instance createInstance(const InstanceDescriptor &descriptor = {});
    void setLoggingConfig(Instance instance, const LoggingConfig &config);
    LoggingConfig getLoggingConfig(Instance instance);
    inline Logger getLogger(Instance instance)
    {
        return instance != nullptr ? instance->getLogger() : Logger{};
    }
    inline Logger getDeviceLogger(Device device)
    {
        return device != nullptr ? device->getLogger() : Logger{};
    }
    inline bool shouldLog(const Logger &logger, LogLevel level)
    {
        return logger != nullptr && logger->shouldLog(level);
    }
    inline bool shouldLogChannel(const Logger &logger, LogChannel channel, LogLevel level)
    {
        return logger != nullptr && logger->shouldLogChannel(channel, level);
    }
    inline void flushLogger(const Logger &logger)
    {
        if (logger != nullptr)
        {
            logger->flush();
        }
    }
    inline void logMessage(
        const Logger &logger,
        LogLevel level,
        eastl::string_view category,
        eastl::string_view message)
    {
        if (logger != nullptr)
        {
            logger->log(level, category, message);
        }
    }
    inline void logChannelMessage(
        const Logger &logger,
        LogChannel channel,
        LogLevel level,
        eastl::string_view category,
        eastl::string_view message)
    {
        if (logger != nullptr)
        {
            logger->logChannel(channel, level, category, message);
        }
    }
    inline void logTraceMessage(const Logger &logger, eastl::string_view category, eastl::string_view message)
    {
        logMessage(logger, LogLevel::Trace, category, message);
    }
    inline void logDebugMessage(const Logger &logger, eastl::string_view category, eastl::string_view message)
    {
        logMessage(logger, LogLevel::Debug, category, message);
    }
    inline void logInfoMessage(const Logger &logger, eastl::string_view category, eastl::string_view message)
    {
        logMessage(logger, LogLevel::Info, category, message);
    }
    inline void logWarnMessage(const Logger &logger, eastl::string_view category, eastl::string_view message)
    {
        logMessage(logger, LogLevel::Warn, category, message);
    }
    inline void logErrorMessage(const Logger &logger, eastl::string_view category, eastl::string_view message)
    {
        logMessage(logger, LogLevel::Error, category, message);
    }
    void destroyInstance(Instance instance);
} // namespace GVM::RHI
