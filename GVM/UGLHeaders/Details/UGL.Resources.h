#pragma once
#include "UGL.Base.h"
#include "UGL.Format.h"
#include "UGL.Types.h"
namespace UGL
{
    // DSL resource contract:
    // - User-authored DSL code must not treat resources as raw pointers or introduce pointer-typed
    //   state around resource access.
    // - The only pointer semantics allowed in user-authored DSL code is the implicit `this`
    //   of the current class member function.
    // - Any raw pointers involved in host/runtime mapping are backend interop details rather than
    //   part of the DSL surface language.

    struct MapRead
    {
    };

    struct MapWrite
    {
    };

    struct CopySrc
    {
    };

    struct CopyDst
    {
    };

    struct Index
    {
    };

    struct Vertex
    {
    };

    struct Uniform
    {
    };

    struct Storage
    {
    };

    struct Indirect
    {
    };

    template <class... Args>
    class BufferUsage : public Args...
    {
    };

    template <typename T>
    concept IsStorageBuffer = std::is_base_of_v<Storage, T>;

    template <typename T>
    concept IsUniformBuffer = std::is_base_of_v<Uniform, T>;

    template <typename T>
    concept IsVertexBuffer = std::is_base_of_v<Vertex, T>;
    template <typename T>
    concept IsIndirectBuffer = std::is_base_of_v<Indirect, T>;
    template <typename T>
    concept IsIndexBuffer = std::is_base_of_v<Index, T>;
    template <typename T>
    concept IsCopyDstBuffer = std::is_base_of_v<CopyDst, T>;

    template <typename T>
    concept IsCopySrcBuffer = std::is_base_of_v<CopySrc, T>;

    template <typename T>
    concept IsCopyDstTexture = std::is_base_of_v<CopyDst, T>;

    template <typename T>
    concept IsCopySrcTexture = std::is_base_of_v<CopySrc, T>;

    template <class T, class Usages>
    class BufferImpl final : Usages
    {
        string label;
        uint64_t size;
        T fake_t;

    public:
        BufferImpl() = default;
        BufferImpl(string label, uint64_t size)
            : label(label)
            , size(size)
        {
        }

        uint64_t getStorageSize() const
        {
            return size;
        }

        // Contract note:
        // Runtime buffers must follow a strict map -> access range -> unmap sequence.
        // The range access ABI happens to be pointer-typed on the host side, but that does not
        // make raw pointers part of the DSL surface language. User-authored DSL code still follows
        // the "no raw pointers; only implicit this is allowed" contract.
        void map()
        {
        }

        // Contract note:
        // Only call this while the runtime buffer is in a mapped state. Pointer-typed host ABI
        // here is a runtime detail, not a DSL-language permission.
        const void *getConstMappedRange(uint64_t offset, uint64_t size) const
        {
            return nullptr;
        }

        // Contract note:
        // Only call this while the runtime buffer is in a mapped state. Pointer-typed host ABI
        // here is a runtime detail, not a DSL-language permission.
        void *getMappedRange(uint64_t offset, uint64_t size) const
        {
            return nullptr;
        }

        // Contract note:
        // Unmap immediately after CPU-side access completes.
        void unmap()
        {
        }
    };

    template <class T, class Usages>
    class Buffer final : Usages
    {
        BufferImpl<T, Usages> *impl;

    public:
        Buffer() = default;
        BufferImpl<T, Usages> *operator->()
        {
            return impl;
        }
    };

    template <class T>
    concept IsBufferCopyDst = std::is_base_of_v<CopyDst, T>;

    /* void writeBuffer(IsBufferCopyDst auto buffer, const void *data, uint64_t size)
    {
    } */
    template <class T, class Usages>
    class BufferRange : public Usages
    {
    public:
        BufferRange(Buffer<T, Usages> buffer, uint64_t offset = 0, uint64_t size = 0)
        {
        }
    };

    template <class T>
    class StructuredBuffer
    {
        T fake_t;

    public:
        StructuredBuffer<T> operator=(IsStorageBuffer auto b)
        {
            return StructuredBuffer<T>();
        }

