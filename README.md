# GVM

GVM is a C++20 graphics runtime with a shader domain-specific language (DSL).
Write GPU programs and resource bindings in UGL, compile them with UGLC, and
use the generated C++ interfaces to submit graphics and compute work through
Metal or Vulkan.

## Architecture

| Component | Role |
| --- | --- |
| [GVMRHI](GVM/GVMRHI) | Rendering hardware interface, resources, command submission, and Metal/Vulkan backends. |
| [GVMCore](GVM/GVMCore) | Runtime support for the generated C++ interfaces. |
| [UGL](GVM/UGLHeaders) | Shader DSL types and declarations, exposed through `<UGL.h>`. |
| [UGLC](UGLC) | Compiler that generates C++ host interfaces and shader artifacts from UGL headers. |

```text
UGL headers -> UGLC -> generated C++ interfaces + MSL / SPIR-V
                              |
                         GVMCore -> GVMRHI -> Metal / Vulkan
```

UGLC uses the UGLIR shader pipeline by default. It emits Metal Shading Language
(MSL) and SPIR-V directly. The optional Legacy pipeline is disabled by default
and must be enabled at build time; UGLIR does not silently fall back to Legacy.
Hull and domain shader stages are not currently supported by UGLIR.

## Platform status

| Platform / backend | Validation status |
| --- | --- |
| macOS on Apple silicon / Metal | Primary development platform; locally validated. |
| macOS on Apple silicon / Vulkan via MoltenVK | Locally validated with a working Vulkan runtime. |
| Linux and Windows | Platform code is present; full runtime validation is still outstanding. |
| Android and OpenHarmony | Experimental sample host integration. |

