// dear imgui: Renderer for WebGPU
// This needs to be used along with a Platform Binding (e.g. GLFW)
// (Please note that WebGPU is currently experimental, will not run on non-beta browsers, and may break.)

// Implemented features:
//  [X] Renderer: User texture binding. Use 'GVMTextureView' as ImTextureID. Read the FAQ about ImTextureID/ImTextureRef!
//  [X] Renderer: Large meshes support (64k+ vertices) even with 16-bit indices (ImGuiBackendFlags_RendererHasVtxOffset).
//  [X] Renderer: Expose selected render state for draw callbacks to use. Access in '(ImGui_ImplXXXX_RenderState*)GetPlatformIO().Renderer_RenderState'.
//  [X] Renderer: Texture updates support for dynamic font system (ImGuiBackendFlags_RendererHasTextures).

// You can use unmodified imgui_impl_* files in your project. See examples/ folder for examples of using this.
// Prefer including the entire imgui/ repository into your project (either as a copy or as a submodule), and only build the backends you need.
// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

// CHANGELOG
// (minor and older changes stripped away, please see git history for details)
//  2025-09-18: Call platform_io.ClearRendererHandlers() on shutdown.
//  2025-06-12: Added support for ImGuiBackendFlags_RendererHasTextures, for dynamic font atlas. (#8465)
//  2025-02-26: Recreate image bind groups during render. (#8426, #8046, #7765, #8027) + Update for latest webgpu-native changes.
//  2024-10-14: Update Dawn support for change of string usages. (#8082, #8083)
//  2024-10-07: Expose selected render state in ImGui_ImplGVM_RenderState, which you can access in 'void* platform_io.Renderer_RenderState' during draw callbacks.
//  2024-10-07: Changed default texture sampler to Clamp instead of Repeat/Wrap.
//  2024-09-16: Added support for optional IMGUI_IMPL_WEBGPU_BACKEND_DAWN / IMGUI_IMPL_WEBGPU_BACKEND_GVM define to handle ever-changing native implementations. (#7977)
//  2024-01-22: Added configurable PipelineMultisampleState struct. (#7240)
//  2024-01-22: (Breaking) ImGui_ImplGVM_Init() now takes a ImGui_ImplGVM_InitInfo structure instead of variety of parameters, allowing for easier further changes.
//  2024-01-22: Fixed pipeline layout leak. (#7245)
//  2024-01-17: Explicitly fill all of GVMDepthStencilState since standard removed defaults.
//  2023-07-13: Use GVMShaderModuleWGSLDescriptor's code instead of source. use GVMMipmapFilterMode_Linear instead of GVMFilterMode_Linear. (#6602)
//  2023-04-11: Align buffer sizes. Use WGSL shaders instead of precompiled SPIR-V.
//  2023-04-11: Reorganized backend to pull data from a single structure to facilitate usage with multiple-contexts (all g_XXXX access changed to bd->XXXX).
//  2023-01-25: Revert automatic pipeline layout generation (see https://github.com/gpuweb/gpuweb/issues/2470)
//  2022-11-24: Fixed validation error with default depth buffer settings.
//  2022-11-10: Fixed rendering when a depth buffer is enabled. Added 'GVMTextureFormat depth_format' parameter to ImGui_ImplGVM_Init().
//  2022-10-11: Using 'nullptr' instead of 'NULL' as per our switch to C++11.
//  2021-11-29: Passing explicit buffer sizes to GVMRenderPassEncoderSetVertexBuffer()/GVMRenderPassEncoderSetIndexBuffer().
//  2021-08-24: Fixed for latest specs.
//  2021-05-24: Add support for draw_data->FramebufferScale.
//  2021-05-19: Replaced direct access to ImDrawCmd::TextureId with a call to ImDrawCmd::GetTexID(). (will become a requirement)
//  2021-05-16: Update to latest WebGPU specs (compatible with Emscripten 2.0.20 and Chrome Canary 92).
//  2021-02-18: Change blending equation to preserve alpha in output buffer.
//  2021-01-28: Initial version.


#ifndef IMGUI_DISABLE
#include "imgui_impl_GVM.h"
#include "ImGui_ImplGVM_VulkanShaders.hpp"
#include <limits.h>


// Dear ImGui prototypes from imgui_internal.h
extern ImGuiID ImHashData(const void *data_p, size_t data_size, ImU32 seed);
#define MEMALIGN(_SIZE, _ALIGN) (((_SIZE) + ((_ALIGN) - 1)) & ~((_ALIGN) - 1)) // Memory align (copied from IM_ALIGN() macro).

// WebGPU data
struct ImGui_ImplGVM_Texture
{
    GVM::RHI::Texture Texture;
    GVM::RHI::TextureView TextureView;
};

struct RenderResources
{
    GVM::RHI::Sampler Sampler;                                // Sampler for textures
    GVM::RHI::Buffer Uniforms;                                // Shader uniforms
    GVM::RHI::BindGroup CommonBindGroup = nullptr;            // Resources bind-group to bind the common resources to pipeline
    ImGuiStorage ImageBindGroups;                             // Resources bind-group to bind the font/image resources to pipeline (this is a key->value map)
    GVM::RHI::BindGroupLayout ImageBindGroupLayout = nullptr; // Cache layout used for the image bind group. Avoids allocating unnecessary JS objects when working with WebASM
};

struct FrameResources
{
    GVM::RHI::Buffer IndexBuffer;
    GVM::RHI::Buffer VertexBuffer;
    ImDrawIdx *IndexBufferHost;
    ImDrawVert *VertexBufferHost;
    int IndexBufferSize;
    int VertexBufferSize;
};

struct Uniforms
{
    float MVP[4][4];
    float Gamma;
};

struct ImGui_ImplGVM_Data
{
    ImGui_ImplGVM_InitInfo initInfo;
    GVM::RHI::Device GVMDevice = nullptr;
    GVM::RHI::Queue defaultQueue = nullptr;
    GVM::RHI::TextureFormat renderTargetFormat = GVM::RHI::TextureFormat::Undefined;
    GVM::RHI::TextureFormat depthStencilFormat = GVM::RHI::TextureFormat::Undefined;
    GVM::RHI::RenderPipeline pipelineState = nullptr;

    RenderResources renderResources;
    FrameResources *pFrameResources = nullptr;
    unsigned int numFramesInFlight = 0;
    unsigned int frameIndex = UINT_MAX;
};

