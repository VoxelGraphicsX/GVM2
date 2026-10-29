#pragma once

#include <CodeGen/BaseASTVisitor.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    enum class RenderSetBaseBoundKind : std::uint8_t
    {
        None,
        InstanceCount,
        VertexCount,
        IndexCount,
    };

    enum class RenderSetFieldKind : std::uint8_t
    {
        Buffer,
        Texture,
    };

    struct RenderSetFieldInfo
    {
        const clang::FieldDecl *fieldDecl = nullptr;
        std::string fieldName;
        RenderSetFieldKind fieldKind = RenderSetFieldKind::Buffer;
        RenderSetBaseBoundKind baseBoundKind = RenderSetBaseBoundKind::None;
        clang::QualType componentType;
        clang::QualType elementType;
        int maxTextureResourceCount = 0;
    };

    struct RenderSetLayoutInfo
    {
        const clang::CXXRecordDecl *renderSetDecl = nullptr;
        std::string renderSetTypeName;
        std::vector<RenderSetFieldInfo> fields;
    };

    const clang::CXXRecordDecl *tryGetRenderSetTypeDeclFromType(const clang::QualType &type, BaseASTVisitor &visitor);
    bool isRenderSetParameterType(const clang::QualType &type, BaseASTVisitor &visitor);
    /** Returns true when a function has at least one UGL::RenderSet<T> parameter. */
    bool functionHasRenderSetParameter(const clang::FunctionDecl *func, BaseASTVisitor &visitor);
    RenderSetLayoutInfo buildRenderSetLayoutInfo(const clang::CXXRecordDecl *renderSetDecl, BaseASTVisitor &visitor);
    RenderSetLayoutInfo buildRenderSetLayoutInfo(const clang::QualType &type, BaseASTVisitor &visitor);
} // namespace UGLC::CodeGen
