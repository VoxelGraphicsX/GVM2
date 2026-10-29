#include "WebglInstancingDynamicRuntimeAdapter.hpp"

#include "UGLBin/exports.hpp"

#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        constexpr uint32_t MainInstanceCount = 10000u;
        constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle =
            ExportedRenderSet::sceneSet;
        constexpr GVM::Core::RenderSetHandle EnvironmentRenderSetHandle =
            ExportedRenderSet::environmentSet;
        constexpr double Pi = 3.14159265358979323846;

        static_assert(sizeof(WebglInstancingDynamicHostVertex) == 48u);
        static_assert(sizeof(WebglInstancingDynamicHostObjectData) == 96u);
        static_assert(sizeof(WebglInstancingDynamicHostInstanceData) == 80u);
        static_assert(sizeof(WebglInstancingDynamicHostMaterialData) == 16u);

        /** Advances the xorshift32 stream installed by the reference harness. */
        float nextDynamicRandom(uint32_t &state)
        {
            uint32_t value = state;
            value ^= value << 13u;
            value ^= value >> 17u;
            value ^= value << 5u;
            state = value;
            return static_cast<float>(value >> 8u) / 16777216.0f;
        }

        /** Converts one hue helper branch exactly like Three Color.setHSL. */
        float dynamicHueToRgb(float p, float q, float hue)
        {
            if (hue < 0.0f) hue += 1.0f;
            if (hue > 1.0f) hue -= 1.0f;
            if (hue < 1.0f / 6.0f) return p + (q - p) * 6.0f * hue;
            if (hue < 0.5f) return q;
            if (hue < 2.0f / 3.0f)
                return p + (q - p) * 6.0f * (2.0f / 3.0f - hue);
            return p;
        }

        /** Evaluates Three's working-linear HSL conversion. */
        glm::vec3 makeDynamicHsl(float hue, float saturation, float lightness)
        {
            const float p = lightness <= 0.5f
                ? lightness * (1.0f + saturation)
                : lightness + saturation - lightness * saturation;
            const float q = 2.0f * lightness - p;
            return glm::vec3(
                dynamicHueToRgb(q, p, hue + 1.0f / 3.0f),
                dynamicHueToRgb(q, p, hue),
                dynamicHueToRgb(q, p, hue - 1.0f / 3.0f));
        }

        /** Creates the generated-backend zero-to-one projection. */
        glm::mat4 makeDynamicProjection()
        {
            constexpr double NearDistance = 0.1;
            constexpr double FarDistance = 100.0;
            const double top = NearDistance * std::tan(60.0 * Pi / 360.0);
            const double right = top * (800.0 / 500.0);
            glm::mat4 result(0.0f);
            result[0u][0u] = float(NearDistance / right);
            result[1u][1u] = float(-NearDistance / top);
            result[2u][2u] = float(-FarDistance / (FarDistance - NearDistance));
            result[2u][3u] = -1.0f;
            result[3u][2u] = float(
                -FarDistance * NearDistance / (FarDistance - NearDistance));
            return result;
        }

        /** Computes the deterministic animated Three camera position. */
        glm::vec3 makeDynamicCameraPosition(float timeSeconds)
        {
            return glm::vec3(
                std::sin(timeSeconds / 4.0f) * 10.0f,
                8.0f + std::cos(timeSeconds / 2.0f) * 2.0f,
                std::cos(timeSeconds / 4.0f) * 10.0f);
        }

        /** Creates the animated camera matrix including Three's mutable up vector. */
        glm::mat4 makeDynamicViewProjection(float timeSeconds)
        {
            const glm::vec3 camera = makeDynamicCameraPosition(timeSeconds);
            const glm::vec3 target(
                std::sin(timeSeconds / 4.0f) * -8.0f,
                0.0f,
                std::cos(timeSeconds / 2.0f) * -8.0f);
            const glm::vec3 up = glm::normalize(glm::vec3(
                std::sin(timeSeconds / 400.0f), 1.0f, 0.0f));
            return makeDynamicProjection() *
                glm::lookAtRH(camera, target, up);
        }

        /** Converts the shared box stream into position, normal, and UV vertices. */
        void buildDynamicBox(
            eastl::vector<WebglInstancingDynamicHostVertex> &vertices,
            eastl::vector<uint32_t> &indices)
        {
            eastl::vector<TexturedBoxHostVertex> sourceVertices;
            buildTexturedBoxGeometry(1.0f, 1.0f, 1.0f,
                                     sourceVertices, indices);
            const glm::vec3 faceNormals[] = {
                {1.0f,0.0f,0.0f},{-1.0f,0.0f,0.0f},
                {0.0f,1.0f,0.0f},{0.0f,-1.0f,0.0f},
                {0.0f,0.0f,1.0f},{0.0f,0.0f,-1.0f},
            };
            vertices.reserve(sourceVertices.size());
            for (uint32_t index = 0u;
                 index < sourceVertices.size();
                 ++index)
            {
                const TexturedBoxHostVertex &source = sourceVertices[index];
                vertices.push_back({
                    glm::vec4(source.position.x, source.position.y,
                              source.position.z, source.position.w),
                    glm::vec4(faceNormals[index / 4u], 0.0f),
                    glm::vec4(source.texCoord.x, source.texCoord.y, 0.0f, 0.0f),
                });
            }
        }

        /** Creates one column-major translated and scaled instance matrix. */
        WebglInstancingDynamicHostInstanceData makeDynamicInstance(
            const glm::vec3 &position,
            const glm::vec3 &scale,
            const glm::vec3 &color)
        {
            return {
                glm::vec4(scale.x, 0.0f, 0.0f, 0.0f),
                glm::vec4(0.0f, scale.y, 0.0f, 0.0f),
                glm::vec4(0.0f, 0.0f, scale.z, 0.0f),
                glm::vec4(position, 1.0f),
                glm::vec4(color, 1.0f),
            };
        }

        /** Builds the complete target-frame main instance component. */
        void buildDynamicMainInstances(
            uint32_t targetFrame,
            eastl::vector<WebglInstancingDynamicHostInstanceData> &instances)
        {
            const float time = float(targetFrame) / 60.0f;
            const float tween = 0.0f;
            uint32_t randomState = DefaultThreeRandomSeed;
            for (uint32_t draw = 0u; draw < 380u; ++draw)
                nextDynamicRandom(randomState);
            instances.reserve(MainInstanceCount);
            for (uint32_t x = 0u; x < 100u; ++x)
            {
                for (uint32_t z = 0u; z < 100u; ++z)
                {
                    const float saturation = 0.5f + nextDynamicRandom(randomState) * 0.5f;
                    const float lightness = 0.5f + nextDynamicRandom(randomState) * 0.5f;
                    const glm::vec3 baseColor = makeDynamicHsl(1.0f, saturation, lightness);
                    const float seed = nextDynamicRandom(randomState);
                    const glm::vec3 position(
                        49.5f - float(x),
                        std::abs(std::sin((time + seed) * 2.0f + seed)),
                        49.5f - float(z));
                    const float distanceFraction = glm::length(position) / 75.0f;
                    const bool useNext = tween > 0.0f && distanceFraction <= tween;
                    const glm::vec3 multiplier = useNext
                        ? glm::vec3(1.0f, 1.0f, 0.0f)
                        : glm::vec3(0.0f, 1.0f, 1.0f);
                    instances.push_back(makeDynamicInstance(
                        position, glm::vec3(1.0f, 2.0f, 1.0f),
                        baseColor * multiplier));
                }
            }
            if (randomState != 1280281240u)
                throw std::logic_error("Dynamic instance random stream diverged from the r185 lock.");
        }

        /** Builds one room, one six-box entity, and six area-light entities. */
        void buildDynamicEnvironmentInstances(
            eastl::vector<WebglInstancingDynamicHostInstanceData> &instances)
        {
            instances.reserve(13u);
            instances.push_back(makeDynamicInstance(
                {-0.757f,9.719f,0.717f},{31.713f,28.305f,28.591f},{0.8f,0.8f,0.8f}));
            const glm::vec3 boxPositions[] = {
                {-10.906f,-1.491f,1.846f},{-5.607f,-4.254f,-0.758f},
                {6.167f,-2.643f,7.803f},{-2.017f,-3.482f,6.124f},
                {2.291f,-4.256f,-2.621f},{-2.193f,-3.869f,-5.547f},
            };
            const glm::vec3 boxScales[] = {
                {2.328f,7.905f,4.651f},{1.970f,1.534f,3.955f},
                {3.927f,6.285f,3.687f},{2.002f,4.566f,2.064f},
                {1.546f,1.552f,1.496f},{3.875f,3.487f,2.986f},
            };
            for (uint32_t index=0u; index<6u; ++index)
                instances.push_back(makeDynamicInstance(
                    boxPositions[index], boxScales[index], {0.8f,0.8f,0.8f}));
            const glm::vec3 lightPositions[] = {
                {-16.116f,10.87f,8.208f},{-16.109f,14.521f,-8.207f},
                {14.904f,8.698f,-1.832f},{-0.462f,5.39f,14.520f},
                {3.235f,7.986f,-12.541f},{0.0f,16.5f,0.0f},
            };
            const glm::vec3 lightScales[] = {
                {0.1f,2.428f,2.739f},{0.1f,2.425f,2.751f},
                {0.15f,4.265f,6.331f},{4.38f,5.441f,0.088f},
                {2.5f,2.0f,0.1f},{1.0f,0.1f,1.0f},
            };
            for (uint32_t index=0u; index<6u; ++index)
                instances.push_back(makeDynamicInstance(
                    lightPositions[index], lightScales[index], {1.0f,1.0f,1.0f}));
        }

        /** Appends one typed component payload to an entity allocation. */
        void appendDynamicPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const void *value,
            uint64_t byteCount,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle=component,.bufferName=name,.value=value,
                .dataStorageSize=byteCount,.instanceCount=instanceCount,
            });
        }

        /** Allocates one textured box entity in either logical Scene Set. */
        void allocateDynamicEntity(
            GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
            const eastl::vector<WebglInstancingDynamicHostVertex> &vertices,
            const eastl::vector<uint32_t> &indices,
            const WebglInstancingDynamicHostObjectData &objectData,
            const WebglInstancingDynamicHostInstanceData *instances,
            uint32_t instanceCount,
            const WebglInstancingDynamicHostMaterialData &materialData,
            const RgbaImageData &texture,
            const eastl::vector<uint64_t> &textureMipOffsets,
            const char *name)
        {
            GVM::Core::RenderSetAllocInfo allocation;
            allocation.verticesCount=static_cast<uint32_t>(vertices.size());
            allocation.indicesCount=static_cast<uint32_t>(indices.size());
            allocation.instanceCount=instanceCount;
            appendDynamicPayload(allocation,WebglInstancingDynamicSceneRenderSetComponents::vertices,
                name,vertices.data(),vertices.size()*sizeof(vertices[0]),1u);
            appendDynamicPayload(allocation,WebglInstancingDynamicSceneRenderSetComponents::indices,
                name,indices.data(),indices.size()*sizeof(indices[0]),1u);
            appendDynamicPayload(allocation,WebglInstancingDynamicSceneRenderSetComponents::objects,
                name,&objectData,sizeof(objectData),1u);
            appendDynamicPayload(allocation,WebglInstancingDynamicSceneRenderSetComponents::instances,
                name,instances,uint64_t(instanceCount)*sizeof(*instances),instanceCount);
            appendDynamicPayload(allocation,WebglInstancingDynamicSceneRenderSetComponents::materials,
                name,&materialData,sizeof(materialData),1u);
            GVM::Core::RenderSetTextureComponentAllocInfo textureInfo;
            textureInfo.textureComponentHandle=
                WebglInstancingDynamicSceneRenderSetComponents::textures;
            textureInfo.textures.push_back({
                .textureName=name,
                .format=GVM::RHI::TextureFormat::RGBA8UnormSrgb,
                .width=texture.width,.height=texture.height,
                .data=texture.pixels.data(),
                .dataStorageBytes=texture.pixels.size(),
                .mipmapOffsetBytes=textureMipOffsets,
            });
            allocation.textureInfos.push_back(eastl::move(textureInfo));
            encoder.allocEntity(allocation);
        }

        /** Creates parent directories for a requested evidence file. */
        void prepareDynamicPath(const std::filesystem::path &path)
        {
            if(!path.parent_path().empty())
                std::filesystem::create_directories(path.parent_path());
        }

        /** Writes one optional UTF-8 evidence artifact. */
        void writeDynamicText(const eastl::string &path,const std::string &text)
        {
            if(path.empty()) return;
            const std::filesystem::path outputPath(path.c_str());
            prepareDynamicPath(outputPath);
            std::ofstream output(outputPath,std::ios::trunc);output<<text;
            if(!output) throw std::runtime_error("Could not write dynamic-instancing evidence.");
        }
    }

    void WebglInstancingDynamicRuntimeAdapter::initializeResources(
        GVM::Core::AbstractRendererImpl &renderer,
        GVM::Core::DeviceProxy inDevice,
        const ThreeSampleHostOptions &options)
    {
        const bool initial=options.scenarioId=="initial"&&options.targetFrame==0u;
        const bool animated=options.scenarioId=="animated"&&options.targetFrame==60u;
        const bool tween=options.scenarioId=="color-tween"&&options.targetFrame==240u;
        if(options.caseId!="webgl_instancing_dynamic"||(!initial&&!animated&&!tween)||
           options.width!=800u||options.height!=500u||
           options.randomSeed!=DefaultThreeRandomSeed||options.assetRoot.empty()||
           !options.inputReplayPath.empty())
            throw std::invalid_argument("webgl_instancing_dynamic requires one frozen r185 scenario.");
        device=inDevice;
        buildDynamicBox(vertices,indices);
        buildDynamicMainInstances(options.targetFrame,mainInstances);
        buildDynamicEnvironmentInstances(environmentInstances);
        edgeTexture=decodeJpegRgba8(
            std::filesystem::path(options.assetRoot.c_str())/"textures"/"edge3.jpg");
        const eastl::vector<RgbaImageData> edgeTextureMips =
            buildSrgbMipChain(edgeTexture);
        edgeTexture.pixels.clear();
        edgeTextureMipOffsets.clear();
        for (const RgbaImageData &mip : edgeTextureMips)
        {
            edgeTextureMipOffsets.push_back(edgeTexture.pixels.size());
            edgeTexture.pixels.insert(
                edgeTexture.pixels.end(), mip.pixels.begin(), mip.pixels.end());
        }
        RgbaImageData whiteTexture;
        whiteTexture.width=1u;whiteTexture.height=1u;
        whiteTexture.pixels={255u,255u,255u,255u};
        const float time=float(options.targetFrame)/60.0f;
        const glm::vec3 mainCameraPosition=makeDynamicCameraPosition(time);
        WebglInstancingDynamicHostObjectData mainObject{
            makeDynamicViewProjection(time),glm::vec4(mainCameraPosition,1.0f),
            glm::vec4(time,0.0f,0.0f,0.0f)};
        WebglInstancingDynamicHostMaterialData mainMaterial{{1.0f,1.0f,1.0f,1.0f}};
        const auto sceneEncoder=renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if(!sceneEncoder) throw std::runtime_error("Could not create dynamic main Set encoder.");
        allocateDynamicEntity(*sceneEncoder,vertices,indices,mainObject,
            mainInstances.data(),MainInstanceCount,mainMaterial,edgeTexture,
            edgeTextureMipOffsets,"dynamic-main-grid");
        renderer.executeRenderSetCommand(SceneRenderSetHandle,sceneEncoder);

        const glm::mat4 environmentProjection=makeDynamicProjection()*
            glm::lookAtRH(glm::vec3(0.0f,0.0f,18.0f),glm::vec3(0.0f),glm::vec3(0.0f,1.0f,0.0f));
        WebglInstancingDynamicHostObjectData environmentObject{
            environmentProjection,glm::vec4(0.0f,0.0f,18.0f,1.0f),
            glm::vec4(time,1.0f,0.0f,0.0f)};
        const auto environmentEncoder=renderer.createRenderSetCommandEncoder(EnvironmentRenderSetHandle);
        if(!environmentEncoder) throw std::runtime_error("Could not create dynamic environment Set encoder.");
        WebglInstancingDynamicHostMaterialData roomMaterial{{0.7f,0.7f,0.7f,1.0f}};
        allocateDynamicEntity(*environmentEncoder,vertices,indices,environmentObject,
            &environmentInstances[0],1u,roomMaterial,whiteTexture,{0u},"room");
        allocateDynamicEntity(*environmentEncoder,vertices,indices,environmentObject,
            &environmentInstances[1],6u,roomMaterial,whiteTexture,{0u},"boxes");
        const float intensities[]={50.0f,50.0f,17.0f,43.0f,20.0f,100.0f};
        for(uint32_t light=0u;light<6u;++light)
        {
            WebglInstancingDynamicHostMaterialData lightMaterial{{
                intensities[light],intensities[light],intensities[light],1.0f}};
            allocateDynamicEntity(*environmentEncoder,vertices,indices,environmentObject,
                &environmentInstances[7u+light],1u,lightMaterial,whiteTexture,{0u},"area-light");
        }
        renderer.executeRenderSetCommand(EnvironmentRenderSetHandle,environmentEncoder);
    }

    void WebglInstancingDynamicRuntimeAdapter::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex)
    { (void)renderer;(void)options;(void)frameIndex; }

    void WebglInstancingDynamicRuntimeAdapter::afterFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options,
        uint32_t frameIndex,
        GVM::RHI::Texture readbackTexture,
        uint32_t width,
        uint32_t height)
    {
        (void)renderer;if(captureWritten||frameIndex!=options.targetFrame)return;
        const uint64_t byteCount=uint64_t(width)*height*4u;
        eastl::vector<uint8_t> rgba(static_cast<size_t>(byteCount));
        device->graphicsQueue(0)->readTexture(readbackTexture,rgba.data(),rgba.size())->submit();
        if(!options.captureRgbaPath.empty()){
            const std::filesystem::path path(options.captureRgbaPath.c_str());prepareDynamicPath(path);
            std::ofstream output(path,std::ios::binary|std::ios::trunc);
            output.write(reinterpret_cast<const char*>(rgba.data()),static_cast<std::streamsize>(rgba.size()));
            if(!output)throw std::runtime_error("Could not write dynamic-instancing RGBA.");}
        std::ostringstream metadata;metadata<<"{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_instancing_dynamic\",\n  \"scenarioId\":\""<<options.scenarioId.c_str()<<"\",\n  \"pipeline\":\""<<options.pipeline.c_str()<<"\",\n  \"backend\":\""<<threeSampleBackendName(options.backend)<<"\",\n  \"frame\":"<<frameIndex<<",\n  \"randomSeed\":"<<options.randomSeed<<",\n  \"width\":"<<width<<",\n  \"height\":"<<height<<",\n  \"rowStrideBytes\":"<<uint64_t(width)*4u<<",\n  \"byteCount\":"<<byteCount<<",\n  \"format\":\"rgba8unorm\",\n  \"samplePolicy\":{\"mode\":\"single-sample\",\"msaaEnabled\":false,\"simulateMsaa\":false},\n  \"gpuWorkDslOnly\":true\n}\n";writeDynamicText(options.captureMetadataPath,metadata.str());
        std::ostringstream snapshot;snapshot<<"{\n  \"schemaVersion\":1,\n  \"caseId\":\"webgl_instancing_dynamic\",\n  \"scenarioId\":\""<<options.scenarioId.c_str()<<"\",\n  \"frame\":"<<frameIndex<<",\n  \"renderSetPolicy\":\"required\",\n  \"gpuWorkDslOnly\":true,\n  \"sceneRenderSetCount\":2,\n  \"renderableObjectCount\":9,\n  \"drawCommandCount\":4,\n  \"scenePassCount\":2,\n  \"screenPassCount\":2,\n  \"directDrawFallback\":false,\n  \"usesRenderEntityID\":true,\n  \"usesRenderEntityInstanceID\":true,\n  \"sampleCount\":1,\n  \"msaaEnabled\":false,\n  \"sceneRoots\":[{\"id\":\"scene\",\"renderSetCount\":1,\"renderSetId\":\"scene\",\"renderSetType\":\"WebglInstancingDynamicSceneRenderSet\",\"renderableObjectCount\":1,\"entityCount\":1,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"dynamic-main-grid\",\"instanceCount\":10000}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"main-lit\",\"renderClass\":\"WebglInstancingDynamicMainLitPass\",\"renderSetId\":\"scene\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]},{\"id\":\"roomEnvironment\",\"renderSetCount\":1,\"renderSetId\":\"roomEnvironment\",\"renderSetType\":\"WebglInstancingDynamicSceneRenderSet\",\"renderableObjectCount\":8,\"entityCount\":8,\"entities\":[{\"entityId\":0,\"logicalRenderableId\":\"room\",\"instanceCount\":1},{\"entityId\":1,\"logicalRenderableId\":\"boxes\",\"instanceCount\":6},{\"entityId\":2,\"logicalRenderableId\":\"area-light-1\",\"instanceCount\":1},{\"entityId\":3,\"logicalRenderableId\":\"area-light-2\",\"instanceCount\":1},{\"entityId\":4,\"logicalRenderableId\":\"area-light-3\",\"instanceCount\":1},{\"entityId\":5,\"logicalRenderableId\":\"area-light-4\",\"instanceCount\":1},{\"entityId\":6,\"logicalRenderableId\":\"area-light-5\",\"instanceCount\":1},{\"entityId\":7,\"logicalRenderableId\":\"area-light-6\",\"instanceCount\":1}],\"componentSchema\":[{\"name\":\"vertices\",\"kind\":\"buffer\",\"role\":\"vertex\"},{\"name\":\"indices\",\"kind\":\"buffer\",\"role\":\"index\"},{\"name\":\"objects\",\"kind\":\"buffer\",\"role\":\"object\"},{\"name\":\"instances\",\"kind\":\"buffer\",\"role\":\"instance\"},{\"name\":\"materials\",\"kind\":\"buffer\",\"role\":\"material\"},{\"name\":\"textures\",\"kind\":\"texture\",\"role\":\"texture\"}],\"drawCommandCount\":1,\"directDrawFallback\":false,\"scenePasses\":[{\"name\":\"room-environment-capture\",\"renderClass\":\"WebglInstancingDynamicEnvironmentCapturePass\",\"renderSetId\":\"roomEnvironment\",\"renderSetBindingCount\":1,\"drawMode\":\"render-set-indexed-indirect\",\"invocationCount\":1,\"drawCommandCount\":1,\"usesStandaloneGeometry\":false,\"usesExplicitDrawCount\":false}]}],\n  \"screenPasses\":[{\"name\":\"pmrem-atlas-convolution\",\"renderClass\":\"WebglInstancingDynamicPmremConvolutionPass\",\"drawMode\":\"fullscreen-triangle\",\"drawCommandCount\":1},{\"name\":\"neutral-tone-map-resolve\",\"renderClass\":\"WebglInstancingDynamicNeutralResolvePass\",\"drawMode\":\"fullscreen-triangle\",\"drawCommandCount\":1}]\n}\n";writeDynamicText(options.sceneSnapshotPath,snapshot.str());captureWritten=true;
    }

    void WebglInstancingDynamicRuntimeAdapter::shutdown(
        GVM::Core::AbstractRendererImpl &renderer,
        const ThreeSampleHostOptions &options)
    {
        (void)renderer;(void)options;vertices.clear();indices.clear();
        mainInstances.clear();environmentInstances.clear();edgeTexture.pixels.clear();
        edgeTextureMipOffsets.clear();
    }
} // namespace GVM::ThreeSamples
