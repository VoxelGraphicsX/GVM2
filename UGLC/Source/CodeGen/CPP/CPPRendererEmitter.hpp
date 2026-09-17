#pragma once

#include <string>
#include <utility>
#include <vector>

#include <clang/AST/DeclCXX.h>

namespace UGLC::CodeGen::CPP
{
    class CPPVisitor;

    /**
     * Emits host C++ wrappers for DSL renderer classes.
     *
     * A DSL renderer derives from `UGL::AbstractRenderer` and may export ordinary host
     * variables or render sets. This emitter owns the generated `RendererImpl` class,
     * ring-buffered export setters, render-set command dispatch, and `exports.hpp`
     * declarations while `CPPVisitor` remains responsible for AST traversal.
     *
     * DSL example:
     * @code
     * struct SceneRenderer : UGL::AbstractRenderer
     * {
     *     Camera camera [[Export]];
     *     UGL::RenderSet<SceneSet> scene [[Export]];
     *     void render();
     * };
     * @endcode
     *
     * Generated host C++ example:
     * @code
     * class SceneRendererImpl : public GVM::Core::AbstractRendererImpl
     * {
     *     eastl::array<Camera, 3> camera__UGL__ARRAY;
     *     void renderImpl();
     *     void setCamera(const Camera& var);
     * };
     * using SceneRenderer = eastl::intrusive_ptr<SceneRendererImpl>;
     * @endcode
     */
    class CPPRendererEmitter
    {
    public:
        /** Creates an emitter that uses the owning visitor for type spelling and function lowering. */
        explicit CPPRendererEmitter(CPPVisitor &visitor);

        /** Emits the host C++ replacement for one renderer DSL record. */
        std::string replaceRendererClass(const clang::CXXRecordDecl *decl);

    private:
        /** Returns the hidden frame-ring array name used for an exported renderer variable. */
        std::string generateExportArrayVariableName(const std::string &name) const;

        /** Emits the enum that tracks which exported variables need delayed frame-ring assignment. */
        std::string generateExportVariableEnum(const std::vector<std::pair<std::string, clang::FieldDecl *>> &rendererExportedVariables);

        /** Emits host virtual methods that route render-set command handles to exported render sets. */
        std::string makeRenderSetExecuteCommand(const std::vector<std::string> &exportedRenderSets);

        /** Emits the `exports.hpp` constants for exported render sets and their component handles. */
        std::string makeExportHeader(const std::string &rendererNamespaceName,
                                     const std::vector<std::string> &exportedRenderSets,
                                     const std::vector<std::string> &exportedRenderSetTypes);

        CPPVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::CPP