These statuses describe the current validation scope, not a compatibility
guarantee for every device or driver. GPU tests and windowed samples require
a working graphics runtime. See the current [CI results](https://github.com/VoxelGraphicsX/GVM2/actions)
for automated checks.

## Quick start on macOS

### Requirements

- Git, CMake 3.22 or newer, and a C++20 compiler.
- macOS 15 or newer for the Metal 3.2 runtime. CI uses Xcode 16.4
  with its macOS SDK for Metal and Objective-C++.
- Python 3 when using the automatic LLVM download for UGLC.
- Node.js 20 or newer to run the test runners.
- A working Vulkan loader and MoltenVK installation to run Vulkan applications.
  Dependency downloads provide build headers and libraries, but do not install
  a system Vulkan runtime.

```sh
git clone https://github.com/VoxelGraphicsX/GVM2.git GVM
cd GVM
```

### Build the Metal runtime

This configuration builds the runtime without the compiler, samples, or tests.
It does not require LLVM or a Vulkan runtime.

```sh
cmake -S . -B build/runtime \
  -DCMAKE_BUILD_TYPE=Release \
  -DGVM_BUILD_SAMPLES=OFF \
  -DGVM_BUILD_TESTS=OFF \
  -DGVM_BUILD_UGLC=OFF \
  -DGVM_RHI_ENABLE_VULKAN=OFF \
  -DGVM_INSTALL=ON
cmake --build build/runtime --parallel 3
```

### Build UGLC and the triangle sample

Use a separate build directory to enable the compiler and ordinary samples.
UGLC downloads its pinned LLVM bundle on the first configure unless a compatible
local toolchain is supplied. The first build can therefore take considerably
longer than a runtime-only build.

```sh
cmake -S . -B build/samples \
  -DCMAKE_BUILD_TYPE=Release \
  -DGVM_BUILD_SAMPLES=ON \
  -DGVM_BUILD_TESTS=OFF \
  -DGVM_BUILD_UGLC=ON \
  -DGVM_RHI_ENABLE_VULKAN=ON \
  -DUGLC_ENABLE_LEGACY=OFF
cmake --build build/samples --target UGLC --parallel 3
cmake --build build/samples \
  --target GVMRUNTIMES_SAMPLES_Test_01_Triangle_bin --parallel 3
```

The [triangle application](GVMRuntime_Samples/Test_01_Triangle/main.cpp) explicitly
selects Vulkan. With Vulkan/MoltenVK available, run it from the repository root:

```sh
./build/samples/GVMRUNTIMES_SAMPLES_Test_01_Triangle_bin
```

This opens an interactive window; closing it exits the sample. Its shaders use
the default UGLIR pipeline. Additional examples are in
[GVMRuntime_Samples](GVMRuntime_Samples) and [GVMRHI_Samples](GVMRHI_Samples).

## UGL examples

UGL uses C++-style syntax with DSL-specific declarations such as `constructor`
and binding attributes. Compile these headers with UGLC to generate the host
interfaces and shaders; they are not ordinary C++ translation units to compile
directly with your application compiler.

### Draw a triangle

The vertex shader generates three positions and colors without a vertex buffer.
The fragment shader writes the interpolated color to an RGBA8 attachment.

```cpp
#include "UGL.h"
using namespace UGL;

/// Carries clip-space position and interpolated color.
struct TriangleVertex
{
    float4 position [[Position]];
    float4 color [[Attribute0]];
};

/// Defines the triangle's color attachment.
struct TriangleFrameBuffer : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/// Draws a colored triangle without a vertex buffer.
class Triangle final : public IRenderClass
{
public:
    /// Creates a render pipeline with no resource bindings.
    constructor() {}

private:
    /// Generates one of the three vertices by its vertex ID.
    TriangleVertex vertex(uint id [[VertexID]])
    {
        const float2 positions[3] = {
            float2( 0.0f,  0.7f),
            float2( 0.7f, -0.7f),
            float2(-0.7f, -0.7f),
        };
        const float4 colors[3] = {
            float4(1.0f, 0.2f, 0.2f, 1.0f),
            float4(0.2f, 1.0f, 0.2f, 1.0f),
            float4(0.2f, 0.2f, 1.0f, 1.0f),
        };

        TriangleVertex output;
        output.position = float4(positions[id], 0.0f, 1.0f);
        output.color = colors[id];
        return output;
    }

    /// Writes the interpolated vertex color to the attachment.
    TriangleFrameBuffer fragment(TriangleVertex input)
    {
        TriangleFrameBuffer output;
        output.color = half4(input.color);
        return output;
    }
};
```

Bind an RGBA8 render target and draw three vertices with `firstVertex = 0`.
The [triangle renderer](GVMRuntime_Samples/Test_01_Triangle/Test_01_Triangle.hpp)
demonstrates render-class creation, attachment setup, submission, and presentation.
Its [application entry point](GVMRuntime_Samples/Test_01_Triangle/main.cpp) provides
the window and frame loop used by the quick start.

### Generate a texture with compute

Each thread writes one pixel of a two-dimensional color gradient. The shader
uses an 8-by-8 workgroup and checks the texture bounds before writing.

```cpp
#include "UGL.h"
using namespace UGL;

/// Binds the writable output texture.
struct GradientResources final : public IBindGroup
{
    /// Binds an RGBA8 texture with storage usage.
    constructor(
        RWTexture2D<TextureFormat::RGBA8Unorm> image [[Binding0]]
    ) {}
};

/// Generates a color gradient with one thread per pixel.
class [[LocalWorkGroupSize(8, 8, 1)]] Gradient final
    : public IComputeClass
{
public:
    /// Creates the compute pipeline with its output binding.
    constructor(
        BindGroup<GradientResources> resources [[Slot0]]
    ) {}

private:
    /// Writes the pixel-center coordinates as red and green.
    void compute(uint3 id [[DispatchThreadID]])
    {
        uint width, height;
        resources->image->getDimensions(width, height);

        if (id.x >= width || id.y >= height)
            return;

        const float2 uv =
            (float2(id.xy) + float2(0.5f)) / float2(width, height);

        resources->image->write(
            id.xy, half4(uv.x, uv.y, 0.25f, 1.0f));
    }
};
```

Create an RGBA8 texture with storage usage, bind its view through
`GradientResources`, and dispatch `width` by `height` by `1` threads. GVM converts
these thread counts to workgroup counts; the bounds check handles partial groups.
The compute-pattern case in the [render feature examples](tests/render_cases/RenderFeatureSuite.hpp)
demonstrates storage-texture setup, compute submission, and presentation.

For an installed-package example of the `UGLCOMPILE` CMake integration, see the
[installed consumer](tests/install/CMakeLists.txt).

## Use GVM in an application

### From source

Place the repository at `external/GVM` in your application and add it before
requesting GLM or EASTL:

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyApp LANGUAGES C CXX)

