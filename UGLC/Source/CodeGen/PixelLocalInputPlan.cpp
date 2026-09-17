#include "PixelLocalInputPlan.hpp"

#include "BaseASTVisitor.hpp"
#include "PixelLocalFieldAnalysis.hpp"
#include "UGLC.Constants.hpp"

#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace UGLC::CodeGen::PixelLocalInputPlan
{
    bool functionHasPixelLocalInputParameter(const BaseASTVisitor &visitor, const clang::FunctionDecl *func)
    {
        if (func == nullptr)
        {
            return false;
        }

        for (unsigned paramIndex = 0; paramIndex < func->getNumParams(); ++paramIndex)
        {
            if (visitor.checkAttibuteByName(func->getParamDecl(paramIndex), mUGLAttributePixelLocalInputName))
            {
                return true;
            }
        }
        return false;
    }

    bool isDirectPixelLocalInputFieldRead(const BaseASTVisitor &visitor, const clang::FunctionDecl *func, const clang::CXXMemberCallExpr *expr)
    {
        if (func == nullptr)
        {
            return false;
        }

        for (unsigned paramIndex = 0; paramIndex < func->getNumParams(); ++paramIndex)
        {
            const clang::ParmVarDecl *param = func->getParamDecl(paramIndex);
            if (visitor.checkAttibuteByName(param, mUGLAttributePixelLocalInputName) &&
                PixelLocalFieldAnalysis::isDirectPixelLocalInputFieldRead(expr, param))
            {
                return true;
            }
        }
        return false;
    }

    ParameterPlan collectParameterPlan(const BaseASTVisitor &visitor, const clang::FunctionDecl *shaderFunc, const clang::ParmVarDecl *param)
    {
        ParameterPlan plan;
        plan.paramDecl = param;
        if (param == nullptr)
        {
            return plan;
        }

        plan.recordDecl = PixelLocalFieldAnalysis::getSelfOrPointeeCXXRecordDecl(param->getType());
        if (plan.recordDecl == nullptr || !visitor.checkDerivedClassByName(plan.recordDecl, mUGLFrameBufferBaseName))
        {
            throw std::runtime_error("Pixel-local input parameter \"" + param->getNameAsString() + "\" must use a UGL::IFrameBuffer record type.");
        }

        const std::unordered_set<std::string> readFields = PixelLocalFieldAnalysis::collectPixelLocalReadFields(shaderFunc, param);
        std::uint32_t colorIndex = 0u;
        for (const clang::FieldDecl *field : plan.recordDecl->fields())
        {
            const auto *fieldRecordDecl = field->getType()->getAsCXXRecordDecl();
            const std::string baseTypeName = visitor.getClassCanonicalName(fieldRecordDecl, nullptr);
            if (baseTypeName == mUGLColorAttachmentName)
            {
                ++colorIndex;
                continue;
            }

            if (baseTypeName == mUGLPixelLocalColorAttachmentName)
            {
                const std::uint32_t currentColorIndex = colorIndex++;
                if (!readFields.contains(field->getNameAsString()))
                {
                    continue;
                }

                AttachmentPlan attachment;
                attachment.fieldDecl = field;
                attachment.fieldName = field->getNameAsString();
                attachment.colorAttachmentIndex = currentColorIndex;
                attachment.inputAttachmentIndex = static_cast<std::uint32_t>(plan.attachments.size());
                plan.attachments.emplace_back(std::move(attachment));
                continue;
            }
        }
        return plan;
    }

    FunctionPlan collectFunctionPlan(const BaseASTVisitor &visitor, const clang::FunctionDecl *shaderFunc)
    {
        FunctionPlan plan;
        if (shaderFunc == nullptr)
        {
            return plan;
        }

        for (unsigned paramIndex = 0; paramIndex < shaderFunc->getNumParams(); ++paramIndex)
        {
            const clang::ParmVarDecl *param = shaderFunc->getParamDecl(paramIndex);
            if (visitor.checkAttibuteByName(param, mUGLAttributePixelLocalInputName))
            {
                plan.parameters.emplace_back(collectParameterPlan(visitor, shaderFunc, param));
            }
        }
        return plan;
    }
} // namespace UGLC::CodeGen::PixelLocalInputPlan