// Backend data stored in io.BackendRendererUserData to allow support for multiple Dear ImGui contexts
// It is STRONGLY preferred that you use docking branch with multi-viewports (== single Dear ImGui context + multiple windows) instead of multiple Dear ImGui contexts.
static ImGui_ImplGVM_Data *ImGui_ImplGVM_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplGVM_Data *)ImGui::GetIO().BackendRendererUserData : nullptr;
}

//-----------------------------------------------------------------------------
// SHADERS
//-----------------------------------------------------------------------------


const char *vertexShaderSrc_metal = R"(
#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;
#ifndef mul
#define mul(a,b) (a*b)
#endif
#ifndef frac
#define frac(a) fract(a)
#endif
#ifndef lerp
#define lerp(a,b,c) mix(a,b,c)
#endif
#ifndef atomicOr
#define atomicOr(a,b) atomic_fetch_or_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef atomicStore
#define atomicStore(a,b) atomic_store_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef GroupMemoryBarrierWithGroupSync
#define GroupMemoryBarrierWithGroupSync() threadgroup_barrier(mem_flags::mem_threadgroup)
#endif
#ifndef atomicLoad
#define atomicLoad(a) atomic_load_explicit(&a,memory_order_relaxed)
#endif
#ifndef atomicMax
#define atomicMax(a,b) atomic_fetch_max_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef atomicMin
#define atomicMin(a,b) atomic_fetch_min_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef ddx
#define ddx(x) dfdx(x)
#endif
#ifndef ddy
#define ddy(x) dfdy(x)
#endif
#ifndef QuadReadAcrossX
#define QuadReadAcrossX(x) quad_shuffle_xor(x, 0x1)
#endif
#ifndef QuadReadAcrossY
#define QuadReadAcrossY(x) quad_shuffle_xor(x, 0x2)
#endif
#ifndef QuadReadAcrossDiagonal
#define QuadReadAcrossDiagonal(x) quad_shuffle_xor(x, 0x3)
#endif
#ifndef QuadReadLaneAt
#define QuadReadLaneAt(x,n) quad_shuffle(x, n)
#endif
#ifndef asuint
#define asuint(...) as_type<uint>(__VA_ARGS__)
#endif
#ifndef asfloat
#define asfloat(...) as_type<float>(__VA_ARGS__)
#endif