        StructuredBuffer() = default;
        StructuredBuffer(IsStorageBuffer auto b)
        {
        }
        StructuredBuffer(const StructuredBuffer<T> &) = default;
        const T &operator[](uint64_t index) const
        {
            return fake_t;
        }
    };

    template <class T>
    class RWStructuredBuffer
    {
        T fake_t;

    public:
        RWStructuredBuffer<T> operator=(IsStorageBuffer auto b)
        {
            return RWStructuredBuffer<T>();
        }

        RWStructuredBuffer() = default;
        RWStructuredBuffer(IsStorageBuffer auto b)
        {
        }
        RWStructuredBuffer(const RWStructuredBuffer<T> &) = default;
        T &operator[](uint64_t index)
        {
            return fake_t;
        }
        const T &operator[](uint64_t index) const
        {
            return fake_t;
        }
    };

    namespace Private
    {
        template <class T>
        inline constexpr bool AlwaysFalse = false;
    }

    template <class T>
    class StorageBuffer
    {
        static_assert(Private::AlwaysFalse<T>,
                      "UGL::StorageBuffer<T> has been removed. Use UGL::StructuredBuffer<T> for read-only access or UGL::RWStructuredBuffer<T> for read-write access.");
    };

    namespace Private
    {
        // Primary template declaration
        template <class T, bool = std::is_class_v<T>>
        class UniformBufferDataPacker;

        // Partial specialization for class/struct types: inherits from T
        template <class T>
        class UniformBufferDataPacker<T, true> : public T
        {
            T fake_t;

        public:
            const T &read() const
            {
                return fake_t;
            }
            operator T() const
            {
                return fake_t;
            }
        };

        // Partial specialization for non-class/struct types: does not inherit from T
        template <class T>
        class UniformBufferDataPacker<T, false>
        {
            T fake_t;

        public:
            const T &read() const
            {
                return fake_t;
            }
            explicit operator T() const
            {
                return fake_t;
            }
        };
    } // namespace Private


    template <class T>
    class UniformBuffer //: public BufferRange
    {
        T fake_t;

    public:
        UniformBuffer<T> operator=(IsUniformBuffer auto b)
        {
            return BufferImpl<T, UGL::BufferUsage<UGL::Uniform>>();
        }
        UniformBuffer() = default;

        UniformBuffer(IsUniformBuffer auto b)
        {
        }
        const Private::UniformBufferDataPacker<T> *operator->()
        {
            return {};
        }
    };

    enum class AddressMode
    {
        Repeat = 0x00000000,
        MirrorRepeat = 0x00000001,
        ClampToEdge = 0x00000002,
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

    struct SamplerDescriptor
    {
        string label;
        AddressMode addressModeU;
        AddressMode addressModeV;
        AddressMode addressModeW;
        FilterMode magFilter;
        FilterMode minFilter;
        MipmapFilterMode mipmapFilter;
        float lodMinClamp;
        float lodMaxClamp;
        CompareFunction compare;
        uint16_t maxAnisotropy;
    };

    class Sampler
    {
    };

    struct RenderAttachment
    {
    };

    struct PixelLocalAttachment
    {
    };

    struct TextureBinding
    {
    };

    struct StorageBinding
    {
    };

    template <class... Args>
    class TextureUsage : public Args...
    {
    };

    template <typename T>
    concept IsStorageTexture = std::is_base_of_v<StorageBinding, T>;

    template <typename T>
    concept IsSampledTexture = std::is_base_of_v<TextureBinding, T>;

    template <typename T>
    concept IsRenderAttachment = std::is_base_of_v<RenderAttachment, T>;

    template <typename T>
    concept IsPixelLocalAttachment = std::is_base_of_v<PixelLocalAttachment, T>;

    namespace TextureDimension
    {
        struct e1D
        {
        };

        struct e2D
        {
        };

        struct e3D
        {
        };
    } // namespace TextureDimension

    template <typename T>
    concept IsTextureDimension1D = std::is_base_of_v<TextureDimension::e1D, T>;

    template <typename T>
    concept IsTextureDimension2D = std::is_base_of_v<TextureDimension::e2D, T>;

