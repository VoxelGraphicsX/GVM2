#include <CodeGen/RenderSetLayoutInfo.hpp>

#include <stdexcept>

namespace UGLC::CodeGen
{
    namespace
    {
        RenderSetBaseBoundKind resolveRenderSetBaseBoundKind(const clang::FieldDecl *fieldDecl, BaseASTVisitor &visitor)
        {
            if (fieldDecl == nullptr)
            {
                return RenderSetBaseBoundKind::None;
            }

            for (const auto *attr : visitor.getAllAttributes(fieldDecl))
            {
                const std::string rawAttribute = visitor.generateRawAttribute(attr);
                if (rawAttribute == mUGLRenderSetVertexBuffer)
                {
                    return RenderSetBaseBoundKind::VertexCount;
                }
                if (rawAttribute == mUGLRenderSetIndexBuffer)
                {
                    return RenderSetBaseBoundKind::IndexCount;
                }
            }

            return RenderSetBaseBoundKind::None;
        }
    } // namespace

    const clang::CXXRecordDecl *tryGetRenderSetTypeDeclFromType(const clang::QualType &type, BaseASTVisitor &visitor)
    {
        const clang::QualType resolvedType = visitor.getUnqualifiedType(type);
        const auto *recordDecl = resolvedType->getAsCXXRecordDecl();
        if (recordDecl == nullptr || visitor.getClassCanonicalName(recordDecl) != mUGLRenderSetName)
        {
            return nullptr;
        }

        const auto templateArgs = visitor.getTemplateArgumentsFromType(resolvedType);
        if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
        {
            return nullptr;
        }

        return visitor.getUnqualifiedType(templateArgs.front().getAsType())->getAsCXXRecordDecl();
    }

    bool isRenderSetParameterType(const clang::QualType &type, BaseASTVisitor &visitor)
    {
        return tryGetRenderSetTypeDeclFromType(type, visitor) != nullptr;
    }

    bool functionHasRenderSetParameter(const clang::FunctionDecl *func, BaseASTVisitor &visitor)
    {
        if (func == nullptr)
        {
            return false;
        }

        for (unsigned i = 0; i < func->getNumParams(); ++i)
        {
            if (isRenderSetParameterType(func->getParamDecl(i)->getType(), visitor))
            {
                return true;
            }
        }
        return false;
    }

    RenderSetLayoutInfo buildRenderSetLayoutInfo(const clang::CXXRecordDecl *renderSetDecl, BaseASTVisitor &visitor)
    {
        if (renderSetDecl == nullptr)
        {
            throw std::runtime_error("Internal error: buildRenderSetLayoutInfo received a null RenderSet declaration.");
        }

        RenderSetLayoutInfo layoutInfo;
        layoutInfo.renderSetDecl = renderSetDecl;
        layoutInfo.renderSetTypeName = visitor.getClassCanonicalName(renderSetDecl);

        for (const auto *fieldDecl : renderSetDecl->fields())
        {
            const clang::QualType componentType = visitor.getUnqualifiedType(fieldDecl->getType());
            const auto templateArgs = visitor.getTemplateArgumentsFromType(componentType);
            const std::string componentTypeName = visitor.getClassCanonicalName(componentType->getAsCXXRecordDecl());

            RenderSetFieldInfo fieldInfo;
            fieldInfo.fieldDecl = fieldDecl;
            fieldInfo.fieldName = fieldDecl->getNameAsString();
            fieldInfo.componentType = componentType;
            fieldInfo.baseBoundKind = resolveRenderSetBaseBoundKind(fieldDecl, visitor);

            if (componentTypeName == mUGLRenderSetBufferComponentClassName)
            {
                if (templateArgs.empty() || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
                {
                    throw std::runtime_error("RenderSet \"" + renderSetDecl->getQualifiedNameAsString()
                                             + "\" field \"" + fieldDecl->getNameAsString()
                                             + "\" requires BufferComponent<ElementType>.");
                }

                fieldInfo.fieldKind = RenderSetFieldKind::Buffer;
                fieldInfo.elementType = visitor.getUnqualifiedType(templateArgs.front().getAsType());
                layoutInfo.fields.push_back(std::move(fieldInfo));
                continue;
            }

            if (componentTypeName == mUGLRenderSetTextureComponentClassName)
            {
                if (templateArgs.size() < 2 || templateArgs.front().getKind() != clang::TemplateArgument::Type || templateArgs.front().getAsType().isNull())
                {
                    throw std::runtime_error("RenderSet \"" + renderSetDecl->getQualifiedNameAsString()
                                             + "\" field \"" + fieldDecl->getNameAsString()
                                             + "\" requires TextureComponent<ElementType, MaxResourceCount>.");
                }

                fieldInfo.fieldKind = RenderSetFieldKind::Texture;
                fieldInfo.elementType = visitor.getUnqualifiedType(templateArgs.front().getAsType());
                fieldInfo.maxTextureResourceCount = static_cast<int>(visitor.getIntValueFromTemplateArgument(templateArgs.at(1)));
                layoutInfo.fields.push_back(std::move(fieldInfo));
                continue;
            }

            throw std::runtime_error("RenderSet \"" + renderSetDecl->getQualifiedNameAsString()
                                     + "\" contains unsupported component field \"" + fieldDecl->getNameAsString()
                                     + "\" with type \"" + visitor.generateTypeCanonicalName(fieldDecl->getType())
                                     + "\". Supported component types are UGL::BufferComponent<T> and UGL::TextureComponent<T, MaxResourceCount>.");
        }

        return layoutInfo;
    }

    RenderSetLayoutInfo buildRenderSetLayoutInfo(const clang::QualType &type, BaseASTVisitor &visitor)
    {
        const auto *renderSetDecl = tryGetRenderSetTypeDeclFromType(type, visitor);
        if (renderSetDecl == nullptr)
        {
            throw std::runtime_error("Internal error: buildRenderSetLayoutInfo received a non-RenderSet type \""
                                     + visitor.generateTypeCanonicalName(type) + "\".");
        }
        return buildRenderSetLayoutInfo(renderSetDecl, visitor);
    }
} // namespace UGLC::CodeGen
