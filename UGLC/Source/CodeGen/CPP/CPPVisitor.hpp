#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "CPPBindGroupTypeConvertor.hpp"
#include "CPPFunctionConvertor.hpp"
#include "CPPHostShaderOnlyGuard.hpp"
#include "CPPRendererEmitter.hpp"
#include "CPPShaderArtifactEmitter.hpp"
#include "CPPTypeConvertor.hpp"
#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/ShaderBindGroupInfo.hpp>
#include <CodeGen/ShaderSourceEmitter.hpp>
#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/UGLC.Constants.hpp>

namespace clang
{
    class ClassTemplateDecl;
}

namespace UGLC::CodeGen
{
    class IShaderSourceEmitter;
}

namespace UGLC::CodeGen::CPP
{
    class CPPHostClassValidator;

    struct BufferUsageInfo
    {
        std::vector<std::string> usageInfos;
        std::string typeName;
    };
    struct TextureUsageInfo
    {
        std::vector<std::string> usageInfos;
        std::string typeName;
        std::string dimension; // 2D, 3D, Cube
        bool usesPixelLocalAttachment = false;
        bool usesPersistentTextureAccess = false;
    };
    std::string convertBufferUsage(const std::string &canonicalName);
    std::string convertTextureUsage(const std::string &canonicalName);
    std::string convertTextureDimension(const std::string &canonicalName);
    std::string convertTextureFormat(const std::string &canonicalName);
    int convertVertexFormatToStorageBytes(const std::string &canonicalName);

    class CPPVisitor : public clang::RecursiveASTVisitor<CPPVisitor>, public BaseASTVisitor
    {
        friend class CPPHostShaderOnlyGuard;
        friend class CPPHostClassValidator;
        friend class CPPRendererEmitter;
        friend class CPPShaderArtifactEmitter;
        // std::string mCodeGenResult;
        CPPTypeConvertor mTypeConvertor;
        CPPFunctionConvertor mFuncConvertor;
        CPPHostShaderOnlyGuard mHostShaderOnlyGuard;
        CPPRendererEmitter mRendererEmitter;
        CPPShaderArtifactEmitter mShaderArtifactEmitter;
        std::unique_ptr<CPPHostClassValidator> mHostClassValidator;
        ShaderSourcePipelineOptions mShaderSourcePipelineOptions;
        /** Resolves a host declaration to the same concrete record identity as the shader frontend. */
        std::string getPreparedRecordSymbolName(const clang::CXXRecordDecl &record) const;

        /** Requires the prepared class-wide resource interface for an emitted shader. */
        const UGLIR::Reflection &requirePreparedClassInterface(const clang::CXXRecordDecl &record) const;

        /** Applies authoritative shader binding IDs to host resource declarations after compatibility checks. */
        void applyPreparedBindGroupBindings(const clang::CXXRecordDecl &record, std::vector<BaseShaderResourceBinding> &bindings);

        /** Orders host RenderSet components using the prepared shader interface. */
        void applyPreparedRenderSetFields(const clang::CXXRecordDecl &record, std::vector<clang::FieldDecl *> &fields);


    public:
        explicit CPPVisitor(clang::Rewriter *Rewriter, clang::ASTContext *Context, ShaderSourcePipelineOptions shaderSourcePipelineOptions = {});
        ~CPPVisitor();


        /** Scans wrapper and create-call types before rewriting so template variants are known up front. */
        void collectStaticShaderVariants(clang::TranslationUnitDecl *translationUnitDecl);

        // 1. 访问命名空间
        bool VisitNamespaceDecl(clang::NamespaceDecl *decl);

        // 2. 访问类或结构体
        bool VisitCXXRecordDecl(clang::CXXRecordDecl *decl);

        /** Rewrites UGL class template declarations at the template node so primary traversal cannot emit artifacts. */
        bool TraverseClassTemplateDecl(clang::ClassTemplateDecl *decl);

        // 3. 访问函数
        bool VisitFunctionDecl(clang::FunctionDecl *decl);

        // 4. 访问全局变量
        bool VisitVarDecl(clang::VarDecl *decl);