    template <typename T>
    concept IsTextureDimension3D = std::is_base_of_v<TextureDimension::e3D, T>;

    namespace Private
    {
        class ThisIsTextureView
        {
        };
    } // namespace Private

    template <typename T>
    concept IsTextureView = std::is_base_of_v<Private::ThisIsTextureView, T>;

    template <class T, class Usages, class Dimension>
    class TextureView final : Usages, Dimension, Private::ThisIsTextureView
    {
    public:
        using inner_type = T;
    };

    struct TextureViewDescriptor
    {
        string label;
        uint baseMipLevel = 0;
        uint mipLevelCount = 1;
        uint baseArrayLayer = 0;
        uint arrayLayerCount = 1;
    };

    template <class T, class Usages, class Dimension>
    class TextureImpl final : Usages, Dimension
    {
        string label;
        T fake_t;
        uint32_t mWidth;
        uint32_t mHeight;
        uint32_t mDepth = 1;
        uint32_t mMipLevelCount = 1;
        uint32_t mLayerCount = 1;

    public:
        TextureImpl() = default;
        TextureImpl(string label, uint32_t width, uint32_t height, uint32_t depth = 1, uint32_t mipLevelCount = 1, uint32_t layerCount = 1)
            : label(label)
            , mWidth(width)
        {
        }
        TextureView<T, Usages, Dimension> createView()
        {
            return TextureView<T, Usages, Dimension>();
        }
        TextureView<T, Usages, Dimension> createView(const TextureViewDescriptor &descriptor)
        {
            return TextureView<T, Usages, Dimension>();
        }

        [[nodiscard]]
        virtual uint32_t getWidth() const
        {
            return 1;
        }
        [[nodiscard]]
        virtual uint32_t getHeight() const
        {
            return 1;
        }
        [[nodiscard]]
        virtual uint32_t getDepth() const
        {
            return 1;
        }
        [[nodiscard]]
        virtual uint32_t getMipLevelCount() const
        {
            return 1;
        }
        [[nodiscard]]
        virtual uint32_t getArrayLayerCount() const
        {
            return 1;
        }
        virtual void destroy()
        {
        }
    };

    template <class T, class Usages, class Dimension>
    class Texture final : Usages, Dimension
    {
        TextureImpl<T, Usages, Dimension> *impl;

    public:
        TextureImpl<T, Usages, Dimension> *operator->()
        {
            return impl;
        }
    };


    namespace Private
    {
        // ==========================================
        // 类型萃取工具：用于推导 HLSL 风格的返回类型
        // ==========================================

        // 辅助：获取向量或标量的基础类型 (value_type)
        // 如果 T 有 value_type (如 glm::vec, float4)，则取之；否则取 T 本身
        // ==========================================
        // 修复后的类型萃取工具：兼容 Wrapper 和 Raw 类型
        // ==========================================

        // 1. 格式解包工具：
        // 如果 T 是 TextureFormat 包装器（有 TrueType），则取 TrueType；
        // 如果 T 已经是原生类型（如 float4），则取 T 本身。
        template <typename T>
        struct UnwrapFormatHelper
        {
            using Type = T;
        };

        template <typename T>
            requires requires { typename T::TrueType; }
        struct UnwrapFormatHelper<T>
        {
            using Type = typename T::TrueType;
        };

        template <typename T>
        using UnwrappedType = typename UnwrapFormatHelper<T>::Type;

        // 2. 标量提取工具：
        // 从 UnwrappedType 中提取基础标量（例如从 float4 提取 float，从 uint 提取 uint）
        template <typename T>
        struct GetScalarHelper
        {
            using Type = T;
        };

        // 如果是 GLM 向量或类似容器，取 value_type
        template <typename T>
            requires requires { typename T::value_type; }
        struct GetScalarHelper<T>
        {
            using Type = typename T::value_type;
        };

        // 组合 1 和 2：获取 T 的最终基础标量类型
        template <typename T>
        using ScalarComponentType = typename GetScalarHelper<UnwrappedType<T>>::Type;

