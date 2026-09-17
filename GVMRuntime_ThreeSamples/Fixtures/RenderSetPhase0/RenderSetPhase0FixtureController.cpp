#include "RenderSetPhase0FixtureController.hpp"

#include "UGLBin/exports.hpp"

#include <EASTL/array.h>
#include <GVMCore/Public/GAbstractRenderSetCommandEncoder.hpp>
#include <GVMCore/Public/GRenderSetCommand.hpp>

#include <fstream>
#include <stdexcept>

namespace GVM::ThreeSamples
{
    namespace
    {
        static constexpr GVM::Core::RenderSetHandle SceneRenderSetHandle = ExportedRenderSet::sceneSet;

        /** Mirrors one 16-byte shader float4 without depending on generated host symbols. */
        struct alignas(16) RenderSetPhase0FixtureFloat4
        {
            float x;
            float y;
            float z;
            float w;
        };

        /** Mirrors one 16-byte shader uint4 without depending on generated host symbols. */
        struct alignas(16) RenderSetPhase0FixtureUint4
        {
            uint32_t x;
            uint32_t y;
            uint32_t z;
            uint32_t w;
        };

        /** Mirrors the 32-byte RenderSet vertex payload emitted by both UGLC pipelines. */
        struct alignas(16) RenderSetPhase0FixtureVertex
        {
            RenderSetPhase0FixtureFloat4 position;
            RenderSetPhase0FixtureFloat4 texCoord;
        };

        /** Mirrors the 32-byte per-entity object component emitted by both UGLC pipelines. */
        struct alignas(16) RenderSetPhase0FixtureObjectData
        {
            RenderSetPhase0FixtureFloat4 offsetAndScale;
            RenderSetPhase0FixtureUint4 materialAndReserved;
        };

        /** Mirrors the 32-byte per-instance component emitted by both UGLC pipelines. */
        struct alignas(16) RenderSetPhase0FixtureInstanceData
        {
            RenderSetPhase0FixtureFloat4 offsetAndScale;
            RenderSetPhase0FixtureFloat4 tint;
        };

        /** Mirrors the 16-byte per-material component emitted by both UGLC pipelines. */
        struct alignas(16) RenderSetPhase0FixtureMaterialData
        {
            RenderSetPhase0FixtureFloat4 baseColor;
        };

        static_assert(sizeof(RenderSetPhase0FixtureFloat4) == 16u);
        static_assert(sizeof(RenderSetPhase0FixtureUint4) == 16u);
        static_assert(sizeof(RenderSetPhase0FixtureVertex) == 32u);
        static_assert(sizeof(RenderSetPhase0FixtureObjectData) == 32u);
        static_assert(sizeof(RenderSetPhase0FixtureInstanceData) == 32u);
        static_assert(sizeof(RenderSetPhase0FixtureMaterialData) == 16u);

        /** Appends one typed buffer payload to a RenderSet allocation descriptor. */
        template <class Element, size_t ElementCount>
        void appendBufferPayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            GVM::Core::RenderComponentHandle component,
            const char *name,
            const eastl::array<Element, ElementCount> &payload,
            uint32_t instanceCount)
        {
            allocation.bufferInfos.push_back({
                .bufferComponentHandle = component,
                .bufferName = name,
                .value = payload.data(),
                .dataStorageSize = sizeof(Element) * payload.size(),
                .instanceCount = instanceCount,
            });
        }