        // 6. 访问类型定义 (typedef)
        bool VisitTypedefNameDecl(clang::TypedefNameDecl *decl);
        bool allowExplicitThisPointerAccess() const override
        {
            return true;
        }
        std::optional<std::string> getErasedTemplateSpecializationName(const clang::ClassTemplateSpecializationDecl *decl, const AbstractTypeConvertor *typeConvertor = nullptr) const override;
    private:
        clang::Rewriter *Rewriter;
        BufferUsageInfo mLastBufferInfo;
        TextureUsageInfo mLastTextureInfo;


        enum class RenderComponentType : int
        {
            BufferComponent = 1,
            TextureComponent
        };

        struct RenderComponentCreateInfo
        {
            RenderComponentType type;
            std::string typeName;
            std::string varName;
            const clang::FieldDecl *fieldDecl;
            int numElement = 1;
            std::string typeString;
        };

        struct VertexAttributeLayoutInfo
        {
            const clang::FieldDecl *fieldDecl;
            int shaderLocation = -1;
        };

        struct RenderVaryingLayoutInfo
        {
            const clang::FieldDecl *fieldDecl = nullptr;
            clang::QualType fieldType;
        };

        std::unordered_map<std::string, std::vector<RenderComponentCreateInfo>> mRenderSetComponentInfos;
        std::unordered_map<std::string, std::string> mBindGroupVisibilityExprCache;
        std::unordered_set<const clang::ClassTemplateSpecializationDecl *> mMaterializedTemplateSpecializations;
        std::unordered_set<const clang::ClassTemplateSpecializationDecl *> mShaderVariantRootSpecializations;
        bool mBindGroupVisibilityCacheReady = false;
        void insertLineDirective(clang::SourceLocation loc) const;

        const std::string mUGLFrameRingBufferVariableName = "UGL__FrameRingBufferCount";
        const std::string mUGLFrameCounterVariableName = "UGL__FrameCounter";