        // 3. 概念定义 (基于基础标量)
        template <typename T>
        concept IsHalfFormat = std::is_same_v<half, ScalarComponentType<T>>;

        template <typename T>
        concept IsFloatFormat = std::is_floating_point_v<ScalarComponentType<T>>;

        template <typename T>
        concept IsUnsignedFormat = std::is_unsigned_v<ScalarComponentType<T>> && !IsFloatFormat<T>;

        template <typename T>
        concept IsSignedFormat = std::is_signed_v<ScalarComponentType<T>> && !IsFloatFormat<T>;

        // 4. HLSL 返回类型推导 (主模板声明)
        template <typename T, typename Enable = void>
        struct HLSLReturnTypeTrait;


        // 浮点型 -> half4
        template <typename T>
            requires IsHalfFormat<T>
        struct HLSLReturnTypeTrait<T>
        {
            using Type = half4;
        };

        // 浮点型 -> float4
        template <typename T>
            requires IsFloatFormat<T>
        struct HLSLReturnTypeTrait<T>
        {
            using Type = float4;
        };

        // 无符号整型 -> uint4
        template <typename T>
            requires IsUnsignedFormat<T>
        struct HLSLReturnTypeTrait<T>
        {
            using Type = uint4;
        };

        // 有符号整型 -> int4
        template <typename T>
            requires IsSignedFormat<T>
        struct HLSLReturnTypeTrait<T>
        {
            using Type = int4;
        };

        // 简化别名
        template <typename T>
        using GetTextureReturnType = typename HLSLReturnTypeTrait<T>::Type;
        //=======================================================================================

        template <class T>
        class BaseTexture2DAccessPacker
        {
            T fake_t;

        public:
            // 定义 HLSL 对应的返回类型
            using HLSLType = GetTextureReturnType<T>;

            // 修改返回值类型为 HLSLType
            HLSLType read(uint2 index, uint mipLevels = 0) const
            {
                // 这里实际应该做格式转换 (Format Conversion)
                // 例如将 uint8_t 扩展为 uint4(val, 0, 0, 1)
                // 目前仅返回默认构造的值以通过编译
                return HLSLType();
            }


            void getDimensions(uint width, uint height) const
            {
            }
        };

        template <class T>
        class Texture2DAccessPacker final : public BaseTexture2DAccessPacker<T>
        {
        public:
            // 引用基类中推导出的类型
            using HLSLType = typename BaseTexture2DAccessPacker<T>::HLSLType;

            HLSLType sample(Sampler s, float2 uv) const
            {
                return HLSLType();
            }
            HLSLType sampleLevel(Sampler s, float2 uv, float lod) const
            {
                return HLSLType();
            }
            HLSLType sampleGrad(Sampler s, float2 uv, float2 x, float2 y) const
            {
                return HLSLType();
            }

