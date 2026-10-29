#include "HLSLAggregateInitializerEmitter.hpp"

#include <CodeGen/Legacy/HLSL/HLSLIdentifierUtils.hpp>

#include <llvm/Support/Casting.h>

#include <cctype>
#include <iterator>
#include <optional>
#include <stdexcept>

namespace UGLC::CodeGen::HLSL
{
    HLSLAggregateInitializerEmitter::HLSLAggregateInitializerEmitter(BaseASTVisitor &visitor, AbstractTypeConvertor *typeConvertor, HLSLVisitorLocalStorage &localStorage)
        : mVisitor(visitor)
        , mTypeConvertor(typeConvertor)
        , mLocalStorage(localStorage)
    {
    }

    void HLSLAggregateInitializerEmitter::resetShaderScope()
    {
        mPendingHelperDefinitions.clear();
        mPendingHelperNames.clear();
    }

    const std::vector<std::string> &HLSLAggregateInitializerEmitter::pendingHelperDefinitions() const
    {
        return mPendingHelperDefinitions;
    }

    bool HLSLAggregateInitializerEmitter::isImplicitOrDefaultInitializer(const clang::Expr *expr) const
    {
        if (expr == nullptr || mVisitor.isImplicitNode(expr))
        {
            return true;
        }

        expr = stripTransparentExprWrappers(expr);
        if (llvm::isa<clang::ImplicitValueInitExpr>(expr) || llvm::isa<clang::NoInitExpr>(expr))
        {
            return true;
        }
        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr))
        {
            return constructExpr->getNumArgs() == 0;
        }
        return false;
    }

    const clang::ConditionalOperator *HLSLAggregateInitializerEmitter::unwrapUnsupportedConditionalExpr(const clang::Expr *expr) const
    {
        if (expr == nullptr)
        {
            return nullptr;
        }

        expr = expr->IgnoreParenImpCasts();
        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr); constructExpr != nullptr && constructExpr->getNumArgs() == 1)
        {
            expr = constructExpr->getArg(0)->IgnoreParenImpCasts();
        }

        const auto *conditionalExpr = llvm::dyn_cast<clang::ConditionalOperator>(expr);
        if (conditionalExpr == nullptr)
        {
            return nullptr;
        }

        const clang::QualType conditionalType = mVisitor.getUnqualifiedType(conditionalExpr->getType());
        if (conditionalType->getAsCXXRecordDecl() != nullptr || conditionalType->isArrayType())
        {
            return conditionalExpr;
        }

        return nullptr;
    }

    const clang::CallExpr *HLSLAggregateInitializerEmitter::unwrapAtomicReturningCall(const clang::Expr *expr) const
    {
        if (expr == nullptr)
        {
            return nullptr;
        }

        expr = expr->IgnoreParenImpCasts();
        const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(expr);
        if (callExpr == nullptr)
        {
            return nullptr;
        }

        const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(callExpr->getCalleeDecl());
        return atomicBuiltinReturnsOriginalValue(getAtomicBuiltinKind(calleeDecl)) ? callExpr : nullptr;
    }

    std::string HLSLAggregateInitializerEmitter::makeZeroInitializer(const clang::QualType &targetType)
    {
        if (targetType.isNull())
        {
            return "0";
        }

        if (const auto *arrayType = llvm::dyn_cast_or_null<clang::ConstantArrayType>(mVisitor.Context->getAsArrayType(targetType)))
        {
            const size_t elementCount = static_cast<size_t>(arrayType->getSize().getZExtValue());
            std::vector<std::string> elementInitializers;
            elementInitializers.reserve(elementCount);
            for (size_t index = 0; index < elementCount; ++index)
            {
                elementInitializers.emplace_back(makeZeroInitializer(arrayType->getElementType()));
            }
            return "{ " + stringJoin(elementInitializers, ", ") + " }";
        }

        const clang::QualType realType = mVisitor.getUnqualifiedType(targetType);
        const std::string typeName = mVisitor.generateTypeCanonicalName(realType, mTypeConvertor);

        if (const auto vectorType = parseHLSLVectorType(typeName))
        {
            const std::string zeroLiteral = makeScalarZeroLiteral(vectorType->scalarType);
            return typeName + "(" + makeRepeatedArgumentList(zeroLiteral, vectorType->elementCount) + ")";
        }
        if (const auto matrixType = parseHLSLMatrixType(typeName))
        {
            const std::string zeroLiteral = makeScalarZeroLiteral(matrixType->scalarType);
            return typeName + "(" + makeRepeatedArgumentList(zeroLiteral, matrixType->rowCount * matrixType->columnCount) + ")";
        }
        if (realType->isBooleanType())
        {
            return "false";
        }
        if (realType->isIntegerType() || realType->isEnumeralType())
        {
            return "0";
        }
        if (realType->isRealFloatingType())
        {
            return "0.0f";
        }
        if (const auto *recordDecl = realType->getAsCXXRecordDecl())
        {
            std::vector<std::string> fieldInitializers;
            for (const auto *field : recordDecl->fields())
            {
                fieldInitializers.emplace_back(makeZeroInitializer(field->getType()));
            }
            return "{ " + stringJoin(fieldInitializers, ", ") + " }";
        }
        return typeName + "(0)";
    }

    std::string HLSLAggregateInitializerEmitter::emitDefaultInitializer(const clang::QualType &targetType)
    {
        if (const auto *arrayType = llvm::dyn_cast_or_null<clang::ConstantArrayType>(mVisitor.Context->getAsArrayType(targetType)))
        {
            const size_t count = static_cast<size_t>(arrayType->getSize().getZExtValue());
            std::vector<std::string> elements;
            elements.reserve(count);
            for (size_t index = 0; index < count; ++index)
            {
                elements.push_back(emitDefaultInitializer(arrayType->getElementType()));
            }
            return "{ " + stringJoin(elements, ", ") + " }";
        }
        const clang::QualType realType = mVisitor.getUnqualifiedType(targetType);
        const std::string typeName = mVisitor.generateTypeCanonicalName(realType, mTypeConvertor);
        if (parseHLSLVectorType(typeName) || parseHLSLMatrixType(typeName))
        {
            return makeZeroInitializer(targetType);
        }
        if (const auto *recordDecl = realType->getAsCXXRecordDecl())
        {
            std::vector<std::string> fields;
            for (const auto *field : recordDecl->fields())
            {
                fields.push_back(field->hasInClassInitializer()
                    ? emitTypedInitializer(field->getType(), field->getInClassInitializer())
                    : emitDefaultInitializer(field->getType()));
            }
            return "{ " + stringJoin(fields, ", ") + " }";
        }
        return makeZeroInitializer(targetType);
    }

    std::string HLSLAggregateInitializerEmitter::buildZeroInitializationStatements(const clang::QualType &targetType, const std::string &targetExpression)
    {
        if (targetType.isNull())
        {
            return mVisitor.mSpaceManager.getSpace() + targetExpression + " = 0" + mVisitor.EOS();
        }

        if (const auto *arrayType = llvm::dyn_cast_or_null<clang::ConstantArrayType>(mVisitor.Context->getAsArrayType(targetType)))
        {
            std::string result;
            const size_t elementCount = static_cast<size_t>(arrayType->getSize().getZExtValue());
            for (size_t index = 0; index < elementCount; ++index)
            {
                result += buildZeroInitializationStatements(arrayType->getElementType(), targetExpression + "[" + std::to_string(index) + "]");
            }
            return result;
        }

        const clang::QualType realType = mVisitor.getUnqualifiedType(targetType);
        const std::string typeName = mVisitor.generateTypeCanonicalName(realType, mTypeConvertor);
        if (parseHLSLVectorType(typeName).has_value() || parseHLSLMatrixType(typeName).has_value() || realType->isBooleanType() || realType->isIntegerType() || realType->isEnumeralType() || realType->isRealFloatingType())
        {
            return mVisitor.mSpaceManager.getSpace() + targetExpression + " = " + makeZeroInitializer(targetType) + mVisitor.EOS();
        }

        if (const auto *recordDecl = realType->getAsCXXRecordDecl())
        {
            std::string result;
            for (const auto *field : recordDecl->fields())
            {
                result += buildZeroInitializationStatements(field->getType(), targetExpression + "." + field->getNameAsString());
            }
            return result;
        }

        return mVisitor.mSpaceManager.getSpace() + targetExpression + " = " + typeName + "(0)" + mVisitor.EOS();
    }

    std::string HLSLAggregateInitializerEmitter::emitTypedInitializer(const clang::QualType &targetType, const clang::Expr *expr)
    {
        if (expr == nullptr)
        {
            return makeZeroInitializer(targetType);
        }

        expr = expr->IgnoreParenImpCasts();
        if (const auto *stdInitList = llvm::dyn_cast<clang::CXXStdInitializerListExpr>(expr))
        {
            expr = stdInitList->getSubExpr();
        }

        if (llvm::isa<clang::ImplicitValueInitExpr>(expr) || llvm::isa<clang::NoInitExpr>(expr))
        {
            return makeZeroInitializer(targetType);
        }

        const clang::QualType realType = targetType.isNull() ? clang::QualType() : mVisitor.getUnqualifiedType(targetType);
        const std::string typeName = targetType.isNull() ? std::string() : mVisitor.generateTypeCanonicalName(realType, mTypeConvertor);

        if (const auto *initList = llvm::dyn_cast<clang::InitListExpr>(expr))
        {
            const clang::InitListExpr *syntacticInitList = initList->isSyntacticForm() ? initList : initList->getSyntacticForm();
            if (syntacticInitList == nullptr)
            {
                syntacticInitList = initList;
            }

            if (const auto *arrayType = llvm::dyn_cast_or_null<clang::ConstantArrayType>(mVisitor.Context->getAsArrayType(targetType)))
            {
                const size_t totalCount = static_cast<size_t>(arrayType->getSize().getZExtValue());
                std::vector<std::string> elementInitializers;
                elementInitializers.reserve(totalCount);
                for (size_t index = 0; index < totalCount; ++index)
                {
                    const clang::Expr *elementExpr = index < syntacticInitList->getNumInits() ? syntacticInitList->getInit(static_cast<unsigned>(index)) : nullptr;
                    elementInitializers.emplace_back(emitTypedInitializer(arrayType->getElementType(), elementExpr));
                }
                return "{ " + stringJoin(elementInitializers, ", ") + " }";
            }

            if (const auto vectorType = parseHLSLVectorType(typeName))
            {
                std::vector<std::string> args;
                const unsigned initCount = syntacticInitList->getNumInits();
                args.reserve(static_cast<size_t>(vectorType->elementCount));
                for (unsigned index = 0; index < initCount; ++index)
                {
                    args.emplace_back(mVisitor.TranslateExpr(syntacticInitList->getInit(index)));
                }
                while (static_cast<int>(args.size()) < vectorType->elementCount)
                {
                    args.emplace_back(makeScalarZeroLiteral(vectorType->scalarType));
                }
                return typeName + "(" + stringJoin(args, ", ") + ")";
            }

            if (const auto *recordDecl = realType->getAsCXXRecordDecl())
            {
                std::vector<std::string> fieldInitializers;
                fieldInitializers.reserve(std::distance(recordDecl->field_begin(), recordDecl->field_end()));
                unsigned fieldIndex = 0;
                for (const auto *field : recordDecl->fields())
                {
                    const clang::Expr *fieldExpr = fieldIndex < syntacticInitList->getNumInits() ? syntacticInitList->getInit(fieldIndex) : nullptr;
                    fieldInitializers.emplace_back(emitTypedInitializer(field->getType(), fieldExpr));
                    ++fieldIndex;
                }
                return "{ " + stringJoin(fieldInitializers, ", ") + " }";
            }
        }

        return mVisitor.TranslateExpr(expr);
    }

    std::string HLSLAggregateInitializerEmitter::emitTypedInitializerExpression(const clang::QualType &targetType, const clang::Expr *expr)
    {
        const clang::QualType rawTargetType = targetType;
        const clang::QualType realType = targetType.isNull() ? clang::QualType() : mVisitor.getUnqualifiedType(targetType);
        if (expr != nullptr)
        {
            expr = stripTransparentExprWrappers(expr);
        }

        if (const auto *stdInitList = llvm::dyn_cast_or_null<clang::CXXStdInitializerListExpr>(expr))
        {
            expr = stdInitList->getSubExpr();
        }

        if (const auto *constructExpr = llvm::dyn_cast_or_null<clang::CXXConstructExpr>(expr))
        {
            if (constructExpr->getNumArgs() == 0)
            {
                expr = nullptr;
            }
            else if (constructExpr->isListInitialization())
            {
                expr = constructExpr->getArg(0);
            }
        }

        if ((expr != nullptr && llvm::isa<clang::ImplicitValueInitExpr>(expr)) || (expr != nullptr && llvm::isa<clang::NoInitExpr>(expr)))
        {
            expr = nullptr;
        }

        if (expr == nullptr)
        {
            if (const auto *recordDecl = realType.isNull() ? nullptr : realType->getAsCXXRecordDecl())
            {
                const std::string helperName = registerAggregateConstructionHelper(recordDecl);
                std::vector<std::string> argumentExpressions;
                argumentExpressions.reserve(static_cast<size_t>(std::distance(recordDecl->field_begin(), recordDecl->field_end())));
                for (const auto *field : recordDecl->fields())
                {
                    argumentExpressions.push_back(emitTypedInitializerExpression(field->getType(), nullptr));
                }
                return helperName + "(" + stringJoin(argumentExpressions, ", ") + ")";
            }

            if (!rawTargetType.isNull() && rawTargetType->isArrayType())
            {
                throw std::runtime_error("HLSL cannot materialize an array aggregate as an expression for type \"" + mVisitor.generateTypeCanonicalName(rawTargetType, mTypeConvertor) + "\". Please rewrite the DSL to initialize a named temporary before passing or returning this value.");
            }

            return makeZeroInitializer(targetType);
        }

        if (const auto *initList = llvm::dyn_cast<clang::InitListExpr>(expr))
        {
            const clang::InitListExpr *syntacticInitList = initList->isSyntacticForm() ? initList : initList->getSyntacticForm();
            if (syntacticInitList == nullptr)
            {
                syntacticInitList = initList;
            }

            if (!rawTargetType.isNull() && rawTargetType->isArrayType())
            {
                throw std::runtime_error("HLSL cannot materialize an array aggregate as an expression for type \"" + mVisitor.generateTypeCanonicalName(rawTargetType, mTypeConvertor) + "\". Please rewrite the DSL to initialize a named temporary before passing or returning this value.");
            }

            const std::string typeName = realType.isNull() ? std::string() : mVisitor.generateTypeCanonicalName(realType, mTypeConvertor);
            if (const auto vectorType = parseHLSLVectorType(typeName))
            {
                std::vector<std::string> args;
                const unsigned initCount = syntacticInitList->getNumInits();
                args.reserve(static_cast<size_t>(vectorType->elementCount));
                for (unsigned index = 0; index < initCount; ++index)
                {
                    args.emplace_back(mVisitor.TranslateExpr(syntacticInitList->getInit(index)));
                }
                while (static_cast<int>(args.size()) < vectorType->elementCount)
                {
                    args.emplace_back(makeScalarZeroLiteral(vectorType->scalarType));
                }
                return typeName + "(" + stringJoin(args, ", ") + ")";
            }

            if (const auto matrixType = parseHLSLMatrixType(typeName))
            {
                std::vector<std::string> args;
                const unsigned initCount = syntacticInitList->getNumInits();
                args.reserve(static_cast<size_t>(matrixType->rowCount * matrixType->columnCount));
                for (unsigned index = 0; index < initCount; ++index)
                {
                    args.emplace_back(mVisitor.TranslateExpr(syntacticInitList->getInit(index)));
                }
                while (static_cast<int>(args.size()) < matrixType->rowCount * matrixType->columnCount)
                {
                    args.emplace_back(makeScalarZeroLiteral(matrixType->scalarType));
                }
                return typeName + "(" + stringJoin(args, ", ") + ")";
            }

            if (const auto *recordDecl = realType->getAsCXXRecordDecl())
            {
                const std::string helperName = registerAggregateConstructionHelper(recordDecl);
                std::vector<std::string> argumentExpressions;
                argumentExpressions.reserve(static_cast<size_t>(std::distance(recordDecl->field_begin(), recordDecl->field_end())));

                unsigned fieldIndex = 0;
                for (const auto *field : recordDecl->fields())
                {
                    const clang::Expr *fieldExpr = fieldIndex < syntacticInitList->getNumInits() ? syntacticInitList->getInit(fieldIndex) : nullptr;
                    argumentExpressions.push_back(emitTypedInitializerExpression(field->getType(), fieldExpr));
                    ++fieldIndex;
                }

                return helperName + "(" + stringJoin(argumentExpressions, ", ") + ")";
            }
        }

        return mVisitor.TranslateExpr(expr);
    }

    std::string HLSLAggregateInitializerEmitter::buildAtomicReturningVarDecl(const clang::VarDecl *decl, const std::vector<std::string> &qualifiers, const std::string &variableTypeName, const std::string &variableName, const clang::CallExpr *atomicCall)
    {
        const auto *calleeDecl = llvm::dyn_cast_or_null<clang::FunctionDecl>(atomicCall->getCalleeDecl());
        const AtomicBuiltinKind atomicKind = getAtomicBuiltinKind(calleeDecl);
        const std::string tempTypeName = mVisitor.generateTypeCanonicalName(atomicCall->getType(), mTypeConvertor);
        const std::string tempName = makeGeneratedIdentifier(variableName + "_atomic_result", atomicCall->getExprLoc());

        std::string result;
        // HLSL atomic builtins cannot be emitted as expression blocks and cannot be hidden behind a helper wrapper:
        // DXC requires the first `Interlocked*` argument to remain the original groupshared/UAV lvalue.
        //
        // DSL:
        //   const uint scalarBase = atomicAdd(counter, 1u);
        //
        // HLSL:
        //   uint __uglc_scalarBase_atomic_result_N;
        //   InterlockedAdd(counter, 1u, __uglc_scalarBase_atomic_result_N);
        //   const uint scalarBase = __uglc_scalarBase_atomic_result_N;
        result += tempTypeName + " " + tempName + mVisitor.EOS();
        if (mLocalStorage.hasAtomicTarget(atomicCall->getArg(0)))
        {
            if (const auto translated = mLocalStorage.tryBuildAtomicDispatchStatements(atomicCall->getArg(0), getAtomicInterlockedFunctionName(atomicKind), tempName, {mVisitor.TranslateExpr(atomicCall->getArg(1))}, atomicKind == AtomicBuiltinKind::CompareExchange, mVisitor.mSpaceManager.getSpace()))
            {
                result += *translated;
            }
        }
        else
        {
            result += mVisitor.mSpaceManager.getSpace() + getAtomicInterlockedFunctionName(atomicKind) + "(" + mVisitor.TranslateExpr(atomicCall->getArg(0)) + ", " + mVisitor.TranslateExpr(atomicCall->getArg(1)) + ", " + tempName + ")" + mVisitor.EOS();
        }

        if (!qualifiers.empty())
        {
            result += mVisitor.mSpaceManager.getSpace() + stringJoin(qualifiers, " ") + " ";
        }
        else
        {
            result += mVisitor.mSpaceManager.getSpace();
        }
        result += variableTypeName + " " + variableName + mVisitor.generateDeclArraySpecifier(decl->getType()) + " = " + tempName;
        return result;
    }

    std::string HLSLAggregateInitializerEmitter::makeAggregateConstructionHelperName(const clang::CXXRecordDecl *recordDecl) const
    {
        const auto *canonicalRecordDecl = llvm::cast<clang::CXXRecordDecl>(recordDecl->getCanonicalDecl());
        return "__uglc_make_" + flattenQualifiedHLSLIdentifierForHelperName(canonicalRecordDecl->getQualifiedNameAsString());
    }

    std::string HLSLAggregateInitializerEmitter::registerAggregateConstructionHelper(const clang::CXXRecordDecl *recordDecl)
    {
        const auto *canonicalRecordDecl = llvm::cast<clang::CXXRecordDecl>(recordDecl->getCanonicalDecl());
        if (const auto helperIt = mPendingHelperNames.find(canonicalRecordDecl); helperIt != mPendingHelperNames.end())
        {
            return helperIt->second;
        }

        const std::string helperName = makeAggregateConstructionHelperName(canonicalRecordDecl);
        mPendingHelperNames.emplace(canonicalRecordDecl, helperName);
        mPendingHelperDefinitions.push_back(makeAggregateConstructionHelperDefinition(canonicalRecordDecl, helperName));
        return helperName;
    }

    std::string HLSLAggregateInitializerEmitter::makeAggregateConstructionHelperDefinition(const clang::CXXRecordDecl *recordDecl, const std::string &helperName)
    {
        const clang::QualType recordType = mVisitor.Context->getTypeDeclType(const_cast<clang::CXXRecordDecl *>(recordDecl));
        const std::string recordTypeName = mVisitor.generateTypeCanonicalName(recordType, mTypeConvertor);
        const size_t fieldCount = static_cast<size_t>(std::distance(recordDecl->field_begin(), recordDecl->field_end()));

        std::vector<std::string> parameterDecls;
        std::vector<std::string> aggregateInitializers;
        parameterDecls.reserve(fieldCount);
        aggregateInitializers.reserve(fieldCount);

        unsigned fieldIndex = 0;
        for (const auto *field : recordDecl->fields())
        {
            if (field->getType()->isArrayType())
            {
                throw std::runtime_error("HLSL aggregate temporary helper for \"" + recordDecl->getQualifiedNameAsString() + "\" cannot be synthesized because field \"" + field->getNameAsString() + "\" is an array. Please materialize a named temporary before passing or returning this aggregate.");
            }

            const std::string parameterName = "__uglc_field_" + std::to_string(fieldIndex++);
            parameterDecls.push_back(mVisitor.generateTypeCanonicalName(field->getType(), mTypeConvertor) + " " + parameterName);
            aggregateInitializers.push_back(parameterName);
        }

        std::string result;
        result += recordTypeName + " " + helperName + "(" + stringJoin(parameterDecls, ", ") + ")" + mVisitor.NewLine();
        result += "{" + mVisitor.NewLine();
        result += "    " + recordTypeName + " __uglc_value = { " + stringJoin(aggregateInitializers, ", ") + " }" + mVisitor.EOS();
        result += "    return __uglc_value;" + mVisitor.NewLine();
        result += "}" + mVisitor.NewLine();
        return result;
    }

    const clang::Expr *HLSLAggregateInitializerEmitter::stripTransparentExprWrappers(const clang::Expr *expr)
    {
        const clang::Expr *current = expr;
        while (current != nullptr)
        {
            current = current->IgnoreParens();
            if (const auto *castExpr = llvm::dyn_cast<clang::ImplicitCastExpr>(current))
            {
                current = castExpr->getSubExpr();
                continue;
            }
            if (const auto *cleanupsExpr = llvm::dyn_cast<clang::ExprWithCleanups>(current))
            {
                current = cleanupsExpr->getSubExpr();
                continue;
            }
            if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(current))
            {
                current = bindTemporaryExpr->getSubExpr();
                continue;
            }
            if (const auto *materializeExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(current))
            {
                current = materializeExpr->getSubExpr();
                continue;
            }
            return current;
        }
        return expr;
    }

    std::string HLSLAggregateInitializerEmitter::makeScalarZeroLiteral(const std::string &typeName)
    {
        if (typeName == "bool")
        {
            return "false";
        }
        if (typeName == "half" || typeName == "float" || typeName == "double")
        {
            return "0.0f";
        }
        return "0";
    }

    std::optional<HLSLAggregateInitializerEmitter::HLSLVectorTypeInfo> HLSLAggregateInitializerEmitter::parseHLSLVectorType(const std::string &typeName)
    {
        const size_t qualifierPos = typeName.rfind("::");
        const std::string unqualifiedTypeName = qualifierPos == std::string::npos ? typeName : typeName.substr(qualifierPos + 2);

        const size_t digitPos = unqualifiedTypeName.find_last_not_of("0123456789");
        if (digitPos == std::string::npos || digitPos + 1 >= unqualifiedTypeName.size())
        {
            return std::nullopt;
        }
        if (unqualifiedTypeName.find('x', digitPos + 1) != std::string::npos)
        {
            return std::nullopt;
        }

        const std::string scalarType = unqualifiedTypeName.substr(0, digitPos + 1);
        const std::string suffix = unqualifiedTypeName.substr(digitPos + 1);
        if (suffix.size() != 1 || !std::isdigit(static_cast<unsigned char>(suffix.front())))
        {
            return std::nullopt;
        }

        const int elementCount = suffix.front() - '0';
        if (elementCount < 2 || elementCount > 4)
        {
            return std::nullopt;
        }

        static constexpr const char *kSupportedScalarPrefixes[] = {
            "bool",
            "half",
            "float",
            "double",
            "int",
            "uint",
        };
        for (const char *prefix : kSupportedScalarPrefixes)
        {
            if (scalarType == prefix)
            {
                return HLSLVectorTypeInfo{scalarType, elementCount};
            }
        }
        return std::nullopt;
    }

    std::optional<HLSLAggregateInitializerEmitter::HLSLMatrixTypeInfo> HLSLAggregateInitializerEmitter::parseHLSLMatrixType(const std::string &typeName)
    {
        const size_t xPos = typeName.find('x');
        if (xPos == std::string::npos || xPos == 0 || xPos + 1 >= typeName.size())
        {
            return std::nullopt;
        }

        size_t leftDigitStart = xPos;
        while (leftDigitStart > 0 && std::isdigit(static_cast<unsigned char>(typeName[leftDigitStart - 1])))
        {
            --leftDigitStart;
        }
        if (leftDigitStart == xPos)
        {
            return std::nullopt;
        }

        const std::string scalarType = typeName.substr(0, leftDigitStart);
        const std::string rowDigits = typeName.substr(leftDigitStart, xPos - leftDigitStart);
        const std::string colDigits = typeName.substr(xPos + 1);
        if (rowDigits.empty() || colDigits.empty())
        {
            return std::nullopt;
        }

        static constexpr const char *kSupportedScalarPrefixes[] = {
            "half",
            "float",
            "double",
            "int",
            "uint",
        };
        bool supportedScalar = false;
        for (const char *prefix : kSupportedScalarPrefixes)
        {
            if (scalarType == prefix)
            {
                supportedScalar = true;
                break;
            }
        }
        if (!supportedScalar)
        {
            return std::nullopt;
        }

        return HLSLMatrixTypeInfo{
            scalarType,
            std::stoi(rowDigits),
            std::stoi(colDigits),
        };
    }

    std::string HLSLAggregateInitializerEmitter::makeRepeatedArgumentList(const std::string &argExpr, int count)
    {
        std::vector<std::string> args(static_cast<size_t>(count), argExpr);
        return stringJoin(args, ", ");
    }

    HLSLAggregateInitializerEmitter::AtomicBuiltinKind HLSLAggregateInitializerEmitter::getAtomicBuiltinKind(const clang::FunctionDecl *callee)
    {
        if (callee == nullptr)
        {
            return AtomicBuiltinKind::None;
        }

        const std::string qualifiedName = callee->getCanonicalDecl()->getQualifiedNameAsString();
        if (qualifiedName == "UGL::atomicAdd")
        {
            return AtomicBuiltinKind::Add;
        }
        if (qualifiedName == "UGL::atomicOr")
        {
            return AtomicBuiltinKind::Or;
        }
        if (qualifiedName == "UGL::atomicAnd")
        {
            return AtomicBuiltinKind::And;
        }
        if (qualifiedName == "UGL::atomicLoad")
        {
            return AtomicBuiltinKind::Load;
        }
        if (qualifiedName == "UGL::atomicStore")
        {
            return AtomicBuiltinKind::Store;
        }
        if (qualifiedName == "UGL::atomicCompareExchange")
        {
            return AtomicBuiltinKind::CompareExchange;
        }
        if (qualifiedName == "UGL::atomicMax")
        {
            return AtomicBuiltinKind::Max;
        }
        if (qualifiedName == "UGL::atomicMin")
        {
            return AtomicBuiltinKind::Min;
        }
        return AtomicBuiltinKind::None;
    }

    bool HLSLAggregateInitializerEmitter::atomicBuiltinReturnsOriginalValue(const AtomicBuiltinKind kind)
    {
        return kind == AtomicBuiltinKind::Add || kind == AtomicBuiltinKind::Or || kind == AtomicBuiltinKind::And;
    }

    std::string HLSLAggregateInitializerEmitter::getAtomicInterlockedFunctionName(const AtomicBuiltinKind kind)
    {
        switch (kind)
        {
        case AtomicBuiltinKind::Add:
            return "InterlockedAdd";
        case AtomicBuiltinKind::Or:
            return "InterlockedOr";
        case AtomicBuiltinKind::And:
            return "InterlockedAnd";
        case AtomicBuiltinKind::Store:
            return "InterlockedExchange";
        case AtomicBuiltinKind::CompareExchange:
            return "InterlockedCompareExchange";
        case AtomicBuiltinKind::Max:
            return "InterlockedMax";
        case AtomicBuiltinKind::Min:
            return "InterlockedMin";
        default:
            return {};
        }
    }

    std::string HLSLAggregateInitializerEmitter::makeGeneratedIdentifier(const std::string &prefix, const clang::SourceLocation location)
    {
        return "__uglc_" + prefix + "_" + std::to_string(location.getRawEncoding());
    }
} // namespace UGLC::CodeGen::HLSL