        /** Appends one deterministic RGBA8 texture to an entity texture component. */
        void appendTexturePayload(
            GVM::Core::RenderSetAllocInfo &allocation,
            const char *name,
            const eastl::array<uint8_t, 4u> &rgba)
        {
            GVM::Core::RenderSetTextureComponentAllocInfo textureComponent;
            textureComponent.textureComponentHandle = RenderSetPhase0SceneSetComponents::albedo;
            textureComponent.textures.push_back({
                .textureName = name,
                .format = GVM::RHI::TextureFormat::RGBA8Unorm,
                .width = 1u,
                .height = 1u,
                .data = rgba.data(),
                .dataStorageBytes = rgba.size(),
                .mipmapOffsetBytes = {0u},
            });
            allocation.textureInfos.push_back(eastl::move(textureComponent));
        }
    } // namespace

    void RenderSetPhase0FixtureController::initialize(GVM::Core::AbstractRendererImpl &renderer)
    {
        if (state.initialized)
        {
            throw std::logic_error("RenderSetPhase0FixtureController can only be initialized once.");
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("RenderSetPhase0 fixture could not create the scene command encoder.");
        }

        state.removedSingleEntity = allocateSingleEntity(*encoder, false);
        state.instancedEntity = allocateInstancedEntity(*encoder);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        state.initialized = true;
    }

    void RenderSetPhase0FixtureController::beforeFrame(
        GVM::Core::AbstractRendererImpl &renderer,
        uint32_t frameIndex)
    {
        if (!state.initialized)
        {
            throw std::logic_error("RenderSetPhase0FixtureController must be initialized before rendering.");
        }
        if (frameIndex != 1u || state.mutationApplied)
        {
            return;
        }

        const auto encoder = renderer.createRenderSetCommandEncoder(SceneRenderSetHandle);
        if (!encoder)
        {
            throw std::runtime_error("RenderSetPhase0 fixture could not create its mutation command encoder.");
        }

        encoder->removeEntity(state.removedSingleEntity);
        state.replacementEntity = allocateSingleEntity(*encoder, true);
        renderer.executeRenderSetCommand(SceneRenderSetHandle, encoder);
        state.mutationApplied = true;
    }

    void RenderSetPhase0FixtureController::writeSnapshot(const std::filesystem::path &outputPath) const
    {
        if (!outputPath.parent_path().empty())
        {
            std::filesystem::create_directories(outputPath.parent_path());
        }

        std::ofstream output(outputPath, std::ios::out | std::ios::trunc);
        if (!output)
        {
            throw std::runtime_error("RenderSetPhase0 fixture could not open the requested snapshot path.");
        }

        output << "{\n"
               << "  \"sceneRenderSetCount\": 1,\n"
               << "  \"entityCount\": 2,\n"
               << "  \"drawCommandCount\": 1,\n"
               << "  \"removedEntity\": " << state.removedSingleEntity << ",\n"
               << "  \"replacementEntity\": " << state.replacementEntity << ",\n"
               << "  \"reusedEntityIndex\": "
               << ((state.mutationApplied && state.removedSingleEntity == state.replacementEntity) ? "true" : "false")
               << ",\n"
               << "  \"mutationApplied\": " << (state.mutationApplied ? "true" : "false") << ",\n"
               << "  \"entities\": [\n"
               << "    {\"role\": \"instanced\", \"entity\": " << state.instancedEntity
               << ", \"instanceCount\": 3},\n"
               << "    {\"role\": \"replacement\", \"entity\": " << state.replacementEntity
               << ", \"instanceCount\": 1}\n"
               << "  ],\n"
               << "  \"componentSchema\": [\"vertices\", \"indices\", \"objects\", \"instances\", \"materials\", \"albedo\"]\n"
               << "}\n";
    }

    const RenderSetPhase0FixtureState &RenderSetPhase0FixtureController::getState() const
    {
        return state;
    }

    GVM::Core::RenderEntityIndex RenderSetPhase0FixtureController::allocateSingleEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder,
        bool replacement) const
    {
        const float horizontalOffset = replacement ? 0.55f : -0.55f;
        const RenderSetPhase0FixtureFloat4 materialColor = replacement
            ? RenderSetPhase0FixtureFloat4{0.15f, 0.85f, 0.95f, 1.0f}
            : RenderSetPhase0FixtureFloat4{0.95f, 0.25f, 0.15f, 1.0f};
        const eastl::array<uint8_t, 4u> texture = replacement
            ? eastl::array<uint8_t, 4u>{160u, 255u, 255u, 255u}
            : eastl::array<uint8_t, 4u>{255u, 190u, 145u, 255u};

        const eastl::array<RenderSetPhase0FixtureVertex, 3u> vertices = {
            RenderSetPhase0FixtureVertex{.position = {-0.65f, -0.55f, 0.0f, 1.0f}, .texCoord = {0.0f, 1.0f, 0.0f, 0.0f}},
            RenderSetPhase0FixtureVertex{.position = {0.65f, -0.55f, 0.0f, 1.0f}, .texCoord = {1.0f, 1.0f, 0.0f, 0.0f}},
            RenderSetPhase0FixtureVertex{.position = {0.0f, 0.65f, 0.0f, 1.0f}, .texCoord = {0.5f, 0.0f, 0.0f, 0.0f}},
        };
        const eastl::array<uint32_t, 3u> indices = {0u, 1u, 2u};
        const eastl::array<RenderSetPhase0FixtureObjectData, 1u> objects = {
            RenderSetPhase0FixtureObjectData{
                .offsetAndScale = {horizontalOffset, 0.36f, 0.38f, 0.38f},
                .materialAndReserved = {0u, 0u, 0u, 0u},
            },
        };
        const eastl::array<RenderSetPhase0FixtureInstanceData, 1u> instances = {
            RenderSetPhase0FixtureInstanceData{
                .offsetAndScale = {0.0f, 0.0f, 1.0f, 1.0f},
                .tint = {1.0f, 1.0f, 1.0f, 1.0f},
            },
        };
        const eastl::array<RenderSetPhase0FixtureMaterialData, 1u> materials = {
            RenderSetPhase0FixtureMaterialData{.baseColor = materialColor},
        };

        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = 1u;
        const char *suffix = replacement ? "Replacement" : "Initial";
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::vertices, suffix, vertices, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::indices, suffix, indices, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::objects, suffix, objects, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::instances, suffix, instances, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::materials, suffix, materials, 1u);
        appendTexturePayload(allocation,
                             replacement ? "RenderSetPhase0ReplacementAlbedo" : "RenderSetPhase0InitialAlbedo",
                             texture);
        return encoder.allocEntity(allocation);
    }

    GVM::Core::RenderEntityIndex RenderSetPhase0FixtureController::allocateInstancedEntity(
        GVM::Core::AbstractRenderSetCommandEncoderImpl &encoder) const
    {
        const eastl::array<RenderSetPhase0FixtureVertex, 4u> vertices = {
            RenderSetPhase0FixtureVertex{.position = {-0.65f, -0.65f, 0.0f, 1.0f}, .texCoord = {0.0f, 1.0f, 0.0f, 0.0f}},
            RenderSetPhase0FixtureVertex{.position = {0.65f, -0.65f, 0.0f, 1.0f}, .texCoord = {1.0f, 1.0f, 0.0f, 0.0f}},
            RenderSetPhase0FixtureVertex{.position = {0.65f, 0.65f, 0.0f, 1.0f}, .texCoord = {1.0f, 0.0f, 0.0f, 0.0f}},
            RenderSetPhase0FixtureVertex{.position = {-0.65f, 0.65f, 0.0f, 1.0f}, .texCoord = {0.0f, 0.0f, 0.0f, 0.0f}},
        };
        const eastl::array<uint32_t, 6u> indices = {0u, 1u, 2u, 0u, 2u, 3u};
        const eastl::array<RenderSetPhase0FixtureObjectData, 1u> objects = {
            RenderSetPhase0FixtureObjectData{
                .offsetAndScale = {0.0f, -0.34f, 0.42f, 0.42f},
                .materialAndReserved = {0u, 0u, 0u, 0u},
            },
        };
        const eastl::array<RenderSetPhase0FixtureInstanceData, 3u> instances = {
            RenderSetPhase0FixtureInstanceData{.offsetAndScale = {-0.58f, 0.0f, 0.72f, 0.72f}, .tint = {0.25f, 0.95f, 0.30f, 1.0f}},
            RenderSetPhase0FixtureInstanceData{.offsetAndScale = {0.0f, 0.0f, 0.72f, 0.72f}, .tint = {0.25f, 0.50f, 1.0f, 1.0f}},
            RenderSetPhase0FixtureInstanceData{.offsetAndScale = {0.58f, 0.0f, 0.72f, 0.72f}, .tint = {1.0f, 0.82f, 0.22f, 1.0f}},
        };
        const eastl::array<RenderSetPhase0FixtureMaterialData, 1u> materials = {
            RenderSetPhase0FixtureMaterialData{.baseColor = {0.90f, 0.90f, 0.90f, 1.0f}},
        };
        const eastl::array<uint8_t, 4u> texture = {235u, 245u, 255u, 255u};

        GVM::Core::RenderSetAllocInfo allocation;
        allocation.verticesCount = static_cast<uint32_t>(vertices.size());
        allocation.indicesCount = static_cast<uint32_t>(indices.size());
        allocation.instanceCount = static_cast<uint32_t>(instances.size());
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::vertices, "InstancedVertices", vertices, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::indices, "InstancedIndices", indices, 1u);
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::objects, "InstancedObject", objects, 1u);
        appendBufferPayload(allocation,
                            RenderSetPhase0SceneSetComponents::instances,
                            "InstancedTransforms",
                            instances,
                            static_cast<uint32_t>(instances.size()));
        appendBufferPayload(allocation, RenderSetPhase0SceneSetComponents::materials, "InstancedMaterial", materials, 1u);
        appendTexturePayload(allocation, "RenderSetPhase0InstancedAlbedo", texture);
        return encoder.allocEntity(allocation);
    }
} // namespace GVM::ThreeSamples