            HLSLType gather(Sampler s, float2 uv, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherRed(Sampler s, float2 uv, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherGreen(Sampler s, float2 uv, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherBlue(Sampler s, float2 uv, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherAlpha(Sampler s, float2 uv, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherCmp(Sampler s, float2 uv, HLSLType compareValue, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
        };
        template <class T>
        class RWTexture2DAccessPacker final : public Private::BaseTexture2DAccessPacker<T>
        {
            T fake_t;

        public:
            using HLSLType = typename Private::BaseTexture2DAccessPacker<T>::HLSLType;

            // Read 返回 HLSL 向量
            HLSLType read(uint2 index, uint mipLevels = 0) const
            {
                return HLSLType();
            }

            // Write 接受 HLSL 向量 (D 应该能隐式转换为 HLSLType)
            template <class D>
            void write(uint2 index, const D &value)
            {
                // 这里在真实实现中，需要将 value (例如 uint4)
                // 压缩回 T::TrueType (例如 uint8_t 或 uint2)
                // fake_t.setData(...);
            }

            void getDimensions(uint width, uint height) const
            {
            }
        };

        /**
         * Provides DSL-visible access methods for sampled 3D textures without carrying backend state.
         * Use this packer through `Texture3D<T>::operator->()` so UGLC can lower calls to native shader APIs.
         */
        template <class T>
        class Texture3DAccessPacker final
        {
        public:
            using HLSLType = GetTextureReturnType<T>;

            HLSLType read(uint3 index, uint mipLevel = 0) const
            {
                return HLSLType();
            }

            HLSLType sample(Sampler s, float3 uv) const
            {
                return HLSLType();
            }

            HLSLType sampleLevel(Sampler s, float3 uv, float lod) const
            {
                return HLSLType();
            }

            HLSLType sampleGrad(Sampler s, float3 uv, float3 x, float3 y) const
            {
                return HLSLType();
            }

            void getDimensions(uint width, uint height, uint depth) const
            {
            }
        };

        /**
         * Provides DSL-visible access methods for read-write 3D storage textures.
         * Use this packer through `RWTexture3D<T>::operator->()`; it intentionally exposes no gather or attachment operations.
         */
        template <class T>
        class RWTexture3DAccessPacker final
        {
        public:
            using HLSLType = GetTextureReturnType<T>;

            HLSLType read(uint3 index) const
            {
                return HLSLType();
            }

            template <class D>
            void write(uint3 index, const D &value)
            {
            }

            void getDimensions(uint width, uint height, uint depth) const
            {
            }
        };
    } // namespace Private


    template <class T>
    class Texture2D
    {
        T fake_t;

    public:
        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        Texture2D<T> operator=(U b)
        {
            return Texture2D<T>();
        }

        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        Texture2D(U b)
        {
        }

        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        Texture2D(Texture2D<U> b)
        {
        }

        Texture2D() = default;
        Private::Texture2DAccessPacker<T> *operator->()
        {
            return {};
        }
    };

    template <class T>
    class Texture3D
    {
        T fake_t;

    public:
        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension3D<U> && IsTextureView<U>)
        Texture3D<T> operator=(U b)
        {
            return Texture3D<T>();
        }

        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension3D<U> && IsTextureView<U>)
        Texture3D(U b)
        {
        }

        Texture3D() = default;

        Private::Texture3DAccessPacker<T> *operator->()
        {
            return {};
        }
    };


    template <class T>
    class RWTexture2D
    {
        T fake_t;

    public:
        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        RWTexture2D<T> operator=(U b)
        {
            return RWTexture2D<T>();
        }

        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        RWTexture2D(U b)
        {
        }

        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        RWTexture2D(RWTexture2D<U> b)
        {
        }

        Private::RWTexture2DAccessPacker<T> *operator->()
        {
            return {};
        }
        RWTexture2D() = default;
    };

    template <class T>
    class RWTexture3D
    {
        T fake_t;

    public:
        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension3D<U> && IsTextureView<U>)
        RWTexture3D<T> operator=(U b)
        {
            return RWTexture3D<T>();
        }

        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension3D<U> && IsTextureView<U>)
        RWTexture3D(U b)
        {
        }

        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        RWTexture3D(RWTexture3D<U> b)
        {
        }

        Private::RWTexture3DAccessPacker<T> *operator->()
        {
            return {};
        }

        RWTexture3D() = default;
    };


    /*  namespace Private
     {
         // 定义 Concept: 检查 Arr 是否像一个由 TextType 组成的数组
         template <typename Arr, typename TextType>
         concept IsTextureArray = requires(Arr a) {
             // 1. 必须有 size() 方法，且返回可转换为 size_t 的类型
             { a.size() } -> std::convertible_to<std::size_t>;

             // 2. 必须支持下标访问，且结果可以转换为 TextType
             // 注意：这里用 convertible_to 允许引用转换
             { a[0] } -> std::convertible_to<TextType>;
         };

     } */ // namespace Private
    //=======================================================================================
    namespace Private
    {
        template <class T>
        class BaseTexture2DArrayAccessPacker
        {
            T fake_t;

        public:
            // 定义 HLSL 对应的返回类型
            using HLSLType = GetTextureReturnType<T>;

