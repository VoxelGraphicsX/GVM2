#include "TaskProjectionBuilder.hpp"
#include <clang/AST/DeclTemplate.h>
#include <clang/Sema/Sema.h>
#include <clang/Basic/DiagnosticSema.h>

namespace UGLC::CodeGen
{
    void TaskProjectionBuilder::complete(clang::Sema &sema, clang::TagDecl &declaration)
    {
        auto *task = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(&declaration);
        if (task == nullptr || task->getQualifiedNameAsString() != "UGL::Task" || !task->field_empty())
        {
            return;
        }
        const auto value = task->getTemplateArgs()[0].getAsType();
        const auto *record = value->getAsCXXRecordDecl();
        if (record == nullptr || !record->hasDefinition() || record->getQualifiedNameAsString().starts_with("UGL::"))
        {
            return;
        }
        auto &context = sema.Context;
        for (const auto *field : record->getDefinition()->fields())
        {
            if (field->getAccess() != clang::AS_public || field->isUnnamedBitField())
            {
                continue;
            }
            clang::TemplateArgumentListInfo arguments;
            arguments.addArgument(clang::TemplateArgumentLoc(clang::TemplateArgument(field->getType()),
                context.getTrivialTypeSourceInfo(field->getType(), field->getLocation())));
            const auto projected = sema.CheckTemplateIdType(clang::TemplateName(task->getSpecializedTemplate()),
                field->getLocation(), arguments);
            if (projected.isNull() || sema.RequireCompleteType(field->getLocation(), projected, clang::diag::err_incomplete_type))
            {
                return;
            }
            auto *member = clang::FieldDecl::Create(context, task, field->getBeginLoc(), field->getLocation(),
                field->getIdentifier(), projected, context.getTrivialTypeSourceInfo(projected), nullptr, false,
                clang::ICIS_NoInit);
            member->setAccess(clang::AS_public);
            member->setImplicit();
            task->addDecl(member);
        }
    }
}