template<typename T>
inline T firstbitlow(T x)
{
    return ctz(x);
}
template<typename T>
inline T firstbithigh(T x)
{
    return sizeof(T) * 8-1-clz(x);
}
template <class T>
void atomicCompareExchange(threadgroup atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

template <class T>
void atomicCompareExchange(device atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

void DeviceMemoryBarrier()
{
    atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed);
}

template <class T, class U>
T atomicAdd(device atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}

template <class T, class U>
T atomicAdd(threadgroup atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}
struct UGLSampleFloatTextureArrayWraper
{
    texture2d<float> texture [[id(0)]];
    float4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    float4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLSampleUIntTextureArrayWraper
{
    texture2d<uint> texture [[id(0)]];
    uint4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    uint4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGL_RenderEntityInfo_
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
namespace ScreenSpaceRendering
{

    struct UIBindingGroup
    {
         texture2d<float, access::read_write> inputAOBuffer;
         sampler AOSampler;
    };
    struct UIBindingGroup_INPUTDATA
    {
         texture2d<float, access::read_write> inputAOBuffer [[id(0)]];
         sampler AOSampler [[id(1)]];
    };
    struct UIVertexInput
    {

         float2 position [[attribute(0)]];

         float2 uv [[attribute(1)]];

         float4 color [[attribute(2)]];
    };
    struct UIVertexOutput
    {

         float4 pos [[position]];

         float4 color [[attribute(0)]];

         float2 texCoord [[attribute(1)]];
    };    struct UIFrameBuffer
    {
        float4 color[[color(0)]];
    };
    struct Uniforms
    {

         float4x4 mvp;

         float gamma;
    };
    struct UI0BindGroup
    {
        thread ScreenSpaceRendering::Uniforms* uniforms;
        thread ScreenSpaceRendering::Uniforms uniforms_TEMPDATA;
         sampler sampler0;
    };
    struct UI0BindGroup_INPUTDATA
    {
        constant ScreenSpaceRendering::Uniforms* uniforms [[id(0)]];
         sampler sampler0 [[id(1)]];
    };
    struct UI1BindGroup
    {
         texture2d<float> texture0;
    };
    struct UI1BindGroup_INPUTDATA
    {
         texture2d<float> texture0 [[id(0)]];
    };
} // namespace ScreenSpaceRendering

namespace ScreenSpaceRendering::UIPass
{
}
vertex ScreenSpaceRendering::UIVertexOutput vertexMain(uint vid [[vertex_id]], ScreenSpaceRendering::UIVertexInput vInput [[stage_in]], const constant ScreenSpaceRendering::UI0BindGroup_INPUTDATA* BindGroup0_INPUTDATA [[buffer(1)]], const constant ScreenSpaceRendering::UI1BindGroup_INPUTDATA* BindGroup1_INPUTDATA [[buffer(2)]], uint _Backup_InstanceID [[instance_id]])
{
    using namespace ScreenSpaceRendering;
    thread ScreenSpaceRendering::UI0BindGroup * BindGroup0;
    thread ScreenSpaceRendering::UI0BindGroup BindGroup0_TEMPDATA;
    {
        BindGroup0_TEMPDATA.uniforms_TEMPDATA = *BindGroup0_INPUTDATA->uniforms;
        BindGroup0_TEMPDATA.uniforms = &BindGroup0_TEMPDATA.uniforms_TEMPDATA;
        BindGroup0_TEMPDATA.sampler0 = BindGroup0_INPUTDATA->sampler0;
        BindGroup0 = &BindGroup0_TEMPDATA;
    }
    thread ScreenSpaceRendering::UI1BindGroup * BindGroup1;
    thread ScreenSpaceRendering::UI1BindGroup BindGroup1_TEMPDATA;
    {
        BindGroup1_TEMPDATA.texture0 = BindGroup1_INPUTDATA->texture0;
        BindGroup1 = &BindGroup1_TEMPDATA;
    }
    {

         ScreenSpaceRendering::UIVertexOutput out;

        out.pos = mul(BindGroup0->uniforms->mvp, float4(vInput.position, 0, 1));

        out.texCoord = vInput.uv;

        out.color = float4(vInput.color) / float4(255.0);
        return out;
        //ScreenSpaceRendering::UIVertexOutput  __REVERSED_VERTEX__OUTPUT__ = out; __REVERSED_VERTEX__OUTPUT__.pos.y=-__REVERSED_VERTEX__OUTPUT__.pos.y;return __REVERSED_VERTEX__OUTPUT__;
;
    }
}
    )";
const char *fragmentShaderSrc_metal = R"(
#include <metal_stdlib>
#include <metal_atomic>
using namespace metal;
#ifndef mul
#define mul(a,b) (a*b)
#endif
#ifndef frac
#define frac(a) fract(a)
#endif
#ifndef lerp
#define lerp(a,b,c) mix(a,b,c)
#endif
#ifndef atomicOr
#define atomicOr(a,b) atomic_fetch_or_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef atomicStore
#define atomicStore(a,b) atomic_store_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef GroupMemoryBarrierWithGroupSync
#define GroupMemoryBarrierWithGroupSync() threadgroup_barrier(mem_flags::mem_threadgroup)
#endif
#ifndef atomicLoad
#define atomicLoad(a) atomic_load_explicit(&a,memory_order_relaxed)
#endif
#ifndef atomicMax
#define atomicMax(a,b) atomic_fetch_max_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef atomicMin
#define atomicMin(a,b) atomic_fetch_min_explicit(&a,b,memory_order_relaxed)
#endif
#ifndef ddx
#define ddx(x) dfdx(x)
#endif
#ifndef ddy
#define ddy(x) dfdy(x)
#endif
#ifndef QuadReadAcrossX
#define QuadReadAcrossX(x) quad_shuffle_xor(x, 0x1)
#endif
#ifndef QuadReadAcrossY
#define QuadReadAcrossY(x) quad_shuffle_xor(x, 0x2)
#endif
#ifndef QuadReadAcrossDiagonal
#define QuadReadAcrossDiagonal(x) quad_shuffle_xor(x, 0x3)
#endif
#ifndef QuadReadLaneAt
#define QuadReadLaneAt(x,n) quad_shuffle(x, n)
#endif
#ifndef asuint
#define asuint(...) as_type<uint>(__VA_ARGS__)
#endif
#ifndef asfloat
#define asfloat(...) as_type<float>(__VA_ARGS__)
#endif

template<typename T>
inline T firstbitlow(T x)
{
    return ctz(x);
}
template<typename T>
inline T firstbithigh(T x)
{
    return sizeof(T) * 8-1-clz(x);
}
template <class T>
void atomicCompareExchange(threadgroup atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

template <class T>
void atomicCompareExchange(device atomic<T>& atom, T compare, T value, thread T &originalValue)
{
    atomic_compare_exchange_weak_explicit(&atom, &compare, value, memory_order_relaxed,memory_order_relaxed);
    originalValue = compare;
}

void DeviceMemoryBarrier()
{
    atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed);
}

template <class T, class U>
T atomicAdd(device atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}

template <class T, class U>
T atomicAdd(threadgroup atomic<T>& atom, U b)
{
    return atomic_fetch_add_explicit(&atom,b,memory_order_relaxed);
}
struct UGLSampleFloatTextureArrayWraper
{
    texture2d<float> texture [[id(0)]];
    float4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    float4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGLSampleUIntTextureArrayWraper
{
    texture2d<uint> texture [[id(0)]];
    uint4 sample(sampler s, float2 uv)
    {
        return texture.sample(s,uv);
    }
    uint4 read(ushort2 index)
    {
        return texture.read(index);
    }
};
struct UGL_RenderEntityInfo_
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};
namespace ScreenSpaceRendering
{

    struct UIBindingGroup
    {
         texture2d<float, access::read_write> inputAOBuffer;
         sampler AOSampler;
    };
    struct UIBindingGroup_INPUTDATA
    {
         texture2d<float, access::read_write> inputAOBuffer [[id(0)]];
         sampler AOSampler [[id(1)]];
    };
    struct UIVertexInput
    {

         float2 position [[attribute(0)]];

         float2 uv [[attribute(1)]];

         float4 color [[attribute(2)]];
    };
    struct UIVertexOutput
    {

         float4 pos [[position]];

         float4 color [[attribute(0)]];

         float2 texCoord [[attribute(1)]];
    };    struct UIFrameBuffer
    {
        float4 color[[color(0)]];
    };
    struct Uniforms
    {

         float4x4 mvp;

         float gamma;
    };
    struct UI0BindGroup
    {
        thread ScreenSpaceRendering::Uniforms* uniforms;
        thread ScreenSpaceRendering::Uniforms uniforms_TEMPDATA;
         sampler sampler0;
    };
    struct UI0BindGroup_INPUTDATA
    {
        constant ScreenSpaceRendering::Uniforms* uniforms [[id(0)]];
         sampler sampler0 [[id(1)]];
    };
    struct UI1BindGroup
    {
         texture2d<float> texture0;
    };
    struct UI1BindGroup_INPUTDATA
    {
         texture2d<float> texture0 [[id(0)]];
    };
} // namespace ScreenSpaceRendering

namespace ScreenSpaceRendering::UIPass
{
}
fragment ScreenSpaceRendering::UIFrameBuffer fragmentMain(ScreenSpaceRendering::UIVertexOutput vertexIn[[stage_in]], const constant ScreenSpaceRendering::UI0BindGroup_INPUTDATA* BindGroup0_INPUTDATA [[buffer(1)]], const constant ScreenSpaceRendering::UI1BindGroup_INPUTDATA* BindGroup1_INPUTDATA [[buffer(2)]])
{
    using namespace ScreenSpaceRendering;
    thread ScreenSpaceRendering::UI0BindGroup * BindGroup0;
    thread ScreenSpaceRendering::UI0BindGroup BindGroup0_TEMPDATA;
    {
        BindGroup0_TEMPDATA.uniforms_TEMPDATA = *BindGroup0_INPUTDATA->uniforms;
        BindGroup0_TEMPDATA.uniforms = &BindGroup0_TEMPDATA.uniforms_TEMPDATA;
        BindGroup0_TEMPDATA.sampler0 = BindGroup0_INPUTDATA->sampler0;
        BindGroup0 = &BindGroup0_TEMPDATA;
    }
    thread ScreenSpaceRendering::UI1BindGroup * BindGroup1;
    thread ScreenSpaceRendering::UI1BindGroup BindGroup1_TEMPDATA;
    {
        BindGroup1_TEMPDATA.texture0 = BindGroup1_INPUTDATA->texture0;
        BindGroup1 = &BindGroup1_TEMPDATA;
    }
    {

         ScreenSpaceRendering::UIFrameBuffer foutput;

         float2 uv = vertexIn.texCoord;



         float4 res = BindGroup1->texture0.sample(BindGroup0->sampler0, uv)*vertexIn.color;
         //res.rgb = pow(res.rgb,float3(BindGroup0->uniforms->gamma));
         if(res.a<.1)
         {
            //discard_fragment();
         }

        foutput.color = res;

        return foutput;
    }
}
    )";


static void SafeRelease(ImDrawIdx *&res)
{
    if (res)
        delete[] res;
    res = nullptr;
}
static void SafeRelease(ImDrawVert *&res)
{
    if (res)
        delete[] res;
    res = nullptr;
}


static void SafeRelease(FrameResources &res)
{
    SafeRelease(res.IndexBufferHost);
    SafeRelease(res.VertexBufferHost);
}


static GVM::RHI::ShaderModule ImGui_ImplGVM_CreateShaderModule(
    const char *label,
    const char *metal_source,
    const uint32_t *vulkan_spirv,
    size_t vulkan_spirv_word_count)
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();


    GVM::RHI::ShaderModuleDescriptor desc = {};
    desc.label = label;

    switch (bd->initInfo.BackendType)
    {
    case GVM::RHI::GraphicsBackend::Metal:
        desc.code = metal_source;
        break;
    case GVM::RHI::GraphicsBackend::Vulkan:
        if (vulkan_spirv == nullptr || vulkan_spirv_word_count == 0)
        {
            throw std::runtime_error(std::string("Missing Vulkan SPIR-V payload for ImGui shader: ") + label);
        }
        desc.spirv.assign(vulkan_spirv, vulkan_spirv + vulkan_spirv_word_count);
        break;
    default:
        throw std::runtime_error("unimplemented backend");
    }


    return bd->GVMDevice->createShaderModule(desc);
}