        bool checkUGLDerivedClass(const clang::CXXRecordDecl *decl) const;
        /** Returns true when a UGL template primary must be emitted through concrete specializations. */
        bool shouldEmitUGLTemplateSpecializationBundle(const clang::CXXRecordDecl *decl) const;
        /** Rewrites a UGL template primary as a forward declaration plus every instantiated concrete specialization. */
        std::string replaceUGLTemplateSpecializationBundle(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl);
        /** Rewrites a non-UGL template primary by keeping it and appending erased concrete record variants. */
        std::string replacePlainTemplateSpecializationBundle(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl);
        /** Returns true when a concrete specialization should be emitted as an erased ordinary class. */
        bool shouldMaterializeTemplateSpecialization(const clang::ClassTemplateSpecializationDecl *specializationDecl) const;
        /** Returns true when a concrete shader specialization is an explicit DSL variant root. */
        bool isShaderVariantRootSpecialization(const clang::ClassTemplateSpecializationDecl *specializationDecl) const;
        /** Emits a plain helper record from a template primary under the current concrete substitution context. */
        std::string replacePlainTemplateSpecializationRecord(const clang::CXXRecordDecl *primaryDecl);
        /** Emits a host-only shell for shader resource-handle behavior records without preserving shader storage. */
        std::string replaceShaderResourceBehaviorRecordShell(const clang::CXXRecordDecl *decl);
        /** Emits an empty host-compatible method body for shader-only resource behavior methods. */
        std::string generateShaderResourceBehaviorShellMethod(const clang::FunctionDecl *func);
        /** Returns true when a direct shader resource handle type cannot be stored or passed by ordinary generated host code. */
        bool directShaderResourceHandleRequiresHostShell(const clang::QualType &type) const;
        /** Returns true when a function signature mentions direct shader resource handles that cannot execute on the CPU host. */
        bool functionSignatureUsesDirectShaderResourceHandles(const clang::FunctionDecl *func) const;
        /** Returns true when a non-template host record must be emitted as a shader-only resource behavior shell. */
        bool recordUsesDirectShaderResourceHandles(const clang::CXXRecordDecl *decl) const;
        /** Emits the forward declaration that keeps the shader template primary visible without producing an artifact. */
        std::string generateUGLTemplateForwardDeclaration(const clang::ClassTemplateDecl *templateDecl, const clang::CXXRecordDecl *primaryDecl);
        /** Returns the host C++ spelling used when defining a record or explicit specialization. */
        std::string getRecordEmissionName(const clang::CXXRecordDecl *decl);
        /** Returns a stable artifact label for a concrete shader, bind group, framebuffer, or render set record. */
        std::string getRecordVariantLabel(const clang::CXXRecordDecl *decl);
        std::string replaceUGLClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLFrameBufferClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLBindGroupClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLRenderClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLComputeClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLRenderSetClass(const clang::CXXRecordDecl *decl);
        std::string replaceUGLRendererClass(const clang::CXXRecordDecl *decl);
        bool isShaderClassBindGroupParamType(const clang::QualType &qt) const;
        void validateShaderClassBindGroupSlots(const clang::CXXRecordDecl *decl, const clang::FunctionDecl *createFunc, const std::string &ownerKind, int bindgroupBufferOffset);
        std::string buildEmbeddedShaderArtifactMember(const std::string &memberName,
                                                      const std::string &shaderHeaderVariableName,
                                                      const clang::CXXRecordDecl *shaderClassDecl,
                                                      const clang::FunctionDecl *entryFunction,
                                                      const BindGroupInfoMap &bindGroupInfoMap,
                                                      const std::vector<clang::Decl *> &extraDecls,
                                                      UGLC::CodeGen::IShaderSourceEmitter &shaderSourceEmitter);
        BindGroupInfoMap createBindGroupInfoMap(const clang::CXXRecordDecl *decl, int bindgroupStartIndex);
        int getBindGroupCountFromInfoMap(const BindGroupInfoMap &infoMap, int bindgroupStartIndex);
        std::string generateBindGoupAssign(const BindGroupInfoMap &bmap);
        std::array<std::string, 3> getValidatedLocalWorkGroupSize(const clang::CXXRecordDecl *decl);
        std::vector<VertexAttributeLayoutInfo> getValidatedVertexAttributeLayout(const clang::ParmVarDecl *vertexInputParam, const std::string &renderClassName);
        int getValidatedVertexAttributeLocation(const clang::FieldDecl *fieldDecl, const std::string &renderClassName, const std::string &vertexInputTypeName);
        bool isRenderVaryingSystemSemanticField(const clang::FieldDecl *fieldDecl) const;
        int getValidatedRenderVaryingAttributeLocation(const clang::FieldDecl *fieldDecl,
                                                       const std::string &renderClassName,
                                                       const std::string &recordRole,
                                                       const std::string &recordTypeName) const;
        std::unordered_map<int, RenderVaryingLayoutInfo> collectValidatedRenderVaryingLayoutOrThrow(const clang::CXXRecordDecl *recordDecl,
                                                                                                     const std::string &renderClassName,
                                                                                                     const std::string &recordRole,
                                                                                                     bool requirePosition = false) const;
        void validateVertexFragmentVaryingContractOrThrow(const clang::CXXRecordDecl *vertexOutputRecord,
                                                          const clang::CXXRecordDecl *fragmentInputRecord,
                                                          const std::string &renderClassName) const;
        const clang::FunctionDecl *requireCreateMethodOrThrow(const clang::CXXRecordDecl *decl, const std::string &ownerKind) const;
        const clang::FunctionDecl *requireRendererRenderMethodOrThrow(const clang::CXXRecordDecl *decl) const;
        void validateFrameBufferFieldsOrThrow(const clang::CXXRecordDecl *decl, const std::string &framebufferName) const;
        clang::QualType requireAttachmentTemplateTypeOrThrow(const clang::FieldDecl *fieldDecl,
                                                             const std::string &ownerKind,
                                                             const std::string &ownerName) const;
        void validateRenderTargetRecordOrThrow(const clang::CXXRecordDecl *recordDecl, const std::string &renderClassName) const;
        void validateRenderSetComponentFieldAttributesOrThrow(const clang::FieldDecl *fieldDecl, const std::string &renderSetName) const;
        void validateRenderSetTextureResourceCountOrThrow(const clang::FieldDecl *fieldDecl, const std::string &renderSetName, int maxResourceCount) const;