            // 修改返回值类型为 HLSLType
            HLSLType read(uint2 index, uint layerIndex = 0, uint mipLevel = 0) const
            {
                // 这里实际应该做格式转换 (Format Conversion)
                // 例如将 uint8_t 扩展为 uint4(val, 0, 0, 1)
                // 目前仅返回默认构造的值以通过编译
                return HLSLType();
            }


            void getDimensions(uint width, uint height, uint layerCount) const
            {
            }
        };

        template <class T>
        class Texture2DArrayAccessPacker final : public BaseTexture2DArrayAccessPacker<T>
        {
        public:
            // 引用基类中推导出的类型
            using HLSLType = typename BaseTexture2DArrayAccessPacker<T>::HLSLType;

            HLSLType sample(Sampler s, float2 uv, uint layerIndex) const
            {
                return HLSLType();
            }
            HLSLType sampleLevel(Sampler s, float2 uv, uint layerIndex, float lod) const
            {
                return HLSLType();
            }
            HLSLType sampleGrad(Sampler s, float2 uv, uint layerIndex, float2 x, float2 y) const
            {
                return HLSLType();
            }

            HLSLType gather(Sampler s, float2 uv, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherRed(Sampler s, float2 uv, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherGreen(Sampler s, float2 uv, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherBlue(Sampler s, float2 uv, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherAlpha(Sampler s, float2 uv, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
            HLSLType gatherCmp(Sampler s, float2 uv, HLSLType compareValue, uint layerIndex, int2 offset = int2(0)) const
            {
                return HLSLType();
            }
        };
        template <class T>
        class RWTexture2DArrayAccessPacker final : public Private::BaseTexture2DArrayAccessPacker<T>
        {
            T fake_t;

        public:
            using HLSLType = typename Private::BaseTexture2DArrayAccessPacker<T>::HLSLType;

            // Read 返回 HLSL 向量
            HLSLType read(uint2 index, uint mipLevels = 0) const
            {
                return HLSLType();
            }

            // Write 接受 HLSL 向量 (D 应该能隐式转换为 HLSLType)
            template <class D>
            void write(uint2 index, uint layerIndex, const D &value)
            {
                // 这里在真实实现中，需要将 value (例如 uint4)
                // 压缩回 T::TrueType (例如 uint8_t 或 uint2)
                // fake_t.setData(...);
            }
        };
    } // namespace Private
    //--------------Bindless
    template <class T>
    struct Texture2DArray
    {

        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        Texture2DArray<T> operator=(U b)
        {
            return Texture2DArray<T>();
        }

        template <typename U>
            requires(IsSampledTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        Texture2DArray(U b)
        {
        }
        // ---------------------------------------------------------
        // 新增机制：赋值运算符
        // ---------------------------------------------------------
        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        Texture2DArray &operator=(const Texture2DArray<U> &other)
        {
            return *this;
        }

        // ---------------------------------------------------------
        // 可选：构造函数支持
        // ---------------------------------------------------------
        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        Texture2DArray(const Texture2DArray<U> &other)
        {
        }

        // 默认构造
        Texture2DArray() = default;
        Private::Texture2DArrayAccessPacker<T> *operator->()
        {
            return {};
        }
    };
    //--------------Bindless
    template <class T>
    struct RWTexture2DArray
    {

        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        RWTexture2DArray<T> operator=(U b)
        {
            return RWTexture2DArray<T>();
        }

        template <typename U>
            requires(IsStorageTexture<U> && IsTextureDimension2D<U> && IsTextureView<U>)
        RWTexture2DArray(U b)
        {
        }
        // ---------------------------------------------------------
        // 新增机制：赋值运算符
        // ---------------------------------------------------------
        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        RWTexture2DArray &operator=(const RWTexture2DArray<U> &other)
        {
            return *this;
        }

        // ---------------------------------------------------------
        // 可选：构造函数支持
        // ---------------------------------------------------------
        template <typename U>
            requires(std::is_same_v<T, typename U::TrueType>)
        RWTexture2DArray(const RWTexture2DArray<U> &other)
        {
        }

        // 默认构造
        RWTexture2DArray() = default;
        Private::RWTexture2DArrayAccessPacker<T> *operator->()
        {
            return {};
        }
    };

    //-------------------------


    namespace Private
    {

        class BufferProxy
        {
        public:
            template <typename T, typename U>
            operator Buffer<T, U>() const
            {
                return Buffer<T, U>();
            }
        };

        class TextureProxy
        {
        public:
            template <typename T, typename U, typename D>

            operator Texture<T, U, D>() const
            {
                return Texture<T, U, D>();
            }
        };
    } // namespace Private

    class IBindGroup
    {
    public:
        IBindGroup()
        {
        }

        void create()
        {
        }
    };
    template <typename T>
    concept IsIBindGroup = std::is_base_of_v<IBindGroup, T>;
    template <IsIBindGroup T>
    class BindGroup final
    {
        T *t = nullptr;
        T &operator*() = delete;

    public:
        T *operator->()
        {
            return t;
        }
    };

    // 1. 定义一个基础的结构体，默认值为 false
    template <typename T>
    struct is_bind_group_impl : std::false_type
    {
    };

    // 2. 利用模板偏特化，匹配 BindGroup<T> 的情况
    //    注意：这里的 T 必须符合 BindGroup 原本的约束 (IsIBindGroup)
    template <typename T>
    struct is_bind_group_impl<BindGroup<T>> : std::true_type
    {
    };

    // 3. 定义 Concept
    //    关键点：使用 std::remove_cvref_t 去除 const/volatile 和引用符号
    //    这样 setBindGroup(const BindGroup<T>&) 也能通过检查
    template <typename T>
    concept IsBindGroupWrapper = is_bind_group_impl<std::remove_cvref_t<T>>::value;

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

    enum class PixelLocalAccess
    {
        ReadOnly = 0x00000000,
        WriteOnly = 0x00000001,
        ReadWrite = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class PixelLocalStorage
    {
        Transient = 0x00000000,
        Persistent = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    enum class PixelLocalLoad
    {
        DontCare = 0x00000000,
        Clear = 0x00000001,
        Load = 0x00000002,
        Force32 = 0x7FFFFFFF
    };

    enum class PixelLocalStore
    {
        Discard = 0x00000000,
        Store = 0x00000001,
        Force32 = 0x7FFFFFFF
    };

    struct Color
    {
        double r;
        double g;
        double b;
        double a;
    };

    template <class T, class Other>
    concept HasExactInnerType = requires { typename Other::inner_type; } && std::same_as<typename Other::inner_type, T>;

    template <typename T>
    concept IsSwapchainPreferredFormat = std::same_as<T, TextureFormat::PreferredSwapchain>;

    template <typename T>
    concept IsSwapchainCompatibleColorAttachmentFormat =
        std::same_as<T, TextureFormat::RGBA8Unorm> ||
        std::same_as<T, TextureFormat::RGBA8UnormSrgb> ||
        std::same_as<T, TextureFormat::BGRA8Unorm> ||
        std::same_as<T, TextureFormat::BGRA8UnormSrgb>;

    template <class T, class Other>
    concept HasCompatibleColorAttachmentInnerType =
        requires { typename Other::inner_type; } &&
        (std::same_as<typename Other::inner_type, T> ||
         (IsSwapchainCompatibleColorAttachmentFormat<T> && IsSwapchainPreferredFormat<typename Other::inner_type>));

    template <typename T>
    struct ColorAttachment
    {
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        Color clearValue = {0, 0, 0.2, 1};
        using inner_type = T;

        template <class U>
            requires std::is_assignable_v<T &, U>
        ColorAttachment<T> operator=(U &&u)
        {
            return ColorAttachment<T>();
        }

        template <typename U>
            requires(IsRenderAttachment<U> && IsTextureDimension2D<U> && IsTextureView<U> && HasCompatibleColorAttachmentInnerType<T, U>)
        ColorAttachment<T> operator=(U t)
        {
            return ColorAttachment<T>();
        }
    };

    template <typename T,
              PixelLocalAccess Access,
              PixelLocalStorage Storage,
              PixelLocalLoad Load,
              PixelLocalStore Store>
    struct PixelLocalColorAttachment
    {
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        Color clearValue = {0, 0, 0.2, 1};
        using inner_type = T;
        static constexpr PixelLocalAccess access = Access;
        static constexpr PixelLocalStorage storage = Storage;
        static constexpr PixelLocalLoad load = Load;
        static constexpr PixelLocalStore store = Store;

        PixelLocalColorAttachment() = default;
        PixelLocalColorAttachment(const PixelLocalColorAttachment &) = delete;
        PixelLocalColorAttachment &operator=(const PixelLocalColorAttachment &) = delete;
        // Required for returning framebuffer structs by value; UGLC rejects direct attachment value moves.
        PixelLocalColorAttachment(PixelLocalColorAttachment &&) = default;
        PixelLocalColorAttachment &operator=(PixelLocalColorAttachment &&) = delete;

        template <class U>
            requires std::is_assignable_v<T &, U>
        PixelLocalColorAttachment operator=(U &&)
        {
            return PixelLocalColorAttachment();
        }

        template <typename U>
            requires(IsRenderAttachment<U> && IsPixelLocalAttachment<U> && IsTextureDimension2D<U> && IsTextureView<U> && HasCompatibleColorAttachmentInnerType<T, U>)
        PixelLocalColorAttachment operator=(U)
        {
            return PixelLocalColorAttachment();
        }

        typename T::TrueType read() const
        {
            return {};
        }
    };


    namespace DepthStencilAttachmentWritePattern
    {


        struct None
        {
            None &operator=(float &&) = delete;
        };
        struct Less
        {
            Less operator=(float &&t)
            {
                return Less();
            }
        };
        struct Greater
        {
            Greater operator=(float &&t)
            {
                return Greater();
            }
        };
    } // namespace DepthStencilAttachmentWritePattern
    template <typename T, class N = DepthStencilAttachmentWritePattern::None>
    struct DepthStencilAttachment : public N
    {
        using N::operator=;
        LoadOp depthLoadOp;
        StoreOp depthStoreOp;
        float depthClearValue;

        template <typename U>
            requires(IsRenderAttachment<U> && IsTextureDimension2D<U>)
        DepthStencilAttachment operator=(U t)
        {
            return DepthStencilAttachment();
        }
    };

    template <typename T,
              PixelLocalAccess Access,
              PixelLocalStorage Storage,
              PixelLocalLoad Load,
              PixelLocalStore Store,
              class N = DepthStencilAttachmentWritePattern::None>
    struct PixelLocalDepthAttachment : public N
    {
        using inner_type = T;
        static constexpr PixelLocalAccess access = Access;
        static constexpr PixelLocalStorage storage = Storage;
        static constexpr PixelLocalLoad load = Load;
        static constexpr PixelLocalStore store = Store;
        LoadOp depthLoadOp;
        StoreOp depthStoreOp;
        float depthClearValue;

        PixelLocalDepthAttachment() = default;
        PixelLocalDepthAttachment(const PixelLocalDepthAttachment &) = delete;
        PixelLocalDepthAttachment &operator=(const PixelLocalDepthAttachment &) = delete;
        // Required for returning framebuffer structs by value; UGLC rejects direct attachment value moves.
        PixelLocalDepthAttachment(PixelLocalDepthAttachment &&) = default;
        PixelLocalDepthAttachment &operator=(PixelLocalDepthAttachment &&) = delete;

        template <typename U>
            requires(IsRenderAttachment<U> && IsPixelLocalAttachment<U> && IsTextureDimension2D<U>)
        PixelLocalDepthAttachment operator=(U)
        {
            return PixelLocalDepthAttachment();
        }

        PixelLocalDepthAttachment operator=(const typename T::TrueType &)
        {
            return PixelLocalDepthAttachment();
        }
    };

    struct IFrameBuffer
    {
    };


    template <class T, int N>
    class InputPatch
    {
    public:
        T operator[](int index)
        {
            return T();
        }
    };

    template <class T, int N>
    class OutputPatch
    {
    public:
        T operator[](int index) const
        {
            return T();
        }
    };


} // namespace UGL