static GVM::RHI::BindGroup ImGui_ImplGVM_CreateImageBindGroup(GVM::RHI::BindGroupLayout layout, GVM::RHI::TextureView texture)
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    eastl::vector<GVM::RHI::BindGroupEntry> image_bg_entries = {(GVM::RHI::BindGroupEntry){.binding = 0, .textureView = texture}};

    GVM::RHI::BindGroupDescriptor image_bg_descriptor = {};
    image_bg_descriptor.layout = layout;
    // image_bg_descriptor.entryCount = sizeof(image_bg_entries) / sizeof(GVMBindGroupEntry);
    image_bg_descriptor.entries = image_bg_entries;
    return bd->GVMDevice->createBindGroup(image_bg_descriptor);
}

static void ImGui_ImplGVM_SetupRenderState(ImDrawData *draw_data, GVM::RHI::RenderPassEncoder ctx, FrameResources *fr)
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();

    // Setup orthographic projection matrix into our constant buffer
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right).
    {
        float L = draw_data->DisplayPos.x;
        float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
        float T = draw_data->DisplayPos.y;
        float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
        float mvp[4][4] = {
            {2.0f / (R - L), 0.0f, 0.0f, 0.0f},
            {0.0f, 2.0f / (T - B), 0.0f, 0.0f},
            {0.0f, 0.0f, 0.5f, 0.0f},
            {(R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f},
        };
        bd->defaultQueue->writeBuffer(GVM::RHI::BufferRange(bd->renderResources.Uniforms, offsetof(Uniforms, MVP)), mvp, sizeof(Uniforms::MVP));
        float gamma;
        switch (bd->renderTargetFormat)
        {
        case GVM::RHI::TextureFormat::ASTC10x10UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC10x5UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC10x6UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC10x8UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC12x10UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC12x12UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC4x4UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC5x5UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC6x5UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC6x6UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC8x5UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC8x6UnormSrgb:
        case GVM::RHI::TextureFormat::ASTC8x8UnormSrgb:
        case GVM::RHI::TextureFormat::BC1RGBAUnormSrgb:
        case GVM::RHI::TextureFormat::BC2RGBAUnormSrgb:
        case GVM::RHI::TextureFormat::BC3RGBAUnormSrgb:
        case GVM::RHI::TextureFormat::BC7RGBAUnormSrgb:
        case GVM::RHI::TextureFormat::BGRA8UnormSrgb:
        case GVM::RHI::TextureFormat::ETC2RGB8A1UnormSrgb:
        case GVM::RHI::TextureFormat::ETC2RGB8UnormSrgb:
        case GVM::RHI::TextureFormat::ETC2RGBA8UnormSrgb:
        case GVM::RHI::TextureFormat::RGBA8UnormSrgb:
            gamma = 2.2f;
            break;
        default:
            gamma = 1.0f;
        }
        bd->defaultQueue->writeBuffer(GVM::RHI::BufferRange(bd->renderResources.Uniforms, offsetof(Uniforms, Gamma)), &gamma, sizeof(Uniforms::Gamma));
    }

    // Setup viewport
    ctx->setViewport(0, 0, draw_data->FramebufferScale.x * draw_data->DisplaySize.x, draw_data->FramebufferScale.y * draw_data->DisplaySize.y, 0, 1);

    // Bind shader and vertex buffers
    // GVMRenderPassEncoderSetVertexBuffer(ctx, 0, fr->VertexBuffer, 0, fr->VertexBufferSize * sizeof(ImDrawVert));
    ctx->setVertexBuffer(fr->VertexBuffer, 0);
    ctx->setIndexBuffer(fr->IndexBuffer, sizeof(ImDrawIdx) == 2 ? GVM::RHI::IndexFormat::Uint16 : GVM::RHI::IndexFormat::Uint32);
    // GVMRenderPassEncoderSetIndexBuffer(ctx, fr->IndexBuffer, sizeof(ImDrawIdx) == 2 ? GVMIndexFormat_Uint16 : GVMIndexFormat_Uint32, 0, fr->IndexBufferSize * sizeof(ImDrawIdx));
    ctx->setPipeline(bd->pipelineState);
    // GVMRenderPassEncoderSetPipeline(ctx, bd->pipelineState);
    // GVMRenderPassEncoderSetBindGroup(ctx, 0, bd->renderResources.CommonBindGroup, 0, nullptr);
    ctx->setBindGroup(bd->renderResources.CommonBindGroup, 0);

    // Setup blend factor
    // GVMColor blend_color = {0.f, 0.f, 0.f, 0.f};
    // GVMRenderPassEncoderSetBlendConstant(ctx, &blend_color);
}

