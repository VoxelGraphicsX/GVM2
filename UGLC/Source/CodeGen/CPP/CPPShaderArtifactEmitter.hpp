#pragma once

#include <string>
#include <vector>

#include <CodeGen/ShaderBindGroupInfo.hpp>

namespace clang
{
    class ClassTemplateSpecializationDecl;
    class CXXRecordDecl;
    class Decl;
    class FunctionDecl;
} // namespace clang

namespace UGLC::CodeGen
{
    class IShaderSourceEmitter;
} // namespace UGLC::CodeGen

namespace UGLC::CodeGen::CPP
{
    class CPPVisitor;

    /**
     * Emits embedded shader artifact members for generated host C++ classes.
     *
     * Render and compute DSL classes are lowered into host wrappers that keep the
     * selected backend shader text next to optional HLSL/SPIR-V companion data. This
     * emitter owns that stage-local artifact assembly while the visitor continues to
     * own class traversal and high-level wrapper layout.
     *
     * DSL example:
     * @code
     * struct Particles : UGL::AbstractCompute
     * {
     *     [[LocalWorkGroupSize(64, 1, 1)]]
     *     void compute(uint3 threadId [[GlobalInvocationID]]);
     * };
     * @endcode
     *
     * Generated host C++ example:
     * @code
     * const UGLC::Generated::ShaderArtifact computeShaderArtifact =
     *     UGLC::Generated::MakeShaderArtifact(mslPrelude + R"(...)",
     *                                         hlslSource,
     *                                         computeShaderArtifact_SpirvWords,
     *                                         spirvWordCount);
     * @endcode
     */
    class CPPShaderArtifactEmitter
    {
    public:
        /** Creates an artifact emitter backed by the owning host C++ visitor. */
        explicit CPPShaderArtifactEmitter(CPPVisitor &visitor);

        /** Emits one generated `ShaderArtifact` member for a render or compute shader stage. */
        std::string buildEmbeddedShaderArtifactMember(const std::string &memberName,
                                                      const std::string &shaderHeaderVariableName,
                                                      const clang::CXXRecordDecl *shaderClassDecl,
                                                      const clang::FunctionDecl *entryFunction,
                                                      const BindGroupInfoMap &bindGroupInfoMap,
                                                      const std::vector<clang::Decl *> &extraDecls,
                                                      UGLC::CodeGen::IShaderSourceEmitter &shaderSourceEmitter,
                                                      const clang::ClassTemplateSpecializationDecl *templateSpecialization = nullptr);

    private:
        CPPVisitor &mVisitor;
    };
} // namespace UGLC::CodeGen::CPP
