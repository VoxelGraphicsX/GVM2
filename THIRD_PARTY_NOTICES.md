# Third-Party Notices

GVM is licensed under Apache-2.0. Third-party code retains its own copyright
and license. The following records cover vendored sources, runtime dependencies,
compiler dependencies, ordinary samples, ThreeSamples, and tests. Optional private
sample dependencies are identified separately; private sample sources are not
included in the public distribution.

Full license and notice texts are included in `licenses/`. Existing copyright
notices in source headers must also be retained. `licenses/manifest.json` records
the source, version and SHA-256 of each accompanying text.

| Component | Version | License | Included license / notice |
| --- | --- | --- | --- |
| EASTL | 3.27.01 | BSD-3-Clause | [EASTL.txt](licenses/EASTL.txt) |
| EABase | 0699a15efdfd20b6cecf02153bfa5663decb653c | BSD-3-Clause | [EABase.txt](licenses/EABase.txt) |
| glm | 1.0.1 | MIT or Happy Bunny | [glm.txt](licenses/glm.txt) |
| cxxopts | v3.3.1 | MIT | [cxxopts.txt](licenses/cxxopts.txt) |
| nlohmann-json | v3.12.0 | MIT | [nlohmann-json.txt](licenses/nlohmann-json.txt) |
| GoogleTest | v1.15.2 | BSD-3-Clause | [GoogleTest.txt](licenses/GoogleTest.txt) |
| SDL2 | release-2.32.10 | Zlib | [SDL2.txt](licenses/SDL2.txt) |
| SDL2-hidapi | release-2.32.10 | BSD-3-Clause | [SDL2-hidapi.txt](licenses/SDL2-hidapi.txt) |
| DearImGui | v1.92.4 | MIT | [DearImGui.txt](licenses/DearImGui.txt) |
| volk | 1.4.304 | MIT | [volk.txt](licenses/volk.txt) |
| VulkanMemoryAllocator | v3.1.0 | MIT | [VulkanMemoryAllocator.txt](licenses/VulkanMemoryAllocator.txt) |
| SPIRV-Reflect | 10b4f09a24d7ac1603071e767c089551dc6a3949 | Apache-2.0 | [SPIRV-Reflect.txt](licenses/SPIRV-Reflect.txt) |
| SPIRV-Headers | vulkan-sdk-1.4.304.0 | MIT | [SPIRV-Headers.txt](licenses/SPIRV-Headers.txt) |
| SPIRV-Tools | vulkan-sdk-1.4.304.0 | Apache-2.0 | [SPIRV-Tools.txt](licenses/SPIRV-Tools.txt) |
| draco | 1.5.7 | Apache-2.0 | [draco.txt](licenses/draco.txt) |
| pugixml | v1.15 | MIT | [pugixml.txt](licenses/pugixml.txt) |
| meshoptimizer | v1.1.1 | MIT | [meshoptimizer.txt](licenses/meshoptimizer.txt) |
| tinyobjloader | v1.0.7 | MIT | [tinyobjloader.txt](licenses/tinyobjloader.txt) |
| fmt | 11.1.4 | MIT | [fmt.txt](licenses/fmt.txt) |
| spdlog | v1.15.3 | MIT | [spdlog.txt](licenses/spdlog.txt) |
| DirectXShaderCompiler | v1.8.2505.1 | LLVM and Microsoft licenses | [DirectXShaderCompiler.txt](licenses/DirectXShaderCompiler.txt) |
| DirectXShaderCompiler-ThirdParty | v1.8.2505.1 | See notices | [DirectXShaderCompiler-ThirdParty.txt](licenses/DirectXShaderCompiler-ThirdParty.txt) |
| PowerVR | vendored | MIT | [PowerVR.txt](licenses/PowerVR.txt) |
| Metal-cpp | vendored | Apache-2.0 | [Metal-cpp.txt](licenses/Metal-cpp.txt) |
| dds-ktx | v1.1.0 vendored | BSD-2-Clause | [dds-ktx.txt](licenses/dds-ktx.txt) |
| bimg | copyright retained in dds-ktx | BSD-2-Clause | [bimg.txt](licenses/bimg.txt) |
| Three-js | r185 | MIT | [Three-js.txt](licenses/Three-js.txt) |
| LLVM | llvmorg-20.1.7 | Apache-2.0 WITH LLVM-exception | [LLVM.txt](licenses/LLVM.txt) |
| Vulkan-Headers | vulkan-sdk-1.3.280.0 | Apache-2.0 and MIT | [Vulkan-Headers.txt](licenses/Vulkan-Headers.txt) |
| defer | vendored | BSD-3-Clause | [defer.txt](licenses/defer.txt) |
| Checkerboard | vendored | MIT | [Checkerboard.txt](licenses/Checkerboard.txt) |
| CPM-cmake | vendored | MIT | [CPM-cmake.txt](licenses/CPM-cmake.txt) |

## Distribution details

- dds-ktx includes portions of bimg; both copyright notices and license texts are retained.
- GLM offers alternative licenses; GVM uses its MIT option.
- Vulkan-Headers permits Apache-2.0 for its API headers; the accompanying text includes that full grant. The upstream top-level notice is also retained.
- Three.js-derived compatibility code and procedural sample assets use the r185 MIT notice. The PowerVR decoder retains its separate MIT notice.
- DirectXShaderCompiler is optional when `UGLC_ENABLE_LEGACY=ON`; both its main license and third-party notices are included.
- meshoptimizer and tinyobjloader are used only by optional local advanced samples. Their inclusion here does not distribute those private samples.
- Dependencies fetched during configuration retain their upstream source-header notices. External asset packs and platform SDKs are not bundled with this source distribution; redistributors must retain the licenses of any additional assets or SDK components they separately include.

Runtime installation includes GVM's `LICENSE`, `NOTICE`, this attribution file,
and the accompanying license texts under `share/licenses/GVM` (or the configured
CMake data installation directory). Keep these materials with redistributed
source and binary packages.