// Render function
// (this used to be set in io.RenderDrawListsFn and called by ImGui::Render(), but you can now call this directly from your main loop)
void ImGui_ImplGVM_RenderDrawData(ImDrawData *draw_data, GVM::RHI::RenderPassEncoder pass_encoder)
{
    // Avoid rendering when minimized
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0 || draw_data->CmdLists.Size == 0)
        return;

    // Catch up with texture updates. Most of the times, the list will have 1 element with an OK status, aka nothing to do.
    // (This almost always points to ImGui::GetPlatformIO().Textures[] but is part of ImDrawData to allow overriding or disabling texture updates).
    if (draw_data->Textures != nullptr)
        for (ImTextureData *tex : *draw_data->Textures)
            if (tex->Status != ImTextureStatus_OK)
                ImGui_ImplGVM_UpdateTexture(tex);

    // FIXME: Assuming that this only gets called once per frame!
    // If not, we can't just re-allocate the IB or VB, we'll have to do a proper allocator.
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    bd->frameIndex = bd->frameIndex + 1;
    FrameResources *fr = &bd->pFrameResources[bd->frameIndex % bd->numFramesInFlight];

    // Create and grow vertex/index buffers if needed
    if (fr->VertexBuffer.isNull() || fr->VertexBufferSize < draw_data->TotalVtxCount)
    {
        if (fr->VertexBuffer.isNull() == false)
        {
            bd->GVMDevice->freeBuffer(fr->VertexBuffer);
        }
        SafeRelease(fr->VertexBufferHost);
        fr->VertexBufferSize = draw_data->TotalVtxCount + 5000;

        GVM::RHI::BufferDescriptor vb_desc = {"Dear ImGui Vertex buffer", GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Vertex, MEMALIGN(fr->VertexBufferSize * sizeof(ImDrawVert), 4)};
        fr->VertexBuffer = bd->GVMDevice->createBuffer(vb_desc);
        if (fr->VertexBuffer.isNull())
            return;

        fr->VertexBufferHost = new ImDrawVert[fr->VertexBufferSize];
    }
    if (fr->IndexBuffer.isNull() || fr->IndexBufferSize < draw_data->TotalIdxCount)
    {
        if (fr->IndexBuffer.isNull() == false)
        {
            bd->GVMDevice->freeBuffer(fr->IndexBuffer);
        }
        SafeRelease(fr->IndexBufferHost);
        fr->IndexBufferSize = draw_data->TotalIdxCount + 10000;

        GVM::RHI::BufferDescriptor ib_desc = {"Dear ImGui Index buffer", GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Index, MEMALIGN(fr->IndexBufferSize * sizeof(ImDrawIdx), 4)};
        fr->IndexBuffer = bd->GVMDevice->createBuffer(ib_desc);
        if (fr->IndexBuffer.isNull())
            return;

        fr->IndexBufferHost = new ImDrawIdx[fr->IndexBufferSize];
    }

    // Upload vertex/index data into a single contiguous GPU buffer
    ImDrawVert *vtx_dst = (ImDrawVert *)fr->VertexBufferHost;
    ImDrawIdx *idx_dst = (ImDrawIdx *)fr->IndexBufferHost;
    for (const ImDrawList *draw_list : draw_data->CmdLists)
    {
        memcpy(vtx_dst, draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Size * sizeof(ImDrawVert));
        memcpy(idx_dst, draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx_dst += draw_list->VtxBuffer.Size;
        idx_dst += draw_list->IdxBuffer.Size;
    }
    int64_t vb_write_size = MEMALIGN((char *)vtx_dst - (char *)fr->VertexBufferHost, 4);
    int64_t ib_write_size = MEMALIGN((char *)idx_dst - (char *)fr->IndexBufferHost, 4);
    bd->defaultQueue->writeBuffer(fr->VertexBuffer, fr->VertexBufferHost, vb_write_size);
    bd->defaultQueue->writeBuffer(fr->IndexBuffer, fr->IndexBufferHost, ib_write_size);

    // Setup desired render state
    ImGui_ImplGVM_SetupRenderState(draw_data, pass_encoder, fr);

    // Setup render state structure (for callbacks and custom texture bindings)
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();
    ImGui_ImplGVM_RenderState render_state;
    render_state.Device = bd->GVMDevice;
    render_state.RenderPassEncoder = pass_encoder;
    platform_io.Renderer_RenderState = &render_state;

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    ImVec2 clip_scale = draw_data->FramebufferScale;
    ImVec2 clip_off = draw_data->DisplayPos;
    for (const ImDrawList *draw_list : draw_data->CmdLists)
    {
        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd *pcmd = &draw_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplGVM_SetupRenderState(draw_data, pass_encoder, fr);
                else
                    pcmd->UserCallback(draw_list, pcmd);
            }
            else
            {
                // Bind custom texture
                ImTextureID tex_id = pcmd->GetTexID();
                ImGuiID tex_id_hash = ImHashData(&tex_id, sizeof(tex_id), 0);
                GVM::RHI::BindGroup bind_group = (GVM::RHI::BindGroupImpl *)bd->renderResources.ImageBindGroups.GetVoidPtr(tex_id_hash);
                if (!bind_group)
                {
                    bind_group = ImGui_ImplGVM_CreateImageBindGroup(bd->renderResources.ImageBindGroupLayout, *((GVM::RHI::TextureView *)tex_id));
                    bd->renderResources.ImageBindGroups.SetVoidPtr(tex_id_hash, bind_group.get());
                }
                pass_encoder->setBindGroup((GVM::RHI::BindGroup)bind_group, 1);

                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);

                // Clamp to viewport as GVMRenderPassEncoderSetScissorRect() won't accept values that are off bounds
                if (clip_min.x < 0.0f)
                {
                    clip_min.x = 0.0f;
                }
                if (clip_min.y < 0.0f)
                {
                    clip_min.y = 0.0f;
                }
                if (clip_max.x > fb_width)
                {
                    clip_max.x = (float)fb_width;
                }
                if (clip_max.y > fb_height)
                {
                    clip_max.y = (float)fb_height;
                }
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
                    continue;

                // Apply scissor/clipping rectangle, Draw
                pass_encoder->setScissorRect((uint32_t)clip_min.x, (uint32_t)clip_min.y, (uint32_t)(clip_max.x - clip_min.x), (uint32_t)(clip_max.y - clip_min.y));
                pass_encoder->drawIndexed(pcmd->ElemCount, 1, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset, 0);
            }
        }
        global_idx_offset += draw_list->IdxBuffer.Size;
        global_vtx_offset += draw_list->VtxBuffer.Size;
    }

    // Remove all ImageBindGroups
    ImGuiStorage &image_bind_groups = bd->renderResources.ImageBindGroups;
    for (int i = 0; i < image_bind_groups.Data.Size; i++)
    {
        // GVM::RHI::BindGroup bind_group = (GVM::RHI::BindGroup)image_bind_groups.Data[i].val_p;
    }
    image_bind_groups.Data.resize(0);

    platform_io.Renderer_RenderState = nullptr;
}