set(GVM_BUILD_SAMPLES OFF CACHE BOOL "Build GVM samples")
set(GVM_BUILD_TESTS OFF CACHE BOOL "Build GVM tests")
set(GVM_BUILD_UGLC OFF CACHE BOOL "Build UGLC")
add_subdirectory(external/GVM)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE GVM::GVM)
```

Enable `GVM_BUILD_UGLC` when generating DSL code in this build. Alternatively,
set `GVM_UGLC_EXECUTABLE` to an existing host UGLC executable while leaving the
in-tree compiler disabled.

### From an installation

Install the runtime built in the quick start:

```sh
cmake --install build/runtime --prefix "$PWD/build/install"
```

Set `CMAKE_PREFIX_PATH` to that install prefix when configuring your application:

```cmake
cmake_minimum_required(VERSION 3.22)
project(MyApp LANGUAGES C CXX)

find_package(GVM CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE GVM::GVM)
```

The CMake targets propagate the required include paths, link dependencies, and
C++20 requirement. Public entry points include `<UGL.h>`,
`<GVMRHI/GVMRHI.hpp>`, and `<GVMCore/GVMCore.Public.hpp>`.

The installation contains the runtime SDK and code-generation CMake integration;
it does not install UGLC or its LLVM toolchain. Generating new DSL code requires
a separately built host UGLC. The [installed consumer](tests/install/CMakeLists.txt)
shows how to supply it. Installed consumers also resolve pinned dependency
sources through CMake, so an uncached configure needs network access.

### GLM and EASTL

Source and installed consumers use the same dependency rules. GVM supplies
GLM 1.0.1 and EASTL 3.27.01 with the configuration needed by its public types
and allocator. Add or find GVM first, then reuse its targets:

```cmake
target_link_libraries(app PRIVATE GVM::GVM GVM::GLM GVM::EASTL)
```

`GVM::GVM` already carries its runtime dependencies. The separate targets are
also available to components that use only the supplied math or containers.
Do not introduce a second GLM/EASTL copy or override their configuration macros
in the same application. GVM rejects independently provided dependency targets
rather than combining incompatible configurations. Other math and container
libraries can be used in application code; convert their values at GVM API
boundaries.

## Build options and compiler toolchain

Defaults below apply when configuring GVM as the top-level project:

| CMake option | Default | Purpose |
| --- | --- | --- |
| `GVM_BUILD_UGLC` | `ON` | Build the host shader compiler. |
| `GVM_BUILD_SAMPLES` | `ON` | Enable ordinary samples. |
| `GVM_BUILD_TESTS` | `ON` | Build automated runtime tests. |
| `GVM_INSTALL` | `ON` | Enable runtime package installation. |
| `GVM_RHI_ENABLE_VULKAN` | `ON` | Build the Vulkan backend. |
| `UGLC_ENABLE_LEGACY` | `OFF` | Build the optional Legacy shader pipeline and DXC dependency. |
| `GVM_BUILD_THREE_SAMPLES` | `OFF` | Enable the Three.js compatibility sample matrix. |
| `GVM_RHI_ENABLE_LOGGING` | `OFF` | Enable runtime logging support. |
| `GVM_RHI_USE_SPDLOG_LOGGER` | `OFF` | Add the built-in spdlog provider when logging is enabled. |

Samples, tests, and installation default to `OFF` when GVM is added as a
subdirectory; UGLC still defaults to `ON`. The quick-start commands set these
options explicitly to control the size of the build.

UGLC uses Clang/LLVM 20.1.7. Automatic download URLs and SHA-256 checksums are
recorded in the [toolchain manifest](cmake/UGLC/llvm_bundles.json). To use a
compatible local bundle, configure with `UGLC_LLVM_AUTO_DOWNLOAD=OFF` and
`UGLC_LLVM_ROOT` pointing to its root directory.

When invoking UGLC, `--shader-pipeline=uglir` selects the default pipeline
explicitly. `--shader-pipeline=legacy` requires a compiler built with
`UGLC_ENABLE_LEGACY=ON`. Runtime backend selection is separate: applications
can set `InstanceDescriptor::preferredBackend`, while the Node test runner
accepts `--backend metal` or `--backend vulkan`. An unspecified backend defaults
to Metal on macOS.

Downloaded sources live in `.cache/cpm`, LLVM bundles in `.cache/uglc/llvm`, and
dependency build products in `.cache/cpm-build/<build-key>`. Each build directory
has a separate dependency build key. Use `CPM_SOURCE_CACHE` and
`UGLC_LLVM_CACHE_DIR` CMake options to share download caches. Reuse build
directories when appropriate; removing `.cache` causes downloads and dependency
builds to be repeated. Generated code, executables, and test reports remain in
the corresponding build directory.

## Tests

Configure and build a dedicated test tree with the default UGLIR pipeline:

```sh
cmake -S . -B build/tests -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DGVM_BUILD_SAMPLES=OFF \
  -DGVM_BUILD_TESTS=ON \
  -DGVM_BUILD_UGLC=ON \
  -DGVM_RHI_ENABLE_VULKAN=ON \
  -DUGLC_ENABLE_LEGACY=OFF