        virtual std::string generateFunctionSignatureParam(const clang::ParmVarDecl *param, AbstractTypeConvertor *typeConvertor = nullptr) override;
        virtual std::string generateFunctionDefinition(const clang::FunctionDecl *func) override;

        /** Preserves dependent `if constexpr` conditions for the host compiler to instantiate. */
        std::string translateIfStmt(const clang::IfStmt *stmt) override;

        virtual std::string translateCXXOperatorCallExprFuncCall(const clang::CXXOperatorCallExpr *E) override;
        virtual std::string translateCXXOperatorCallExprBinaryOp(const clang::CXXOperatorCallExpr *E) override;

        virtual std::string translateCallExpr(const clang::CallExpr *E) override;
        virtual std::string translateCXXMemberCallExpr(const clang::CXXMemberCallExpr *E) override;
        std::string inferBindGroupVisibilityExpr(const clang::CXXRecordDecl *bindGroupDecl);

        /** Lowers host C-style vector conversions through the constructor path. */
        std::string translateCStyleCastExpr(const clang::CStyleCastExpr *expr) override;
        /** Lowers host static vector conversions through the constructor path. */
        std::string translateCXXStaticCastExpr(const clang::CXXStaticCastExpr *expr) override;
        virtual std::string translateCXXConstructExprFunction(const clang::CXXConstructExpr *E) override;


        int getAttributeNumber(const std::string &input, const std::string &keyword) const;
        virtual std::string translateVarDecl(const clang::VarDecl *VD) override;
        bool functionRequiresDeletedHostDefinition(const clang::FunctionDecl *func);
        std::string generateDeletedFunctionDeclaration(const clang::FunctionDecl *func, const std::string &funcNameOverride = "");
        void setResourceUseInfo(const clang::QualType &qt)
        {
            // Resource creation translation reuses these scratch structs while
            // visiting one expression tree. Reset them eagerly so usage flags
            // from a previous buffer/texture declaration never leak into the
            // next resource descriptor.
            mLastBufferInfo = {};
            mLastTextureInfo = {};

            std::string typeName = generateTypeCanonicalName(qt);

            if (typeName.starts_with("UGL::Buffer<"))
            {
                std::string secondTemplateArgs;
                auto templates = getTemplateArgumentsFromType(qt);
                if (templates.size() > 1)
                {
                    auto usageTypes = getTemplateArgumentsFromType(templates.at(1).getAsType());
                    for (auto &t : usageTypes)
                    {
                        mLastBufferInfo.usageInfos.emplace_back(convertBufferUsage(translateTemplateArgument(t, &mTypeConvertor)));
                    }
                    mLastBufferInfo.typeName = translateTemplateArgument(templates.front(), &mTypeConvertor);
                    secondTemplateArgs = translateTemplateArgument(templates.at(1), &mTypeConvertor);
                }
            }
            else if (typeName.starts_with("UGL::Texture<"))
            {
                std::string secondTemplateArgs;
                auto templates = getTemplateArgumentsFromType(qt);
                if (templates.size() > 1)
                {
                    auto usageTypes = getTemplateArgumentsFromType(templates.at(1).getAsType());
                    for (auto &t : usageTypes)
                    {
                        const std::string usageName = translateTemplateArgument(t, &mTypeConvertor);
                        if (usageName == "UGL::PixelLocalAttachment")
                        {
                            mLastTextureInfo.usesPixelLocalAttachment = true;
                        }
                        else if (usageName == "UGL::TextureBinding" || usageName == "UGL::StorageBinding" ||
                                 usageName == "UGL::CopySrc" || usageName == "UGL::CopyDst")
                        {
                            mLastTextureInfo.usesPersistentTextureAccess = true;
                        }
                        mLastTextureInfo.usageInfos.emplace_back(convertTextureUsage(usageName));
                    }
                    mLastTextureInfo.typeName = (translateTemplateArgument(templates.front(), &mTypeConvertor));
                    mLastTextureInfo.dimension = convertTextureDimension(translateTemplateArgument(templates.at(2), &mTypeConvertor));
                    secondTemplateArgs = translateTemplateArgument(templates.at(1), &mTypeConvertor);
                }
            }
        }
    };

} // namespace UGLC::CodeGen::CPP