static void ImGui_ImplGVM_DestroyTexture(ImTextureData *tex)
{
    if (ImGui_ImplGVM_Texture *backend_tex = (ImGui_ImplGVM_Texture *)tex->BackendUserData)
    {
        IM_DELETE(backend_tex);

        // Clear identifiers and mark as destroyed (in order to allow e.g. calling InvalidateDeviceObjects while running)
        tex->SetTexID(ImTextureID_Invalid);
        tex->BackendUserData = nullptr;
    }
    tex->SetStatus(ImTextureStatus_Destroyed);
}

void ImGui_ImplGVM_UpdateTexture(ImTextureData *tex)
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    if (tex->Status == ImTextureStatus_WantCreate)
    {
        // Create and upload new texture to graphics system
        // IMGUI_DEBUG_LOG("UpdateTexture #%03d: WantCreate %dx%d\n", tex->UniqueID, tex->Width, tex->Height);
        IM_ASSERT(tex->TexID == ImTextureID_Invalid && tex->BackendUserData == nullptr);
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);
        ImGui_ImplGVM_Texture *backend_tex = IM_NEW(ImGui_ImplGVM_Texture)();

        // Create texture
        GVM::RHI::TextureDescriptor tex_desc = {};

        tex_desc.label = "Dear ImGui Texture";

        tex_desc.dimension = GVM::RHI::TextureDimension::e2D;
        tex_desc.size.width = tex->Width;
        tex_desc.size.height = tex->Height;
        tex_desc.size.depth = 1;
        tex_desc.arrayLayerCount = 1;
        tex_desc.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        tex_desc.mipLevelCount = 1;
        tex_desc.usage = GVM::RHI::TextureUsage::CopyDst | GVM::RHI::TextureUsage::TextureBinding;
        backend_tex->Texture = bd->GVMDevice->createTexture(tex_desc);

        // Create texture view
        GVM::RHI::TextureViewDescriptor tex_view_desc = {};
        tex_view_desc.format = GVM::RHI::TextureFormat::RGBA8Unorm;
        tex_view_desc.dimension = GVM::RHI::TextureViewDimension::e2D;
        tex_view_desc.baseMipLevel = 0;
        tex_view_desc.mipLevelCount = 1;
        tex_view_desc.baseArrayLayer = 0;
        tex_view_desc.arrayLayerCount = 1;
        tex_view_desc.aspect = GVM::RHI::TextureAspect::All;
        backend_tex->TextureView = backend_tex->Texture->createView(tex_view_desc);

        // Store identifiers
        tex->SetTexID((ImTextureID)&backend_tex->TextureView);
        tex->BackendUserData = backend_tex;
        // We don't set tex->Status to ImTextureStatus_OK to let the code fallthrough below.
    }

    if (tex->Status == ImTextureStatus_WantCreate || tex->Status == ImTextureStatus_WantUpdates)
    {
        ImGui_ImplGVM_Texture *backend_tex = (ImGui_ImplGVM_Texture *)tex->BackendUserData;
        IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

        // We could use the smaller rect on _WantCreate but using the full rect allows us to clear the texture.
        const int upload_x = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.x;
        const int upload_y = (tex->Status == ImTextureStatus_WantCreate) ? 0 : tex->UpdateRect.y;
        const int upload_w = (tex->Status == ImTextureStatus_WantCreate) ? tex->Width : tex->UpdateRect.w;
        const int upload_h = (tex->Status == ImTextureStatus_WantCreate) ? tex->Height : tex->UpdateRect.h;

        // Update full texture or selected blocks. We only ever write to textures regions which have never been used before!
        // This backend choose to use tex->UpdateRect but you can use tex->Updates[] to upload individual regions.
        GVM::RHI::ImageCopyTexture dst_view = {};

        dst_view.texture = backend_tex->Texture;
        dst_view.mipLevel = 0;
        dst_view.origin = {(uint32_t)upload_x, (uint32_t)upload_y, 0};
        dst_view.aspect = GVM::RHI::TextureAspect::All;

        GVM::RHI::TextureDataLayout layout = {};

        layout.offset = 0;
        layout.bytesPerRow = tex->Width * tex->BytesPerPixel;
        layout.rowsPerImage = upload_h;
        GVM::RHI::Extent3D write_size = {(uint32_t)upload_w, (uint32_t)upload_h, 1};
        bd->defaultQueue->writeTexture(dst_view, tex->GetPixelsAt(upload_x, upload_y), (uint64_t)(tex->Width * upload_h * tex->BytesPerPixel), layout, write_size);
        // GVMQueueWriteTexture(bd->defaultQueue, &dst_view, tex->GetPixelsAt(upload_x, upload_y), (uint32_t)(tex->Width * upload_h * tex->BytesPerPixel), &layout, &write_size);
        tex->SetStatus(ImTextureStatus_OK);
    }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        ImGui_ImplGVM_DestroyTexture(tex);
}

