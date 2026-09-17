#include "SPIRVPreflight.hpp"

#include <algorithm>
#include <utility>

namespace UGLC::CodeGen::SPIRVEmitter
{
    namespace
    {
        /** Returns true when a reflected resource uses the requested component-table ABI role. */
        bool hasComponentTableRole(const UGLIR::Module &module, UGLIR::ResourceRole role)
        {
            return std::any_of(module.reflection.resources.begin(), module.reflection.resources.end(), [&role](const UGLIR::ResourceBinding &resource) {
                return resource.resourceRole == role;
            });
        }

        /** Returns true when an index-table resource exists for one component-table resource. */
        bool hasComponentIndexTable(const UGLIR::Module &module, UGLIR::ResourceRole role, uint32_t resourceIndex)
        {
            return std::any_of(module.reflection.resources.begin(), module.reflection.resources.end(), [&](const UGLIR::ResourceBinding &resource) {
                return resource.resourceRole == role && resource.resourceIndex == resourceIndex;
            });
        }

        /** Returns true when the stage interface asks the backend to synthesize draw-entity builtins. */
        bool usesDrawInfoBuiltins(const UGLIR::Module &module)
        {
            return std::any_of(module.reflection.stageInputs.begin(), module.reflection.stageInputs.end(), [](const UGLIR::StageIOBinding &binding) {
                return binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityID ||
                       binding.semanticKind == UGLIR::BuiltinSemanticKind::DrawEntityInstanceID;
            });
        }

        /** Appends one preflight diagnostic to the output vector. */
        void addDiagnostic(std::vector<SPIRVPreflightDiagnostic> &diagnostics,
                           const UGLIR::SourceLocation &location,
                           std::string message)
        {
            diagnostics.push_back(SPIRVPreflightDiagnostic{
                .sourceLocation = location,
                .message = std::move(message),
            });
        }
    } // namespace

    std::vector<SPIRVPreflightDiagnostic> runSPIRVBackendPreflight(const UGLIR::Module &module)
    {
        std::vector<SPIRVPreflightDiagnostic> diagnostics;
        uint32_t entryCount = 0;
        for (const UGLIR::Function &function : module.functions)
        {
            if (function.isEntryPoint)
            {
                ++entryCount;
            }
        }
        if (entryCount != 1u)
        {
            addDiagnostic(diagnostics, module.sourceLocation, "direct SPIR-V emission requires exactly one entry function per UGLIR module.");
        }

        if (usesDrawInfoBuiltins(module))
        {
            if (module.reflection.stage != UGLIR::ShaderStage::Vertex)
            {
                addDiagnostic(diagnostics, module.sourceLocation, "draw-entity builtins are only supported for vertex-stage direct SPIR-V emission.");
            }
            if (!hasComponentTableRole(module, UGLIR::ResourceRole::AccessBounds))
            {
                addDiagnostic(diagnostics, module.sourceLocation, "draw-entity builtins require access-bounds reflection.");
            }
            if (!hasComponentTableRole(module, UGLIR::ResourceRole::DrawInfo))
            {
                addDiagnostic(diagnostics, module.sourceLocation, "draw-entity builtins require draw-info reflection.");
            }
            if (!hasComponentTableRole(module, UGLIR::ResourceRole::CommandParams))
            {
                addDiagnostic(diagnostics, module.sourceLocation, "draw-entity builtins require command-params reflection.");
            }
        }

        for (const UGLIR::ResourceBinding &resource : module.reflection.resources)
        {
            if (resource.kind == UGLIR::ResourceKind::BindGroup || resource.kind == UGLIR::ResourceKind::Unknown)
            {
                addDiagnostic(diagnostics, resource.sourceLocation, "unsupported reflected resource kind for direct SPIR-V emission.");
            }
            if (resource.resourceRole == UGLIR::ResourceRole::TextureValue)
            {
                if (resource.arrayCount <= 1u)
                {
                    addDiagnostic(diagnostics, resource.sourceLocation, "component-table texture resources must lower to a descriptor array with MaxResourceCount greater than one.");
                }
                if (resource.textureDimension != UGLIR::TextureDimension::Texture2D)
                {
                    addDiagnostic(diagnostics, resource.sourceLocation, "component-table texture resources must use descriptor-array Texture2D ABI, not array-texture ABI.");
                }
                if (!hasComponentIndexTable(module, UGLIR::ResourceRole::TextureIndexTable, resource.resourceIndex))
                {
                    addDiagnostic(diagnostics, resource.sourceLocation, "component-table texture resources require a matching index-table buffer.");
                }
            }
            if (resource.resourceRole == UGLIR::ResourceRole::BufferValue &&
                !hasComponentIndexTable(module, UGLIR::ResourceRole::BufferIndexTable, resource.resourceIndex))
            {
                addDiagnostic(diagnostics, resource.sourceLocation, "component-table buffer resources require a matching index-table buffer.");
            }
            if (resource.kind == UGLIR::ResourceKind::InputAttachment &&
                module.reflection.entryKind != UGLIR::ShaderEntryKind::PixelLocal)
            {
                addDiagnostic(diagnostics, resource.sourceLocation, "input attachments are only supported on pixel-local fragment entries.");
            }
        }
        return diagnostics;
    }
} // namespace UGLC::CodeGen::SPIRVEmitter
