/** Type-name spelling, parsing, and classification helpers shared by the UGLIR lowering passes.
    These were extracted from UGLIRLowering.cpp; every function is a pure string or AST-type utility
    with no dependence on lowering state. */
#include "UGLIRTypeSpelling.hpp"
#include <clang/AST/Attr.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace UGLC::CodeGen::UGLIR
{
    namespace TypeSpelling
    {
        bool isFragmentKillCapableEntryKind(ShaderEntryKind entryKind)
        {
            return entryKind == ShaderEntryKind::Fragment || entryKind == ShaderEntryKind::PixelLocal;
        }

        const char *describeEntryKindForDiagnostic(ShaderEntryKind entryKind)
        {
            switch (entryKind)
            {
            case ShaderEntryKind::Compute:
                return "compute";
            case ShaderEntryKind::Vertex:
                return "vertex";
            case ShaderEntryKind::Fragment:
                return "fragment";
            case ShaderEntryKind::PixelLocal:
                return "pixel";
            case ShaderEntryKind::None:
                return "shader";
            }
            return "shader";
        }

        const char *getFragmentKillIntrinsicDiagnosticName(IntrinsicCallKind intrinsicCallKind)
        {
            switch (intrinsicCallKind)
            {
            case IntrinsicCallKind::DiscardFragment:
                return "discard_fragment";
            case IntrinsicCallKind::Clip:
                return "clip";
            default:
                return "";
            }
        }

        std::string makeUGLIntrinsicStem(const std::string &functionName)
        {
            std::string stem = functionName;
            const size_t lastScope = stem.rfind("::");
            if (lastScope != std::string::npos)
            {
                stem = stem.substr(lastScope + 2);
            }
            const size_t memberScope = stem.rfind('.');
            if (memberScope != std::string::npos)
            {
                stem = stem.substr(memberScope + 1);
            }
            const size_t templateMarker = stem.find("__T");
            if (templateMarker != std::string::npos)
            {
                stem = stem.substr(0, templateMarker);
            }
            if (stem.rfind("UGL__", 0) == 0)
            {
                stem = stem.substr(std::string("UGL__").size());
            }
            if (stem.rfind("std____math__", 0) == 0)
            {
                stem = stem.substr(std::string("std____math__").size());
            }
            return stem;
        }

        IntrinsicCallKind classifyTextureIntrinsicCallKind(const std::string &methodName)
        {
            switch (UGLC::CodeGen::classifyTextureMemberCall(makeUGLIntrinsicStem(methodName)))
            {
            case UGLC::CodeGen::TextureMemberCallKind::Read:
                return IntrinsicCallKind::TextureRead;
            case UGLC::CodeGen::TextureMemberCallKind::Write:
                return IntrinsicCallKind::TextureWrite;
            case UGLC::CodeGen::TextureMemberCallKind::Sample:
                return IntrinsicCallKind::TextureSample;
            case UGLC::CodeGen::TextureMemberCallKind::SampleLevel:
                return IntrinsicCallKind::TextureSampleLevel;
            case UGLC::CodeGen::TextureMemberCallKind::SampleGrad:
                return IntrinsicCallKind::TextureSampleGrad;
            case UGLC::CodeGen::TextureMemberCallKind::Gather:
            case UGLC::CodeGen::TextureMemberCallKind::GatherCmp:
                return IntrinsicCallKind::TextureGather;
            case UGLC::CodeGen::TextureMemberCallKind::GatherRed:
                return IntrinsicCallKind::TextureGatherRed;
            case UGLC::CodeGen::TextureMemberCallKind::GatherGreen:
                return IntrinsicCallKind::TextureGatherGreen;
            case UGLC::CodeGen::TextureMemberCallKind::GatherBlue:
                return IntrinsicCallKind::TextureGatherBlue;
            case UGLC::CodeGen::TextureMemberCallKind::GatherAlpha:
                return IntrinsicCallKind::TextureGatherAlpha;
            case UGLC::CodeGen::TextureMemberCallKind::GetDimensions:
                return IntrinsicCallKind::TextureGetDimensions;
            case UGLC::CodeGen::TextureMemberCallKind::Unknown:
                return IntrinsicCallKind::None;
            }
            return IntrinsicCallKind::None;
        }

        IntrinsicCallKind classifyCallIntrinsicCallKind(const std::string &functionName)
        {
            const std::string stem = makeUGLIntrinsicStem(functionName);
            static const std::unordered_map<std::string, IntrinsicCallKind> intrinsicCallKinds = {
                {"discard_fragment", IntrinsicCallKind::DiscardFragment},
                {"clip", IntrinsicCallKind::Clip},
                {"GroupMemoryBarrier", IntrinsicCallKind::GroupMemoryBarrier},
                {"GroupMemoryBarrierWithGroupSync", IntrinsicCallKind::GroupMemoryBarrierWithGroupSync},
                {"DeviceMemoryBarrier", IntrinsicCallKind::DeviceMemoryBarrier},
                {"DeviceMemoryBarrierWithGroupSync", IntrinsicCallKind::DeviceMemoryBarrierWithGroupSync},
                {"AllMemoryBarrier", IntrinsicCallKind::AllMemoryBarrier},
                {"AllMemoryBarrierWithGroupSync", IntrinsicCallKind::AllMemoryBarrierWithGroupSync},
                {"atomicAdd", IntrinsicCallKind::AtomicAdd},
                {"atomicAnd", IntrinsicCallKind::AtomicAnd},
                {"atomicOr", IntrinsicCallKind::AtomicOr},
                {"atomicMin", IntrinsicCallKind::AtomicMin},
                {"atomicMax", IntrinsicCallKind::AtomicMax},
                {"atomicLoad", IntrinsicCallKind::AtomicLoad},
                {"atomicStore", IntrinsicCallKind::AtomicStore},
                {"atomicCompareExchange", IntrinsicCallKind::AtomicCompareExchange},
                {"WaveGetLaneIndex", IntrinsicCallKind::WaveGetLaneIndex},
                {"WaveGetLaneCount", IntrinsicCallKind::WaveGetLaneCount},
                {"WaveActiveBallot", IntrinsicCallKind::WaveActiveBallot},
                {"WaveActiveCountBits", IntrinsicCallKind::WaveActiveCountBits},
                {"WavePrefixCountBits", IntrinsicCallKind::WavePrefixCountBits},
                {"WavePrefixSum", IntrinsicCallKind::WavePrefixSum},
                {"WaveReadLaneAt", IntrinsicCallKind::WaveReadLaneAt},
                {"WaveReadLaneFirst", IntrinsicCallKind::WaveReadLaneFirst},
                {"WaveMatch", IntrinsicCallKind::WaveMatch},
                {"QuadReadLaneAt", IntrinsicCallKind::QuadReadLaneAt},
                {"QuadReadAcrossX", IntrinsicCallKind::QuadReadAcrossX},
                {"QuadReadAcrossY", IntrinsicCallKind::QuadReadAcrossY},
                {"QuadReadAcrossDiagonal", IntrinsicCallKind::QuadReadAcrossDiagonal},
                {"asfloat", IntrinsicCallKind::BitcastAsFloat},
                {"asuint", IntrinsicCallKind::BitcastAsUInt},
                {"asint", IntrinsicCallKind::BitcastAsInt},
                {"abs", IntrinsicCallKind::MathAbs},
                {"acos", IntrinsicCallKind::MathAcos},
                {"all", IntrinsicCallKind::MathAll},
                {"any", IntrinsicCallKind::MathAny},
                {"asin", IntrinsicCallKind::MathAsin},
                {"atan", IntrinsicCallKind::MathAtan},
                {"atan2", IntrinsicCallKind::MathAtan2},
                {"ceil", IntrinsicCallKind::MathCeil},
                {"clamp", IntrinsicCallKind::MathClamp},
                {"cos", IntrinsicCallKind::MathCos},
                {"cross", IntrinsicCallKind::MathCross},
                {"ddx", IntrinsicCallKind::MathDdx},
                {"ddy", IntrinsicCallKind::MathDdy},
                {"distance", IntrinsicCallKind::MathDistance},
                {"dot", IntrinsicCallKind::MathDot},
                {"exp", IntrinsicCallKind::MathExp},
                {"exp2", IntrinsicCallKind::MathExp2},
                {"floor", IntrinsicCallKind::MathFloor},
                {"frac", IntrinsicCallKind::MathFrac},
                {"fmod", IntrinsicCallKind::MathFmod},
                {"firstbithigh", IntrinsicCallKind::MathFirstBitHigh},
                {"firstbitlow", IntrinsicCallKind::MathFirstBitLow},
                {"length", IntrinsicCallKind::MathLength},
                {"lerp", IntrinsicCallKind::MathLerp},
                {"log", IntrinsicCallKind::MathLog},
                {"log2", IntrinsicCallKind::MathLog2},
                {"max", IntrinsicCallKind::MathMax},
                {"min", IntrinsicCallKind::MathMin},
                {"modf", IntrinsicCallKind::MathModf},
                {"mul", IntrinsicCallKind::MathMul},
                {"normalize", IntrinsicCallKind::MathNormalize},
                {"pow", IntrinsicCallKind::MathPow},
                {"reflect", IntrinsicCallKind::MathReflect},
                {"round", IntrinsicCallKind::MathRound},
                {"rsqrt", IntrinsicCallKind::MathRsqrt},
                {"saturate", IntrinsicCallKind::MathSaturate},
                {"sign", IntrinsicCallKind::MathSign},
                {"sin", IntrinsicCallKind::MathSin},
                {"sincos", IntrinsicCallKind::MathSincos},
                {"sqrt", IntrinsicCallKind::MathSqrt},
                {"step", IntrinsicCallKind::MathStep},
                {"smoothstep", IntrinsicCallKind::MathSmoothstep},
                {"tan", IntrinsicCallKind::MathTan},
                {"transpose", IntrinsicCallKind::MathTranspose},
            };
            const auto iter = intrinsicCallKinds.find(stem);
            if (iter != intrinsicCallKinds.end())
            {
                return iter->second;
            }
            return IntrinsicCallKind::None;
        }

        const clang::Expr *stripDefaultInitializerWrappers(const clang::Expr *expr)
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

        bool isOmissibleFramebufferDefaultInitializer(const clang::Expr *expr)
        {
            const clang::Expr *init = stripDefaultInitializerWrappers(expr);
            if (init == nullptr)
            {
                return true;
            }
            if (llvm::isa<clang::ImplicitValueInitExpr>(init) || llvm::isa<clang::CXXScalarValueInitExpr>(init))
            {
                return true;
            }
            if (llvm::isa<clang::CXXDefaultInitExpr>(init) || llvm::isa<clang::CXXDefaultArgExpr>(init))
            {
                return true;
            }
            if (const auto *constantExpr = llvm::dyn_cast<clang::ConstantExpr>(init))
            {
                return isOmissibleFramebufferDefaultInitializer(constantExpr->getSubExpr());
            }
            if (const auto *initListExpr = llvm::dyn_cast<clang::InitListExpr>(init))
            {
                for (const clang::Expr *childInit : initListExpr->inits())
                {
                    if (!isOmissibleFramebufferDefaultInitializer(childInit))
                    {
                        return false;
                    }
                }
                return true;
            }
            if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(init))
            {
                for (const clang::Expr *arg : constructExpr->arguments())
                {
                    if (!isOmissibleFramebufferDefaultInitializer(arg))
                    {
                        return false;
                    }
                }
                return true;
            }
            return false;
        }

        const clang::CXXRecordDecl *recordDefinition(const clang::CXXRecordDecl *recordDecl)
        {
            if (recordDecl == nullptr)
            {
                return nullptr;
            }
            if (const clang::CXXRecordDecl *definition = recordDecl->getDefinition())
            {
                return definition;
            }
            return recordDecl;
        }

        const clang::Stmt *stripNonSemanticStatementWrappers(const clang::Stmt *stmt)
        {
            const clang::Stmt *current = stmt;
            while (const auto *attributedStmt = llvm::dyn_cast_or_null<clang::AttributedStmt>(current))
            {
                current = attributedStmt->getSubStmt();
            }
            return current;
        }

        std::vector<const clang::FieldDecl *> collectRecordFields(const clang::CXXRecordDecl *recordDecl)
        {
            std::vector<const clang::FieldDecl *> result;
            if (recordDecl == nullptr)
            {
                return result;
            }
            if (const clang::CXXRecordDecl *definition = recordDecl->getDefinition())
            {
                recordDecl = definition;
            }
            if (recordDecl->isThisDeclarationADefinition())
            {
                for (const clang::FieldDecl *fieldDecl : recordDecl->fields())
                {
                    result.push_back(fieldDecl);
                }
                if (!result.empty())
                {
                    return result;
                }
            }
            if (const clang::CXXRecordDecl *memberPattern = recordDecl->getInstantiatedFromMemberClass())
            {
                return collectRecordFields(memberPattern);
            }
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                return result;
            }
            const clang::CXXRecordDecl *patternDecl = recordDefinition(specializationDecl->getSpecializedTemplate()->getTemplatedDecl());
            if (patternDecl == nullptr || !patternDecl->isThisDeclarationADefinition())
            {
                return result;
            }
            for (const clang::FieldDecl *fieldDecl : patternDecl->fields())
            {
                result.push_back(fieldDecl);
            }
            return result;
        }

        bool isStatelessRecordType(clang::QualType type)
        {
            if (type.isNull())
            {
                return false;
            }
            const clang::CXXRecordDecl *recordDecl = recordDefinition(type.getNonReferenceType()->getAsCXXRecordDecl());
            return recordDecl != nullptr && collectRecordFields(recordDecl).empty();
        }

        bool startsWith(const std::string &value, const std::string &prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }

        bool containsPathFragment(const std::string &value, const std::string &fragment)
        {
            return value.find(fragment) != std::string::npos;
        }

        std::string trimCopy(const std::string &input)
        {
            const size_t first = input.find_first_not_of(" \t\r\n");
            if (first == std::string::npos)
            {
                return {};
            }
            const size_t last = input.find_last_not_of(" \t\r\n");
            return input.substr(first, last - first + 1);
        }

        bool isFunctionStyleAttributeName(const std::string &rawAttribute, const std::string &attributeName)
        {
            if (rawAttribute == attributeName)
            {
                return true;
            }
            return rawAttribute.size() > attributeName.size() && startsWith(rawAttribute, attributeName) && rawAttribute[attributeName.size()] == '(';
        }

        std::vector<std::string> splitAttributeParameters(const std::string &rawAttribute)
        {
            std::vector<std::string> result;
            const size_t leftParen = rawAttribute.find('(');
            const size_t rightParen = rawAttribute.rfind(')');
            if (leftParen == std::string::npos || rightParen == std::string::npos || rightParen < leftParen)
            {
                return result;
            }

            const std::string params = rawAttribute.substr(leftParen + 1, rightParen - leftParen - 1);
            size_t start = 0;
            while (start <= params.size())
            {
                const size_t end = params.find(',', start);
                const std::string currentParam = end == std::string::npos ? params.substr(start) : params.substr(start, end - start);
                result.emplace_back(trimCopy(currentParam));
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return result;
        }

        std::vector<std::string> getAnnotationStrings(const clang::Decl *decl)
        {
            std::vector<std::string> result;
            if (decl == nullptr || !decl->hasAttrs())
            {
                return result;
            }
            for (const clang::Attr *attr : decl->getAttrs())
            {
                if (const auto *annotateAttr = llvm::dyn_cast<clang::AnnotateAttr>(attr))
                {
                    result.push_back(annotateAttr->getAnnotation().str());
                }
            }
            return result;
        }

        bool isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName)
        {
            if (!startsWith(rawAttribute, attributeName) || rawAttribute.size() == attributeName.size())
            {
                return false;
            }
            for (size_t index = attributeName.size(); index < rawAttribute.size(); ++index)
            {
                if (!std::isdigit(static_cast<unsigned char>(rawAttribute[index])))
                {
                    return false;
                }
            }
            return true;
        }

        int getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName)
        {
            if (!isExactIndexedAttribute(rawAttribute, attributeName))
            {
                return -1;
            }
            int result = 0;
            for (size_t index = attributeName.size(); index < rawAttribute.size(); ++index)
            {
                result = result * 10 + static_cast<int>(rawAttribute[index] - '0');
            }
            return result;
        }

        std::string stripElaboratedPrefix(std::string name)
        {
            const std::string structPrefix = "struct ";
            const std::string classPrefix = "class ";
            if (startsWith(name, structPrefix))
            {
                return name.substr(structPrefix.size());
            }
            if (startsWith(name, classPrefix))
            {
                return name.substr(classPrefix.size());
            }
            return name;
        }

        std::string typeSpelling(const clang::QualType &type)
        {
            clang::LangOptions langOptions;
            clang::PrintingPolicy policy(langOptions);
            policy.SuppressTagKeyword = true;
            policy.SuppressScope = false;
            policy.FullyQualifiedName = false;
            return stripElaboratedPrefix(type.getUnqualifiedType().getAsString(policy));
        }

        std::string qualifiedName(const clang::NamedDecl *decl)
        {
            if (decl == nullptr)
            {
                return {};
            }
            return decl->getQualifiedNameAsString();
        }

        std::string normalizeStdName(std::string name)
        {
            const std::string libcxxPrefix = "std::__1::";
            if (startsWith(name, libcxxPrefix))
            {
                return "std::" + name.substr(libcxxPrefix.size());
            }
            return name;
        }

        bool parseVectorAlias(const std::string &name, std::string &elementType, uint32_t &width)
        {
            std::string localName = name;
            const std::string uglPrefix = "UGL::";
            if (startsWith(localName, uglPrefix))
            {
                localName = localName.substr(uglPrefix.size());
            }
            if (localName.size() < 4)
            {
                return false;
            }

            const char widthChar = localName.back();
            if (widthChar < '2' || widthChar > '4')
            {
                return false;
            }

            const std::string scalarName = localName.substr(0, localName.size() - 1);
            if (scalarName == "float")
            {
                elementType = "f32";
            }
            else if (scalarName == "int")
            {
                elementType = "i32";
            }
            else if (scalarName == "uint")
            {
                elementType = "u32";
            }
            else if (scalarName == "half")
            {
                elementType = "f16";
            }
            else if (scalarName == "bool")
            {
                elementType = "bool";
            }
            else
            {
                return false;
            }

            width = static_cast<uint32_t>(widthChar - '0');
            return true;
        }

        bool parseMatrixAlias(const std::string &name, ParsedMatrixAlias &result)
        {
            std::string localName = name;
            const std::string uglPrefix = "UGL::";
            if (startsWith(localName, uglPrefix))
            {
                localName = localName.substr(uglPrefix.size());
            }
            const size_t separator = localName.rfind('x');
            if (separator == std::string::npos || separator < 2u || separator + 1u >= localName.size())
            {
                return false;
            }

            const char rowChar = localName[separator - 1u];
            const char columnChar = localName[separator + 1u];
            if (columnChar < '2' || columnChar > '4' || rowChar < '2' || rowChar > '4' || separator + 2u != localName.size())
            {
                return false;
            }

            const std::string scalarName = localName.substr(0, separator - 1u);
            ScalarKind scalarKind = ScalarKind::None;
            if (scalarName == "float")
            {
                scalarKind = ScalarKind::Float;
                result.elementType = "f32";
            }
            else if (scalarName == "half")
            {
                scalarKind = ScalarKind::Half;
                result.elementType = "f16";
            }
            else
            {
                return false;
            }

            result.scalarKind = scalarKind;
            result.columns = static_cast<uint32_t>(columnChar - '0');
            result.rows = static_cast<uint32_t>(rowChar - '0');
            result.canonicalName = scalarName + std::to_string(result.columns) + "x" + std::to_string(result.rows);
            return true;
        }

        std::vector<std::string> splitTemplateArgumentList(const std::string &argumentText)
        {
            std::vector<std::string> result;
            size_t argumentStart = 0;
            int nestedDepth = 0;
            for (size_t index = 0; index <= argumentText.size(); ++index)
            {
                const bool atEnd = index == argumentText.size();
                const char ch = atEnd ? ',' : argumentText[index];
                if (!atEnd)
                {
                    if (ch == '<')
                    {
                        ++nestedDepth;
                    }
                    else if (ch == '>')
                    {
                        --nestedDepth;
                    }
                }
                if ((atEnd || ch == ',') && nestedDepth == 0)
                {
                    result.push_back(trimCopy(argumentText.substr(argumentStart, index - argumentStart)));
                    argumentStart = index + 1u;
                }
            }
            return result;
        }

        ScalarKind scalarKindFromUGLTemplateScalarSpelling(std::string scalarName)
        {
            scalarName = trimCopy(scalarName);
            if (startsWith(scalarName, "UGL::"))
            {
                scalarName = scalarName.substr(5);
            }
            if (scalarName == "float" || scalarName == "float_t")
            {
                return ScalarKind::Float;
            }
            if (scalarName == "half" || scalarName == "half_t")
            {
                return ScalarKind::Half;
            }
            if (scalarName == "int" || scalarName == "int_t")
            {
                return ScalarKind::Int;
            }
            if (scalarName == "uint" || scalarName == "uint_t" || scalarName == "unsigned int")
            {
                return ScalarKind::UInt;
            }
            if (scalarName == "bool" || scalarName == "bool_t")
            {
                return ScalarKind::Bool;
            }
            return ScalarKind::None;
        }

        std::string parsedScalarIRTypeName(ScalarKind kind)
        {
            switch (kind)
            {
            case ScalarKind::Bool:
                return "bool";
            case ScalarKind::Int:
                return "i32";
            case ScalarKind::UInt:
                return "u32";
            case ScalarKind::Float:
                return "f32";
            case ScalarKind::Half:
                return "f16";
            case ScalarKind::None:
                return {};
            }
            return {};
        }

        std::string parsedScalarAliasName(ScalarKind kind)
        {
            switch (kind)
            {
            case ScalarKind::Bool:
                return "bool";
            case ScalarKind::Int:
                return "int";
            case ScalarKind::UInt:
                return "uint";
            case ScalarKind::Float:
                return "float";
            case ScalarKind::Half:
                return "half";
            case ScalarKind::None:
                return {};
            }
            return {};
        }

        bool parseMatrixTemplateSpelling(const std::string &name, ParsedMatrixAlias &result)
        {
            std::string localName = name;
            if (startsWith(localName, "UGL::"))
            {
                localName = localName.substr(5);
            }
            if (!startsWith(localName, "Matrix<") || localName.back() != '>')
            {
                return false;
            }
            const std::string argumentText = localName.substr(7u, localName.size() - 8u);
            const std::vector<std::string> arguments = splitTemplateArgumentList(argumentText);
            if (arguments.size() != 3u)
            {
                return false;
            }

            const ScalarKind scalarKind = scalarKindFromUGLTemplateScalarSpelling(arguments[0]);
            if (scalarKind == ScalarKind::None)
            {
                return false;
            }
            const std::string rowsText = trimCopy(arguments[1]);
            const std::string columnsText = trimCopy(arguments[2]);
            if (rowsText.size() != 1u || columnsText.size() != 1u ||
                rowsText[0] < '1' || rowsText[0] > '4' ||
                columnsText[0] < '1' || columnsText[0] > '4')
            {
                return false;
            }

            result.scalarKind = scalarKind;
            result.elementType = parsedScalarIRTypeName(scalarKind);
            result.rows = static_cast<uint32_t>(rowsText[0] - '0');
            result.columns = static_cast<uint32_t>(columnsText[0] - '0');
            result.canonicalName = parsedScalarAliasName(scalarKind) + std::to_string(result.columns) + "x" + std::to_string(result.rows);
            return true;
        }

        const Type *findLoweredTypeByName(const Module &module, const std::string &typeName)
        {
            const auto iter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
                return type.name == typeName;
            });
            return iter == module.types.end() ? nullptr : &*iter;
        }

        bool isWorkgroupWrapperTypeName(const Module &module, const std::string &typeName)
        {
            if (const Type *type = findLoweredTypeByName(module, typeName))
            {
                return type->kind == TypeKind::Workgroup;
            }
            return typeName.find("GroupShared<") != std::string::npos;
        }

        uint32_t bitWidthForScalarKind(ScalarKind kind)
        {
            switch (kind)
            {
            case ScalarKind::Bool:
                return 1;
            case ScalarKind::Half:
                return 16;
            case ScalarKind::Int:
            case ScalarKind::UInt:
            case ScalarKind::Float:
                return 32;
            case ScalarKind::None:
                return 0;
            }
            return 0;
        }

        std::string scalarIRTypeName(ScalarKind kind)
        {
            switch (kind)
            {
            case ScalarKind::Bool:
                return "bool";
            case ScalarKind::Int:
                return "i32";
            case ScalarKind::UInt:
                return "u32";
            case ScalarKind::Float:
                return "f32";
            case ScalarKind::Half:
                return "f16";
            case ScalarKind::None:
                return {};
            }
            return {};
        }

        bool describeConstructValueShape(const Module &module,
                                         const std::string &typeName,
                                         ScalarKind &scalarKind,
                                         uint32_t &componentCount)
        {
            std::string vectorElementType;
            uint32_t vectorWidth = 0;
            if (parseVectorAlias(typeName, vectorElementType, vectorWidth))
            {
                scalarKind = scalarKindFromTypeName(vectorElementType);
                componentCount = vectorWidth;
                return scalarKind != ScalarKind::None;
            }

            if (const Type *type = findLoweredTypeByName(module, typeName))
            {
                switch (type->kind)
                {
                case TypeKind::Bool:
                    scalarKind = ScalarKind::Bool;
                    componentCount = 1;
                    return true;
                case TypeKind::Int:
                    scalarKind = ScalarKind::Int;
                    componentCount = 1;
                    return true;
                case TypeKind::UInt:
                    scalarKind = ScalarKind::UInt;
                    componentCount = 1;
                    return true;
                case TypeKind::Float:
                    scalarKind = ScalarKind::Float;
                    componentCount = 1;
                    return true;
                case TypeKind::Half:
                    scalarKind = ScalarKind::Half;
                    componentCount = 1;
                    return true;
                case TypeKind::Vector:
                    scalarKind = type->scalarKind != ScalarKind::None ? type->scalarKind : scalarKindFromTypeName(type->elementType);
                    componentCount = type->vectorWidth;
                    return scalarKind != ScalarKind::None && componentCount > 1u;
                default:
                    break;
                }
            }

            scalarKind = scalarKindFromTypeName(typeName);
            componentCount = scalarKind == ScalarKind::None ? 0u : 1u;
            return scalarKind != ScalarKind::None;
        }

        bool isVectorSwizzleName(const std::string &memberName)
        {
            if (memberName.empty() || memberName.size() > 4u)
            {
                return false;
            }
            for (const char ch : memberName)
            {
                if (std::string("xyzwrgba").find(ch) == std::string::npos)
                {
                    return false;
                }
            }
            return true;
        }

        Expression makeUIntLiteralExpression(uint32_t value, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Literal;
            result.type = "u32";
            result.value = std::to_string(value);
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeResourceDeclRefExpression(const ResourceBinding &resource, std::string typeName)
        {
            Expression result;
            result.kind = ExpressionKind::DeclRef;
            result.type = std::move(typeName);
            result.name = resource.name;
            result.sourceLocation = resource.sourceLocation;
            return result;
        }

        Expression makeMemberRefExpression(Expression base, std::string memberName, std::string typeName, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::MemberRef;
            result.type = std::move(typeName);
            result.name = std::move(memberName);
            result.operands.push_back(std::move(base));
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeSubscriptExpression(Expression base, Expression index, std::string typeName, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Subscript;
            result.type = std::move(typeName);
            result.operands.push_back(std::move(base));
            result.operands.push_back(std::move(index));
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeBinaryExpression(std::string operatorName, Expression lhs, Expression rhs, std::string typeName, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Binary;
            result.type = std::move(typeName);
            result.operatorName = std::move(operatorName);
            result.operands.push_back(std::move(lhs));
            result.operands.push_back(std::move(rhs));
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeConditionalExpression(Expression condition, Expression trueValue, Expression falseValue, std::string typeName, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Conditional;
            result.type = std::move(typeName);
            result.operands.push_back(std::move(condition));
            result.operands.push_back(std::move(trueValue));
            result.operands.push_back(std::move(falseValue));
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeStoreExpression(Expression destination, Expression value, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Store;
            result.type = "void";
            result.operands.push_back(std::move(destination));
            result.operands.push_back(std::move(value));
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeGenericCallExpression(std::string name, std::string typeName, std::vector<Expression> operands, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Call;
            result.type = std::move(typeName);
            result.name = std::move(name);
            result.intrinsicCallKind = classifyCallIntrinsicCallKind(result.name);
            result.operands = std::move(operands);
            result.sourceLocation = std::move(location);
            return result;
        }

        Expression makeSafeUnsignedIndexExpression(Expression index, Expression exclusiveBound, SourceLocation location)
        {
            Expression maxBound = makeGenericCallExpression("max",
                                                            "u32",
                                                            {std::move(exclusiveBound), makeUIntLiteralExpression(1u, location)},
                                                            location);
            Expression lastIndex = makeBinaryExpression("-",
                                                        std::move(maxBound),
                                                        makeUIntLiteralExpression(1u, location),
                                                        "u32",
                                                        location);
            return makeGenericCallExpression("min", "u32", {std::move(index), std::move(lastIndex)}, location);
        }

        Expression makeBoolAndExpression(Expression lhs, Expression rhs, SourceLocation location)
        {
            return makeBinaryExpression("&&", std::move(lhs), std::move(rhs), "bool", std::move(location));
        }

        Statement makeExpressionStatement(Expression expression)
        {
            Statement statement;
            statement.kind = StatementKind::Expression;
            statement.sourceLocation = expression.sourceLocation;
            statement.expressions.push_back(std::move(expression));
            return statement;
        }

        Expression makeScalarConvertExpression(Expression value, std::string targetType, ScalarKind targetScalarKind, SourceLocation location)
        {
            std::string valueElementType;
            uint32_t valueVectorWidth = 1;
            const bool valueIsVectorAlias = parseVectorAlias(value.type, valueElementType, valueVectorWidth);
            const bool valueIsScalar = !valueIsVectorAlias || valueVectorWidth <= 1u;
            if (value.type == targetType || (valueIsScalar && scalarKindFromTypeName(value.type) == targetScalarKind))
            {
                return value;
            }

            Expression result;
            result.kind = ExpressionKind::Construct;
            result.type = std::move(targetType);
            result.name = result.type;
            result.operands.push_back(std::move(value));
            result.sourceLocation = std::move(location);
            result.constructInfo.kind = ConstructKind::ScalarConvert;
            result.constructInfo.targetScalarKind = targetScalarKind;
            result.constructInfo.targetComponentCount = 1u;
            result.constructInfo.components.push_back(ConstructComponent{
                .operandIndex = 0u,
                .sourceComponentIndex = 0u,
                .sourceScalarKind = scalarKindFromTypeName(result.operands.front().type),
                .targetScalarKind = result.constructInfo.targetScalarKind,
                .sourceIsVector = false,
            });
            return result;
        }

        Expression makeUIntExpression(Expression value, SourceLocation location)
        {
            return makeScalarConvertExpression(std::move(value), "u32", ScalarKind::UInt, std::move(location));
        }

        const ResourceBinding *findResourceByName(const Module &module, const std::string &name)
        {
            const auto iter = std::find_if(module.reflection.resources.begin(), module.reflection.resources.end(), [&name](const ResourceBinding &resource) {
                return resource.name == name;
            });
            return iter == module.reflection.resources.end() ? nullptr : &*iter;
        }

        const ResourceBinding *findResourceByRole(const Module &module,
                                                  uint32_t bindGroupIndex,
                                                  ResourceRole role,
                                                  std::optional<uint32_t> resourceIndex)
        {
            const auto iter = std::find_if(module.reflection.resources.begin(), module.reflection.resources.end(), [&](const ResourceBinding &resource) {
                if (resource.bindGroupIndex != bindGroupIndex || resource.resourceRole != role)
                {
                    return false;
                }
                return !resourceIndex.has_value() || resource.resourceIndex == *resourceIndex;
            });
            return iter == module.reflection.resources.end() ? nullptr : &*iter;
        }

        size_t erasedResourcePayloadOffset(const Expression &expression)
        {
            return !expression.operands.empty() && expression.operands.front().type == "resource_selector" ? 1u : 0u;
        }

        const ResourceBinding *findSelectedResource(const Module &module, const Expression &expression)
        {
            if (expression.operands.empty() || expression.operands.front().type != "resource_selector")
            {
                return nullptr;
            }
            return findResourceByName(module, expression.operands.front().name);
        }

        std::optional<uint32_t> findSelectedBindGroupIndex(const Module &module, const Expression &expression)
        {
            if (expression.operands.empty() || expression.operands.front().type != "resource_selector")
            {
                return std::nullopt;
            }
            const std::string &selectorName = expression.operands.front().name;
            if (const ResourceBinding *resource = findResourceByName(module, selectorName))
            {
                return resource->bindGroupIndex;
            }
            const size_t dotPosition = selectorName.find('.');
            const std::string bindGroupName = dotPosition == std::string::npos ? selectorName : selectorName.substr(0, dotPosition);
            for (const ResourceBinding &resource : module.reflection.resources)
            {
                if (resource.name == bindGroupName ||
                    resource.name.rfind(bindGroupName + ".", 0) == 0)
                {
                    return resource.bindGroupIndex;
                }
            }
            return std::nullopt;
        }

        std::string extractTemplateArgumentSpelling(const std::string &typeName, unsigned argumentIndex)
        {
            const size_t leftAngle = typeName.find('<');
            const size_t rightAngle = typeName.rfind('>');
            if (leftAngle == std::string::npos || rightAngle == std::string::npos || rightAngle <= leftAngle)
            {
                return {};
            }

            unsigned currentArgument = 0;
            size_t argumentStart = leftAngle + 1;
            int nestedDepth = 0;
            for (size_t index = leftAngle + 1; index <= rightAngle; ++index)
            {
                const bool atEnd = index == rightAngle;
                const char ch = atEnd ? ',' : typeName[index];
                if (!atEnd)
                {
                    if (ch == '<')
                    {
                        ++nestedDepth;
                    }
                    else if (ch == '>')
                    {
                        --nestedDepth;
                    }
                }
                if ((atEnd || ch == ',') && nestedDepth == 0)
                {
                    if (currentArgument == argumentIndex)
                    {
                        return trimCopy(typeName.substr(argumentStart, index - argumentStart));
                    }
                    ++currentArgument;
                    argumentStart = index + 1;
                }
            }
            return {};
        }

        std::unordered_map<std::string, clang::QualType> collectClassTypeTemplateArguments(const clang::CXXRecordDecl *recordDecl)
        {
            std::unordered_map<std::string, clang::QualType> result;
            recordDecl = recordDefinition(recordDecl);
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                return result;
            }
            const clang::ClassTemplateDecl *templateDecl = specializationDecl->getSpecializedTemplate();
            if (templateDecl == nullptr)
            {
                return result;
            }
            const clang::TemplateParameterList *parameters = templateDecl->getTemplateParameters();
            const clang::TemplateArgumentList &arguments = specializationDecl->getTemplateArgs();
            const unsigned argumentCount = std::min(parameters->size(), arguments.size());
            for (unsigned index = 0; index < argumentCount; ++index)
            {
                const auto *typeParameter = llvm::dyn_cast_or_null<clang::TemplateTypeParmDecl>(parameters->getParam(index));
                const clang::TemplateArgument &argument = arguments[index];
                if (typeParameter == nullptr || argument.getKind() != clang::TemplateArgument::Type)
                {
                    continue;
                }
                result[typeParameter->getNameAsString()] = argument.getAsType();
            }
            return result;
        }

        clang::QualType resolveDependentTypeAliasSpelling(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                         std::string typeName)
        {
            typeName = trimCopy(typeName);
            const std::string typenamePrefix = "typename ";
            if (startsWith(typeName, typenamePrefix))
            {
                typeName = trimCopy(typeName.substr(typenamePrefix.size()));
            }

            const auto directIter = typeArguments.find(typeName);
            if (directIter != typeArguments.end())
            {
                return directIter->second;
            }

            const size_t separator = typeName.find("::");
            if (separator == std::string::npos)
            {
                return {};
            }
            const std::string ownerName = typeName.substr(0, separator);
            const std::string aliasName = typeName.substr(separator + 2);
            const auto ownerIter = typeArguments.find(ownerName);
            if (ownerIter == typeArguments.end())
            {
                return {};
            }
            const clang::CXXRecordDecl *ownerRecord = recordDefinition(ownerIter->second.getNonReferenceType()->getAsCXXRecordDecl());
            if (ownerRecord == nullptr)
            {
                return {};
            }
            for (const clang::Decl *decl : ownerRecord->decls())
            {
                const auto *typeAliasDecl = llvm::dyn_cast_or_null<clang::TypedefNameDecl>(decl);
                if (typeAliasDecl != nullptr && typeAliasDecl->getNameAsString() == aliasName)
                {
                    return typeAliasDecl->getUnderlyingType();
                }
            }
            return {};
        }

        clang::QualType resolveClassTypeTemplateArgument(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                        const clang::QualType &type)
        {
            const clang::QualType strippedType = type.getNonReferenceType().getUnqualifiedType();
            const clang::Type *typePtr = strippedType.getTypePtrOrNull();
            if (const auto *templateType = llvm::dyn_cast_or_null<clang::TemplateTypeParmType>(typePtr))
            {
                const auto iter = typeArguments.find(templateType->getDecl()->getNameAsString());
                if (iter != typeArguments.end())
                {
                    return iter->second;
                }
            }
            if (const auto *substType = llvm::dyn_cast_or_null<clang::SubstTemplateTypeParmType>(typePtr))
            {
                return substType->getReplacementType();
            }
            if (const auto *dependentNameType = llvm::dyn_cast_or_null<clang::DependentNameType>(typePtr))
            {
                clang::QualType qualifierType;
                const clang::NestedNameSpecifier *qualifier = dependentNameType->getQualifier();
                if (qualifier != nullptr)
                {
                    if (qualifier->getKind() == clang::NestedNameSpecifier::Identifier && qualifier->getPrefix() == nullptr)
                    {
                        const auto qualifierIter = typeArguments.find(qualifier->getAsIdentifier()->getName().str());
                        if (qualifierIter != typeArguments.end())
                        {
                            qualifierType = qualifierIter->second;
                        }
                    }
                    else if (qualifier->getKind() == clang::NestedNameSpecifier::TypeSpec ||
                             qualifier->getKind() == clang::NestedNameSpecifier::TypeSpecWithTemplate)
                    {
                        qualifierType = clang::QualType(qualifier->getAsType(), 0);
                    }
                }
                qualifierType = qualifierType.isNull() ? qualifierType : resolveClassTypeTemplateArgument(typeArguments, qualifierType);
                const clang::CXXRecordDecl *qualifierRecord = qualifierType.isNull() ? nullptr : recordDefinition(qualifierType.getNonReferenceType()->getAsCXXRecordDecl());
                if (qualifierRecord != nullptr && dependentNameType->getIdentifier() != nullptr)
                {
                    const std::string aliasName = dependentNameType->getIdentifier()->getName().str();
                    for (const clang::Decl *decl : qualifierRecord->decls())
                    {
                        const auto *typeAliasDecl = llvm::dyn_cast_or_null<clang::TypedefNameDecl>(decl);
                        if (typeAliasDecl != nullptr && typeAliasDecl->getNameAsString() == aliasName)
                        {
                            return typeAliasDecl->getUnderlyingType();
                        }
                    }
                }
            }

            const auto iter = typeArguments.find(typeSpelling(type));
            if (iter != typeArguments.end())
            {
                return iter->second;
            }
            clang::QualType nestedAlias = resolveDependentTypeAliasSpelling(typeArguments, typeSpelling(type));
            return nestedAlias.isNull() ? type : nestedAlias;
        }

        std::string extractTextureFormatToken(const std::string &spelledType)
        {
            const std::string marker = "TextureFormat::";
            const size_t markerOffset = spelledType.find(marker);
            if (markerOffset == std::string::npos)
            {
                return {};
            }
            const size_t begin = markerOffset + marker.size();
            size_t end = begin;
            while (end < spelledType.size())
            {
                const char ch = spelledType[end];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_')
                {
                    break;
                }
                ++end;
            }
            return spelledType.substr(begin, end - begin);
        }

        ScalarKind scalarKindFromEncodedUGLTemplateToken(std::string token)
        {
            if (token == "UGL::half_t" || token == "UGL_half_t" || token == "half_t")
            {
                token = "half";
            }
            else if (token == "unsigned_int")
            {
                token = "uint";
            }
            std::replace(token.begin(), token.end(), '_', ' ');
            return scalarKindFromTypeName(token);
        }

        std::optional<EncodedUGLVectorSpecialization> parseEncodedUGLVectorSpecialization(const std::string &canonicalName)
        {
            std::string localName = canonicalName;
            if (startsWith(localName, "UGL::"))
            {
                localName = localName.substr(5);
            }
            constexpr std::string_view kPrefix = "Vector__T";
            if (!startsWith(localName, std::string(kPrefix)))
            {
                return std::nullopt;
            }
            const size_t widthMarker = localName.rfind("_I");
            if (widthMarker == std::string::npos || widthMarker + 2u >= localName.size())
            {
                return std::nullopt;
            }
            const std::string scalarToken = localName.substr(kPrefix.size(), widthMarker - kPrefix.size());
            const std::string widthToken = localName.substr(widthMarker + 2u);
            if (widthToken.size() != 1u || widthToken[0] < '2' || widthToken[0] > '4')
            {
                return std::nullopt;
            }
            EncodedUGLVectorSpecialization result;
            result.scalarKind = scalarKindFromEncodedUGLTemplateToken(scalarToken);
            result.width = static_cast<uint32_t>(widthToken[0] - '0');
            return result.scalarKind == ScalarKind::None ? std::nullopt : std::optional<EncodedUGLVectorSpecialization>(result);
        }

        std::optional<EncodedUGLMatrixSpecialization> parseEncodedUGLMatrixSpecialization(const std::string &canonicalName)
        {
            std::string localName = canonicalName;
            if (startsWith(localName, "UGL::"))
            {
                localName = localName.substr(5);
            }
            constexpr std::string_view kPrefix = "Matrix__T";
            if (!startsWith(localName, std::string(kPrefix)))
            {
                return std::nullopt;
            }
            const size_t columnMarker = localName.rfind("_I");
            if (columnMarker == std::string::npos || columnMarker + 2u >= localName.size())
            {
                return std::nullopt;
            }
            const size_t rowMarker = localName.rfind("_I", columnMarker - 1u);
            if (rowMarker == std::string::npos || rowMarker <= kPrefix.size())
            {
                return std::nullopt;
            }
            const std::string scalarToken = localName.substr(kPrefix.size(), rowMarker - kPrefix.size());
            const std::string rowToken = localName.substr(rowMarker + 2u, columnMarker - rowMarker - 2u);
            const std::string columnToken = localName.substr(columnMarker + 2u);
            if (rowToken.size() != 1u || columnToken.size() != 1u ||
                rowToken[0] < '2' || rowToken[0] > '4' ||
                columnToken[0] < '2' || columnToken[0] > '4')
            {
                return std::nullopt;
            }
            EncodedUGLMatrixSpecialization result;
            result.scalarKind = scalarKindFromEncodedUGLTemplateToken(scalarToken);
            result.rows = static_cast<uint32_t>(rowToken[0] - '0');
            result.columns = static_cast<uint32_t>(columnToken[0] - '0');
            return result.scalarKind == ScalarKind::None ? std::nullopt : std::optional<EncodedUGLMatrixSpecialization>(result);
        }

        std::string makeFramebufferTextureFormatValueTypeName(const std::string &textureFormat)
        {
            return framebufferTextureFormatValueTypeName(textureFormatFromToken(textureFormat));
        }

        bool isBindGroupHandleName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLBindGroupName;
        }

        bool isRenderSetHandleName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLRenderSetName;
        }

        bool isSupportedBufferResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderUniformBufferName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderStructuredBufferName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWStructuredBufferName;
        }

        bool isSampledTextureResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderTexture2DName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderTexture2DArrayName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderTexture3DName;
        }

        bool isStorageTextureResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderRWTexture2DName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWTexture2DArrayName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWTexture3DName;
        }

        bool isTextureAccessPackerName(const std::string &canonicalName)
        {
            return canonicalName.find("Texture2DAccessPacker") != std::string::npos ||
                   canonicalName.find("Texture2DArrayAccessPacker") != std::string::npos ||
                   canonicalName.find("Texture3DAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture2DAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture2DArrayAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture3DAccessPacker") != std::string::npos;
        }

        bool isUniformBufferDataPackerName(const std::string &canonicalName)
        {
            return canonicalName.find("UniformBufferDataPacker") != std::string::npos;
        }

        bool isBufferComponentDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("BufferComponentDataPack") != std::string::npos;
        }

        bool isTextureComponentDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("TextureComponentDataPack") != std::string::npos;
        }

        bool isDrawDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("RenderSetDataPack") != std::string::npos;
        }

        ErasedResourceCallKind classifyErasedBufferComponentCall(std::string_view methodName)
        {
            switch (UGLC::CodeGen::classifyRenderSetBufferComponentCall(methodName))
            {
            case UGLC::CodeGen::RenderSetBufferComponentCallKind::GetRaw:
                return ErasedResourceCallKind::BufferGetRaw;
            case UGLC::CodeGen::RenderSetBufferComponentCallKind::CheckValid:
                return ErasedResourceCallKind::BufferCheckValid;
            case UGLC::CodeGen::RenderSetBufferComponentCallKind::Get:
                return ErasedResourceCallKind::BufferGet;
            case UGLC::CodeGen::RenderSetBufferComponentCallKind::Unknown:
                return ErasedResourceCallKind::None;
            }
            return ErasedResourceCallKind::None;
        }

        ErasedResourceCallKind classifyErasedTextureComponentCall(std::string_view methodName)
        {
            switch (UGLC::CodeGen::classifyRenderSetTextureComponentCall(methodName))
            {
            case UGLC::CodeGen::RenderSetTextureComponentCallKind::Get:
                return ErasedResourceCallKind::TextureSelect;
            case UGLC::CodeGen::RenderSetTextureComponentCallKind::Unknown:
                return ErasedResourceCallKind::None;
            }
            return ErasedResourceCallKind::None;
        }

        ErasedResourceCallKind classifyErasedDrawDataCall(std::string_view methodName)
        {
            switch (UGLC::CodeGen::classifyRenderSetDataPackCall(methodName))
            {
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityInfo:
                return ErasedResourceCallKind::DrawInfoStoreFields;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityCMDParams:
                return ErasedResourceCallKind::DrawCommandParamsStoreFields;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityIndexCount:
                return ErasedResourceCallKind::DrawInfoIndexCount;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityInstanceCount:
                return ErasedResourceCallKind::DrawInfoInstanceCount;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityFirstIndex:
                return ErasedResourceCallKind::DrawInfoFirstIndex;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityVertexOffset:
                return ErasedResourceCallKind::DrawInfoVertexOffset;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityGlobalInstanceBase:
                return ErasedResourceCallKind::DrawInfoGlobalInstanceBase;
            case UGLC::CodeGen::RenderSetDataPackCallKind::GetRenderEntityVersion:
                return ErasedResourceCallKind::DrawInfoVersion;
            case UGLC::CodeGen::RenderSetDataPackCallKind::CheckValid:
                return ErasedResourceCallKind::DrawInfoCheckValid;
            case UGLC::CodeGen::RenderSetDataPackCallKind::Unknown:
                return ErasedResourceCallKind::None;
            }
            return ErasedResourceCallKind::None;
        }

        bool isErasedDrawDataStoreCall(ErasedResourceCallKind callKind)
        {
            return callKind == ErasedResourceCallKind::DrawInfoStoreFields ||
                   callKind == ErasedResourceCallKind::DrawCommandParamsStoreFields;
        }

        bool isSamplerResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderSamplerName;
        }

        bool isGroupSharedName(const std::string &canonicalName)
        {
            return canonicalName == "UGL::GroupShared";
        }

        bool isFramebufferAttachmentName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLColorAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLDepthAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLPixelLocalColorAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName;
        }

        TypeRole typeRoleFromSourceTypeName(const std::string &canonicalName, const std::string &spelledType, const std::string &recordSymbolName)
        {
            const std::string &name = recordSymbolName.empty() ? spelledType : recordSymbolName;
            if (canonicalName == "UGL::detail::Swizzle" ||
                name.find("::detail::Swizzle__") != std::string::npos ||
                spelledType.find("UGL::detail::Swizzle<") != std::string::npos)
            {
                return TypeRole::SwizzleProxy;
            }
            if (canonicalName == "UGL::Color" ||
                canonicalName == "UGL::LoadOp" ||
                canonicalName == "UGL::StoreOp" ||
                startsWith(canonicalName, "UGL::TextureFormat::") ||
                startsWith(spelledType, "UGL::TextureFormat::") ||
                startsWith(name, "ColorAttachment__") ||
                startsWith(name, "DepthStencilAttachment__") ||
                startsWith(name, "PixelLocalColorAttachment__") ||
                startsWith(name, "PixelLocalDepthAttachment__") ||
                startsWith(name, "UGL::ColorAttachment__") ||
                startsWith(name, "UGL::DepthStencilAttachment__") ||
                startsWith(name, "UGL::PixelLocalColorAttachment__") ||
                startsWith(name, "UGL::PixelLocalDepthAttachment__") ||
                startsWith(name, "BufferComponent__") ||
                startsWith(name, "TextureComponent__") ||
                startsWith(name, "UGL__BufferComponent__") ||
                startsWith(name, "UGL__TextureComponent__") ||
                name.find("::(anonymous union") != std::string::npos ||
                name.find(" *") != std::string::npos)
            {
                return TypeRole::ImplementationOnly;
            }
            return TypeRole::Value;
        }

        std::string framebufferAttachmentNameFromDependentSpelling(const std::string &typeName)
        {
            if (startsWith(typeName, "UGL::ColorAttachment<") || startsWith(typeName, "ColorAttachment<"))
            {
                return UGLC::CodeGen::mUGLColorAttachmentName;
            }
            if (startsWith(typeName, "UGL::DepthStencilAttachment<") || startsWith(typeName, "DepthStencilAttachment<"))
            {
                return UGLC::CodeGen::mUGLDepthAttachmentName;
            }
            if (startsWith(typeName, "UGL::PixelLocalColorAttachment<") || startsWith(typeName, "PixelLocalColorAttachment<"))
            {
                return UGLC::CodeGen::mUGLPixelLocalColorAttachmentName;
            }
            if (startsWith(typeName, "UGL::PixelLocalDepthAttachment<") || startsWith(typeName, "PixelLocalDepthAttachment<"))
            {
                return UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName;
            }
            return {};
        }

        TextureDimension textureDimensionForResourceName(const std::string &canonicalName)
        {
            if (canonicalName == UGLC::CodeGen::mUGLShaderTexture2DArrayName || canonicalName == UGLC::CodeGen::mUGLShaderRWTexture2DArrayName)
            {
                return TextureDimension::Texture2DArray;
            }
            if (canonicalName == UGLC::CodeGen::mUGLShaderTexture3DName || canonicalName == UGLC::CodeGen::mUGLShaderRWTexture3DName)
            {
                return TextureDimension::Texture3D;
            }
            return TextureDimension::Texture2D;
        }

        bool isIgnoredLocation(const clang::SourceManager &sourceManager, clang::SourceLocation location)
        {
            location = UGLC::CodeGen::normalizeDiagnosticLocation(sourceManager, location);
            if (location.isInvalid())
            {
                return true;
            }
            if (sourceManager.isInSystemHeader(location) || sourceManager.isInExternCSystemHeader(location))
            {
                return true;
            }

            const clang::PresumedLoc presumedLoc = sourceManager.getPresumedLoc(location);
            if (presumedLoc.isInvalid())
            {
                return true;
            }

            const std::string filePath = presumedLoc.getFilename();
            return containsPathFragment(filePath, "/GVM/UGLHeaders/") || containsPathFragment(filePath, "\\GVM\\UGLHeaders\\") || containsPathFragment(filePath, "/UGLHeaders/") || containsPathFragment(filePath, "\\UGLHeaders\\");
        }
        clang::QualType getTemplateTypeArgument(const clang::QualType &type, unsigned index)
        {
            const clang::CXXRecordDecl *recordDecl = type.getNonReferenceType()->getAsCXXRecordDecl();
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDefinition(recordDecl));
            }
            if (specializationDecl == nullptr || specializationDecl->getTemplateArgs().size() <= index)
            {
                return {};
            }
            const clang::TemplateArgument &argument = specializationDecl->getTemplateArgs()[index];
            if (argument.getKind() != clang::TemplateArgument::Type)
            {
                return {};
            }
            return argument.getAsType();
        }

        std::optional<uint32_t> getTemplateUnsignedIntegerArgument(const clang::QualType &type, unsigned index)
        {
            const clang::CXXRecordDecl *recordDecl = type.getNonReferenceType()->getAsCXXRecordDecl();
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDefinition(recordDecl));
            }
            if (specializationDecl == nullptr || specializationDecl->getTemplateArgs().size() <= index)
            {
                return std::nullopt;
            }
            const clang::TemplateArgument &argument = specializationDecl->getTemplateArgs()[index];
            if (argument.getKind() != clang::TemplateArgument::Integral)
            {
                return std::nullopt;
            }
            const llvm::APSInt value = argument.getAsIntegral();
            if (value.isNegative())
            {
                return std::nullopt;
            }
            return static_cast<uint32_t>(value.getZExtValue());
        }

        std::string canonicalRecordName(const clang::QualType &type)
        {
            clang::QualType recordType = type.getNonReferenceType();
            if (recordType->isPointerType())
            {
                recordType = recordType->getPointeeType().getNonReferenceType();
            }
            const clang::CXXRecordDecl *recordDecl = recordType->getAsCXXRecordDecl();
            recordDecl = recordDefinition(recordDecl);
            if (recordDecl == nullptr)
            {
                return {};
            }
            return normalizeStdName(qualifiedName(recordDecl));
        }
        bool isUserAuthoredDecl(const clang::SourceManager &sourceManager, const clang::Decl *decl)
        {
            return decl != nullptr && !decl->isImplicit() && !isIgnoredLocation(sourceManager, decl->getLocation());
        }

        bool isUserAuthoredRecordDecl(const clang::SourceManager &sourceManager, const clang::CXXRecordDecl *recordDecl)
        {
            if (isUserAuthoredDecl(sourceManager, recordDecl))
            {
                return true;
            }
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                return false;
            }
            const clang::ClassTemplateDecl *templateDecl = specializationDecl->getSpecializedTemplate();
            return templateDecl != nullptr && isUserAuthoredDecl(sourceManager, templateDecl->getTemplatedDecl());
        }

        bool isUserAuthoredFunction(const clang::SourceManager &sourceManager, const clang::FunctionDecl *functionDecl)
        {
            return functionDecl != nullptr && isUserAuthoredDecl(sourceManager, functionDecl);
        }

        bool isDerivedFromUGLBase(const clang::SourceManager &sourceManager, const clang::CXXRecordDecl *decl, const std::string &baseName)
        {
            const clang::CXXRecordDecl *definition = decl == nullptr ? nullptr : decl->getDefinition();
            if (definition == nullptr)
            {
                return false;
            }
            for (const clang::CXXBaseSpecifier &base : definition->bases())
            {
                const clang::CXXRecordDecl *baseDecl = base.getType()->getAsCXXRecordDecl();
                if (baseDecl == nullptr)
                {
                    continue;
                }
                if (qualifiedName(baseDecl) == baseName || isDerivedFromUGLBase(sourceManager, baseDecl, baseName))
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace TypeSpelling
} // namespace UGLC::CodeGen::UGLIR