static void ImGui_ImplGVM_CreateUniformBuffer()
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    GVM::RHI::BufferDescriptor ub_desc = {"Dear ImGui Uniform buffer", GVM::RHI::BufferUsage::CopyDst | GVM::RHI::BufferUsage::Uniform, MEMALIGN(sizeof(Uniforms), 16)};
    bd->renderResources.Uniforms = bd->GVMDevice->createBuffer(ub_desc);
}

bool ImGui_ImplGVM_CreateDeviceObjects()
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    if (!bd->GVMDevice)
        return false;
    if (bd->pipelineState)
        ImGui_ImplGVM_InvalidateDeviceObjects();

    // Create render pipeline
    GVM::RHI::RenderPipelineDescriptor graphics_pipeline_desc = {};
    graphics_pipeline_desc.primitive.topology = GVM::RHI::PrimitiveTopology::TriangleList;
    graphics_pipeline_desc.primitive.stripIndexFormat = GVM::RHI::IndexFormat::Undefined;
    graphics_pipeline_desc.primitive.frontFace = GVM::RHI::FrontFace::CW;
    graphics_pipeline_desc.primitive.cullMode = GVM::RHI::CullMode::None;

    // Bind group layouts
    eastl::vector<GVM::RHI::BindGroupLayoutEntry> common_bg_layout_entries(2);
    common_bg_layout_entries[0].binding = 0;
    common_bg_layout_entries[0].visibility = GVM::RHI::ShaderStage::Vertex | GVM::RHI::ShaderStage::Fragment;
    common_bg_layout_entries[0].buffer.type = GVM::RHI::BufferBindingType::Uniform;
    common_bg_layout_entries[0].buffer.access = GVM::RHI::StorageBufferAccess::ReadOnly;
    common_bg_layout_entries[1].binding = 1;
    common_bg_layout_entries[1].visibility = GVM::RHI::ShaderStage::Fragment;
    common_bg_layout_entries[1].sampler.type = GVM::RHI::SamplerBindingType::Filtering;

    eastl::vector<GVM::RHI::BindGroupLayoutEntry> image_bg_layout_entries(1);
    image_bg_layout_entries[0].binding = 0;
    image_bg_layout_entries[0].visibility = GVM::RHI::ShaderStage::Fragment;
    image_bg_layout_entries[0].texture.sampleType = GVM::RHI::TextureSampleType::Float;
    image_bg_layout_entries[0].texture.viewDimension = GVM::RHI::TextureViewDimension::e2D;

    GVM::RHI::BindGroupLayoutDescriptor common_bg_layout_desc = {};
    common_bg_layout_desc.entries = common_bg_layout_entries;

    GVM::RHI::BindGroupLayoutDescriptor image_bg_layout_desc = {};
    image_bg_layout_desc.entries = image_bg_layout_entries;

    eastl::vector<GVM::RHI::BindGroupLayout> bg_layouts(2);
    bg_layouts[0] = bd->GVMDevice->createBindGroupLayout(common_bg_layout_desc);
    bg_layouts[1] = bd->GVMDevice->createBindGroupLayout(image_bg_layout_desc);

    GVM::RHI::PipelineLayoutDescriptor layout_desc = {};
    layout_desc.bindGroupLayouts = bg_layouts;
    graphics_pipeline_desc.layout = bd->GVMDevice->createPipelineLayout(layout_desc);

    const char *vertexSource = nullptr;
    const char *fragmentSource = nullptr;

    switch (bd->initInfo.BackendType)
    {
    case GVM::RHI::GraphicsBackend::Metal:
    case GVM::RHI::GraphicsBackend::Vulkan:
        vertexSource = vertexShaderSrc_metal;
        fragmentSource = fragmentShaderSrc_metal;
        break;
    default:
        throw std::runtime_error("unimplemented backend");
    }

    // Create the vertex shader
    GVM::RHI::ShaderModule vertex_shader_module = ImGui_ImplGVM_CreateShaderModule(
        "ImGuiVertexShader",
        vertexSource,
        GVM::Plugin::Detail::ImGuiVertexShaderSpirv,
        GVM::Plugin::Detail::ImGuiVertexShaderSpirvWordCount);
    graphics_pipeline_desc.vertex.module = vertex_shader_module;
    graphics_pipeline_desc.vertex.entryPoint = "vertexMain";

    // Vertex input configuration
    eastl::vector<GVM::RHI::VertexAttribute> attribute_desc = {

        {GVM::RHI::VertexFormat::Float32x2, (uint64_t)offsetof(ImDrawVert, pos), 0},
        {GVM::RHI::VertexFormat::Float32x2, (uint64_t)offsetof(ImDrawVert, uv), 1},
        {GVM::RHI::VertexFormat::Unorm8x4, (uint64_t)offsetof(ImDrawVert, col), 2},

    };

    eastl::vector<GVM::RHI::VertexBufferLayout> buffer_layouts(1);
    buffer_layouts[0].arrayStride = sizeof(ImDrawVert);
    buffer_layouts[0].stepMode = GVM::RHI::VertexStepMode::Vertex;
    buffer_layouts[0].attributes = attribute_desc;

    graphics_pipeline_desc.vertex.buffers = buffer_layouts;

    // Create the pixel shader
    GVM::RHI::ShaderModule pixel_shader_module = ImGui_ImplGVM_CreateShaderModule(
        "ImGuiFragmentShader",
        fragmentSource,
        GVM::Plugin::Detail::ImGuiFragmentShaderSpirv,
        GVM::Plugin::Detail::ImGuiFragmentShaderSpirvWordCount);

    // Create the blending setup
    GVM::RHI::BlendState blend_state = {};
    blend_state.alpha.operation = GVM::RHI::BlendOperation::Add;
    blend_state.alpha.srcFactor = GVM::RHI::BlendFactor::One;
    blend_state.alpha.dstFactor = GVM::RHI::BlendFactor::OneMinusSrcAlpha;
    blend_state.color.operation = GVM::RHI::BlendOperation::Add;
    blend_state.color.srcFactor = GVM::RHI::BlendFactor::SrcAlpha;
    blend_state.color.dstFactor = GVM::RHI::BlendFactor::OneMinusSrcAlpha;

    GVM::RHI::ColorTargetState color_state = {};
    color_state.format = bd->renderTargetFormat;
    color_state.blend = blend_state;
    color_state.blendEnabled = true;
    color_state.writeMask = GVM::RHI::ColorWriteMask::All;

    GVM::RHI::FragmentState fragment_state = {};
    fragment_state.module = pixel_shader_module;
    fragment_state.entryPoint = "fragmentMain";
    fragment_state.targets = {color_state};

    graphics_pipeline_desc.fragment = fragment_state;

    // Create depth-stencil State
    GVM::RHI::DepthStencilState depth_stencil_state = {};
    depth_stencil_state.format = bd->depthStencilFormat;

    depth_stencil_state.depthTestEnabled = false;

    depth_stencil_state.depthWriteEnabled = false;

    depth_stencil_state.depthCompare = GVM::RHI::CompareFunction::Always;

    // Configure disabled depth-stencil state
    if (bd->depthStencilFormat != GVM::RHI::TextureFormat::Undefined)
    {
        graphics_pipeline_desc.depthStencil = depth_stencil_state;
    }
    bd->pipelineState = bd->GVMDevice->createRenderPipeline(graphics_pipeline_desc);

    ImGui_ImplGVM_CreateUniformBuffer();

    // Create sampler
    // (Bilinear sampling is required by default. Set 'io.Fonts->Flags |= ImFontAtlasFlags_NoBakedLines' or 'style.AntiAliasedLinesUseTex = false' to allow point/nearest sampling)
    GVM::RHI::SamplerDescriptor sampler_desc = {};
    sampler_desc.minFilter = GVM::RHI::FilterMode::Linear;
    sampler_desc.magFilter = GVM::RHI::FilterMode::Linear;
    sampler_desc.mipmapFilter = GVM::RHI::MipmapFilterMode::Linear;
    sampler_desc.addressModeU = GVM::RHI::AddressMode::ClampToEdge;
    sampler_desc.addressModeV = GVM::RHI::AddressMode::ClampToEdge;
    sampler_desc.addressModeW = GVM::RHI::AddressMode::ClampToEdge;
    sampler_desc.maxAnisotropy = 1;
    bd->renderResources.Sampler = bd->GVMDevice->createSampler(sampler_desc);

    // Create resource bind group
    eastl::vector<GVM::RHI::BindGroupEntry> common_bg_entries = {
        (GVM::RHI::BindGroupEntry){.binding = 0, .buffer = GVM::RHI::BufferRange(bd->renderResources.Uniforms, 0, MEMALIGN(sizeof(Uniforms), 16))},
        (GVM::RHI::BindGroupEntry){.binding = 1, .sampler = bd->renderResources.Sampler},
    };
    GVM::RHI::BindGroupDescriptor common_bg_descriptor = {};
    common_bg_descriptor.layout = bg_layouts[0];
    common_bg_descriptor.entries = common_bg_entries;
    bd->renderResources.CommonBindGroup = bd->GVMDevice->createBindGroup(common_bg_descriptor);
    bd->renderResources.ImageBindGroupLayout = bg_layouts[1];


    return true;
}