cmake --build build/tests --parallel 3
```

Run the compiler tests:

```sh
node tests/runners/uglc/node/cli.mjs run \
  --build-dir build/tests --group all \
  --no-build true --no-open-report true
```

Run the full GVM Metal suite:

```sh
node tests/runners/node/cli.mjs run \
  --profile macos-debug --build-dir build/tests \
  --backend metal --group all --no-open-report true
```

For Vulkan, use the same command with `--backend vulkan` and a working Vulkan
runtime. Run GPU suites sequentially. The GVM runner configures and builds the
selected test tree before execution; render tests use hidden background windows.
Pass/fail/skip results are recorded in the generated report.

### Reports, images, and logs

Reports stay under the build tree. By default, each runner overwrites its
`runs/latest` report, writes raw test images where applicable, and rotates logs.
Reports redact machine-specific paths and identity fields.

| Runner option | Default | Effect |
| --- | --- | --- |
| `--write-images true\|false` | `true` | GVM only: write raw RGBA and PPM images. Disabling this also skips file-based pixel comparisons. |
| `--output-mode overwrite\|timestamp` | `overwrite` | Reuse the latest report or create timestamped runs. |
| `--log-mode rotate\|timestamp` | `rotate` | Rotate logs in place or group them by run. |
| `--keep-runs N` | `2` | Maximum retained managed report directories. |
| `--keep-log-runs N` | `2` | Maximum retained timestamped log groups per report. |
| `--log-max-mib N` | `10` | Per-file text log limit, up to 10 MiB. |
| `--log-backups N` | `2` | Rotated backups per log, up to two. |

The runners stop below 10 GiB of free disk space, above 1 GiB per report, or
above 2 GiB of managed reports. Retention applies to directories created by the
current output policy; older unmanaged directories are left untouched.

## License

GVM is licensed under [Apache-2.0](LICENSE). See [NOTICE](NOTICE),
[third-party notices](THIRD_PARTY_NOTICES.md), and the [license texts](licenses)
for dependencies and incorporated code. These notices are also included in
the runtime installation.