void ImGui_ImplGVM_InvalidateDeviceObjects()
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    if (!bd->GVMDevice)
        return;


    // Destroy all textures
    for (ImTextureData *tex : ImGui::GetPlatformIO().Textures)
        if (tex->RefCount == 1)
            ImGui_ImplGVM_DestroyTexture(tex);

    for (unsigned int i = 0; i < bd->numFramesInFlight; i++)
        SafeRelease(bd->pFrameResources[i]);
}

bool ImGui_ImplGVM_Init(ImGui_ImplGVM_InitInfo *init_info)
{
    ImGuiIO &io = ImGui::GetIO();
    IMGUI_CHECKVERSION();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialized a renderer backend!");

    // Setup backend capabilities flags
    ImGui_ImplGVM_Data *bd = IM_NEW(ImGui_ImplGVM_Data)();
    io.BackendRendererUserData = (void *)bd;

    io.BackendRendererName = "imgui_impl_webgpu_GVM";

    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;  // We can honor ImGuiPlatformIO::Textures[] requests during render.

    bd->initInfo = *init_info;
    bd->GVMDevice = init_info->Device;
    bd->defaultQueue = bd->GVMDevice->getMainQueue();
    bd->renderTargetFormat = init_info->RenderTargetFormat;
    bd->depthStencilFormat = init_info->DepthStencilFormat;
    bd->numFramesInFlight = init_info->NumFramesInFlight;
    bd->frameIndex = UINT_MAX;

    bd->renderResources.Sampler.reset();
    bd->renderResources.Uniforms.reset();
    bd->renderResources.CommonBindGroup = nullptr;
    bd->renderResources.ImageBindGroups.Data.reserve(100);
    bd->renderResources.ImageBindGroupLayout = nullptr;

    // Create buffers with a default size (they will later be grown as needed)
    bd->pFrameResources = new FrameResources[bd->numFramesInFlight];
    for (unsigned int i = 0; i < bd->numFramesInFlight; i++)
    {
        FrameResources *fr = &bd->pFrameResources[i];
        fr->IndexBuffer.reset();
        fr->VertexBuffer.reset();
        fr->IndexBufferHost = nullptr;
        fr->VertexBufferHost = nullptr;
        fr->IndexBufferSize = 10000;
        fr->VertexBufferSize = 5000;
    }

    return true;
}

void ImGui_ImplGVM_Shutdown()
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    IM_ASSERT(bd != nullptr && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO &io = ImGui::GetIO();
    ImGuiPlatformIO &platform_io = ImGui::GetPlatformIO();

    ImGui_ImplGVM_InvalidateDeviceObjects();
    delete[] bd->pFrameResources;
    bd->pFrameResources = nullptr;
    bd->GVMDevice = nullptr;
    bd->numFramesInFlight = 0;
    bd->frameIndex = UINT_MAX;

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures);
    platform_io.ClearRendererHandlers();
    IM_DELETE(bd);
}

void ImGui_ImplGVM_NewFrame()
{
    ImGui_ImplGVM_Data *bd = ImGui_ImplGVM_GetBackendData();
    if (!bd->pipelineState)
        if (!ImGui_ImplGVM_CreateDeviceObjects())
            IM_ASSERT(0 && "ImGui_ImplGVM_CreateDeviceObjects() failed!");
}

//-----------------------------------------------------------------------------

#endif // #ifndef IMGUI_DISABLE
