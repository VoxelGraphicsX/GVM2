#include "UGLIRLowering.hpp"

#include <CodeGen/Diagnostics.hpp>
#include <CodeGen/ShaderBackendCapabilities.hpp>
#include <CodeGen/UGLIR/UGLIRNameUtils.hpp>
#include <CodeGen/UGLIR/UGLIRSourceVerifier.hpp>
#include <CodeGen/UGLIR/UGLIRTypeUtils.hpp>
#include <CodeGen/IntegerConstantExpressionUtils.hpp>
#include <CodeGen/PixelLocalFieldAnalysis.hpp>
#include <CodeGen/RenderSetMemberCallUtils.hpp>
#include <CodeGen/TextureMemberCallUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

#include <clang/AST/Attr.h>
#include <clang/AST/DeclCXX.h>
#include <clang/AST/DeclTemplate.h>
#include <clang/AST/ExprCXX.h>
#include <clang/AST/LambdaCapture.h>
#include <clang/AST/OperationKinds.h>
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Stmt.h>
#include <clang/AST/StmtCXX.h>
#include <clang/AST/Type.h>
#include <clang/Basic/OperatorKinds.h>
#include <clang/Basic/SourceManager.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace UGLC::CodeGen::UGLIR
{
    namespace
    {
        /** Stores one reflected bind-group slot on a compute shader class. */
        struct BindGroupSlot
        {
            const clang::FieldDecl *fieldDecl = nullptr;
            const clang::CXXRecordDecl *bindGroupDecl = nullptr;
            std::string fieldName;
            uint32_t slotIndex = 0;
            bool isRenderSet = false;
        };

        /** Stores one Phase-3 resource binding resolved from a bind-group field. */
        struct LoweredResourceBinding
        {
            const clang::FieldDecl *fieldDecl = nullptr;
            std::string name;
            ResourceKind kind = ResourceKind::Unknown;
            uint32_t bindGroupIndex = 0;
            uint32_t bindingIndex = 0;
            AccessMode accessMode = AccessMode::Read;
            clang::QualType elementType;
            std::string elementTypeName;
            uint32_t arrayCount = 1;
            TextureDimension textureDimension = TextureDimension::None;
            TextureFormat textureFormat = TextureFormat::Unknown;
            ResourceRole resourceRole = ResourceRole::None;
            uint32_t resourceIndex = 0;
            SourceLocation sourceLocation;
        };

        /** Stores one lambda capture after it has been converted into an explicit helper parameter. */
        struct LambdaCaptureLowering
        {
            const clang::ValueDecl *capturedDecl = nullptr;
            const clang::FieldDecl *closureField = nullptr;
            std::string parameterName;
            std::string typeName;
            Expression operand;
            SourceLocation sourceLocation;
        };

        /** Stores the lowered helper symbol and explicit capture operands for one local lambda. */
        struct LambdaLocalLowering
        {
            const clang::LambdaExpr *lambdaExpr = nullptr;
            std::string functionName;
            std::vector<LambdaCaptureLowering> captures;
        };

        /** Stores the function body lowering state shared while one module is active. */
        struct FunctionLoweringState
        {
            Module *module = nullptr;
            const clang::FunctionDecl *functionDecl = nullptr;
            std::string ownerFunctionSymbol;
            std::string constructorResultName;
            uint32_t nextLambdaOrdinal = 0;
            uint32_t nextInitializerOrdinal = 0;
            std::unordered_map<const clang::ValueDecl *, std::string> localNames;
            std::unordered_map<std::string, std::string> localTypeNamesByName;
            std::unordered_set<std::string> allocatedLocalNames;
            std::unordered_map<std::string, std::string> integralTemplateArgumentsByName;
            std::unordered_map<std::string, std::string> typeTemplateArgumentsByName;
            std::unordered_map<std::string, clang::QualType> typeTemplateQualTypesByName;
            std::unordered_map<const clang::ValueDecl *, LambdaLocalLowering> lambdaLocals;
            std::unordered_map<const clang::FieldDecl *, std::string> lambdaCaptureFieldNames;
            std::unordered_map<const clang::ValueDecl *, Expression> valueSubstitutions;
            std::unordered_map<std::string, Expression> thisFieldSubstitutions;
            std::unordered_map<std::string, std::unordered_map<std::string, Expression>> objectResourceFieldSubstitutions;
            std::vector<LambdaLocalLowering> immediateLambdas;

            /** Allocates a deterministic function-local symbol without colliding with existing symbols. */
            std::string allocateLocalName(const std::string &requestedName)
            {
                const std::string baseName = requestedName.empty() ? "__UGL__local" : requestedName;
                std::string candidate = baseName;
                uint32_t suffix = 1;
                while (!allocatedLocalNames.insert(candidate).second)
                {
                    candidate = baseName + "__" + std::to_string(suffix++);
                }
                return candidate;
            }
        };

        /** Returns true when the current UGLIR entry can legally perform fragment-kill operations. */
        bool isFragmentKillCapableEntryKind(ShaderEntryKind entryKind)
        {
            return entryKind == ShaderEntryKind::Fragment || entryKind == ShaderEntryKind::PixelLocal;
        }

        /** Returns a diagnostic-facing description for a UGLIR shader entry kind. */
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

        /** Returns the source spelling used in diagnostics for fragment-kill intrinsics. */
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

        /** Returns the stable builtin stem encoded in a lowered UGL call name. */
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

        /** Converts a public texture DSL method name into a structured UGLIR intrinsic tag. */
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

        /** Returns the structured intrinsic tag for a lowered call name when one is known. */
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

        /** Returns an initializer expression with syntactic wrappers removed for framebuffer default-initializer checks. */
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

        /** Returns true when an initializer only contains compiler-expanded default construction for a framebuffer record. */
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

        /** Returns the record definition when Clang has one available. */
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

        /** Removes statement wrappers with no execution scope while preserving compound statement boundaries. */
        const clang::Stmt *stripNonSemanticStatementWrappers(const clang::Stmt *stmt)
        {
            const clang::Stmt *current = stmt;
            while (const auto *attributedStmt = llvm::dyn_cast_or_null<clang::AttributedStmt>(current))
            {
                current = attributedStmt->getSubStmt();
            }
            return current;
        }

        /** Returns concrete record fields or the template pattern fields for implicit specializations without instantiated fields. */
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

        /** Returns true when a record instance carries no shader-visible state and can be omitted from helper calls. */
        bool isStatelessRecordType(clang::QualType type)
        {
            if (type.isNull())
            {
                return false;
            }
            const clang::CXXRecordDecl *recordDecl = recordDefinition(type.getNonReferenceType()->getAsCXXRecordDecl());
            return recordDecl != nullptr && collectRecordFields(recordDecl).empty();
        }

        /** Returns true when a string starts with the requested prefix. */
        bool startsWith(const std::string &value, const std::string &prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }

        /** Returns true when a string contains the requested path fragment. */
        bool containsPathFragment(const std::string &value, const std::string &fragment)
        {
            return value.find(fragment) != std::string::npos;
        }

        /** Returns a copy of the string with leading and trailing whitespace removed. */
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

        /** Returns true when an annotation spells the requested function-style attribute. */
        bool isFunctionStyleAttributeName(const std::string &rawAttribute, const std::string &attributeName)
        {
            if (rawAttribute == attributeName)
            {
                return true;
            }
            return rawAttribute.size() > attributeName.size() && startsWith(rawAttribute, attributeName) && rawAttribute[attributeName.size()] == '(';
        }

        /** Splits a function-style annotation argument list into trimmed parameter strings. */
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

        /** Returns all clang annotate strings attached to a declaration. */
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

        /** Returns true when a raw annotation is exactly an indexed attribute such as Slot0. */
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

        /** Returns the integer suffix from an exact indexed attribute or -1 when it is not present. */
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

        /** Returns a compact type spelling by stripping common C++ elaboration prefixes. */
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

        /** Returns a stable type spelling suitable for UGLIR names and diagnostics. */
        std::string typeSpelling(const clang::QualType &type)
        {
            clang::LangOptions langOptions;
            clang::PrintingPolicy policy(langOptions);
            policy.SuppressTagKeyword = true;
            policy.SuppressScope = false;
            policy.FullyQualifiedName = false;
            return stripElaboratedPrefix(type.getUnqualifiedType().getAsString(policy));
        }

        /** Returns a stable qualified name for a named declaration. */
        std::string qualifiedName(const clang::NamedDecl *decl)
        {
            if (decl == nullptr)
            {
                return {};
            }
            return decl->getQualifiedNameAsString();
        }

        /** Normalizes libc++ inline namespace spellings into stable std:: names. */
        std::string normalizeStdName(std::string name)
        {
            const std::string libcxxPrefix = "std::__1::";
            if (startsWith(name, libcxxPrefix))
            {
                return "std::" + name.substr(libcxxPrefix.size());
            }
            return name;
        }

        /** Returns true when a type spelling names a supported UGL vector alias. */
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

        /** Stores the scalar type and shape decoded from a public UGL matrix alias. */
        struct ParsedMatrixAlias
        {
            ScalarKind scalarKind = ScalarKind::None;
            std::string elementType;
            uint32_t rows = 0;
            uint32_t columns = 0;
            std::string canonicalName;
        };

        /** Returns true when a type spelling names a supported compact UGL matrix alias. */
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

        /** Splits a simple template argument list while preserving nested template arguments. */
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

        /** Returns the scalar category represented by a public UGL scalar template argument spelling. */
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

        /** Returns the UGLIR scalar element spelling for a parsed compact shader scalar kind. */
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

        /** Returns the compact public scalar alias spelling for a parsed compact shader scalar kind. */
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

        /** Returns true when a type spelling names a supported UGL Matrix<T, R, C> specialization. */
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

        /** Returns the UGLIR type record with the requested name when it is already present in a module. */
        const Type *findLoweredTypeByName(const Module &module, const std::string &typeName)
        {
            const auto iter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
                return type.name == typeName;
            });
            return iter == module.types.end() ? nullptr : &*iter;
        }

        /** Returns true when a lowered expression type is the UGL workgroup-storage wrapper. */
        bool isWorkgroupWrapperTypeName(const Module &module, const std::string &typeName)
        {
            if (const Type *type = findLoweredTypeByName(module, typeName))
            {
                return type->kind == TypeKind::Workgroup;
            }
            return typeName.find("GroupShared<") != std::string::npos;
        }

        /** Returns the bit width used for one scalar value category in UGLIR type metadata. */
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

        /** Returns the scalar UGLIR element type name used inside vector and matrix type records. */
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

        /** Describes a scalar or vector value type as a scalar component type and component count. */
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

        /** Returns true when a member name is a scalar/vector component swizzle. */
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

        /** Creates a UGLIR unsigned integer literal expression. */
        Expression makeUIntLiteralExpression(uint32_t value, SourceLocation location)
        {
            Expression result;
            result.kind = ExpressionKind::Literal;
            result.type = "u32";
            result.value = std::to_string(value);
            result.sourceLocation = std::move(location);
            return result;
        }

        /** Creates a reflected resource declaration reference expression. */
        Expression makeResourceDeclRefExpression(const ResourceBinding &resource)
        {
            Expression result;
            result.kind = ExpressionKind::DeclRef;
            result.type = "resource_selector";
            result.name = resource.name;
            result.sourceLocation = resource.sourceLocation;
            return result;
        }

        /** Creates a UGLIR member reference expression. */
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

        /** Creates a UGLIR subscript expression. */
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

        /** Creates a UGLIR binary expression. */
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

        /** Creates a UGLIR conditional expression. */
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

        /** Creates a UGLIR store expression. */
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

        /** Creates a UGLIR call expression for a generic math helper. */
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

        /** Creates a UGLIR expression that clamps an unsigned index to one less than an exclusive bound. */
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

        /** Creates a UGLIR expression that combines two boolean expressions with logical and. */
        Expression makeBoolAndExpression(Expression lhs, Expression rhs, SourceLocation location)
        {
            return makeBinaryExpression("&&", std::move(lhs), std::move(rhs), "bool", std::move(location));
        }

        /** Creates a UGLIR expression statement that evaluates one expression. */
        Statement makeExpressionStatement(Expression expression)
        {
            Statement statement;
            statement.kind = StatementKind::Expression;
            statement.sourceLocation = expression.sourceLocation;
            statement.expressions.push_back(std::move(expression));
            return statement;
        }

        /** Creates an explicit scalar conversion expression used by lowered internal index arithmetic. */
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

        /** Creates an explicit unsigned conversion expression for internal index arithmetic. */
        Expression makeUIntExpression(Expression value, SourceLocation location)
        {
            return makeScalarConvertExpression(std::move(value), "u32", ScalarKind::UInt, std::move(location));
        }

        /** Finds one reflected resource by its UGLIR resource name. */
        const ResourceBinding *findResourceByName(const Module &module, const std::string &name)
        {
            const auto iter = std::find_if(module.reflection.resources.begin(), module.reflection.resources.end(), [&name](const ResourceBinding &resource) {
                return resource.name == name;
            });
            return iter == module.reflection.resources.end() ? nullptr : &*iter;
        }

        /** Finds one reflected resource by bind-group index, role, and optional component index. */
        const ResourceBinding *findResourceByRole(const Module &module,
                                                  uint32_t bindGroupIndex,
                                                  ResourceRole role,
                                                  std::optional<uint32_t> resourceIndex = std::nullopt)
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

        /** Returns the first payload operand after an optional resource selector operand. */
        size_t erasedResourcePayloadOffset(const Expression &expression)
        {
            return !expression.operands.empty() && expression.operands.front().type == "resource_selector" ? 1u : 0u;
        }

        /** Returns the reflected resource named by an erased resource selector expression. */
        const ResourceBinding *findSelectedResource(const Module &module, const Expression &expression)
        {
            if (expression.operands.empty() || expression.operands.front().type != "resource_selector")
            {
                return nullptr;
            }
            return findResourceByName(module, expression.operands.front().name);
        }

        /** Finds the bind group referenced by an erased resource selector path. */
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

        /** Returns the requested template argument spelling from a simple template-id string. */
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

        /** Collects concrete type template arguments from a class template specialization. */
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

        /** Resolves a dependent nested type alias spelling such as typename Policy::TexCoordType. */
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

        /** Resolves a direct template type parameter spelling to the concrete type used by a shader specialization. */
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

        /** Extracts a UGL texture-format token from a canonical type spelling. */
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

        /** Stores the scalar type and lane count decoded from a UGL Vector specialization name. */
        struct EncodedUGLVectorSpecialization
        {
            ScalarKind scalarKind = ScalarKind::None;
            uint32_t width = 0;
        };

        /** Stores the scalar type and shape decoded from a UGL Matrix specialization name. */
        struct EncodedUGLMatrixSpecialization
        {
            ScalarKind scalarKind = ScalarKind::None;
            uint32_t rows = 0;
            uint32_t columns = 0;
        };

        /** Returns a scalar category from the scalar token embedded in a lowered UGL template specialization name. */
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

        /** Decodes a UGL Vector specialization name produced by the local stable-name utilities. */
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

        /** Decodes a UGL Matrix specialization name produced by the local stable-name utilities. */
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

        /** Maps a framebuffer attachment format token to the DSL payload type carried by UGL::TextureFormat. */
        std::string makeFramebufferTextureFormatValueTypeName(const std::string &textureFormat)
        {
            return framebufferTextureFormatValueTypeName(textureFormatFromToken(textureFormat));
        }

        /** Returns true when a canonical type name is a Phase-3 bind-group handle. */
        bool isBindGroupHandleName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLBindGroupName;
        }

        /** Returns true when a canonical type name is a render-set handle. */
        bool isRenderSetHandleName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLRenderSetName;
        }

        /** Returns true when a canonical type name is a Phase-3 supported buffer wrapper. */
        bool isSupportedBufferResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderUniformBufferName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderStructuredBufferName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWStructuredBufferName;
        }

        /** Returns true when a canonical type name is a sampled texture wrapper supported by Phase 8. */
        bool isSampledTextureResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderTexture2DName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderTexture2DArrayName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderTexture3DName;
        }

        /** Returns true when a canonical type name is a storage texture wrapper supported by Phase 8. */
        bool isStorageTextureResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderRWTexture2DName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWTexture2DArrayName ||
                   canonicalName == UGLC::CodeGen::mUGLShaderRWTexture3DName;
        }

        /** Returns true when a canonical type name is one of the UGL private texture access packers. */
        bool isTextureAccessPackerName(const std::string &canonicalName)
        {
            return canonicalName.find("Texture2DAccessPacker") != std::string::npos ||
                   canonicalName.find("Texture2DArrayAccessPacker") != std::string::npos ||
                   canonicalName.find("Texture3DAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture2DAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture2DArrayAccessPacker") != std::string::npos ||
                   canonicalName.find("RWTexture3DAccessPacker") != std::string::npos;
        }

        /** Returns true when a canonical type name is one of the UGL private uniform-buffer data packers. */
        bool isUniformBufferDataPackerName(const std::string &canonicalName)
        {
            return canonicalName.find("UniformBufferDataPacker") != std::string::npos;
        }

        /** Returns true when a canonical type name is a DSL buffer-component data pack handled by AST erasure. */
        bool isBufferComponentDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("BufferComponentDataPack") != std::string::npos;
        }

        /** Returns true when a canonical type name is a DSL texture-component data pack handled by AST erasure. */
        bool isTextureComponentDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("TextureComponentDataPack") != std::string::npos;
        }

        /** Returns true when a canonical type name is a DSL draw-data pack handled by AST erasure. */
        bool isDrawDataPackName(const std::string &canonicalName)
        {
            return canonicalName.find("RenderSetDataPack") != std::string::npos;
        }

        /** Identifies a RenderSet data-pack operation while lowering DSL calls into ordinary UGLIR. */
        enum class ErasedResourceCallKind
        {
            None,
            BufferGetRaw,
            BufferCheckValid,
            BufferGet,
            TextureSelect,
            DrawInfoStoreFields,
            DrawCommandParamsStoreFields,
            DrawInfoIndexCount,
            DrawInfoInstanceCount,
            DrawInfoFirstIndex,
            DrawInfoVertexOffset,
            DrawInfoGlobalInstanceBase,
            DrawInfoVersion,
            DrawInfoCheckValid
        };

        /** Classifies a buffer-component data-pack method before it is erased into ordinary UGLIR. */
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

        /** Classifies a texture-component data-pack method before it is erased into ordinary UGLIR. */
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

        /** Classifies a draw-data method before it is erased into ordinary UGLIR. */
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

        /** Returns true when a RenderSet-erased call writes values through out parameters. */
        bool isErasedDrawDataStoreCall(ErasedResourceCallKind callKind)
        {
            return callKind == ErasedResourceCallKind::DrawInfoStoreFields ||
                   callKind == ErasedResourceCallKind::DrawCommandParamsStoreFields;
        }

        /** Returns true when a canonical type name is a sampler resource wrapper supported by Phase 8. */
        bool isSamplerResourceName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLShaderSamplerName;
        }

        /** Returns true when a canonical type name is the UGL workgroup-storage wrapper. */
        bool isGroupSharedName(const std::string &canonicalName)
        {
            return canonicalName == "UGL::GroupShared";
        }

        /** Returns true when a canonical type name is a framebuffer attachment wrapper. */
        bool isFramebufferAttachmentName(const std::string &canonicalName)
        {
            return canonicalName == UGLC::CodeGen::mUGLColorAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLDepthAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLPixelLocalColorAttachmentName ||
                   canonicalName == UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName;
        }

        /** Classifies source-level UGL helper types that should not be emitted as shader payload ABI. */
        TypeRole typeRoleFromSourceTypeName(const std::string &canonicalName, const std::string &spelledType, const std::string &recordSymbolName = {})
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

        /** Resolves a framebuffer attachment wrapper name from a dependent template-id spelling. */
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

        /** Returns a resource texture dimension token for a canonical UGL texture wrapper name. */
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

        /** Returns true when a source location belongs to system or UGL implementation headers. */
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

        /** Lowers one translation unit into Phase-3 UGLIR modules. */
        class UGLIRLowerer : public clang::RecursiveASTVisitor<UGLIRLowerer>
        {
        public:
            /** Creates a lowerer bound to one Clang AST context. */
            explicit UGLIRLowerer(clang::ASTContext &context);

            /** Runs shader root discovery and lowers all Phase-3 supported compute roots. */
            UGLIRLoweringResult run(llvm::ArrayRef<ShaderClassRoot> roots);

            /** Discovers concrete roots without lowering or modifying the AST. */
            std::vector<ShaderClassRoot> collectRoots();

            /** Collects shader class roots while walking the translation unit. */
            bool VisitCXXRecordDecl(const clang::CXXRecordDecl *decl);

            /** Collects shader specializations that are only reachable through host wrapper fields. */
            bool VisitFieldDecl(const clang::FieldDecl *decl);

            /** Collects shader specializations that are only reachable through host wrapper variables. */
            bool VisitVarDecl(const clang::VarDecl *decl);

            /** Collects shader wrapper specializations returned by device factory calls. */
            bool VisitCallExpr(const clang::CallExpr *expression);

        private:
            /** Reports a Phase-3 lowering diagnostic at the requested source location. */
            void addDiagnostic(clang::SourceLocation location, const std::string &message);

            /** Reports a lowering diagnostic at a source location already stored in UGLIR. */
            void addDiagnostic(const SourceLocation &location, const std::string &message);

            /** Returns a compact source location for IR dumps. */
            SourceLocation makeSourceLocation(clang::SourceLocation location) const;

            /** Returns true when a declaration belongs to shader-authored source. */
            bool isUserAuthoredDecl(const clang::Decl *decl) const;

            /** Returns true when a record or its instantiated template pattern belongs to shader-authored source. */
            bool isUserAuthoredRecordDecl(const clang::CXXRecordDecl *recordDecl) const;

            /** Returns true when a function definition should be lowered as user shader code. */
            bool isUserAuthoredFunction(const clang::FunctionDecl *functionDecl) const;

            /** Returns true when a record is a dependent template pattern that must not be lowered directly. */
            bool isUninstantiatedTemplatePattern(const clang::CXXRecordDecl *recordDecl) const;

            /** Returns true when a record derives from the requested UGL base class name. */
            bool isDerivedFromUGLBase(const clang::CXXRecordDecl *decl, const std::string &baseName) const;

            /** Classifies a record as a shader class root when it derives from a UGL shader base. */
            bool tryClassifyShaderClass(const clang::CXXRecordDecl *decl, ShaderClassKind &kind) const;

            /** Adds a shader root once using the stable UGLIR record symbol name. */
            void addShaderRoot(const clang::CXXRecordDecl *recordDecl, ShaderClassKind kind);

            /** Collects a shader specialization from a UGL host wrapper type. */
            void collectShaderWrapperType(const clang::QualType &type);

            /** Resolves a shader class resource ABI once for all of its stage modules. */
            void collectClassResourceInterface(const clang::CXXRecordDecl &record, ShaderClassKind kind);

            /** Lowers one shader root or emits a Phase-3 unsupported-stage diagnostic. */
            void lowerShaderRoot(const ShaderClassRoot &root);

            /** Lowers one compute shader class into a complete UGLIR module. */
            std::optional<Module> lowerComputeRoot(const clang::CXXRecordDecl *recordDecl);

            /** Lowers one render shader class into vertex and fragment UGLIR modules. */
            void lowerRenderRoot(const clang::CXXRecordDecl *recordDecl);

            /** Lowers one pixel-local shader class into a fragment-stage UGLIR module. */
            void lowerPixelLocalRoot(const clang::CXXRecordDecl *recordDecl);

            /** Lowers one shader entry into a complete UGLIR module. */
            std::optional<Module> lowerEntryModule(const clang::CXXRecordDecl *recordDecl,
                                                   const clang::CXXMethodDecl *entryMethod,
                                                   ShaderStage stage,
                                                   ShaderEntryKind entryKind,
                                                   const std::array<uint32_t, 3> &workgroupSize);

            /** Finds the required compute entry method on a compute shader class. */
            const clang::CXXMethodDecl *findComputeEntry(const clang::CXXRecordDecl *recordDecl) const;

            /** Finds an optional shader entry method by its DSL spelling. */
            const clang::CXXMethodDecl *findEntryMethod(const clang::CXXRecordDecl *recordDecl, const std::string &entryName) const;

            /** Finds one DSL create method on a record when it exists. */
            const clang::CXXMethodDecl *findCreateMethod(const clang::CXXRecordDecl *recordDecl) const;

            /** Reads and validates the compute workgroup size annotation. */
            std::optional<std::array<uint32_t, 3>> readWorkgroupSize(const clang::CXXRecordDecl *recordDecl);

            /** Resolves all bind-group slots attached to a compute shader class. */
            std::vector<BindGroupSlot> collectBindGroupSlots(const clang::CXXRecordDecl *recordDecl);

            /** Resolves all Phase-3 supported resource bindings from one bind-group slot. */
            std::vector<LoweredResourceBinding> collectResourceBindings(const BindGroupSlot &slot, Module &module);

            /** Appends the value types required by the legacy-compatible RenderSet shader ABI. */
            void ensureRenderSetAuxiliaryTypes(Module &module, clang::SourceLocation location);

            /** Appends stage input and output reflection for one entry function. */
            void collectStageInterface(Module &module, const clang::FunctionDecl *entryMethod, ShaderEntryKind entryKind, const clang::CXXRecordDecl *shaderRecordDecl);

            /** Returns the shader-stage semantic assigned to one entry parameter by the public UGL authoring surface. */
            BuiltinSemantic resolveEntryParameterSemantic(const clang::ParmVarDecl *paramDecl, ShaderEntryKind entryKind) const;

            /** Appends Vulkan input-attachment resource reflection for pixel-local fields that are read by the entry. */
            void collectPixelLocalInputAttachmentResources(Module &module,
                                                           const clang::FunctionDecl *entryMethod,
                                                           const clang::ParmVarDecl *paramDecl,
                                                           const clang::CXXRecordDecl *recordDecl);

            /** Appends field-level stage IO reflection for one record type, optionally limiting framebuffer outputs to active fields. */
            void collectRecordStageIO(Module &module,
                                      const clang::CXXRecordDecl *recordDecl,
                                      std::vector<StageIOBinding> &bindings,
                                      bool outputs,
                                      const std::unordered_set<std::string> *activeOutputFields = nullptr);

            /** Emits diagnostics when a pixel-local entry writes depth attachments that cannot be represented by the supported ABI. */
            void diagnosePixelLocalDepthWrites(const clang::FunctionDecl *entryMethod,
                                               const clang::CXXRecordDecl *returnRecordDecl,
                                               const UGLC::CodeGen::PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis);

            /** Resolves a template argument from a possibly dependent template-id type. */
            clang::QualType resolveTemplateTypeArgument(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                        const clang::QualType &type,
                                                        unsigned index) const;

            /** Extracts a concrete type template argument from a class template specialization type. */
            clang::QualType getTemplateTypeArgument(const clang::QualType &type, unsigned index) const;

            /** Extracts an unsigned integer template argument from a class template specialization type. */
            std::optional<uint32_t> getTemplateUnsignedIntegerArgument(const clang::QualType &type, unsigned index) const;

            /** Returns the canonical declaration name for a type. */
            std::string canonicalRecordName(const clang::QualType &type) const;

            /** Ensures a type exists in the current module and returns its UGLIR type name. */
            std::string ensureType(Module &module, const clang::QualType &type, clang::SourceLocation location);

            /** Ensures a builtin scalar or vector type name exists in the current module. */
            std::string ensureBuiltinTypeName(Module &module, const std::string &typeName, clang::SourceLocation location);

            /** Resolves and ensures one type in the context of a function template specialization. */
            std::string ensureFunctionTypeName(FunctionLoweringState &state, const clang::QualType &type, clang::SourceLocation location);

            /** Returns the shader-visible parameter type, preserving fixed-size arrays before Clang function ABI adjustment. */
            clang::QualType shaderParameterType(const clang::ParmVarDecl *paramDecl) const;

            /** Fills backend-neutral construct metadata for scalar and vector construction expressions. */
            void normalizeConstructInfo(FunctionLoweringState &state, Expression &expression, clang::SourceLocation location);

            /** Returns true when a lowered type name represents workgroup storage or an array of workgroup storage. */
            bool isWorkgroupTypeName(const Module &module, const std::string &typeName) const;

            /** Returns the shader-visible ABI byte size for a lowered type in buffer and uniform records. */
            uint32_t shaderStorageByteSize(const Module &module, const std::string &typeName) const;

            /** Adds a type to a module when a type with the same name does not already exist. */
            void addTypeIfMissing(Module &module, Type type);

            /** Lowers one function definition and recursively queues direct user helper calls. */
            Function lowerFunction(Module &module,
                                   const clang::FunctionDecl *functionDecl,
                                   bool isEntryPoint,
                                   ShaderStage stage,
                                   ShaderEntryKind entryKind,
                                   const std::array<uint32_t, 3> &workgroupSize,
                                   const std::string &overrideName = {},
                                   const std::unordered_map<const clang::ValueDecl *, Expression> &valueSubstitutions = {},
                                   const std::unordered_map<std::string, Expression> &thisFieldSubstitutions = {},
                                   bool zeroInitializeConstructor = false);

            /** Lowers a resolved constructor for one element, including elements of an array construction. */
            Expression lowerConstructExpression(FunctionLoweringState &state, const clang::CXXConstructExpr &expression,
                                                 const std::string &constructedType);

            /** Initializes a local value or record member, expanding array construction into element stores. */
            void appendConstructorInitializer(FunctionLoweringState &state, const Expression &destination,
                                               const clang::Expr &initializer, std::vector<Statement> &statements);

            /** Returns true when a lowered type should be substituted instead of passed through helper function ABI. */
            bool isResourceLikeHelperParameterType(const Module &module, const std::string &typeName) const;

            /** Builds a generic reflected-resource selector from a DSL-erased receiver expression. */
            std::optional<std::string> collectErasedResourceSelectorPath(FunctionLoweringState &state, const clang::Expr *expr) const;

            /** Creates a generic resource selector expression used after AST-side DSL erasure. */
            Expression makeErasedResourceSelectorExpression(const std::string &resourcePath, clang::SourceLocation location) const;

            /** Collects non-resource values captured by a substituted resource expression. */
            void collectResourceSpecializationCaptures(const Module &module, const Expression &expression, std::vector<Expression> &captures) const;

            /** Returns the captured values that must be appended to a resource-specialized helper ABI. */
            std::vector<Expression> getResourceSpecializationExtraCaptures(const clang::FunctionDecl *functionDecl,
                                                                           const std::unordered_map<const clang::ValueDecl *, Expression> &valueSubstitutions,
                                                                           const std::vector<Expression> &captures) const;

            /** Appends synthetic helper parameters that carry dynamic resource selector values. */
            void appendResourceSpecializationCaptureParameters(Function &function, const std::vector<Expression> &captures) const;

            /** Returns a stable key for a resource expression substituted into a helper function. */
            std::string resourceSpecializationKeyForExpression(const Expression &expression) const;

            /** Returns a stable specialized helper name for a call that substitutes resource-like parameters. */
            std::string makeResourceSpecializedHelperName(const clang::FunctionDecl *functionDecl, const std::vector<Expression> &resourceArguments) const;

            /** Returns a stable specialized helper name for a method whose `this` resource fields are substituted. */
            std::string makeThisResourceSpecializedHelperName(const clang::FunctionDecl *functionDecl, const std::unordered_map<std::string, Expression> &fieldSubstitutions) const;



            /** Collects resource-like fields on a policy object that must be represented by call-site substitution. */
            std::vector<std::string> collectResourceLikeFieldNames(FunctionLoweringState &state, const clang::QualType &type);

            /** Registers a local lambda and lowers it into an internal helper function. */
            const LambdaLocalLowering *registerLambdaExpression(FunctionLoweringState &state,
                                                                const clang::LambdaExpr *lambdaExpr,
                                                                const clang::ValueDecl *localDecl);

            /** Finds a lambda expression behind common AST wrapper nodes. */
            const clang::LambdaExpr *getLambdaExpression(const clang::Expr *expr) const;

            /** Resolves a lambda call object into the previously lowered helper metadata. */
            const LambdaLocalLowering *resolveLambdaCallTarget(FunctionLoweringState &state, const clang::Expr *calleeObject);

            /** Lowers one statement into zero or more UGLIR statements. */
            std::vector<Statement> lowerStatement(FunctionLoweringState &state, const clang::Stmt *stmt);

            /** Appends switch case arms while preserving C/C++ fallthrough order. */
            void collectSwitchCasesFromStmt(FunctionLoweringState &state,
                                            const clang::Stmt *stmt,
                                            std::vector<SwitchCase> &cases,
                                            SwitchCase *&currentCase);

            /** Lowers one expression into a typed UGLIR expression tree. */
            Expression lowerExpression(FunctionLoweringState &state, const clang::Expr *expr);

            /** Lowers floating operands at conversion boundaries while preserving branch evaluation and rejecting unsupported dynamic widths. */
            Expression lowerFloatingConversionOperand(FunctionLoweringState &state, const clang::Expr &expr, const std::string &targetTypeName);

            /** Checks whether a floating comparison operand is a binary32 value or an exactly representable constant. */
            bool canCompareInBinary32(const clang::Expr &expr) const;

            /** Rewrites DSL-erasure pseudo calls into ordinary UGLIR expressions and statements. */
            std::vector<Statement> normalizeErasedResourceStatements(Module &module, std::vector<Statement> statements);

            /** Rewrites one DSL-erasure pseudo call expression into ordinary UGLIR when possible. */
            Expression normalizeErasedResourceExpression(Module &module, Expression expression);

            /** Expands one erased resource expression call into ordinary UGLIR using lowering-local metadata. */
            Expression expandErasedResourceCallExpression(Module &module, ErasedResourceCallKind callKind, Expression expression);

            /** Expands an erased draw-data out-parameter call into ordinary UGLIR store statements. */
            std::vector<Statement> expandErasedDrawDataStatement(Module &module, ErasedResourceCallKind callKind, const Expression &expression);

            /** Lowers a draw-data out-parameter expression statement directly into ordinary UGLIR stores. */
            std::optional<std::vector<Statement>> lowerErasedDrawDataExpressionStatement(FunctionLoweringState &state, const clang::Expr *expr);

            /** Lowers a declaration reference expression. */
            Expression lowerDeclRef(FunctionLoweringState &state, const clang::DeclRefExpr *expr);

            /** Folds a shader-visible constant array subscript when both array and index are compile-time known. */
            std::optional<Expression> lowerConstantArraySubscript(FunctionLoweringState &state, const clang::ArraySubscriptExpr *expr);

            /** Folds const integral data members with safe in-class initializers into literals. */
            std::optional<Expression> lowerConstIntegralDefaultMember(FunctionLoweringState &state, const clang::FieldDecl *fieldDecl, const clang::MemberExpr *expr);

            /** Lowers a member expression, including DSL operator-arrow bases. */
            Expression lowerMemberExpr(FunctionLoweringState &state, const clang::MemberExpr *expr);

            /** Lowers a template-pattern dependent member expression using already materialized local types. */
            Expression lowerDependentScopeMemberExpr(FunctionLoweringState &state, const clang::CXXDependentScopeMemberExpr *expr);

            /** Lowers a direct call or member call expression. */
            Expression lowerCallExpr(FunctionLoweringState &state, const clang::CallExpr *expr);

            /** Preserves float evaluation and half rounding in DSL half arithmetic operators. */
            Expression preserveHalfArithmetic(FunctionLoweringState &state, Expression result, clang::SourceLocation location);

            /** Lowers an overloaded operator call expression. */
            Expression lowerOperatorCallExpr(FunctionLoweringState &state, const clang::CXXOperatorCallExpr *expr);

            /** Adds a direct user helper function to the active module queue. */
            void enqueueUserHelper(const clang::FunctionDecl *functionDecl);

            /** Returns the analyzable function definition for a call target. */
            const clang::FunctionDecl *getAnalyzableDefinition(const clang::FunctionDecl *functionDecl) const;

            /** Returns a stable function name used in UGLIR. */
            std::string getFunctionName(const clang::FunctionDecl *functionDecl) const;

            /** Converts a Clang binary opcode into an UGLIR operator spelling. */
            std::string getBinaryOperatorName(clang::BinaryOperatorKind op) const;

            /** Converts a Clang unary opcode into an UGLIR operator spelling. */
            std::string getUnaryOperatorName(clang::UnaryOperatorKind op) const;

            clang::ASTContext &mContext;
            clang::SourceManager &mSourceManager;
            UGLIRLoweringResult mResult;
            std::vector<ShaderClassRoot> mShaderRoots;
            std::unordered_set<std::string> mShaderRootNames;
            Module mClassResourceModule;
            Module *mActiveModule = nullptr;
            std::vector<const clang::FunctionDecl *> mHelperQueue;
            std::unordered_set<std::string> mQueuedHelpers;
            std::unordered_set<std::string> mLoweredHelpers;
            std::unordered_set<std::string> mSyntheticFunctionNames;
            std::vector<Function> mSyntheticFunctions;
        };

        UGLIRLowerer::UGLIRLowerer(clang::ASTContext &context)
            : mContext(context)
            , mSourceManager(context.getSourceManager())
        {
        }

        std::vector<ShaderClassRoot> UGLIRLowerer::collectRoots()
        {
            TraverseDecl(mContext.getTranslationUnitDecl());
            return std::move(mShaderRoots);
        }

        UGLIRLoweringResult UGLIRLowerer::run(llvm::ArrayRef<ShaderClassRoot> roots)
        {
            for (const ShaderClassRoot &root : roots)
            {
                lowerShaderRoot(root);
            }
            return mResult;
        }

        bool UGLIRLowerer::VisitCXXRecordDecl(const clang::CXXRecordDecl *decl)
        {
            if (decl == nullptr || !decl->isCompleteDefinition() || !isUserAuthoredRecordDecl(decl) || isUninstantiatedTemplatePattern(decl))
            {
                return true;
            }

            ShaderClassKind kind = ShaderClassKind::Compute;
            if (tryClassifyShaderClass(decl, kind))
            {
                addShaderRoot(decl, kind);
            }
            return true;
        }

        bool UGLIRLowerer::VisitFieldDecl(const clang::FieldDecl *decl)
        {
            if (decl != nullptr)
            {
                collectShaderWrapperType(decl->getType());
            }
            return true;
        }

        bool UGLIRLowerer::VisitVarDecl(const clang::VarDecl *decl)
        {
            if (decl != nullptr)
            {
                collectShaderWrapperType(decl->getType());
            }
            return true;
        }

        bool UGLIRLowerer::VisitCallExpr(const clang::CallExpr *expression)
        {
            collectShaderWrapperType(expression->getType());
            return true;
        }

        void UGLIRLowerer::addDiagnostic(clang::SourceLocation location, const std::string &message)
        {
            UGLIRLoweringDiagnostic diagnostic;
            diagnostic.message = "UGLIR lowering: " + message;
            location = UGLC::CodeGen::normalizeDiagnosticLocation(mSourceManager, location);
            if (location.isValid())
            {
                const clang::PresumedLoc presumedLoc = mSourceManager.getPresumedLoc(location);
                if (presumedLoc.isValid())
                {
                    diagnostic.filePath = presumedLoc.getFilename();
                    diagnostic.line = presumedLoc.getLine();
                    diagnostic.column = presumedLoc.getColumn();
                    diagnostic.hasSourceLocation = true;
                }
            }
            mResult.diagnostics.push_back(std::move(diagnostic));
        }

        void UGLIRLowerer::addDiagnostic(const SourceLocation &location, const std::string &message)
        {
            UGLIRLoweringDiagnostic diagnostic;
            diagnostic.message = "UGLIR lowering: " + message;
            diagnostic.filePath = location.file;
            diagnostic.line = location.line;
            diagnostic.column = location.column;
            diagnostic.hasSourceLocation = !location.file.empty();
            mResult.diagnostics.push_back(std::move(diagnostic));
        }

        SourceLocation UGLIRLowerer::makeSourceLocation(clang::SourceLocation location) const
        {
            SourceLocation result;
            location = UGLC::CodeGen::normalizeDiagnosticLocation(mSourceManager, location);
            if (location.isInvalid())
            {
                return result;
            }
            const clang::PresumedLoc presumedLoc = mSourceManager.getPresumedLoc(location);
            if (!presumedLoc.isValid())
            {
                return result;
            }
            result.file = presumedLoc.getFilename();
            result.line = presumedLoc.getLine();
            result.column = presumedLoc.getColumn();
            return result;
        }

        bool UGLIRLowerer::isUserAuthoredDecl(const clang::Decl *decl) const
        {
            return decl != nullptr && !decl->isImplicit() && !isIgnoredLocation(mSourceManager, decl->getLocation());
        }

        bool UGLIRLowerer::isUserAuthoredRecordDecl(const clang::CXXRecordDecl *recordDecl) const
        {
            if (isUserAuthoredDecl(recordDecl))
            {
                return true;
            }
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                return false;
            }
            const clang::ClassTemplateDecl *templateDecl = specializationDecl->getSpecializedTemplate();
            return templateDecl != nullptr && isUserAuthoredDecl(templateDecl->getTemplatedDecl());
        }

        bool UGLIRLowerer::isUserAuthoredFunction(const clang::FunctionDecl *functionDecl) const
        {
            return functionDecl != nullptr && isUserAuthoredDecl(functionDecl);
        }

        bool UGLIRLowerer::isUninstantiatedTemplatePattern(const clang::CXXRecordDecl *recordDecl) const
        {
            if (recordDecl == nullptr)
            {
                return true;
            }
            if (recordDecl->isDependentContext())
            {
                return true;
            }
            return recordDecl->getDescribedClassTemplate() != nullptr;
        }

        bool UGLIRLowerer::isDerivedFromUGLBase(const clang::CXXRecordDecl *decl, const std::string &baseName) const
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
                if (qualifiedName(baseDecl) == baseName || isDerivedFromUGLBase(baseDecl, baseName))
                {
                    return true;
                }
            }
            return false;
        }

        bool UGLIRLowerer::tryClassifyShaderClass(const clang::CXXRecordDecl *decl, ShaderClassKind &kind) const
        {
            if (isDerivedFromUGLBase(decl, UGLC::CodeGen::mUGLPixelLocalRenderClassBaseName))
            {
                kind = ShaderClassKind::PixelLocal;
                return true;
            }
            if (isDerivedFromUGLBase(decl, UGLC::CodeGen::mUGLRenderClassBaseName))
            {
                kind = ShaderClassKind::Render;
                return true;
            }
            if (isDerivedFromUGLBase(decl, UGLC::CodeGen::mUGLComputeClassBaseName))
            {
                kind = ShaderClassKind::Compute;
                return true;
            }
            return false;
        }

        void UGLIRLowerer::addShaderRoot(const clang::CXXRecordDecl *recordDecl, ShaderClassKind kind)
        {
            recordDecl = recordDefinition(recordDecl);
            if (recordDecl == nullptr)
            {
                return;
            }
            const std::string symbolName = makeUGLIRRecordSymbolName(*recordDecl);
            if (mShaderRootNames.find(symbolName) != mShaderRootNames.end())
            {
                for (const auto &root : mShaderRoots)
                {
                    if (makeUGLIRRecordSymbolName(*root.recordDecl) == symbolName &&
                        root.recordDecl->getCanonicalDecl() != recordDecl->getCanonicalDecl())
                        throw std::runtime_error(UGLC::CodeGen::formatClangStyleDiagnostic(recordDecl,
                            "UGLIR: distinct shader declarations have the same symbol: " + symbolName));
                }
                return;
            }
            mShaderRootNames.insert(symbolName);
            mShaderRoots.push_back(ShaderClassRoot{recordDecl, kind});
        }

        void UGLIRLowerer::collectShaderWrapperType(const clang::QualType &type)
        {
            if (type.isNull())
            {
                return;
            }
            const std::string wrapperName = canonicalRecordName(type);
            ShaderClassKind expectedKind = ShaderClassKind::Compute;
            if (wrapperName == "UGL::ComputeClass")
            {
                expectedKind = ShaderClassKind::Compute;
            }
            else if (wrapperName == "UGL::RenderClass")
            {
                expectedKind = ShaderClassKind::Render;
            }
            else
            {
                return;
            }
            const clang::QualType shaderType = getTemplateTypeArgument(type, 0);
            const clang::CXXRecordDecl *shaderRecordDecl = recordDefinition(shaderType.getNonReferenceType()->getAsCXXRecordDecl());
            if (shaderRecordDecl == nullptr)
            {
                return;
            }
            ShaderClassKind kind = ShaderClassKind::Compute;
            if (tryClassifyShaderClass(shaderRecordDecl, kind) &&
                (kind == expectedKind || (expectedKind == ShaderClassKind::Render && kind == ShaderClassKind::PixelLocal)))
            {
                addShaderRoot(shaderRecordDecl, kind);
            }
        }

        void UGLIRLowerer::collectClassResourceInterface(const clang::CXXRecordDecl &record, ShaderClassKind kind)
        {
            mClassResourceModule = Module{};
            Module &module = mClassResourceModule;
            module.reflection.shaderClassName = makeUGLIRRecordSymbolName(record);
            std::vector<ShaderStage> stages;
            if (kind == ShaderClassKind::Compute)
                stages.push_back(ShaderStage::Compute);
            else
            {
                if (const auto *vertex = findEntryMethod(&record, mUGLVertexShaderFunctionName))
                {
                    stages.push_back(ShaderStage::Vertex);
                    for (const auto *parameter : vertex->parameters())
                        for (const auto &attribute : getAnnotationStrings(parameter))
                            if (attribute == mUGLAttributeVertexInputName + "0")
                                module.reflection.metalBindGroupBufferOffset =
                                    getMSLShaderBackendCapabilities().renderVertexInputReservedBufferSlots;
                }
                if (kind == ShaderClassKind::PixelLocal || findEntryMethod(&record, mUGLFragmentShaderFunctionName) != nullptr)
                    stages.push_back(ShaderStage::Fragment);
            }
            for (const BindGroupSlot &slot : collectBindGroupSlots(&record))
            {
                module.reflection.bindGroups.push_back(BindGroupBinding{
                    .name = slot.fieldName,
                    .typeName = makeUGLIRRecordSymbolName(*slot.bindGroupDecl),
                    .bindGroupIndex = slot.slotIndex,
                    .isRenderSet = slot.isRenderSet,
                });
                for (const LoweredResourceBinding &binding : collectResourceBindings(slot, module))
                {
                    module.reflection.resources.push_back(ResourceBinding{
                        .name = binding.name,
                        .kind = binding.kind,
                        .bindGroupIndex = binding.bindGroupIndex,
                        .bindingIndex = binding.bindingIndex,
                        .accessMode = binding.accessMode,
                        .elementType = binding.elementTypeName.empty()
                                           ? ensureType(module, binding.elementType, binding.fieldDecl == nullptr ? record.getLocation() : binding.fieldDecl->getLocation())
                                           : binding.elementTypeName,
                        .arrayCount = binding.arrayCount,
                        .textureDimension = binding.textureDimension,
                        .textureFormat = binding.textureFormat,
                        .resourceRole = binding.resourceRole,
                        .resourceIndex = binding.resourceIndex,
                        .visibleStages = stages,
                        .sourceLocation = binding.sourceLocation,
                    });
                }
            }

        }

        void UGLIRLowerer::lowerShaderRoot(const ShaderClassRoot &root)
        {
            const clang::CXXRecordDecl *recordDecl = recordDefinition(root.recordDecl);
            if (recordDecl == nullptr)
            {
                return;
            }
            collectClassResourceInterface(*recordDecl, root.kind);
            if (root.kind == ShaderClassKind::Compute)
            {
                std::optional<Module> module = lowerComputeRoot(recordDecl);
                if (module.has_value())
                {
                    mResult.modules.push_back(std::move(*module));
                }
                return;
            }
            if (root.kind == ShaderClassKind::Render)
            {
                lowerRenderRoot(recordDecl);
                return;
            }
            lowerPixelLocalRoot(recordDecl);
        }

        std::optional<Module> UGLIRLowerer::lowerComputeRoot(const clang::CXXRecordDecl *recordDecl)
        {
            const clang::CXXMethodDecl *computeEntry = findComputeEntry(recordDecl);
            if (computeEntry == nullptr)
            {
                addDiagnostic(recordDecl->getLocation(), "compute shader class \"" + recordDecl->getQualifiedNameAsString() + "\" is missing a compute entry.");
                return std::nullopt;
            }

            std::optional<std::array<uint32_t, 3>> workgroupSize = readWorkgroupSize(recordDecl);
            if (!workgroupSize.has_value())
            {
                return std::nullopt;
            }

            Module module = mClassResourceModule;
            module.name = makeUGLIRRecordSymbolName(*recordDecl);
            module.sourceLocation = makeSourceLocation(recordDecl->getLocation());
            module.reflection.entryName = module.name + ".compute";
            module.reflection.stage = ShaderStage::Compute;
            module.reflection.entryKind = ShaderEntryKind::Compute;
            module.reflection.workgroupSize = *workgroupSize;

            addTypeIfMissing(module, Type{.name = "void", .kind = TypeKind::Void, .sourceLocation = module.sourceLocation});

            mActiveModule = &module;
            mHelperQueue.clear();
            mQueuedHelpers.clear();
            mLoweredHelpers.clear();
            mSyntheticFunctionNames.clear();
            mSyntheticFunctions.clear();
            collectStageInterface(module, computeEntry, ShaderEntryKind::Compute, recordDecl);
            module.functions.push_back(lowerFunction(module, computeEntry, true, ShaderStage::Compute, ShaderEntryKind::Compute, *workgroupSize));
            for (size_t index = 0; index < mHelperQueue.size(); ++index)
            {
                const clang::FunctionDecl *helper = mHelperQueue[index];
                const std::string key = getFunctionName(helper);
                if (key.empty() || mLoweredHelpers.find(key) != mLoweredHelpers.end())
                {
                    continue;
                }
                mLoweredHelpers.insert(key);
                module.functions.push_back(lowerFunction(module, helper, false, ShaderStage::None, ShaderEntryKind::None, {1, 1, 1}));
            }
            module.functions.insert(module.functions.end(), std::make_move_iterator(mSyntheticFunctions.begin()), std::make_move_iterator(mSyntheticFunctions.end()));
            mActiveModule = nullptr;

            return module;
        }

        void UGLIRLowerer::lowerRenderRoot(const clang::CXXRecordDecl *recordDecl)
        {
            const clang::CXXMethodDecl *vertexEntry = findEntryMethod(recordDecl, UGLC::CodeGen::mUGLVertexShaderFunctionName);
            if (vertexEntry == nullptr)
            {
                addDiagnostic(recordDecl->getLocation(), "render shader class \"" + recordDecl->getQualifiedNameAsString() + "\" is missing a vertex entry.");
                return;
            }
            if (const clang::CXXMethodDecl *hullEntry = findEntryMethod(recordDecl, UGLC::CodeGen::mUGLHullShaderFunctionName))
            {
                addDiagnostic(hullEntry->getLocation(), "hull shaders are outside the supported UGLIR scope.");
            }
            if (const clang::CXXMethodDecl *domainEntry = findEntryMethod(recordDecl, UGLC::CodeGen::mUGLDomainShaderFunctionName))
            {
                addDiagnostic(domainEntry->getLocation(), "domain shaders are outside the supported UGLIR scope.");
            }

            if (std::optional<Module> vertexModule = lowerEntryModule(recordDecl, vertexEntry, ShaderStage::Vertex, ShaderEntryKind::Vertex, {1, 1, 1}))
            {
                mResult.modules.push_back(std::move(*vertexModule));
            }
            if (const clang::CXXMethodDecl *fragmentEntry = findEntryMethod(recordDecl, UGLC::CodeGen::mUGLFragmentShaderFunctionName))
            {
                if (std::optional<Module> fragmentModule = lowerEntryModule(recordDecl, fragmentEntry, ShaderStage::Fragment, ShaderEntryKind::Fragment, {1, 1, 1}))
                {
                    mResult.modules.push_back(std::move(*fragmentModule));
                }
            }
        }

        void UGLIRLowerer::lowerPixelLocalRoot(const clang::CXXRecordDecl *recordDecl)
        {
            const clang::CXXMethodDecl *pixelEntry = findEntryMethod(recordDecl, UGLC::CodeGen::mUGLPixelShaderFunctionName);
            if (pixelEntry == nullptr)
            {
                addDiagnostic(recordDecl->getLocation(), "pixel-local shader class \"" + recordDecl->getQualifiedNameAsString() + "\" is missing a pixel entry.");
                return;
            }
            if (findEntryMethod(recordDecl, UGLC::CodeGen::mUGLVertexShaderFunctionName) != nullptr ||
                findEntryMethod(recordDecl, UGLC::CodeGen::mUGLFragmentShaderFunctionName) != nullptr)
            {
                addDiagnostic(recordDecl->getLocation(), "pixel-local shader classes may only declare pixel(...) in Phase 8.");
                return;
            }
            if (std::optional<Module> pixelModule = lowerEntryModule(recordDecl, pixelEntry, ShaderStage::Fragment, ShaderEntryKind::PixelLocal, {1, 1, 1}))
            {
                mResult.modules.push_back(std::move(*pixelModule));
            }
        }

        std::optional<Module> UGLIRLowerer::lowerEntryModule(const clang::CXXRecordDecl *recordDecl,
                                                            const clang::CXXMethodDecl *entryMethod,
                                                            ShaderStage stage,
                                                            ShaderEntryKind entryKind,
                                                            const std::array<uint32_t, 3> &workgroupSize)
        {
            if (recordDecl == nullptr || entryMethod == nullptr)
            {
                return std::nullopt;
            }

            const std::string entryName = entryMethod->getNameAsString();
            Module module = mClassResourceModule;
            module.name = makeUGLIRRecordSymbolName(*recordDecl) + "." + entryName;
            module.sourceLocation = makeSourceLocation(recordDecl->getLocation());
            module.reflection.entryName = module.name;
            module.reflection.stage = stage;
            module.reflection.entryKind = entryKind;
            module.reflection.workgroupSize = workgroupSize;

            addTypeIfMissing(module, Type{.name = "void", .kind = TypeKind::Void, .sourceLocation = module.sourceLocation});

            collectStageInterface(module, entryMethod, entryKind, recordDecl);

            mActiveModule = &module;
            mHelperQueue.clear();
            mQueuedHelpers.clear();
            mLoweredHelpers.clear();
            mSyntheticFunctionNames.clear();
            mSyntheticFunctions.clear();
            module.functions.push_back(lowerFunction(module, entryMethod, true, stage, entryKind, workgroupSize));
            for (size_t index = 0; index < mHelperQueue.size(); ++index)
            {
                const clang::FunctionDecl *helper = mHelperQueue[index];
                const std::string key = getFunctionName(helper);
                if (key.empty() || mLoweredHelpers.find(key) != mLoweredHelpers.end())
                {
                    continue;
                }
                mLoweredHelpers.insert(key);
                module.functions.push_back(lowerFunction(module, helper, false, ShaderStage::None, ShaderEntryKind::None, {1, 1, 1}));
            }
            module.functions.insert(module.functions.end(), std::make_move_iterator(mSyntheticFunctions.begin()), std::make_move_iterator(mSyntheticFunctions.end()));
            normalizeBuiltinTypeReferences(module);
            mActiveModule = nullptr;
            return module;
        }

        const clang::CXXMethodDecl *UGLIRLowerer::findComputeEntry(const clang::CXXRecordDecl *recordDecl) const
        {
            if (recordDecl == nullptr)
            {
                return nullptr;
            }
            for (const clang::CXXMethodDecl *methodDecl : recordDecl->methods())
            {
                if (methodDecl != nullptr && methodDecl->getNameAsString() == UGLC::CodeGen::mUGLComputeShaderFunctionName)
                {
                    return methodDecl;
                }
            }
            return nullptr;
        }

        const clang::CXXMethodDecl *UGLIRLowerer::findEntryMethod(const clang::CXXRecordDecl *recordDecl, const std::string &entryName) const
        {
            if (recordDecl == nullptr)
            {
                return nullptr;
            }
            for (const clang::CXXMethodDecl *methodDecl : recordDecl->methods())
            {
                if (methodDecl != nullptr && methodDecl->getNameAsString() == entryName)
                {
                    return methodDecl;
                }
            }
            return nullptr;
        }

        const clang::CXXMethodDecl *UGLIRLowerer::findCreateMethod(const clang::CXXRecordDecl *recordDecl) const
        {
            if (recordDecl == nullptr)
            {
                return nullptr;
            }
            for (const clang::CXXMethodDecl *methodDecl : recordDecl->methods())
            {
                if (methodDecl != nullptr && methodDecl->getNameAsString() == UGLC::CodeGen::mUGLCTORFunctionName)
                {
                    return methodDecl;
                }
            }
            return nullptr;
        }

        std::optional<std::array<uint32_t, 3>> UGLIRLowerer::readWorkgroupSize(const clang::CXXRecordDecl *recordDecl)
        {
            std::vector<std::string> matchedAttributes;
            for (const std::string &rawAttribute : getAnnotationStrings(recordDecl))
            {
                if (isFunctionStyleAttributeName(rawAttribute, "LocalWorkGroupSize"))
                {
                    matchedAttributes.push_back(rawAttribute);
                }
            }
            if (matchedAttributes.size() != 1)
            {
                addDiagnostic(recordDecl->getLocation(), "compute shader class \"" + recordDecl->getQualifiedNameAsString() + "\" requires exactly one LocalWorkGroupSize annotation in Phase 3.");
                return std::nullopt;
            }

            const std::vector<std::string> params = splitAttributeParameters(matchedAttributes.front());
            if (params.size() != 3)
            {
                addDiagnostic(recordDecl->getLocation(), "LocalWorkGroupSize must contain exactly three integer arguments.");
                return std::nullopt;
            }

            std::array<uint32_t, 3> result{1, 1, 1};
            for (size_t index = 0; index < params.size(); ++index)
            {
                const UGLC::CodeGen::IntegerConstantEvaluationResult evaluated = UGLC::CodeGen::evaluateCompileTimeIntegerExpression(params[index], recordDecl, mContext);
                if (!evaluated.success || evaluated.value <= 0)
                {
                    addDiagnostic(recordDecl->getLocation(), "LocalWorkGroupSize argument \"" + params[index] + "\" must resolve to a positive integer in Phase 3.");
                    return std::nullopt;
                }
                result[index] = static_cast<uint32_t>(evaluated.value);
            }
            return result;
        }

        std::vector<BindGroupSlot> UGLIRLowerer::collectBindGroupSlots(const clang::CXXRecordDecl *recordDecl)
        {
            std::vector<BindGroupSlot> result;
            if (recordDecl == nullptr)
            {
                return result;
            }
            const std::unordered_map<std::string, clang::QualType> shaderTypeArguments = collectClassTypeTemplateArguments(recordDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(recordDecl))
            {
                int slotIndex = -1;
                for (const std::string &rawAttribute : getAnnotationStrings(fieldDecl))
                {
                    const int currentIndex = getIndexedAttributeNumber(rawAttribute, UGLC::CodeGen::mUGLAttributeSlotName);
                    if (currentIndex >= 0)
                    {
                        slotIndex = currentIndex;
                        break;
                    }
                }
                if (slotIndex < 0)
                {
                    continue;
                }

                const clang::QualType fieldType = resolveClassTypeTemplateArgument(shaderTypeArguments, fieldDecl->getType());
                const std::string slotTypeName = canonicalRecordName(fieldType);
                const bool isBindGroupSlot = isBindGroupHandleName(slotTypeName);
                const bool isRenderSetSlot = isRenderSetHandleName(slotTypeName);
                if (!isBindGroupSlot && !isRenderSetSlot)
                {
                    addDiagnostic(fieldDecl->getLocation(), "Phase 8 only supports BindGroup<T> and RenderSet<T> fields with SlotN annotations.");
                    continue;
                }
                const clang::QualType bindGroupType = resolveTemplateTypeArgument(shaderTypeArguments, fieldType, 0);
                const clang::CXXRecordDecl *bindGroupDecl = bindGroupType->getAsCXXRecordDecl();
                bindGroupDecl = recordDefinition(bindGroupDecl);
                if (bindGroupDecl == nullptr)
                {
                    addDiagnostic(fieldDecl->getLocation(), "shader resource slot must resolve to a concrete record type.");
                    continue;
                }
                result.push_back(BindGroupSlot{
                    .fieldDecl = fieldDecl,
                    .bindGroupDecl = bindGroupDecl,
                    .fieldName = fieldDecl->getNameAsString(),
                    .slotIndex = static_cast<uint32_t>(slotIndex),
                    .isRenderSet = isRenderSetSlot,
                });
            }
            std::sort(result.begin(), result.end(), [](const BindGroupSlot &left, const BindGroupSlot &right) {
                return left.slotIndex < right.slotIndex;
            });
            return result;
        }

        void UGLIRLowerer::ensureRenderSetAuxiliaryTypes(Module &module, clang::SourceLocation location)
        {
            const SourceLocation sourceLocation = makeSourceLocation(location);
            ensureBuiltinTypeName(module, "u32", location);
            ensureBuiltinTypeName(module, "i32", location);
            ensureBuiltinTypeName(module, "uint2", location);

            Type entityInfoType;
            entityInfoType.name = "UGL_DrawInfo_";
            entityInfoType.kind = TypeKind::Struct;
            entityInfoType.sourceLocation = sourceLocation;
            entityInfoType.fields.push_back(TypeField{.name = "indexCount", .type = "u32", .offset = 0u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "instanceCount", .type = "u32", .offset = 4u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "firstIndex", .type = "u32", .offset = 8u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "vertexOffset", .type = "i32", .offset = 12u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "globalInstanceBase", .type = "u32", .offset = 16u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "vertexCount", .type = "u32", .offset = 20u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "entityVersion", .type = "u32", .offset = 24u, .sourceLocation = sourceLocation});
            entityInfoType.fields.push_back(TypeField{.name = "cmdParamsOffset", .type = "u32", .offset = 28u, .sourceLocation = sourceLocation});
            addTypeIfMissing(module, std::move(entityInfoType));
        }

        std::vector<LoweredResourceBinding> UGLIRLowerer::collectResourceBindings(const BindGroupSlot &slot, Module &module)
        {
            std::vector<LoweredResourceBinding> result;
            const clang::CXXRecordDecl *bindGroupDecl = recordDefinition(slot.bindGroupDecl);
            if (bindGroupDecl == nullptr)
            {
                return result;
            }
            if (slot.isRenderSet)
            {
                const std::unordered_map<std::string, clang::QualType> bindGroupTypeArguments = collectClassTypeTemplateArguments(bindGroupDecl);
                ensureRenderSetAuxiliaryTypes(module, slot.fieldDecl == nullptr ? bindGroupDecl->getLocation() : slot.fieldDecl->getLocation());
                uint32_t syntheticBinding = 0;
                uint32_t resourceIndex = 0;
                const SourceLocation slotLocation = slot.fieldDecl == nullptr ? makeSourceLocation(bindGroupDecl->getLocation()) : makeSourceLocation(slot.fieldDecl->getLocation());
                result.push_back(LoweredResourceBinding{
                    .fieldDecl = slot.fieldDecl,
                    .name = slot.fieldName + ".AccessBounds",
                    .kind = ResourceKind::StorageBuffer,
                    .bindGroupIndex = slot.slotIndex,
                    .bindingIndex = syntheticBinding++,
                    .accessMode = AccessMode::Read,
                    .elementTypeName = "uint2",
                    .arrayCount = 1,
                    .resourceRole = ResourceRole::AccessBounds,
                    .sourceLocation = slotLocation,
                });
                for (const clang::FieldDecl *fieldDecl : collectRecordFields(bindGroupDecl))
                {
                    const clang::QualType fieldType = resolveClassTypeTemplateArgument(bindGroupTypeArguments, fieldDecl->getType());
                    const std::string componentTypeName = canonicalRecordName(fieldType);
                    if (componentTypeName != UGLC::CodeGen::mUGLRenderSetBufferComponentClassName &&
                        componentTypeName != UGLC::CodeGen::mUGLRenderSetTextureComponentClassName)
                    {
                        continue;
                    }
                    ResourceKind kind = componentTypeName == UGLC::CodeGen::mUGLRenderSetTextureComponentClassName ? ResourceKind::Texture : ResourceKind::StorageBuffer;
                    const clang::QualType elementType = resolveTemplateTypeArgument(bindGroupTypeArguments, fieldType, 0);
                    const uint32_t componentIndex = ++resourceIndex;
                    result.push_back(LoweredResourceBinding{
                        .fieldDecl = fieldDecl,
                        .name = slot.fieldName + "." + fieldDecl->getNameAsString() + "IndexTable",
                        .kind = ResourceKind::StorageBuffer,
                        .bindGroupIndex = slot.slotIndex,
                        .bindingIndex = syntheticBinding++,
                        .accessMode = AccessMode::Read,
                        .elementTypeName = "u32",
                        .arrayCount = 1,
                        .resourceRole = kind == ResourceKind::Texture ? ResourceRole::TextureIndexTable : ResourceRole::BufferIndexTable,
                        .resourceIndex = componentIndex,
                        .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                    });
                    uint32_t resourceArrayCount = 1;
                    TextureDimension textureDimension = TextureDimension::None;
                    if (kind == ResourceKind::Texture)
                    {
                        textureDimension = TextureDimension::Texture2D;
                        if (std::optional<uint32_t> textureCount = getTemplateUnsignedIntegerArgument(fieldType, 1))
                        {
                            resourceArrayCount = std::max<uint32_t>(*textureCount, 1u);
                        }
                    }
                    result.push_back(LoweredResourceBinding{
                        .fieldDecl = fieldDecl,
                        .name = slot.fieldName + "." + fieldDecl->getNameAsString(),
                        .kind = kind,
                        .bindGroupIndex = slot.slotIndex,
                        .bindingIndex = syntheticBinding++,
                        .accessMode = AccessMode::Read,
                        .elementType = elementType,
                        .arrayCount = resourceArrayCount,
                        .textureDimension = textureDimension,
                        .resourceRole = componentTypeName == UGLC::CodeGen::mUGLRenderSetTextureComponentClassName ? ResourceRole::TextureValue : ResourceRole::BufferValue,
                        .resourceIndex = componentIndex,
                        .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                    });
                }
                result.push_back(LoweredResourceBinding{
                    .fieldDecl = slot.fieldDecl,
                    .name = slot.fieldName + ".DrawInfo",
                    .kind = ResourceKind::StorageBuffer,
                    .bindGroupIndex = slot.slotIndex,
                    .bindingIndex = syntheticBinding++,
                    .accessMode = AccessMode::Read,
                    .elementTypeName = "UGL_DrawInfo_",
                    .arrayCount = 1,
                    .resourceRole = ResourceRole::DrawInfo,
                    .sourceLocation = slotLocation,
                });
                result.push_back(LoweredResourceBinding{
                    .fieldDecl = slot.fieldDecl,
                    .name = slot.fieldName + ".CommandParams",
                    .kind = ResourceKind::StorageBuffer,
                    .bindGroupIndex = slot.slotIndex,
                    .bindingIndex = syntheticBinding++,
                    .accessMode = AccessMode::Read,
                    .elementTypeName = "uint2",
                    .arrayCount = 1,
                    .resourceRole = ResourceRole::CommandParams,
                    .sourceLocation = slotLocation,
                });
                return result;
            }
            const std::unordered_map<std::string, clang::QualType> bindGroupTypeArguments = collectClassTypeTemplateArguments(bindGroupDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(bindGroupDecl))
            {
                int bindingIndex = -1;
                for (const std::string &rawAttribute : getAnnotationStrings(fieldDecl))
                {
                    const int currentIndex = getIndexedAttributeNumber(rawAttribute, UGLC::CodeGen::mUGLAttributeBindingName);
                    if (currentIndex >= 0)
                    {
                        bindingIndex = currentIndex;
                        break;
                    }
                }
                if (bindingIndex < 0)
                {
                    continue;
                }

                const clang::QualType fieldType = resolveClassTypeTemplateArgument(bindGroupTypeArguments, fieldDecl->getType());
                const std::string resourceTypeName = canonicalRecordName(fieldType);
                if (!isSupportedBufferResourceName(resourceTypeName) &&
                    !isSampledTextureResourceName(resourceTypeName) &&
                    !isStorageTextureResourceName(resourceTypeName) &&
                    !isSamplerResourceName(resourceTypeName))
                {
                    addDiagnostic(fieldDecl->getLocation(), "Phase 8 supports buffer, texture, storage texture, and sampler resources.");
                    continue;
                }

                ResourceKind kind = ResourceKind::StorageBuffer;
                AccessMode access = AccessMode::Read;
                if (resourceTypeName == UGLC::CodeGen::mUGLShaderUniformBufferName)
                {
                    kind = ResourceKind::UniformBuffer;
                    access = AccessMode::Read;
                }
                else if (resourceTypeName == UGLC::CodeGen::mUGLShaderRWStructuredBufferName)
                {
                    kind = ResourceKind::StorageBuffer;
                    access = AccessMode::ReadWrite;
                }
                else if (isSampledTextureResourceName(resourceTypeName))
                {
                    kind = ResourceKind::Texture;
                    access = AccessMode::Read;
                }
                else if (isStorageTextureResourceName(resourceTypeName))
                {
                    kind = ResourceKind::StorageTexture;
                    access = AccessMode::ReadWrite;
                }
                else if (isSamplerResourceName(resourceTypeName))
                {
                    kind = ResourceKind::Sampler;
                    access = AccessMode::Read;
                }
                else
                {
                    kind = ResourceKind::StorageBuffer;
                    access = AccessMode::Read;
                }

                clang::QualType elementType = resolveTemplateTypeArgument(bindGroupTypeArguments, fieldType, 0);
                std::string elementTypeName;
                const std::string textureFormatToken = extractTextureFormatToken(elementType.isNull() ? typeSpelling(fieldType) : typeSpelling(elementType));
                const TextureFormat textureFormat = textureFormatFromToken(textureFormatToken);
                if (isSamplerResourceName(resourceTypeName))
                {
                    elementType = mContext.UnsignedIntTy;
                }
                if (isStorageTextureResourceName(resourceTypeName))
                {
                    elementTypeName = ensureBuiltinTypeName(module, textureFormatValueTypeName(textureFormat), fieldDecl->getLocation());
                }
                else if (elementType.isNull())
                {
                    addDiagnostic(fieldDecl->getLocation(), "shader resource requires a concrete element type.");
                    continue;
                }
                if (elementTypeName.empty())
                {
                    ensureType(module, elementType, fieldDecl->getLocation());
                }
                result.push_back(LoweredResourceBinding{
                    .fieldDecl = fieldDecl,
                    .name = slot.fieldName + "." + fieldDecl->getNameAsString(),
                    .kind = kind,
                    .bindGroupIndex = slot.slotIndex,
                    .bindingIndex = static_cast<uint32_t>(bindingIndex),
                    .accessMode = access,
                    .elementType = elementType,
                    .elementTypeName = elementTypeName,
                    .textureDimension = (kind == ResourceKind::Texture || kind == ResourceKind::StorageTexture) ? textureDimensionForResourceName(resourceTypeName) : TextureDimension::None,
                    .textureFormat = (isSampledTextureResourceName(resourceTypeName) || isStorageTextureResourceName(resourceTypeName)) ? textureFormat : TextureFormat::Unknown,
                    .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                });
            }
            std::sort(result.begin(), result.end(), [](const LoweredResourceBinding &left, const LoweredResourceBinding &right) {
                return left.bindingIndex < right.bindingIndex;
            });
            return result;
        }

        void UGLIRLowerer::collectStageInterface(Module &module,
                                                 const clang::FunctionDecl *entryMethod,
                                                 ShaderEntryKind entryKind,
                                                 const clang::CXXRecordDecl *shaderRecordDecl)
        {
            if (entryMethod == nullptr)
            {
                return;
            }
            const std::unordered_map<std::string, clang::QualType> shaderTypeArguments = collectClassTypeTemplateArguments(shaderRecordDecl);

            for (const clang::ParmVarDecl *paramDecl : entryMethod->parameters())
            {
                const BuiltinSemantic semantic = resolveEntryParameterSemantic(paramDecl, entryKind);
                uint32_t location = 0;
                if (semantic.kind == BuiltinSemanticKind::VertexInput)
                {
                    location = semantic.index;
                }

                const clang::QualType parameterType = resolveClassTypeTemplateArgument(shaderTypeArguments, paramDecl->getType());
                const clang::CXXRecordDecl *paramRecordDecl = recordDefinition(parameterType.getNonReferenceType()->getAsCXXRecordDecl());

                module.reflection.stageInputs.push_back(StageIOBinding{
                    .name = paramDecl->getNameAsString(),
                    .type = ensureType(module, parameterType, paramDecl->getLocation()),
                    .semantic = semanticDisplayName(semantic.kind, semantic.index),
                    .semanticKind = semantic.kind,
                    .semanticIndex = semantic.index,
                    .location = location,
                    .index = static_cast<uint32_t>(module.reflection.stageInputs.size()),
                    .sourceLocation = makeSourceLocation(paramDecl->getLocation()),
                });

                if (paramRecordDecl != nullptr &&
                    (semantic.kind == BuiltinSemanticKind::StageInput ||
                     semantic.kind == BuiltinSemanticKind::VertexInput ||
                     semantic.kind == BuiltinSemanticKind::PixelLocalInput))
                {
                    collectRecordStageIO(module, paramRecordDecl, module.reflection.stageInputs, false);
                    if (semantic.kind == BuiltinSemanticKind::PixelLocalInput)
                    {
                        collectPixelLocalInputAttachmentResources(module, entryMethod, paramDecl, paramRecordDecl);
                    }
                }
            }

            const clang::QualType returnType = resolveClassTypeTemplateArgument(shaderTypeArguments, entryMethod->getReturnType());
            const clang::CXXRecordDecl *returnRecordDecl = recordDefinition(returnType.getNonReferenceType()->getAsCXXRecordDecl());
            if (returnRecordDecl != nullptr)
            {
                if (entryKind == ShaderEntryKind::Fragment || entryKind == ShaderEntryKind::PixelLocal)
                {
                    const UGLC::CodeGen::PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis writeAnalysis =
                        UGLC::CodeGen::PixelLocalFieldAnalysis::collectRenderTargetWriteFieldAnalysis(entryMethod, returnRecordDecl);
                    if (entryKind == ShaderEntryKind::PixelLocal)
                    {
                        diagnosePixelLocalDepthWrites(entryMethod, returnRecordDecl, writeAnalysis);
                    }
                    if (writeAnalysis.conservativeAllWrites)
                    {
                        addDiagnostic(entryMethod->getLocation(),
                                      "cannot determine the active framebuffer outputs for \"" + entryMethod->getNameAsString()
                                          + "\".  UGLIR requires explicit framebuffer field writes before return. Analysis state: variables="
                                          + std::to_string(writeAnalysis.renderTargetVariableCount)
                                          + ", returned=" + std::to_string(writeAnalysis.returnedVariableCount)
                                          + ", written=" + std::to_string(writeAnalysis.writtenVariableCount)
                                          + ", unknownReturn=" + (writeAnalysis.sawUnknownRenderTargetReturn ? "true" : "false") + ".");
                        return;
                    }
                    const std::unordered_set<std::string> *activeOutputFields = &writeAnalysis.fields;
                    collectRecordStageIO(module, returnRecordDecl, module.reflection.stageOutputs, true, activeOutputFields);
                }
                else
                {
                    collectRecordStageIO(module, returnRecordDecl, module.reflection.stageOutputs, true);
                }
            }
        }

        BuiltinSemantic UGLIRLowerer::resolveEntryParameterSemantic(const clang::ParmVarDecl *paramDecl, ShaderEntryKind entryKind) const
        {
            if (paramDecl == nullptr)
            {
                return {};
            }
            for (const std::string &rawAttribute : getAnnotationStrings(paramDecl))
            {
                if (rawAttribute == UGLC::CodeGen::mUGLAttributeRenderEntityIDName)
                {
                    return BuiltinSemantic{.kind = BuiltinSemanticKind::DrawEntityID};
                }
                if (rawAttribute == UGLC::CodeGen::mUGLAttributeRenderEntityInstanceIDName)
                {
                    return BuiltinSemantic{.kind = BuiltinSemanticKind::DrawEntityInstanceID};
                }
                return builtinSemanticFromToken(rawAttribute);
            }
            const clang::CXXRecordDecl *paramRecordDecl = recordDefinition(paramDecl->getType().getNonReferenceType()->getAsCXXRecordDecl());
            if (entryKind == ShaderEntryKind::Fragment && paramRecordDecl != nullptr)
            {
                return BuiltinSemantic{.kind = BuiltinSemanticKind::StageInput};
            }
            return {};
        }

        /** Appends Vulkan input-attachment resource reflection for pixel-local fields that are read by the entry. */
        void UGLIRLowerer::collectPixelLocalInputAttachmentResources(Module &module,
                                                                     const clang::FunctionDecl *entryMethod,
                                                                     const clang::ParmVarDecl *paramDecl,
                                                                     const clang::CXXRecordDecl *recordDecl)
        {
            recordDecl = recordDefinition(recordDecl);
            if (entryMethod == nullptr || paramDecl == nullptr || recordDecl == nullptr)
            {
                return;
            }

            const std::unordered_set<std::string> readFields = UGLC::CodeGen::PixelLocalFieldAnalysis::collectPixelLocalReadFields(entryMethod, paramDecl);
            if (readFields.empty())
            {
                return;
            }

            uint32_t inputAttachmentDescriptorSet = 0;
            for (const ResourceBinding &resource : module.reflection.resources)
            {
                if (resource.kind != ResourceKind::InputAttachment)
                {
                    inputAttachmentDescriptorSet = std::max<uint32_t>(inputAttachmentDescriptorSet, resource.bindGroupIndex + 1u);
                }
            }

            uint32_t inputAttachmentIndex = 0;
            const std::unordered_map<std::string, clang::QualType> recordTypeArguments = collectClassTypeTemplateArguments(recordDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(recordDecl))
            {
                clang::QualType fieldType = resolveClassTypeTemplateArgument(recordTypeArguments, fieldDecl->getType());
                std::string fieldTypeName = canonicalRecordName(fieldType);
                if (fieldTypeName.empty())
                {
                    fieldTypeName = framebufferAttachmentNameFromDependentSpelling(typeSpelling(fieldType));
                }
                if (fieldTypeName != UGLC::CodeGen::mUGLPixelLocalColorAttachmentName)
                {
                    continue;
                }
                if (!readFields.contains(fieldDecl->getNameAsString()))
                {
                    continue;
                }

                clang::QualType storedType = resolveTemplateTypeArgument(recordTypeArguments, fieldType, 0);
                const TextureFormat textureFormat = textureFormatFromToken(extractTextureFormatToken(storedType.isNull() ? typeSpelling(fieldType) : typeSpelling(storedType)));
                const std::string elementTypeName = ensureBuiltinTypeName(module,
                                                                          framebufferTextureFormatValueTypeName(textureFormat),
                                                                          fieldDecl->getLocation());
                module.reflection.resources.push_back(ResourceBinding{
                    .name = paramDecl->getNameAsString() + "." + fieldDecl->getNameAsString(),
                    .kind = ResourceKind::InputAttachment,
                    .bindGroupIndex = inputAttachmentDescriptorSet,
                    .bindingIndex = inputAttachmentIndex,
                    .accessMode = AccessMode::Read,
                    .elementType = elementTypeName,
                    .arrayCount = 1,
                    .textureDimension = TextureDimension::Subpass,
                    .textureFormat = textureFormat,
                    .isMultisampled = false,
                    .inputAttachmentIndex = inputAttachmentIndex,
                    .resourceRole = ResourceRole::PixelLocalInput,
                    .visibleStages = {ShaderStage::Fragment},
                    .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                });
                ++inputAttachmentIndex;
            }
        }

        void UGLIRLowerer::collectRecordStageIO(Module &module,
                                                const clang::CXXRecordDecl *recordDecl,
                                                std::vector<StageIOBinding> &bindings,
                                                bool outputs,
                                                const std::unordered_set<std::string> *activeOutputFields)
        {
            recordDecl = recordDefinition(recordDecl);
            if (recordDecl == nullptr)
            {
                return;
            }
            uint32_t fieldIndex = 0;
            uint32_t colorIndex = 0;
            const std::unordered_map<std::string, clang::QualType> recordTypeArguments = collectClassTypeTemplateArguments(recordDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(recordDecl))
            {
                BuiltinSemantic semantic;
                uint32_t location = fieldIndex;
                for (const std::string &rawAttribute : getAnnotationStrings(fieldDecl))
                {
                    semantic = builtinSemanticFromToken(rawAttribute);
                    if (semantic.kind == BuiltinSemanticKind::Attribute)
                    {
                        location = semantic.index;
                    }
                    break;
                }

                clang::QualType fieldType = resolveClassTypeTemplateArgument(recordTypeArguments, fieldDecl->getType());
                std::string fieldTypeName = canonicalRecordName(fieldType);
                if (fieldTypeName.empty())
                {
                    fieldTypeName = framebufferAttachmentNameFromDependentSpelling(typeSpelling(fieldType));
                }
                clang::QualType storedType = fieldType;
                std::string storedTypeName;
                if (isFramebufferAttachmentName(fieldTypeName))
                {
                    storedType = resolveTemplateTypeArgument(recordTypeArguments, fieldType, 0);
                    const TextureFormat textureFormat = textureFormatFromToken(extractTextureFormatToken(storedType.isNull() ? typeSpelling(fieldType) : typeSpelling(storedType)));
                    storedTypeName = ensureBuiltinTypeName(module,
                                                           framebufferTextureFormatValueTypeName(textureFormat),
                                                           fieldDecl->getLocation());
                    if (fieldTypeName == UGLC::CodeGen::mUGLDepthAttachmentName)
                    {
                        semantic = BuiltinSemantic{.kind = BuiltinSemanticKind::Depth};
                        location = 0;
                    }
                    else if (fieldTypeName == UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName)
                    {
                        semantic = BuiltinSemantic{.kind = BuiltinSemanticKind::PixelLocalDepth};
                        location = 0;
                    }
                    else
                    {
                        semantic = BuiltinSemantic{
                            .kind = fieldTypeName == UGLC::CodeGen::mUGLPixelLocalColorAttachmentName
                                        ? BuiltinSemanticKind::PixelLocalColor
                                        : BuiltinSemanticKind::Color,
                        };
                        location = colorIndex++;
                    }
                }
                else if (semantic.kind == BuiltinSemanticKind::None && outputs)
                {
                    semantic = BuiltinSemantic{.kind = BuiltinSemanticKind::Field};
                }

                if (outputs && activeOutputFields != nullptr && !activeOutputFields->contains(fieldDecl->getNameAsString()))
                {
                    ++fieldIndex;
                    continue;
                }

                bindings.push_back(StageIOBinding{
                    .name = fieldDecl->getNameAsString(),
                    .type = storedTypeName.empty() ? ensureType(module, storedType, fieldDecl->getLocation()) : storedTypeName,
                    .semantic = semanticDisplayName(semantic.kind, semantic.index),
                    .semanticKind = semantic.kind,
                    .semanticIndex = semantic.index,
                    .location = location,
                    .index = fieldIndex,
                    .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                });
                ++fieldIndex;
            }
        }

        void UGLIRLowerer::diagnosePixelLocalDepthWrites(const clang::FunctionDecl *entryMethod,
                                                         const clang::CXXRecordDecl *returnRecordDecl,
                                                         const UGLC::CodeGen::PixelLocalFieldAnalysis::RenderTargetWriteFieldAnalysis &writeAnalysis)
        {
            returnRecordDecl = recordDefinition(returnRecordDecl);
            if (entryMethod == nullptr || returnRecordDecl == nullptr)
            {
                return;
            }

            const std::unordered_map<std::string, clang::QualType> recordTypeArguments = collectClassTypeTemplateArguments(returnRecordDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(returnRecordDecl))
            {
                const bool writesField = writeAnalysis.conservativeAllWrites || writeAnalysis.fields.contains(fieldDecl->getNameAsString());
                if (!writesField)
                {
                    continue;
                }

                const clang::QualType fieldType = resolveClassTypeTemplateArgument(recordTypeArguments, fieldDecl->getType());
                std::string fieldCanonicalName = canonicalRecordName(fieldType);
                if (fieldCanonicalName.empty())
                {
                    fieldCanonicalName = framebufferAttachmentNameFromDependentSpelling(typeSpelling(fieldType));
                }
                if (fieldCanonicalName != UGLC::CodeGen::mUGLDepthAttachmentName &&
                    fieldCanonicalName != UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName)
                {
                    continue;
                }

                addDiagnostic(fieldDecl->getLocation(),
                              "IPixelLocalRenderClass::pixel() cannot write depth attachment field \"" + fieldDecl->getNameAsString()
                                  + "\". Write native depth from IRenderClass::fragment(), or store depth needed by later pixel-local passes in a PixelLocalColorAttachment field.");
            }
        }

        clang::QualType UGLIRLowerer::resolveTemplateTypeArgument(const std::unordered_map<std::string, clang::QualType> &typeArguments,
                                                                  const clang::QualType &type,
                                                                  unsigned index) const
        {
            clang::QualType argument = getTemplateTypeArgument(type, index);
            if (!argument.isNull())
            {
                return resolveClassTypeTemplateArgument(typeArguments, argument);
            }

            argument = resolveDependentTypeAliasSpelling(typeArguments, extractTemplateArgumentSpelling(typeSpelling(type), index));
            return argument;
        }

        clang::QualType UGLIRLowerer::getTemplateTypeArgument(const clang::QualType &type, unsigned index) const
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

        std::optional<uint32_t> UGLIRLowerer::getTemplateUnsignedIntegerArgument(const clang::QualType &type, unsigned index) const
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

        std::string UGLIRLowerer::canonicalRecordName(const clang::QualType &type) const
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

        /** Returns the canonical primary template name for a class template specialization type. */
        std::string canonicalPrimaryTemplateName(const clang::QualType &type)
        {
            clang::QualType recordType = type.getNonReferenceType();
            if (recordType->isPointerType())
            {
                recordType = recordType->getPointeeType().getNonReferenceType();
            }
            const clang::CXXRecordDecl *recordDecl = recordType->getAsCXXRecordDecl();
            const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDecl);
            if (specializationDecl == nullptr)
            {
                specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(recordDefinition(recordDecl));
            }
            if (specializationDecl == nullptr || specializationDecl->getSpecializedTemplate() == nullptr)
            {
                return {};
            }
            return normalizeStdName(qualifiedName(specializationDecl->getSpecializedTemplate()->getTemplatedDecl()));
        }

        std::string UGLIRLowerer::ensureType(Module &module, const clang::QualType &type, clang::SourceLocation location)
        {
            if (type.isNull())
            {
                return "void";
            }
            clang::QualType unqualifiedType = type.getNonReferenceType().getUnqualifiedType();
            const clang::QualType canonicalValueType = unqualifiedType.getCanonicalType().getUnqualifiedType();
            const std::string spelledType = typeSpelling(unqualifiedType);
            const std::string canonicalName = canonicalRecordName(unqualifiedType);
            const std::string primaryTemplateName = canonicalPrimaryTemplateName(unqualifiedType);

            Type loweredType;
            loweredType.sourceLocation = makeSourceLocation(location);
            loweredType.role = typeRoleFromSourceTypeName(canonicalName, spelledType);
            loweredType.isImplementationOnly = loweredType.role != TypeRole::Value;

            if (canonicalValueType->isVoidType())
            {
                loweredType.name = "void";
                loweredType.kind = TypeKind::Void;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }
            if (canonicalValueType->isBooleanType())
            {
                loweredType.name = "bool";
                loweredType.kind = TypeKind::Bool;
                loweredType.scalarKind = ScalarKind::Bool;
                loweredType.bitWidth = 1;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }
            if (canonicalValueType->isUnsignedIntegerType())
            {
                if (mContext.getTypeSize(canonicalValueType) != 32u)
                {
                    addDiagnostic(location, "UGLIR requires 32-bit integer values; implicit narrowing of type \"" + spelledType + "\" is not supported.");
                }
                loweredType.name = "u32";
                loweredType.kind = TypeKind::UInt;
                loweredType.scalarKind = ScalarKind::UInt;
                loweredType.bitWidth = 32;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }
            if (canonicalValueType->isSignedIntegerType())
            {
                if (mContext.getTypeSize(canonicalValueType) != 32u)
                {
                    addDiagnostic(location, "UGLIR requires 32-bit integer values; implicit narrowing of type \"" + spelledType + "\" is not supported.");
                }
                loweredType.name = "i32";
                loweredType.kind = TypeKind::Int;
                loweredType.scalarKind = ScalarKind::Int;
                loweredType.bitWidth = 32;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }
            if (canonicalValueType->isFloatingType())
            {
                if (mContext.getTypeSize(canonicalValueType) != 32u)
                {
                    addDiagnostic(location, "UGLIR requires 32-bit built-in floating-point values; implicit narrowing of type \"" + spelledType + "\" is not supported.");
                }
                loweredType.name = "f32";
                loweredType.kind = TypeKind::Float;
                loweredType.scalarKind = ScalarKind::Float;
                loweredType.bitWidth = 32;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }
            if (spelledType == "UGL::half" || spelledType == "half" ||
                canonicalName == "UGL::half")
            {
                loweredType.name = "f16";
                loweredType.kind = TypeKind::Half;
                loweredType.scalarKind = ScalarKind::Half;
                loweredType.bitWidth = 16;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (loweredType.role == TypeRole::SwizzleProxy)
            {
                const clang::QualType destinationVectorType = getTemplateTypeArgument(unqualifiedType, 0);
                if (!destinationVectorType.isNull())
                {
                    return ensureType(module, destinationVectorType, location);
                }
            }

            std::string vectorElementType;
            uint32_t vectorWidth = 0;
            if (parseVectorAlias(spelledType, vectorElementType, vectorWidth))
            {
                loweredType.name = startsWith(spelledType, "UGL::") ? spelledType.substr(5) : spelledType;
                loweredType.kind = TypeKind::Vector;
                loweredType.scalarKind = scalarKindFromTypeName(vectorElementType);
                loweredType.elementType = vectorElementType;
                loweredType.bitWidth = bitWidthForScalarKind(loweredType.scalarKind);
                loweredType.vectorWidth = vectorWidth;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            ParsedMatrixAlias matrixAlias;
            if (parseMatrixAlias(spelledType, matrixAlias) || parseMatrixTemplateSpelling(spelledType, matrixAlias))
            {
                loweredType.name = matrixAlias.canonicalName;
                loweredType.kind = TypeKind::Matrix;
                loweredType.scalarKind = matrixAlias.scalarKind;
                loweredType.elementType = matrixAlias.elementType;
                loweredType.bitWidth = bitWidthForScalarKind(matrixAlias.scalarKind);
                loweredType.matrixRows = matrixAlias.rows;
                loweredType.matrixColumns = matrixAlias.columns;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            const std::optional<EncodedUGLVectorSpecialization> encodedVectorSpecialization = parseEncodedUGLVectorSpecialization(canonicalName);
            if (primaryTemplateName == "UGL::Vector" || encodedVectorSpecialization.has_value())
            {
                std::string elementType = ensureType(module, getTemplateTypeArgument(unqualifiedType, 0), location);
                ScalarKind elementScalarKind = scalarKindFromTypeName(elementType);
                std::optional<uint32_t> componentCount = getTemplateUnsignedIntegerArgument(unqualifiedType, 1);
                if (elementScalarKind == ScalarKind::None || !componentCount.has_value())
                {
                    if (encodedVectorSpecialization.has_value())
                    {
                        elementScalarKind = encodedVectorSpecialization->scalarKind;
                        elementType = scalarIRTypeName(elementScalarKind);
                        componentCount = encodedVectorSpecialization->width;
                    }
                }
                if (elementScalarKind != ScalarKind::None && componentCount.has_value() && *componentCount >= 2u && *componentCount <= 4u)
                {
                    loweredType.name = makeVectorTypeName(elementScalarKind, *componentCount);
                    loweredType.kind = TypeKind::Vector;
                    loweredType.scalarKind = elementScalarKind;
                    loweredType.elementType = elementType;
                    loweredType.bitWidth = bitWidthForScalarKind(elementScalarKind);
                    loweredType.vectorWidth = *componentCount;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
            }

            const std::optional<EncodedUGLMatrixSpecialization> encodedMatrixSpecialization = parseEncodedUGLMatrixSpecialization(canonicalName);
            if (primaryTemplateName == "UGL::Matrix" || encodedMatrixSpecialization.has_value())
            {
                std::string elementType = ensureType(module, getTemplateTypeArgument(unqualifiedType, 0), location);
                ScalarKind elementScalarKind = scalarKindFromTypeName(elementType);
                std::optional<uint32_t> rowCount = getTemplateUnsignedIntegerArgument(unqualifiedType, 1);
                std::optional<uint32_t> columnCount = getTemplateUnsignedIntegerArgument(unqualifiedType, 2);
                if (elementScalarKind == ScalarKind::None || !rowCount.has_value() || !columnCount.has_value())
                {
                    if (encodedMatrixSpecialization.has_value())
                    {
                        elementScalarKind = encodedMatrixSpecialization->scalarKind;
                        elementType = scalarIRTypeName(elementScalarKind);
                        rowCount = encodedMatrixSpecialization->rows;
                        columnCount = encodedMatrixSpecialization->columns;
                    }
                }
                if (elementScalarKind != ScalarKind::None &&
                    rowCount.has_value() && columnCount.has_value() &&
                    *rowCount >= 2u && *rowCount <= 4u &&
                    *columnCount >= 2u && *columnCount <= 4u)
                {
                    const std::string scalarName = scalarTypeName(elementScalarKind);
                    loweredType.name = scalarName + std::to_string(*columnCount) + "x" + std::to_string(*rowCount);
                    loweredType.kind = TypeKind::Matrix;
                    loweredType.scalarKind = elementScalarKind;
                    loweredType.elementType = elementType;
                    loweredType.bitWidth = bitWidthForScalarKind(elementScalarKind);
                    loweredType.matrixRows = *rowCount;
                    loweredType.matrixColumns = *columnCount;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
            }

            if (const clang::ArrayType *arrayType = mContext.getAsArrayType(unqualifiedType))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Array;
                loweredType.elementType = ensureType(module, arrayType->getElementType(), location);
                if (const auto *constantArrayType = llvm::dyn_cast<clang::ConstantArrayType>(arrayType))
                {
                    loweredType.arrayCount = static_cast<uint32_t>(constantArrayType->getSize().getZExtValue());
                }
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (isSupportedBufferResourceName(canonicalName))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Buffer;
                loweredType.resourceKind = canonicalName == UGLC::CodeGen::mUGLShaderUniformBufferName ? ResourceKind::UniformBuffer : ResourceKind::StorageBuffer;
                loweredType.elementType = ensureType(module, getTemplateTypeArgument(unqualifiedType, 0), location);
                loweredType.accessMode = canonicalName == UGLC::CodeGen::mUGLShaderRWStructuredBufferName ? AccessMode::ReadWrite : AccessMode::Read;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (isSampledTextureResourceName(canonicalName) || isStorageTextureResourceName(canonicalName))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Texture;
                const clang::QualType textureElementType = getTemplateTypeArgument(unqualifiedType, 0);
                const TextureFormat textureFormat = textureFormatFromToken(extractTextureFormatToken(textureElementType.isNull() ? spelledType : typeSpelling(textureElementType)));
                loweredType.resourceKind = isStorageTextureResourceName(canonicalName) ? ResourceKind::StorageTexture : ResourceKind::Texture;
                loweredType.elementType = textureFormat != TextureFormat::Unknown
                                            ? ensureBuiltinTypeName(module, textureFormatValueTypeName(textureFormat), location)
                                            : ensureType(module, textureElementType, location);
                loweredType.accessMode = isStorageTextureResourceName(canonicalName) ? AccessMode::ReadWrite : AccessMode::Read;
                loweredType.textureDimension = textureDimensionForResourceName(canonicalName);
                loweredType.textureFormat = textureFormat;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (isSamplerResourceName(canonicalName))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Sampler;
                loweredType.resourceKind = ResourceKind::Sampler;
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (isGroupSharedName(canonicalName))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Workgroup;
                loweredType.elementType = ensureType(module, getTemplateTypeArgument(unqualifiedType, 0), location);
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            if (isBindGroupHandleName(canonicalName) || isRenderSetHandleName(canonicalName))
            {
                loweredType.name = spelledType;
                loweredType.kind = TypeKind::Resource;
                loweredType.elementType = ensureType(module, getTemplateTypeArgument(unqualifiedType, 0), location);
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            const clang::CXXRecordDecl *rawRecordDecl = unqualifiedType->getAsCXXRecordDecl();
            const clang::CXXRecordDecl *recordDecl = recordDefinition(rawRecordDecl);
            if (recordDecl != nullptr)
            {
                const std::string recordSymbolName = makeUGLIRRecordSymbolName(*(rawRecordDecl == nullptr ? recordDecl : rawRecordDecl));
                const TypeRole recordRole = typeRoleFromSourceTypeName(canonicalName, spelledType, recordSymbolName);
                if (recordRole != TypeRole::Value ||
                    isDerivedFromUGLBase(recordDecl, UGLC::CodeGen::mUGLRenderSetBaseName))
                {
                    loweredType.role = recordRole == TypeRole::Value ? TypeRole::ImplementationOnly : recordRole;
                    loweredType.isImplementationOnly = true;
                }
                if (parseVectorAlias(recordSymbolName, vectorElementType, vectorWidth))
                {
                    loweredType.name = startsWith(recordSymbolName, "UGL::") ? recordSymbolName.substr(5) : recordSymbolName;
                    loweredType.kind = TypeKind::Vector;
                    loweredType.scalarKind = scalarKindFromTypeName(vectorElementType);
                    loweredType.elementType = vectorElementType;
                    loweredType.bitWidth = bitWidthForScalarKind(loweredType.scalarKind);
                    loweredType.vectorWidth = vectorWidth;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
                if (parseMatrixTemplateSpelling(recordSymbolName, matrixAlias))
                {
                    loweredType.name = matrixAlias.canonicalName;
                    loweredType.kind = TypeKind::Matrix;
                    loweredType.scalarKind = matrixAlias.scalarKind;
                    loweredType.elementType = matrixAlias.elementType;
                    loweredType.bitWidth = bitWidthForScalarKind(matrixAlias.scalarKind);
                    loweredType.matrixRows = matrixAlias.rows;
                    loweredType.matrixColumns = matrixAlias.columns;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
                if (const std::optional<EncodedUGLVectorSpecialization> encodedVector = parseEncodedUGLVectorSpecialization(recordSymbolName))
                {
                    const std::string elementType = scalarIRTypeName(encodedVector->scalarKind);
                    loweredType.name = makeVectorTypeName(encodedVector->scalarKind, encodedVector->width);
                    loweredType.kind = TypeKind::Vector;
                    loweredType.scalarKind = encodedVector->scalarKind;
                    loweredType.elementType = elementType;
                    loweredType.bitWidth = bitWidthForScalarKind(encodedVector->scalarKind);
                    loweredType.vectorWidth = encodedVector->width;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
                if (const std::optional<EncodedUGLMatrixSpecialization> encodedMatrix = parseEncodedUGLMatrixSpecialization(recordSymbolName))
                {
                    const std::string elementType = scalarIRTypeName(encodedMatrix->scalarKind);
                    const std::string scalarName = scalarTypeName(encodedMatrix->scalarKind);
                    loweredType.name = scalarName + std::to_string(encodedMatrix->columns) + "x" + std::to_string(encodedMatrix->rows);
                    loweredType.kind = TypeKind::Matrix;
                    loweredType.scalarKind = encodedMatrix->scalarKind;
                    loweredType.elementType = elementType;
                    loweredType.bitWidth = bitWidthForScalarKind(encodedMatrix->scalarKind);
                    loweredType.matrixRows = encodedMatrix->rows;
                    loweredType.matrixColumns = encodedMatrix->columns;
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }

                loweredType.name = recordSymbolName;
                loweredType.kind = TypeKind::Struct;
                if (canonicalName == "UGL::Color" && loweredType.isImplementationOnly)
                {
                    // Clear-color metadata belongs to the host and has no shader value fields.
                    addTypeIfMissing(module, loweredType);
                    return loweredType.name;
                }
                const std::unordered_map<std::string, clang::QualType> recordTypeArguments = collectClassTypeTemplateArguments(recordDecl);
                uint32_t shaderFieldOffset = 0u;
                const std::vector<const clang::FieldDecl *> recordFields = collectRecordFields(recordDecl);
                for (uint32_t fieldIndex = 0; fieldIndex < recordFields.size(); ++fieldIndex)
                {
                    const clang::FieldDecl *fieldDecl = recordFields[fieldIndex];
                    BuiltinSemantic semantic;
                    uint32_t fieldLocation = 0;
                    for (const std::string &rawAttribute : getAnnotationStrings(fieldDecl))
                    {
                        semantic = builtinSemanticFromToken(rawAttribute);
                        if (semantic.kind == BuiltinSemanticKind::Attribute)
                        {
                            fieldLocation = semantic.index;
                        }
                        break;
                    }
                    if (semantic.kind == BuiltinSemanticKind::Position)
                    {
                        fieldLocation = 0;
                    }
                    const clang::QualType fieldType = resolveClassTypeTemplateArgument(recordTypeArguments, fieldDecl->getType());
                    std::string fieldCanonicalName = canonicalRecordName(fieldType);
                    if (fieldCanonicalName.empty())
                    {
                        fieldCanonicalName = framebufferAttachmentNameFromDependentSpelling(typeSpelling(fieldType));
                    }
                    std::string fieldTypeName;
                    if (isFramebufferAttachmentName(fieldCanonicalName))
                    {
                        const clang::QualType elementType = resolveTemplateTypeArgument(recordTypeArguments, fieldType, 0);
                        const TextureFormat textureFormat = textureFormatFromToken(extractTextureFormatToken(elementType.isNull() ? typeSpelling(fieldType) : typeSpelling(elementType)));
                        fieldTypeName = ensureBuiltinTypeName(module,
                                                              framebufferTextureFormatValueTypeName(textureFormat),
                                                              fieldDecl->getLocation());
                        if (fieldCanonicalName == UGLC::CodeGen::mUGLDepthAttachmentName)
                        {
                            semantic = BuiltinSemantic{.kind = BuiltinSemanticKind::Depth};
                        }
                        else if (fieldCanonicalName == UGLC::CodeGen::mUGLPixelLocalDepthAttachmentName)
                        {
                            semantic = BuiltinSemantic{.kind = BuiltinSemanticKind::PixelLocalDepth};
                        }
                        else if (semantic.kind == BuiltinSemanticKind::None)
                        {
                            semantic = BuiltinSemantic{
                                .kind = fieldCanonicalName == UGLC::CodeGen::mUGLPixelLocalColorAttachmentName
                                            ? BuiltinSemanticKind::PixelLocalColor
                                            : BuiltinSemanticKind::Color,
                            };
                        }
                    }
                    const std::string loweredFieldTypeName = fieldTypeName.empty() ? ensureType(module, fieldType, fieldDecl->getLocation()) : fieldTypeName;
                    const auto loweredFieldTypeIter = std::find_if(module.types.begin(), module.types.end(), [&loweredFieldTypeName](const Type &type) {
                        return type.name == loweredFieldTypeName;
                    });
                    if (loweredFieldTypeIter != module.types.end() &&
                        (loweredFieldTypeIter->kind == TypeKind::Resource ||
                         loweredFieldTypeIter->kind == TypeKind::Buffer ||
                         loweredFieldTypeIter->kind == TypeKind::Texture ||
                         loweredFieldTypeIter->kind == TypeKind::Sampler))
                    {
                        continue;
                    }
                    const uint32_t fieldOffset = shaderFieldOffset;
                    shaderFieldOffset += shaderStorageByteSize(module, loweredFieldTypeName);
                    loweredType.fields.push_back(TypeField{
                        .name = fieldDecl->getNameAsString(),
                        .type = loweredFieldTypeName,
                        .semantic = semanticDisplayName(semantic.kind, semantic.index),
                        .semanticKind = semantic.kind,
                        .semanticIndex = semantic.index,
                        .location = fieldLocation,
                        .offset = fieldOffset,
                        .sourceLocation = makeSourceLocation(fieldDecl->getLocation()),
                    });
                }
                addTypeIfMissing(module, loweredType);
                return loweredType.name;
            }

            loweredType.name = spelledType;
            loweredType.kind = TypeKind::Struct;
            addTypeIfMissing(module, loweredType);
            return loweredType.name;
        }

        std::string UGLIRLowerer::ensureBuiltinTypeName(Module &module, const std::string &typeName, clang::SourceLocation location)
        {
            if (typeName == "void")
            {
                addTypeIfMissing(module, Type{.name = "void", .kind = TypeKind::Void, .sourceLocation = makeSourceLocation(location)});
                return "void";
            }
            if (typeName == "bool")
            {
                addTypeIfMissing(module, Type{.name = "bool", .kind = TypeKind::Bool, .scalarKind = ScalarKind::Bool, .bitWidth = 1, .sourceLocation = makeSourceLocation(location)});
                return "bool";
            }
            if (typeName == "int" || typeName == "i32")
            {
                addTypeIfMissing(module, Type{.name = "i32", .kind = TypeKind::Int, .scalarKind = ScalarKind::Int, .bitWidth = 32, .sourceLocation = makeSourceLocation(location)});
                return "i32";
            }
            if (typeName == "uint" || typeName == "u32")
            {
                addTypeIfMissing(module, Type{.name = "u32", .kind = TypeKind::UInt, .scalarKind = ScalarKind::UInt, .bitWidth = 32, .sourceLocation = makeSourceLocation(location)});
                return "u32";
            }
            if (typeName == "float" || typeName == "f32")
            {
                addTypeIfMissing(module, Type{.name = "f32", .kind = TypeKind::Float, .scalarKind = ScalarKind::Float, .bitWidth = 32, .sourceLocation = makeSourceLocation(location)});
                return "f32";
            }
            if (typeName == "half" || typeName == "f16")
            {
                addTypeIfMissing(module, Type{.name = "f16", .kind = TypeKind::Half, .scalarKind = ScalarKind::Half, .bitWidth = 16, .sourceLocation = makeSourceLocation(location)});
                return "f16";
            }

            std::string elementType;
            uint32_t width = 0;
            if (parseVectorAlias(typeName, elementType, width))
            {
                Type loweredType;
                loweredType.name = typeName;
                loweredType.kind = TypeKind::Vector;
                loweredType.scalarKind = scalarKindFromTypeName(elementType);
                loweredType.bitWidth = loweredType.scalarKind == ScalarKind::Half ? 16u : loweredType.scalarKind == ScalarKind::Bool ? 1u : 32u;
                loweredType.elementType = elementType;
                loweredType.vectorWidth = width;
                loweredType.sourceLocation = makeSourceLocation(location);
                addTypeIfMissing(module, loweredType);
                return typeName;
            }

            Type loweredType;
            loweredType.name = typeName;
            loweredType.kind = TypeKind::Struct;
            loweredType.sourceLocation = makeSourceLocation(location);
            addTypeIfMissing(module, loweredType);
            return typeName;
        }

        std::string UGLIRLowerer::ensureFunctionTypeName(FunctionLoweringState &state, const clang::QualType &type, clang::SourceLocation location)
        {
            if (state.module == nullptr)
            {
                return "void";
            }
            const clang::QualType resolvedType = resolveClassTypeTemplateArgument(state.typeTemplateQualTypesByName, type);
            std::string typeName = ensureType(*state.module, resolvedType, location);
            if (const auto typeArgumentIter = state.typeTemplateArgumentsByName.find(typeName); typeArgumentIter != state.typeTemplateArgumentsByName.end())
            {
                typeName = typeArgumentIter->second;
            }
            if (typeName == "int" || typeName == "uint" || typeName == "float" || typeName == "half")
            {
                return ensureBuiltinTypeName(*state.module, typeName, location);
            }
            return typeName;
        }

        clang::QualType UGLIRLowerer::shaderParameterType(const clang::ParmVarDecl *paramDecl) const
        {
            if (paramDecl == nullptr)
            {
                return {};
            }

            if (const clang::TypeSourceInfo *sourceInfo = paramDecl->getTypeSourceInfo())
            {
                clang::QualType sourceType = sourceInfo->getType();
                if (const auto *decayedType = sourceType->getAs<clang::DecayedType>())
                {
                    sourceType = decayedType->getOriginalType();
                }
                if (const clang::ArrayType *arrayType = mContext.getAsArrayType(sourceType.getNonReferenceType());
                    arrayType != nullptr && llvm::isa<clang::ConstantArrayType>(arrayType))
                {
                    return sourceType;
                }
            }

            clang::QualType originalType = paramDecl->getOriginalType();
            if (!originalType.isNull())
            {
                if (const auto *decayedType = originalType->getAs<clang::DecayedType>())
                {
                    originalType = decayedType->getOriginalType();
                }
                if (const clang::ArrayType *arrayType = mContext.getAsArrayType(originalType.getNonReferenceType());
                    arrayType != nullptr && llvm::isa<clang::ConstantArrayType>(arrayType))
                {
                    return originalType;
                }
            }

            return paramDecl->getType();
        }

        void UGLIRLowerer::normalizeConstructInfo(FunctionLoweringState &state, Expression &expression, clang::SourceLocation location)
        {
            expression.constructInfo = {};
            if (state.module == nullptr || expression.kind != ExpressionKind::Construct)
            {
                return;
            }

            const Type *targetType = findLoweredTypeByName(*state.module, expression.type);
            if (targetType != nullptr && targetType->kind == TypeKind::Struct)
            {
                expression.constructInfo.kind = ConstructKind::Aggregate;
                return;
            }

            ScalarKind targetScalarKind = ScalarKind::None;
            uint32_t targetComponentCount = 0;
            if (!describeConstructValueShape(*state.module, expression.type, targetScalarKind, targetComponentCount))
            {
                return;
            }

            expression.constructInfo.targetScalarKind = targetScalarKind;
            expression.constructInfo.targetComponentCount = targetComponentCount;

            if (expression.operands.empty())
            {
                return;
            }

            if (targetComponentCount <= 1u)
            {
                if (expression.operands.size() != 1u)
                {
                    addDiagnostic(location, "scalar construction received more than one operand.");
                    return;
                }
                ScalarKind sourceScalarKind = ScalarKind::None;
                uint32_t sourceComponentCount = 0;
                (void)describeConstructValueShape(*state.module, expression.operands.front().type, sourceScalarKind, sourceComponentCount);
                expression.constructInfo.kind = ConstructKind::ScalarConvert;
                expression.constructInfo.components.push_back(ConstructComponent{
                    .operandIndex = 0,
                    .sourceComponentIndex = 0,
                    .sourceScalarKind = sourceScalarKind,
                    .targetScalarKind = targetScalarKind,
                    .sourceIsVector = sourceComponentCount > 1u,
                });
                return;
            }

            if (expression.operands.size() == 1u)
            {
                ScalarKind sourceScalarKind = ScalarKind::None;
                uint32_t sourceComponentCount = 0;
                if (!describeConstructValueShape(*state.module, expression.operands.front().type, sourceScalarKind, sourceComponentCount))
                {
                    addDiagnostic(location, "vector construction received an operand with unsupported value type \"" + expression.operands.front().type + "\".");
                    return;
                }
                if (sourceComponentCount <= 1u)
                {
                    expression.constructInfo.kind = ConstructKind::VectorSplat;
                    expression.constructInfo.components.push_back(ConstructComponent{
                        .operandIndex = 0,
                        .sourceComponentIndex = 0,
                        .sourceScalarKind = sourceScalarKind,
                        .targetScalarKind = targetScalarKind,
                        .sourceIsVector = false,
                    });
                    return;
                }
            }

            expression.constructInfo.kind = ConstructKind::VectorFromComponents;
            uint32_t emittedComponentCount = 0;
            for (uint32_t operandIndex = 0; operandIndex < expression.operands.size(); ++operandIndex)
            {
                const Expression &operand = expression.operands[operandIndex];
                ScalarKind sourceScalarKind = ScalarKind::None;
                uint32_t sourceComponentCount = 0;
                if (!describeConstructValueShape(*state.module, operand.type, sourceScalarKind, sourceComponentCount))
                {
                    addDiagnostic(location, "vector construction received an operand with unsupported value type \"" + operand.type + "\".");
                    return;
                }
                for (uint32_t sourceComponentIndex = 0; sourceComponentIndex < sourceComponentCount; ++sourceComponentIndex)
                {
                    expression.constructInfo.components.push_back(ConstructComponent{
                        .operandIndex = operandIndex,
                        .sourceComponentIndex = sourceComponentIndex,
                        .sourceScalarKind = sourceScalarKind,
                        .targetScalarKind = targetScalarKind,
                        .sourceIsVector = sourceComponentCount > 1u,
                    });
                    ++emittedComponentCount;
                }
            }

            if (emittedComponentCount != targetComponentCount)
            {
                addDiagnostic(location, "vector construction received " + std::to_string(emittedComponentCount) +
                                            " scalar components for target type \"" + expression.type + "\".");
            }
        }

        bool UGLIRLowerer::isWorkgroupTypeName(const Module &module, const std::string &typeName) const
        {
            const auto typeIter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
                return type.name == typeName;
            });
            if (typeIter == module.types.end())
            {
                return false;
            }
            if (typeIter->kind == TypeKind::Workgroup)
            {
                return true;
            }
            if (typeIter->kind == TypeKind::Array)
            {
                return isWorkgroupTypeName(module, typeIter->elementType);
            }
            return false;
        }

        uint32_t UGLIRLowerer::shaderStorageByteSize(const Module &module, const std::string &typeName) const
        {
            if (typeName == "void")
            {
                return 0u;
            }

            if (const uint32_t knownValueSize = byteSize(module, typeName); knownValueSize != 0u)
            {
                return knownValueSize;
            }

            const auto typeIter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
                return type.name == typeName;
            });
            if (typeIter == module.types.end())
            {
                return 4u;
            }

            const Type &type = *typeIter;
            if (type.kind == TypeKind::Workgroup)
            {
                return shaderStorageByteSize(module, type.elementType);
            }
            if (type.kind == TypeKind::Array)
            {
                return type.arrayCount * shaderStorageByteSize(module, type.elementType);
            }
            if (type.kind == TypeKind::Vector)
            {
                return type.vectorWidth * shaderStorageByteSize(module, type.elementType);
            }
            if (type.kind == TypeKind::Matrix)
            {
                return type.matrixColumns * type.matrixRows * shaderStorageByteSize(module, type.elementType);
            }
            if (type.kind == TypeKind::Struct)
            {
                uint32_t byteSize = 0u;
                for (const TypeField &field : type.fields)
                {
                    byteSize = std::max<uint32_t>(byteSize, field.offset + shaderStorageByteSize(module, field.type));
                }
                return byteSize;
            }
            if (type.kind == TypeKind::Half)
            {
                return 2u;
            }
            if (type.kind == TypeKind::Bool || type.kind == TypeKind::Int || type.kind == TypeKind::UInt || type.kind == TypeKind::Float)
            {
                return 4u;
            }
            return 4u;
        }

        bool UGLIRLowerer::isResourceLikeHelperParameterType(const Module &module, const std::string &typeName) const
        {
            if (typeName == "resource_selector")
            {
                return true;
            }
            const auto typeIter = std::find_if(module.types.begin(), module.types.end(), [&typeName](const Type &type) {
                return type.name == typeName;
            });
            if (typeIter != module.types.end())
            {
                return typeIter->kind == TypeKind::Resource ||
                       typeIter->kind == TypeKind::Buffer ||
                       typeIter->kind == TypeKind::Texture ||
                       typeIter->kind == TypeKind::Sampler;
            }
            return false;
        }

        std::optional<std::string> UGLIRLowerer::collectErasedResourceSelectorPath(FunctionLoweringState &state, const clang::Expr *expr) const
        {
            if (expr == nullptr)
            {
                return std::nullopt;
            }

            const clang::Expr *current = expr->IgnoreParenImpCasts();
            if (const auto *operatorCall = llvm::dyn_cast<clang::CXXOperatorCallExpr>(current);
                operatorCall != nullptr && operatorCall->getOperator() == clang::OO_Arrow && operatorCall->getNumArgs() == 1)
            {
                return collectErasedResourceSelectorPath(state, operatorCall->getArg(0));
            }

            if (llvm::isa<clang::CXXThisExpr>(current))
            {
                return std::string();
            }

            if (const auto *declRef = llvm::dyn_cast<clang::DeclRefExpr>(current))
            {
                const clang::ValueDecl *valueDecl = declRef->getDecl();
                const auto substitutionIter = state.valueSubstitutions.find(valueDecl);
                if (substitutionIter != state.valueSubstitutions.end())
                {
                    return resourceSpecializationKeyForExpression(substitutionIter->second);
                }
                return valueDecl == nullptr ? std::optional<std::string>() : std::optional<std::string>(valueDecl->getNameAsString());
            }

            if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(current))
            {
                std::optional<std::string> basePath = collectErasedResourceSelectorPath(state, memberExpr->getBase());
                if (!basePath.has_value())
                {
                    return std::nullopt;
                }
                const std::string memberName = memberExpr->getMemberNameInfo().getAsString();
                if (memberName.empty())
                {
                    return basePath;
                }
                if (basePath->empty())
                {
                    return memberName;
                }
                return *basePath + "." + memberName;
            }

            if (const auto *dependentMemberExpr = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(current))
            {
                std::optional<std::string> basePath = collectErasedResourceSelectorPath(state, dependentMemberExpr->getBase());
                if (!basePath.has_value())
                {
                    return std::nullopt;
                }
                const std::string memberName = dependentMemberExpr->getMemberNameInfo().getAsString();
                if (memberName.empty())
                {
                    return basePath;
                }
                if (basePath->empty())
                {
                    return memberName;
                }
                return *basePath + "." + memberName;
            }

            return std::nullopt;
        }

        Expression UGLIRLowerer::makeErasedResourceSelectorExpression(const std::string &resourcePath, clang::SourceLocation location) const
        {
            Expression result;
            result.kind = ExpressionKind::DeclRef;
            result.type = "resource_selector";
            result.name = resourcePath;
            result.sourceLocation = makeSourceLocation(location);
            return result;
        }

        void UGLIRLowerer::collectResourceSpecializationCaptures(const Module &module, const Expression &expression, std::vector<Expression> &captures) const
        {
            if (expression.kind == ExpressionKind::DeclRef &&
                !expression.name.empty() &&
                expression.name != "this" &&
                !isResourceLikeHelperParameterType(module, expression.type))
            {
                const auto existing = std::find_if(captures.begin(), captures.end(), [&expression](const Expression &capture) {
                    return capture.name == expression.name && capture.type == expression.type;
                });
                if (existing == captures.end())
                {
                    captures.push_back(expression);
                }
                return;
            }

            for (const Expression &operand : expression.operands)
            {
                collectResourceSpecializationCaptures(module, operand, captures);
            }
        }

        std::vector<Expression> UGLIRLowerer::getResourceSpecializationExtraCaptures(
            const clang::FunctionDecl *functionDecl,
            const std::unordered_map<const clang::ValueDecl *, Expression> &valueSubstitutions,
            const std::vector<Expression> &captures) const
        {
            std::unordered_set<std::string> existingParameterNames;
            if (functionDecl != nullptr)
            {
                for (const clang::ParmVarDecl *paramDecl : functionDecl->parameters())
                {
                    if (valueSubstitutions.find(paramDecl) != valueSubstitutions.end())
                    {
                        continue;
                    }
                    existingParameterNames.insert(paramDecl->getNameAsString());
                }
            }

            std::vector<Expression> extraCaptures;
            extraCaptures.reserve(captures.size());
            for (const Expression &capture : captures)
            {
                if (!capture.name.empty() && existingParameterNames.insert(capture.name).second)
                {
                    extraCaptures.push_back(capture);
                }
            }
            return extraCaptures;
        }

        void UGLIRLowerer::appendResourceSpecializationCaptureParameters(Function &function, const std::vector<Expression> &captures) const
        {
            std::unordered_set<std::string> existingParameterNames;
            for (const FunctionParameter &parameter : function.parameters)
            {
                existingParameterNames.insert(parameter.name);
            }

            for (const Expression &capture : captures)
            {
                if (capture.name.empty() || !existingParameterNames.insert(capture.name).second)
                {
                    continue;
                }
                function.parameters.push_back(FunctionParameter{
                    .name = capture.name,
                    .type = capture.type,
                    .semantic = {},
                    .isReference = false,
                    .isConstReference = false,
                    .sourceLocation = capture.sourceLocation,
                });
            }
        }

        std::string UGLIRLowerer::resourceSpecializationKeyForExpression(const Expression &expression) const
        {
            if (expression.kind == ExpressionKind::ThisRef)
            {
                return "this";
            }
            if ((expression.kind == ExpressionKind::Construct ||
                 expression.kind == ExpressionKind::Cast ||
                 expression.kind == ExpressionKind::Load) &&
                !expression.operands.empty())
            {
                return resourceSpecializationKeyForExpression(expression.operands.front());
            }
            if (expression.kind == ExpressionKind::MemberRef)
            {
                if (!expression.operands.empty())
                {
                    const std::string ownerKey = resourceSpecializationKeyForExpression(expression.operands.front());
                    if (!ownerKey.empty() && ownerKey != "this")
                    {
                        return ownerKey + "." + expression.name;
                    }
                }
                return expression.name.empty() ? expression.type : expression.name;
            }
            if (expression.kind == ExpressionKind::DeclRef)
            {
                return expression.name.empty() ? expression.type : expression.name;
            }
            if (expression.kind == ExpressionKind::Subscript && !expression.operands.empty())
            {
                std::string key = resourceSpecializationKeyForExpression(expression.operands.front());
                for (size_t index = 1; index < expression.operands.size(); ++index)
                {
                    key += "_index_" + resourceSpecializationKeyForExpression(expression.operands[index]);
                }
                return key.empty() ? expression.type : key;
            }
            if (expression.kind == ExpressionKind::Call)
            {
                std::string key = expression.name.empty() ? expression.type : expression.name;
                for (const Expression &operand : expression.operands)
                {
                    key += "_" + resourceSpecializationKeyForExpression(operand);
                }
                return key;
            }
            if (!expression.name.empty())
            {
                return expression.name;
            }
            return expression.type;
        }

        std::string UGLIRLowerer::makeResourceSpecializedHelperName(const clang::FunctionDecl *functionDecl, const std::vector<Expression> &resourceArguments) const
        {
            std::ostringstream stream;
            stream << getFunctionName(functionDecl) << "__R";
            for (const Expression &argument : resourceArguments)
            {
                stream << "_" << makeUGLIRDebugArtifactStem(resourceSpecializationKeyForExpression(argument));
            }
            return stream.str();
        }

        std::string UGLIRLowerer::makeThisResourceSpecializedHelperName(const clang::FunctionDecl *functionDecl, const std::unordered_map<std::string, Expression> &fieldSubstitutions) const
        {
            std::vector<std::string> fieldNames;
            fieldNames.reserve(fieldSubstitutions.size());
            for (const auto &entry : fieldSubstitutions)
            {
                fieldNames.push_back(entry.first);
            }
            std::sort(fieldNames.begin(), fieldNames.end());

            std::ostringstream stream;
            stream << getFunctionName(functionDecl) << "__ThisR";
            for (const std::string &fieldName : fieldNames)
            {
                const auto iter = fieldSubstitutions.find(fieldName);
                stream << "_" << makeUGLIRDebugArtifactStem(fieldName) << "_" << makeUGLIRDebugArtifactStem(resourceSpecializationKeyForExpression(iter->second));
            }
            return stream.str();
        }

        std::vector<std::string> UGLIRLowerer::collectResourceLikeFieldNames(FunctionLoweringState &state, const clang::QualType &type)
        {
            std::vector<std::string> fieldNames;
            if (state.module == nullptr)
            {
                return fieldNames;
            }
            const clang::QualType resolvedType = resolveClassTypeTemplateArgument(state.typeTemplateQualTypesByName, type);
            const clang::CXXRecordDecl *recordDecl = recordDefinition(resolvedType.getNonReferenceType()->getAsCXXRecordDecl());
            if (recordDecl == nullptr)
            {
                return fieldNames;
            }
            const std::unordered_map<std::string, clang::QualType> recordTypeArguments = collectClassTypeTemplateArguments(recordDecl);
            for (const clang::FieldDecl *fieldDecl : collectRecordFields(recordDecl))
            {
                const clang::QualType fieldType = resolveClassTypeTemplateArgument(recordTypeArguments, fieldDecl->getType());
                const std::string fieldTypeName = ensureFunctionTypeName(state, fieldType, fieldDecl->getLocation());
                if (isResourceLikeHelperParameterType(*state.module, fieldTypeName))
                {
                    fieldNames.push_back(fieldDecl->getNameAsString());
                }
            }
            return fieldNames;
        }

        void UGLIRLowerer::addTypeIfMissing(Module &module, Type type)
        {
            if (type.name == "int" || type.name == "uint" || type.name == "float" || type.name == "half")
            {
                if (type.name == "int")
                {
                    type.name = "i32";
                    type.kind = TypeKind::Int;
                    type.scalarKind = ScalarKind::Int;
                    type.bitWidth = 32;
                }
                else if (type.name == "uint")
                {
                    type.name = "u32";
                    type.kind = TypeKind::UInt;
                    type.scalarKind = ScalarKind::UInt;
                    type.bitWidth = 32;
                }
                else if (type.name == "float")
                {
                    type.name = "f32";
                    type.kind = TypeKind::Float;
                    type.scalarKind = ScalarKind::Float;
                    type.bitWidth = 32;
                }
                else
                {
                    type.name = "f16";
                    type.kind = TypeKind::Half;
                    type.scalarKind = ScalarKind::Half;
                    type.bitWidth = 16;
                }
            }
            const auto iter = std::find_if(module.types.begin(), module.types.end(), [&type](const Type &existingType) {
                return existingType.name == type.name;
            });
            if (iter == module.types.end())
            {
                module.types.push_back(std::move(type));
                return;
            }
            if (iter->kind == TypeKind::Struct && iter->fields.empty() && !type.fields.empty())
            {
                *iter = std::move(type);
            }
        }

        Function UGLIRLowerer::lowerFunction(Module &module,
                                             const clang::FunctionDecl *functionDecl,
                                             bool isEntryPoint,
                                             ShaderStage stage,
                                             ShaderEntryKind entryKind,
                                             const std::array<uint32_t, 3> &workgroupSize,
                                             const std::string &overrideName,
                                             const std::unordered_map<const clang::ValueDecl *, Expression> &valueSubstitutions,
                                             const std::unordered_map<std::string, Expression> &thisFieldSubstitutions,
                                             bool zeroInitializeConstructor)
        {
            Function function;
            function.name = overrideName.empty() ? getFunctionName(functionDecl) : overrideName;
            function.linkage = isEntryPoint ? FunctionLinkage::Exported : FunctionLinkage::Internal;
            function.stage = stage;
            function.entryKind = entryKind;
            function.isEntryPoint = isEntryPoint;
            function.workgroupSize = workgroupSize;
            function.sourceLocation = makeSourceLocation(functionDecl->getLocation());

            FunctionLoweringState state;
            state.module = &module;
            state.functionDecl = functionDecl;
            state.ownerFunctionSymbol = function.name;
            state.valueSubstitutions = valueSubstitutions;
            state.thisFieldSubstitutions = thisFieldSubstitutions;
            if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(functionDecl))
            {
                if (const auto *specializationDecl = llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(methodDecl->getParent()))
                {
                    if (const clang::ClassTemplateDecl *templateDecl = specializationDecl->getSpecializedTemplate())
                    {
                        const clang::TemplateParameterList *templateParameters = templateDecl->getTemplateParameters();
                        const clang::TemplateArgumentList &templateArguments = specializationDecl->getTemplateArgs();
                        const unsigned argumentCount = std::min(templateParameters->size(), templateArguments.size());
                        for (unsigned index = 0; index < argumentCount; ++index)
                        {
                            if (const auto *typeParameter = llvm::dyn_cast_or_null<clang::TemplateTypeParmDecl>(templateParameters->getParam(index)))
                            {
                                const clang::TemplateArgument &argument = templateArguments[index];
                                if (argument.getKind() == clang::TemplateArgument::Type)
                                {
                                    state.typeTemplateQualTypesByName[typeParameter->getNameAsString()] = argument.getAsType();
                                    state.typeTemplateArgumentsByName[typeParameter->getNameAsString()] = ensureType(module, argument.getAsType(), typeParameter->getLocation());
                                }
                                continue;
                            }
                            const auto *nonTypeParameter = llvm::dyn_cast_or_null<clang::NonTypeTemplateParmDecl>(templateParameters->getParam(index));
                            const clang::TemplateArgument &argument = templateArguments[index];
                            if (nonTypeParameter == nullptr || argument.getKind() != clang::TemplateArgument::Integral)
                            {
                                continue;
                            }
                            llvm::SmallString<32> valueText;
                            argument.getAsIntegral().toString(valueText, 10);
                            state.integralTemplateArgumentsByName[nonTypeParameter->getNameAsString()] = valueText.str().str();
                        }
                    }
                }
            }
            const auto *constructor = llvm::dyn_cast<clang::CXXConstructorDecl>(functionDecl);
            function.returnType = constructor == nullptr
                ? ensureFunctionTypeName(state, functionDecl->getReturnType(), functionDecl->getLocation())
                : ensureType(module, mContext.getRecordType(constructor->getParent()), constructor->getLocation());
            if (constructor != nullptr)
            {
                state.constructorResultName = "__UGL__constructedValue";
                state.allocatedLocalNames.insert(state.constructorResultName);
                state.localTypeNamesByName[state.constructorResultName] = function.returnType;
            }

            if (constructor == nullptr && !isEntryPoint && thisFieldSubstitutions.empty())
            {
                if (const auto *methodDecl = llvm::dyn_cast<clang::CXXMethodDecl>(functionDecl))
                {
                    if (!methodDecl->isStatic())
                    {
                        const clang::CXXRecordDecl *parentDecl = recordDefinition(methodDecl->getParent());
                        if (parentDecl != nullptr && !collectRecordFields(parentDecl).empty())
                        {
                            const std::string thisType = ensureType(module, mContext.getRecordType(parentDecl), methodDecl->getLocation());
                            state.allocatedLocalNames.insert("this");
                            state.localTypeNamesByName["this"] = thisType;
                            function.parameters.push_back(FunctionParameter{
                                .name = "this",
                                .type = thisType,
                                .semantic = {},
                                .passingMode = ParameterPassingMode::Value,
                                .isReference = true,
                                .isConstReference = methodDecl->isConst(),
                                .sourceLocation = makeSourceLocation(methodDecl->getLocation()),
                            });
                        }
                    }
                }
            }

            for (const clang::ParmVarDecl *paramDecl : functionDecl->parameters())
            {
                if (state.valueSubstitutions.find(paramDecl) != state.valueSubstitutions.end())
                {
                    continue;
                }
                const std::string paramName = state.allocateLocalName(paramDecl->getNameAsString());
                state.localNames[paramDecl] = paramName;
                const clang::QualType parameterType = shaderParameterType(paramDecl);
                std::string paramType = ensureFunctionTypeName(state, parameterType, paramDecl->getLocation());
                state.localTypeNamesByName[paramName] = paramType;
                std::string semantic;
                if (!isEntryPoint)
                {
                    for (const std::string &rawAttribute : getAnnotationStrings(paramDecl))
                    {
                        semantic = rawAttribute;
                        break;
                    }
                }
                const bool isArrayParameter = findLoweredTypeByName(module, paramType) != nullptr && findLoweredTypeByName(module, paramType)->kind == TypeKind::Array;
                const bool isClangReference = parameterType->isReferenceType();
                BuiltinSemantic builtinSemantic;
                ParameterPassingMode passingMode = ParameterPassingMode::Value;
                if (isEntryPoint)
                {
                    builtinSemantic = resolveEntryParameterSemantic(paramDecl, entryKind);
                    semantic = semanticDisplayName(builtinSemantic.kind, builtinSemantic.index);
                }
                else
                {
                    passingMode = parameterPassingModeFromToken(semantic);
                    if (passingMode == ParameterPassingMode::Value)
                    {
                        builtinSemantic = builtinSemanticFromToken(semantic);
                    }
                    else
                    {
                        semantic.clear();
                    }
                }
                const bool isInputAttribute = passingMode == ParameterPassingMode::In;
                const bool isOutputAttribute = passingMode == ParameterPassingMode::Out || passingMode == ParameterPassingMode::InOut;
                const bool isReference = isClangReference || isArrayParameter || isInputAttribute || isOutputAttribute;
                function.parameters.push_back(FunctionParameter{
                    .name = paramName,
                    .type = paramType,
                    .semantic = semantic,
                    .semanticKind = builtinSemantic.kind,
                    .semanticIndex = builtinSemantic.index,
                    .passingMode = passingMode,
                    .isReference = isReference,
                    .isConstReference = isReference && !isOutputAttribute &&
                                        (isInputAttribute || parameterType.getNonReferenceType().isConstQualified()),
                    .sourceLocation = makeSourceLocation(paramDecl->getLocation()),
                });
            }

            const clang::FunctionDecl *bodyDefinition = nullptr;
            const clang::Stmt *body = functionDecl->getBody(bodyDefinition);
            if (body == nullptr)
            {
                addDiagnostic(functionDecl->getLocation(), "shader function has no concrete definition after semantic preparation.");
            }
            if (constructor != nullptr)
            {
                Statement storage;
                storage.kind = StatementKind::VariableDeclaration;
                storage.name = state.constructorResultName;
                storage.type = function.returnType;
                storage.sourceLocation = function.sourceLocation;
                if (zeroInitializeConstructor)
                {
                    Expression initialValue;
                    initialValue.kind = ExpressionKind::Construct;
                    initialValue.type = function.returnType;
                    initialValue.name = function.returnType;
                    initialValue.sourceLocation = function.sourceLocation;
                    normalizeConstructInfo(state, initialValue, constructor->getLocation());
                    storage.expressions.push_back(std::move(initialValue));
                }
                function.body.push_back(std::move(storage));
                Expression receiver;
                receiver.kind = ExpressionKind::DeclRef;
                receiver.type = function.returnType;
                receiver.name = state.constructorResultName;
                receiver.sourceLocation = function.sourceLocation;
                for (const auto *initializer : constructor->inits())
                {
                    if (!initializer->isMemberInitializer() && !initializer->isDelegatingInitializer())
                    {
                        addDiagnostic(initializer->getSourceLocation(), "base-class constructor initialization is not supported in shader value records.");
                        continue;
                    }
                    Expression destination = receiver;
                    if (initializer->isMemberInitializer())
                    {
                        const auto *field = initializer->getMember();
                        destination.kind = ExpressionKind::MemberRef;
                        destination.name = field->getNameAsString();
                        destination.type = ensureFunctionTypeName(state, field->getType(), field->getLocation());
                        destination.operands.push_back(receiver);
                    }
                    appendConstructorInitializer(state, destination, *initializer->getInit(), function.body);
                }
                if (body != nullptr)
                {
                    auto statements = lowerStatement(state, body);
                    function.body.insert(function.body.end(), std::make_move_iterator(statements.begin()), std::make_move_iterator(statements.end()));
                }
                Statement returned;
                returned.kind = StatementKind::Return;
                returned.sourceLocation = function.sourceLocation;
                returned.expressions.push_back(std::move(receiver));
                function.body.push_back(std::move(returned));
            }
            else if (body != nullptr)
                function.body = lowerStatement(state, body);
            function.body = normalizeErasedResourceStatements(module, std::move(function.body));
            return function;
        }

        Expression UGLIRLowerer::lowerConstructExpression(FunctionLoweringState &state,
                                                          const clang::CXXConstructExpr &expression,
                                                          const std::string &constructedType)
        {
            Expression result;
            result.type = constructedType;
            result.sourceLocation = makeSourceLocation(expression.getBeginLoc());
            const auto *constructor = expression.getConstructor();
            if (constructor != nullptr && isUserAuthoredRecordDecl(constructor->getParent()))
            {
                if (constructor->isTrivial() && constructor->isCopyOrMoveConstructor())
                    return lowerExpression(state, expression.getArg(0));
                if (!constructor->isTrivial())
                {
                    result.kind = ExpressionKind::Call;
                    result.name = getFunctionName(constructor);
                    if (expression.requiresZeroInitialization())
                        result.name += "__zero_initialized";
                    if (mSyntheticFunctionNames.insert(result.name).second)
                    {
                        // Clang has resolved initializer order, default members and the body.
                        // Reuse normal function lowering, with a local result receiver.
                        Function factory = lowerFunction(*state.module, constructor, false, ShaderStage::None,
                                                         ShaderEntryKind::None, {1, 1, 1}, result.name, {}, {},
                                                         expression.requiresZeroInitialization());
                        mSyntheticFunctions.push_back(std::move(factory));
                    }
                    for (const auto *argument : expression.arguments())
                        result.operands.push_back(lowerExpression(state, argument));
                    return result;
                }
            }
            result.kind = ExpressionKind::Construct;
            result.name = result.type;
            for (const clang::Expr *arg : expression.arguments())
            {
                result.operands.push_back(lowerFloatingConversionOperand(state, *arg, result.type));
            }
            normalizeConstructInfo(state, result, expression.getBeginLoc());
            return result;
        }

        void UGLIRLowerer::appendConstructorInitializer(FunctionLoweringState &state,
                                                        const Expression &destination,
                                                        const clang::Expr &initializer,
                                                        std::vector<Statement> &statements)
        {
            const clang::Expr *source = initializer.IgnoreParens();
            if (const auto *defaultMember = llvm::dyn_cast<clang::CXXDefaultInitExpr>(source))
            {
                appendConstructorInitializer(state, destination, *defaultMember->getExpr(), statements);
                return;
            }
            const Type *type = findLoweredTypeByName(*state.module, destination.type);
            if (type != nullptr && type->kind == TypeKind::Array)
            {
                // Copy metadata before recursively lowering can grow the module type table.
                const std::string elementType = type->elementType;
                const uint32_t count = type->arrayCount;
                const SourceLocation location = makeSourceLocation(source->getBeginLoc());
                const auto indexType = ensureBuiltinTypeName(*state.module, "u32", source->getBeginLoc());
                (void)ensureBuiltinTypeName(*state.module, "bool", source->getBeginLoc());
                uint32_t firstElement = 0;
                if (const auto *list = llvm::dyn_cast<clang::InitListExpr>(source))
                {
                    for (const auto *element : list->inits())
                    {
                        const Expression target = makeSubscriptExpression(destination,
                            makeUIntLiteralExpression(firstElement++, location), elementType, location);
                        appendConstructorInitializer(state, target, *element, statements);
                    }
                    if (firstElement == count)
                        return;
                    source = list->getArrayFiller();
                }
                if (source == nullptr || (!llvm::isa<clang::CXXConstructExpr>(source) &&
                                          !llvm::isa<clang::ImplicitValueInitExpr>(source)))
                {
                    addDiagnostic(initializer.getBeginLoc(), "array member initialization requires a concrete Clang element initializer.");
                    return;
                }
                const std::string indexName = state.allocateLocalName("__UGL__initializerIndex" + std::to_string(state.nextInitializerOrdinal++));
                state.localTypeNamesByName[indexName] = indexType;
                Expression index;
                index.kind = ExpressionKind::DeclRef;
                index.name = indexName;
                index.type = indexType;
                index.sourceLocation = location;
                Statement loop;
                loop.kind = StatementKind::For;
                loop.sourceLocation = location;
                Statement declaration;
                declaration.kind = StatementKind::VariableDeclaration;
                declaration.name = indexName;
                declaration.type = indexType;
                declaration.sourceLocation = location;
                declaration.expressions.push_back(makeUIntLiteralExpression(firstElement, location));
                loop.children.push_back(std::move(declaration));
                loop.expressions.push_back(makeBinaryExpression("<", index,
                    makeUIntLiteralExpression(count, location), "bool", location));
                Expression increment;
                increment.kind = ExpressionKind::Unary;
                increment.type = indexType;
                increment.operatorName = "++";
                increment.operands.push_back(index);
                increment.sourceLocation = location;
                loop.expressions.push_back(std::move(increment));
                appendConstructorInitializer(state,
                    makeSubscriptExpression(destination, index, elementType, location), *source, loop.children);
                statements.push_back(std::move(loop));
                return;
            }
            Expression value;
            if (const auto *construction = llvm::dyn_cast<clang::CXXConstructExpr>(source))
                value = lowerConstructExpression(state, *construction, destination.type);
            else if (llvm::isa<clang::ImplicitValueInitExpr>(source))
            {
                value.kind = ExpressionKind::Construct;
                value.name = destination.type;
                value.type = destination.type;
                value.sourceLocation = makeSourceLocation(source->getBeginLoc());
                normalizeConstructInfo(state, value, source->getBeginLoc());
            }
            else
                value = lowerExpression(state, source);
            Expression store = makeStoreExpression(destination, std::move(value), destination.sourceLocation);
            store.type = destination.type;
            store.operatorName = "=";
            Statement statement;
            statement.kind = StatementKind::Expression;
            statement.sourceLocation = store.sourceLocation;
            statement.expressions.push_back(std::move(store));
            statements.push_back(std::move(statement));
        }

        std::vector<Statement> UGLIRLowerer::normalizeErasedResourceStatements(Module &module, std::vector<Statement> statements)
        {
            std::vector<Statement> normalizedStatements;
            for (Statement &statement : statements)
            {
                for (Expression &expression : statement.expressions)
                {
                    expression = normalizeErasedResourceExpression(module, std::move(expression));
                }

                statement.children = normalizeErasedResourceStatements(module, std::move(statement.children));
                statement.elseChildren = normalizeErasedResourceStatements(module, std::move(statement.elseChildren));
                for (SwitchCase &switchCase : statement.switchCases)
                {
                    for (Expression &label : switchCase.labels)
                    {
                        label = normalizeErasedResourceExpression(module, std::move(label));
                    }
                    switchCase.body = normalizeErasedResourceStatements(module, std::move(switchCase.body));
                }
                normalizedStatements.push_back(std::move(statement));
            }
            return normalizedStatements;
        }

        Expression UGLIRLowerer::normalizeErasedResourceExpression(Module &module, Expression expression)
        {
            for (Expression &operand : expression.operands)
            {
                operand = normalizeErasedResourceExpression(module, std::move(operand));
            }

            if (expression.kind == ExpressionKind::Construct &&
                expression.operands.size() == 1u &&
                isWorkgroupWrapperTypeName(module, expression.type))
            {
                return std::move(expression.operands.front());
            }

            return expression;
        }

        Expression UGLIRLowerer::expandErasedResourceCallExpression(Module &module, ErasedResourceCallKind callKind, Expression expression)
        {
            const SourceLocation location = expression.sourceLocation;
            const size_t payloadOffset = erasedResourcePayloadOffset(expression);
            const ResourceBinding *selectedResource = findSelectedResource(module, expression);
            const std::string selectedResourceName = payloadOffset == 1u && !expression.operands.empty()
                                                         ? expression.operands.front().name
                                                         : std::string();
            if ((callKind == ErasedResourceCallKind::BufferGet ||
                 callKind == ErasedResourceCallKind::BufferGetRaw ||
                 callKind == ErasedResourceCallKind::BufferCheckValid))
            {
                if (selectedResource == nullptr || selectedResource->resourceRole != ResourceRole::BufferValue)
                {
                    addDiagnostic(location, "indexed buffer component call could not resolve buffer resource selector \"" + selectedResourceName + "\".");
                    return expression;
                }
                if (callKind == ErasedResourceCallKind::BufferGetRaw)
                {
                    if (expression.operands.size() < payloadOffset + 1u)
                    {
                        addDiagnostic(location, "indexed buffer raw access is missing its index operand.");
                        return expression;
                    }
                    return makeSubscriptExpression(makeResourceDeclRefExpression(*selectedResource),
                                                   makeUIntExpression(expression.operands[payloadOffset], location),
                                                   expression.type,
                                                   location);
                }

                const ResourceBinding *indexTableResource = findResourceByRole(module,
                                                                               selectedResource->bindGroupIndex,
                                                                               ResourceRole::BufferIndexTable,
                                                                               selectedResource->resourceIndex);
                const ResourceBinding *accessBoundsResource = findResourceByRole(module,
                                                                                 selectedResource->bindGroupIndex,
                                                                                 ResourceRole::AccessBounds);
                if (indexTableResource == nullptr || accessBoundsResource == nullptr ||
                    expression.operands.size() < payloadOffset + 1u)
                {
                    addDiagnostic(location, "indexed buffer component call for \"" + selectedResource->name + "\" is missing access bounds, index table, or entity operand.");
                    return expression;
                }

                Expression accessBounds = makeSubscriptExpression(makeResourceDeclRefExpression(*accessBoundsResource),
                                                                  makeUIntLiteralExpression(selectedResource->resourceIndex, location),
                                                                  "uint2",
                                                                  location);
                Expression componentCount = makeMemberRefExpression(accessBounds, "x", "u32", location);
                Expression elementCount = makeMemberRefExpression(accessBounds, "y", "u32", location);
                Expression entity = makeUIntExpression(expression.operands[payloadOffset], location);
                Expression safeEntity = makeSafeUnsignedIndexExpression(entity, componentCount, location);
                Expression rawBase = makeSubscriptExpression(makeResourceDeclRefExpression(*indexTableResource),
                                                             safeEntity,
                                                             "u32",
                                                             location);
                Expression entityInRange = makeBinaryExpression("<", entity, componentCount, "bool", location);
                Expression rawBaseNotSentinel = makeBinaryExpression("!=",
                                                                     rawBase,
                                                                     makeUIntLiteralExpression(UINT32_MAX, location),
                                                                     "bool",
                                                                     location);
                Expression rawBaseInRange = makeBinaryExpression("<", rawBase, elementCount, "bool", location);
                Expression rawBaseValid = makeBoolAndExpression(std::move(rawBaseNotSentinel), std::move(rawBaseInRange), location);
                Expression lookupValid = makeBoolAndExpression(std::move(entityInRange), std::move(rawBaseValid), location);
                if (callKind == ErasedResourceCallKind::BufferCheckValid)
                {
                    return lookupValid;
                }
                if (expression.operands.size() < payloadOffset + 2u)
                {
                    addDiagnostic(location, "indexed buffer get call for \"" + selectedResource->name + "\" is missing its sub-index operand.");
                    return expression;
                }

                Expression safeBase = makeConditionalExpression(lookupValid,
                                                                rawBase,
                                                                makeUIntLiteralExpression(0u, location),
                                                                "u32",
                                                                location);
                Expression safeBaseClamped = makeSafeUnsignedIndexExpression(std::move(safeBase), elementCount, location);
                Expression elementCountMax = makeGenericCallExpression("max",
                                                                       "u32",
                                                                       {elementCount, makeUIntLiteralExpression(1u, location)},
                                                                       location);
                Expression lastElement = makeBinaryExpression("-",
                                                              std::move(elementCountMax),
                                                              makeUIntLiteralExpression(1u, location),
                                                              "u32",
                                                              location);
                Expression physicalRemaining = makeBinaryExpression("-",
                                                                    std::move(lastElement),
                                                                    safeBaseClamped,
                                                                    "u32",
                                                                    location);
                Expression remainingElementCount = makeBinaryExpression("+",
                                                                        std::move(physicalRemaining),
                                                                        makeUIntLiteralExpression(1u, location),
                                                                        "u32",
                                                                        location);
                Expression safeSubIndex = makeSafeUnsignedIndexExpression(makeUIntExpression(expression.operands[payloadOffset + 1u], location),
                                                                          std::move(remainingElementCount),
                                                                          location);
                Expression finalIndex = makeBinaryExpression("+",
                                                             std::move(safeBaseClamped),
                                                             std::move(safeSubIndex),
                                                             "u32",
                                                             location);
                return makeSubscriptExpression(makeResourceDeclRefExpression(*selectedResource),
                                               std::move(finalIndex),
                                               expression.type,
                                               location);
            }

            if (callKind == ErasedResourceCallKind::TextureSelect &&
                selectedResource != nullptr &&
                selectedResource->resourceRole == ResourceRole::TextureValue)
            {
                const ResourceBinding *indexTableResource = findResourceByRole(module,
                                                                               selectedResource->bindGroupIndex,
                                                                               ResourceRole::TextureIndexTable,
                                                                               selectedResource->resourceIndex);
                const ResourceBinding *accessBoundsResource = findResourceByRole(module,
                                                                                 selectedResource->bindGroupIndex,
                                                                                 ResourceRole::AccessBounds);
                if (indexTableResource == nullptr || accessBoundsResource == nullptr ||
                    expression.operands.size() < payloadOffset + 2u)
                {
                    addDiagnostic(location, "indexed texture component call for \"" + selectedResource->name + "\" is missing access bounds, index table, entity operand, or slot operand.");
                    return expression;
                }

                Expression accessBounds = makeSubscriptExpression(makeResourceDeclRefExpression(*accessBoundsResource),
                                                                  makeUIntLiteralExpression(selectedResource->resourceIndex, location),
                                                                  "uint2",
                                                                  location);
                Expression componentListCount = makeMemberRefExpression(accessBounds, "x", "u32", location);
                Expression descriptorCount = makeMemberRefExpression(accessBounds, "y", "u32", location);
                Expression entity = makeUIntExpression(expression.operands[payloadOffset], location);
                Expression slot = makeUIntExpression(expression.operands[payloadOffset + 1u], location);
                Expression safeSlot = makeGenericCallExpression("min",
                                                                "u32",
                                                                {std::move(slot),
                                                                 makeUIntLiteralExpression(UGLC::CodeGen::RenderTextureMaxTextureCountPerEntity - 1u, location)},
                                                                location);
                Expression rawComponentIndex = makeBinaryExpression("+",
                                                                    makeBinaryExpression("*",
                                                                                         std::move(entity),
                                                                                         makeUIntLiteralExpression(UGLC::CodeGen::RenderTextureMaxTextureCountPerEntity, location),
                                                                                         "u32",
                                                                                         location),
                                                                    std::move(safeSlot),
                                                                    "u32",
                                                                    location);
                Expression safeComponentIndex = makeSafeUnsignedIndexExpression(rawComponentIndex, componentListCount, location);
                Expression rawDescriptor = makeSubscriptExpression(makeResourceDeclRefExpression(*indexTableResource),
                                                                   safeComponentIndex,
                                                                   "u32",
                                                                   location);
                Expression componentInRange = makeBinaryExpression("<", rawComponentIndex, componentListCount, "bool", location);
                Expression descriptorNotSentinel = makeBinaryExpression("!=",
                                                                        rawDescriptor,
                                                                        makeUIntLiteralExpression(UINT32_MAX, location),
                                                                        "bool",
                                                                        location);
                Expression descriptorInRange = makeBinaryExpression("<", rawDescriptor, descriptorCount, "bool", location);
                Expression descriptorValid = makeBoolAndExpression(std::move(descriptorNotSentinel), std::move(descriptorInRange), location);
                Expression descriptorUsable = makeBoolAndExpression(std::move(componentInRange), std::move(descriptorValid), location);
                Expression safeDescriptor = makeConditionalExpression(std::move(descriptorUsable),
                                                                      std::move(rawDescriptor),
                                                                      makeUIntLiteralExpression(0u, location),
                                                                      "u32",
                                                                      location);
                return makeSubscriptExpression(makeResourceDeclRefExpression(*selectedResource),
                                               std::move(safeDescriptor),
                                               expression.type,
                                               location);
            }

            const std::optional<uint32_t> selectedBindGroupIndex = findSelectedBindGroupIndex(module, expression);
            if (selectedBindGroupIndex.has_value())
            {
                const ResourceBinding *drawInfoResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::DrawInfo);
                const ResourceBinding *accessBoundsResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::AccessBounds);
                if (drawInfoResource != nullptr && accessBoundsResource != nullptr && expression.operands.size() >= payloadOffset + 1u)
                {
                    Expression entity = makeUIntExpression(expression.operands[payloadOffset], location);
                    Expression entityCount = makeMemberRefExpression(makeSubscriptExpression(makeResourceDeclRefExpression(*accessBoundsResource),
                                                                                             makeUIntLiteralExpression(0u, location),
                                                                                             "uint2",
                                                                                             location),
                                                                     "x",
                                                                     "u32",
                                                                     location);
                    Expression safeEntity = makeSafeUnsignedIndexExpression(entity, entityCount, location);
                    Expression drawInfo = makeSubscriptExpression(makeResourceDeclRefExpression(*drawInfoResource),
                                                                  std::move(safeEntity),
                                                                  "UGL_DrawInfo_",
                                                                  location);
                    if (callKind == ErasedResourceCallKind::DrawInfoCheckValid)
                    {
                        Expression entityInRange = makeBinaryExpression("<", std::move(entity), std::move(entityCount), "bool", location);
                        Expression indexCount = makeMemberRefExpression(drawInfo, "indexCount", "u32", location);
                        Expression instanceCount = makeMemberRefExpression(drawInfo, "instanceCount", "u32", location);
                        Expression hasIndexCount = makeBinaryExpression(">",
                                                                        indexCount,
                                                                        makeUIntLiteralExpression(0u, location),
                                                                        "bool",
                                                                        location);
                        Expression hasInstanceCount = makeBinaryExpression(">",
                                                                           instanceCount,
                                                                           makeUIntLiteralExpression(0u, location),
                                                                           "bool",
                                                                           location);
                        Expression indexCountNotSentinel = makeBinaryExpression("!=",
                                                                                std::move(indexCount),
                                                                                makeUIntLiteralExpression(UINT32_MAX, location),
                                                                                "bool",
                                                                                location);
                        Expression instanceCountNotSentinel = makeBinaryExpression("!=",
                                                                                   std::move(instanceCount),
                                                                                   makeUIntLiteralExpression(UINT32_MAX, location),
                                                                                   "bool",
                                                                                   location);
                        Expression countsArePositive = makeBoolAndExpression(std::move(hasIndexCount),
                                                                             std::move(hasInstanceCount),
                                                                             location);
                        Expression countsAreNotSentinel = makeBoolAndExpression(std::move(indexCountNotSentinel),
                                                                                std::move(instanceCountNotSentinel),
                                                                                location);
                        return makeBoolAndExpression(std::move(entityInRange),
                                                     makeBoolAndExpression(std::move(countsArePositive),
                                                                           std::move(countsAreNotSentinel),
                                                                           location),
                                                     location);
                    }

                    std::string fieldName;
                    std::string fieldType = "u32";
                    if (callKind == ErasedResourceCallKind::DrawInfoIndexCount)
                    {
                        fieldName = "indexCount";
                    }
                    else if (callKind == ErasedResourceCallKind::DrawInfoInstanceCount)
                    {
                        fieldName = "instanceCount";
                    }
                    else if (callKind == ErasedResourceCallKind::DrawInfoFirstIndex)
                    {
                        fieldName = "firstIndex";
                    }
                    else if (callKind == ErasedResourceCallKind::DrawInfoVertexOffset)
                    {
                        fieldName = "vertexOffset";
                        fieldType = "i32";
                    }
                    else if (callKind == ErasedResourceCallKind::DrawInfoGlobalInstanceBase)
                    {
                        fieldName = "globalInstanceBase";
                    }
                    else if (callKind == ErasedResourceCallKind::DrawInfoVersion)
                    {
                        fieldName = "entityVersion";
                    }
                    if (!fieldName.empty())
                    {
                        return makeMemberRefExpression(std::move(drawInfo), std::move(fieldName), std::move(fieldType), location);
                    }
                }
            }

            return expression;
        }

        std::vector<Statement> UGLIRLowerer::expandErasedDrawDataStatement(Module &module, ErasedResourceCallKind callKind, const Expression &expression)
        {
            if (!isErasedDrawDataStoreCall(callKind))
            {
                return {};
            }

            const SourceLocation location = expression.sourceLocation;
            const size_t payloadOffset = erasedResourcePayloadOffset(expression);
            const std::optional<uint32_t> selectedBindGroupIndex = findSelectedBindGroupIndex(module, expression);
            if (!selectedBindGroupIndex.has_value() || expression.operands.size() < payloadOffset + 1u)
            {
                return {};
            }

            std::vector<Statement> statements;
            if (callKind == ErasedResourceCallKind::DrawCommandParamsStoreFields)
            {
                const ResourceBinding *commandParamsResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::CommandParams);
                const ResourceBinding *accessBoundsResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::AccessBounds);
                if (commandParamsResource == nullptr || accessBoundsResource == nullptr)
                {
                    return {};
                }
                Expression commandCount = makeMemberRefExpression(makeSubscriptExpression(makeResourceDeclRefExpression(*accessBoundsResource),
                                                                                          makeUIntLiteralExpression(0u, location),
                                                                                          "uint2",
                                                                                          location),
                                                                  "y",
                                                                  "u32",
                                                                  location);
                Expression safeCommandIndex = makeSafeUnsignedIndexExpression(makeUIntExpression(expression.operands[payloadOffset], location),
                                                                              std::move(commandCount),
                                                                              location);
                Expression commandParams = makeSubscriptExpression(makeResourceDeclRefExpression(*commandParamsResource),
                                                                   std::move(safeCommandIndex),
                                                                   "uint2",
                                                                   location);
                constexpr const char *fieldNames[] = {"x", "y"};
                const size_t outputCount = std::min<size_t>(expression.operands.size() - payloadOffset - 1u, 2u);
                for (size_t index = 0; index < outputCount; ++index)
                {
                    statements.push_back(makeExpressionStatement(makeStoreExpression(expression.operands[payloadOffset + index + 1u],
                                                                                    makeMemberRefExpression(commandParams, fieldNames[index], "u32", location),
                                                                                    location)));
                }
                return statements;
            }

            const ResourceBinding *drawInfoResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::DrawInfo);
            const ResourceBinding *accessBoundsResource = findResourceByRole(module, *selectedBindGroupIndex, ResourceRole::AccessBounds);
            if (drawInfoResource == nullptr || accessBoundsResource == nullptr)
            {
                return {};
            }
            Expression entityCount = makeMemberRefExpression(makeSubscriptExpression(makeResourceDeclRefExpression(*accessBoundsResource),
                                                                                     makeUIntLiteralExpression(0u, location),
                                                                                     "uint2",
                                                                                     location),
                                                             "x",
                                                             "u32",
                                                             location);
            Expression safeEntity = makeSafeUnsignedIndexExpression(makeUIntExpression(expression.operands[payloadOffset], location),
                                                                    std::move(entityCount),
                                                                    location);
            Expression drawInfo = makeSubscriptExpression(makeResourceDeclRefExpression(*drawInfoResource),
                                                          std::move(safeEntity),
                                                          "UGL_DrawInfo_",
                                                          location);
            constexpr const char *fieldNames[] = {"indexCount", "instanceCount", "firstIndex", "vertexOffset", "globalInstanceBase"};
            constexpr const char *fieldTypes[] = {"u32", "u32", "u32", "i32", "u32"};
            const size_t outputCount = std::min<size_t>(expression.operands.size() - payloadOffset - 1u, 5u);
            for (size_t index = 0; index < outputCount; ++index)
            {
                statements.push_back(makeExpressionStatement(makeStoreExpression(expression.operands[payloadOffset + index + 1u],
                                                                                makeMemberRefExpression(drawInfo, fieldNames[index], fieldTypes[index], location),
                                                                                location)));
            }
            return statements;
        }

        std::optional<std::vector<Statement>> UGLIRLowerer::lowerErasedDrawDataExpressionStatement(FunctionLoweringState &state, const clang::Expr *expr)
        {
            const auto *memberCallExpr = llvm::dyn_cast_or_null<clang::CXXMemberCallExpr>(expr == nullptr ? nullptr : expr->IgnoreParenImpCasts());
            if (memberCallExpr == nullptr || state.module == nullptr)
            {
                return std::nullopt;
            }

            const clang::Expr *objectExpr = memberCallExpr->getImplicitObjectArgument();
            const std::string objectTypeName = objectExpr == nullptr ? std::string() : canonicalRecordName(objectExpr->getType());
            if (!isDrawDataPackName(objectTypeName))
            {
                return std::nullopt;
            }

            const clang::FunctionDecl *callee = memberCallExpr->getDirectCallee();
            const std::string calleeName = callee == nullptr ? std::string() : callee->getNameAsString();
            const ErasedResourceCallKind callKind = classifyErasedDrawDataCall(calleeName);
            if (!isErasedDrawDataStoreCall(callKind))
            {
                return std::nullopt;
            }

            std::optional<std::string> erasedResourcePath = collectErasedResourceSelectorPath(state, objectExpr);
            if (!erasedResourcePath.has_value() || erasedResourcePath->empty())
            {
                addDiagnostic(memberCallExpr->getBeginLoc(), "lowered draw-data call could not resolve its reflected resource selector.");
                return std::vector<Statement>{};
            }

            Expression loweredCall;
            loweredCall.kind = ExpressionKind::Call;
            loweredCall.type = "void";
            loweredCall.sourceLocation = makeSourceLocation(memberCallExpr->getBeginLoc());
            loweredCall.operands.push_back(makeErasedResourceSelectorExpression(*erasedResourcePath, objectExpr == nullptr ? memberCallExpr->getBeginLoc() : objectExpr->getBeginLoc()));
            for (const clang::Expr *argument : memberCallExpr->arguments())
            {
                loweredCall.operands.push_back(lowerExpression(state, argument));
            }

            return expandErasedDrawDataStatement(*state.module, callKind, loweredCall);
        }

        const LambdaLocalLowering *UGLIRLowerer::registerLambdaExpression(FunctionLoweringState &state,
                                                                          const clang::LambdaExpr *lambdaExpr,
                                                                          const clang::ValueDecl *localDecl)
        {
            if (lambdaExpr == nullptr || state.module == nullptr)
            {
                return nullptr;
            }
            if (localDecl != nullptr)
            {
                const auto existing = state.lambdaLocals.find(localDecl);
                if (existing != state.lambdaLocals.end())
                {
                    return &existing->second;
                }
            }

            LambdaLocalLowering info;
            info.lambdaExpr = lambdaExpr;
            info.functionName = makeUGLIRLambdaSymbolName(state.ownerFunctionSymbol, state.nextLambdaOrdinal++);

            std::vector<const clang::FieldDecl *> captureFields;
            if (const clang::CXXRecordDecl *lambdaClass = lambdaExpr->getLambdaClass())
            {
                for (const clang::FieldDecl *fieldDecl : lambdaClass->fields())
                {
                    captureFields.push_back(fieldDecl);
                }
            }

            size_t fieldIndex = 0;
            size_t captureIndex = 0;
            for (const clang::LambdaCapture &capture : lambdaExpr->captures())
            {
                const clang::Expr *captureInitializer = nullptr;
                if (captureIndex < static_cast<size_t>(lambdaExpr->capture_size()))
                {
                    captureInitializer = lambdaExpr->capture_init_begin()[captureIndex];
                }
                ++captureIndex;

                if (capture.capturesThis())
                {
                    continue;
                }
                if (!capture.capturesVariable())
                {
                    addDiagnostic(capture.getLocation(), "lambda capture kind is not supported in Phase 7 lowering.");
                    continue;
                }

                const clang::ValueDecl *capturedDecl = capture.getCapturedVar();
                LambdaCaptureLowering loweredCapture;
                loweredCapture.capturedDecl = capturedDecl;
                loweredCapture.closureField = fieldIndex < captureFields.size() ? captureFields[fieldIndex++] : nullptr;
                loweredCapture.parameterName = "capture_" + capturedDecl->getNameAsString();
                if (loweredCapture.parameterName == "capture_")
                {
                    loweredCapture.parameterName += std::to_string(captureIndex);
                }
                loweredCapture.typeName = ensureType(*state.module, capturedDecl->getType(), capture.getLocation());
                loweredCapture.sourceLocation = makeSourceLocation(capture.getLocation());
                if (captureInitializer != nullptr)
                {
                    loweredCapture.operand = lowerExpression(state, captureInitializer);
                }
                else
                {
                    loweredCapture.operand.kind = ExpressionKind::DeclRef;
                    loweredCapture.operand.type = loweredCapture.typeName;
                    const auto nameIter = state.localNames.find(capturedDecl);
                    loweredCapture.operand.name = nameIter == state.localNames.end() ? capturedDecl->getNameAsString() : nameIter->second;
                    loweredCapture.operand.sourceLocation = makeSourceLocation(capture.getLocation());
                }
                info.captures.push_back(std::move(loweredCapture));
            }

            const clang::CXXMethodDecl *callOperator = lambdaExpr->getCallOperator();
            if (callOperator == nullptr || lambdaExpr->isGenericLambda())
            {
                addDiagnostic(lambdaExpr->getBeginLoc(), "generic lambda lowering is not supported in Phase 7.");
                return nullptr;
            }

            Function lambdaFunction;
            lambdaFunction.name = info.functionName;
            lambdaFunction.returnType = ensureType(*state.module, callOperator->getReturnType(), callOperator->getLocation());
            lambdaFunction.linkage = FunctionLinkage::Internal;
            lambdaFunction.stage = ShaderStage::None;
            lambdaFunction.entryKind = ShaderEntryKind::None;
            lambdaFunction.isEntryPoint = false;
            lambdaFunction.sourceLocation = makeSourceLocation(lambdaExpr->getBeginLoc());

            FunctionLoweringState lambdaState;
            lambdaState.module = state.module;
            lambdaState.functionDecl = callOperator;
            lambdaState.ownerFunctionSymbol = lambdaFunction.name;

            for (LambdaCaptureLowering &captureInfo : info.captures)
            {
                captureInfo.parameterName = lambdaState.allocateLocalName(captureInfo.parameterName);
                lambdaFunction.parameters.push_back(FunctionParameter{
                    .name = captureInfo.parameterName,
                    .type = captureInfo.typeName,
                    .semantic = {},
                    .isReference = false,
                    .isConstReference = false,
                    .sourceLocation = captureInfo.sourceLocation,
                });
                if (captureInfo.closureField != nullptr)
                {
                    lambdaState.lambdaCaptureFieldNames[captureInfo.closureField] = captureInfo.parameterName;
                }
                if (captureInfo.capturedDecl != nullptr)
                {
                    lambdaState.localNames[captureInfo.capturedDecl] = captureInfo.parameterName;
                }
                lambdaState.localTypeNamesByName[captureInfo.parameterName] = captureInfo.typeName;
            }

            for (const clang::ParmVarDecl *paramDecl : callOperator->parameters())
            {
                const std::string paramName = lambdaState.allocateLocalName(paramDecl->getNameAsString());
                lambdaState.localNames[paramDecl] = paramName;
                std::string semantic;
                for (const std::string &rawAttribute : getAnnotationStrings(paramDecl))
                {
                    semantic = rawAttribute;
                    break;
                }
                const bool isClangReference = paramDecl->getType()->isReferenceType();
                const ParameterPassingMode passingMode = parameterPassingModeFromToken(semantic);
                const bool isInputAttribute = passingMode == ParameterPassingMode::In;
                const bool isOutputAttribute = passingMode == ParameterPassingMode::Out || passingMode == ParameterPassingMode::InOut;
                const bool isReference = isClangReference || isInputAttribute || isOutputAttribute;
                const std::string paramType = ensureType(*state.module, paramDecl->getType(), paramDecl->getLocation());
                lambdaState.localTypeNamesByName[paramName] = paramType;
                lambdaFunction.parameters.push_back(FunctionParameter{
                    .name = paramName,
                    .type = paramType,
                    .semantic = passingMode == ParameterPassingMode::Value ? semantic : std::string{},
                    .semanticKind = passingMode == ParameterPassingMode::Value ? builtinSemanticFromToken(semantic).kind : BuiltinSemanticKind::None,
                    .semanticIndex = passingMode == ParameterPassingMode::Value ? builtinSemanticFromToken(semantic).index : 0u,
                    .passingMode = passingMode,
                    .isReference = isReference,
                    .isConstReference = isReference && !isOutputAttribute &&
                                        (isInputAttribute || paramDecl->getType().getNonReferenceType().isConstQualified()),
                    .sourceLocation = makeSourceLocation(paramDecl->getLocation()),
                });
            }

            if (const clang::Stmt *body = callOperator->getBody())
            {
                lambdaFunction.body = lowerStatement(lambdaState, body);
            }
            mSyntheticFunctions.push_back(std::move(lambdaFunction));

            if (localDecl != nullptr)
            {
                auto inserted = state.lambdaLocals.emplace(localDecl, std::move(info));
                return &inserted.first->second;
            }
            state.immediateLambdas.push_back(std::move(info));
            return &state.immediateLambdas.back();
        }

        const clang::LambdaExpr *UGLIRLowerer::getLambdaExpression(const clang::Expr *expr) const
        {
            if (expr == nullptr)
            {
                return nullptr;
            }
            expr = expr->IgnoreParenImpCasts();
            if (const auto *cleanupExpr = llvm::dyn_cast<clang::ExprWithCleanups>(expr))
            {
                return getLambdaExpression(cleanupExpr->getSubExpr());
            }
            if (const auto *temporaryExpr = llvm::dyn_cast<clang::MaterializeTemporaryExpr>(expr))
            {
                return getLambdaExpression(temporaryExpr->getSubExpr());
            }
            if (const auto *bindTemporaryExpr = llvm::dyn_cast<clang::CXXBindTemporaryExpr>(expr))
            {
                return getLambdaExpression(bindTemporaryExpr->getSubExpr());
            }
            return llvm::dyn_cast<clang::LambdaExpr>(expr);
        }

        const LambdaLocalLowering *UGLIRLowerer::resolveLambdaCallTarget(FunctionLoweringState &state, const clang::Expr *calleeObject)
        {
            if (calleeObject == nullptr)
            {
                return nullptr;
            }
            const clang::Expr *object = calleeObject->IgnoreParenImpCasts();
            if (const auto *declRef = llvm::dyn_cast<clang::DeclRefExpr>(object))
            {
                const auto iter = state.lambdaLocals.find(declRef->getDecl());
                if (iter != state.lambdaLocals.end())
                {
                    return &iter->second;
                }
            }
            if (const clang::LambdaExpr *lambdaExpr = getLambdaExpression(object))
            {
                return registerLambdaExpression(state, lambdaExpr, nullptr);
            }
            addDiagnostic(calleeObject->getBeginLoc(), "lambda call target is not a supported non-escaping local lambda.");
            return nullptr;
        }

        std::vector<Statement> UGLIRLowerer::lowerStatement(FunctionLoweringState &state, const clang::Stmt *stmt)
        {
            std::vector<Statement> result;
            if (stmt == nullptr)
            {
                return result;
            }
            stmt = stripNonSemanticStatementWrappers(stmt);
            if (const auto *compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt))
            {
                Statement blockStatement;
                blockStatement.kind = StatementKind::Block;
                blockStatement.sourceLocation = makeSourceLocation(compoundStmt->getBeginLoc());
                for (const clang::Stmt *child : compoundStmt->body())
                {
                    std::vector<Statement> childStatements = lowerStatement(state, child);
                    blockStatement.children.insert(blockStatement.children.end(),
                                                   std::make_move_iterator(childStatements.begin()),
                                                   std::make_move_iterator(childStatements.end()));
                }
                result.push_back(std::move(blockStatement));
                return result;
            }
            if (const auto *declStmt = llvm::dyn_cast<clang::DeclStmt>(stmt))
            {
                for (const clang::Decl *decl : declStmt->decls())
                {
                    const auto *varDecl = llvm::dyn_cast<clang::VarDecl>(decl);
                    if (varDecl == nullptr)
                    {
                        continue;
                    }
                    if (const clang::Expr *initializer = varDecl->getInit())
                    {
                        if (const clang::LambdaExpr *lambdaExpr = getLambdaExpression(initializer))
                        {
                            (void)registerLambdaExpression(state, lambdaExpr, varDecl);
                            continue;
                        }
                    }
                    const clang::CXXRecordDecl *varRecordDecl = recordDefinition(varDecl->getType().getNonReferenceType()->getAsCXXRecordDecl());
                    bool hasTrivialEmptyRecordInitializer = varDecl->getInit() == nullptr;
                    if (!hasTrivialEmptyRecordInitializer)
                    {
                        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(varDecl->getInit()->IgnoreParenImpCasts()))
                        {
                            hasTrivialEmptyRecordInitializer = constructExpr->getNumArgs() == 0 && constructExpr->getConstructor()->isTrivial();
                        }
                    }
                    const bool isGroupSharedVariable = isGroupSharedName(canonicalRecordName(varDecl->getType()));
                    if (hasTrivialEmptyRecordInitializer && varRecordDecl != nullptr && varRecordDecl->field_empty() && !isGroupSharedVariable)
                    {
                        const std::string variableName = state.allocateLocalName(varDecl->getNameAsString());
                        state.localNames[varDecl] = variableName;
                        std::string variableType = ensureFunctionTypeName(state, varDecl->getType(), varDecl->getLocation());
                        state.localTypeNamesByName[variableName] = variableType;
                        continue;
                    }
                    Statement variableStatement;
                    variableStatement.kind = StatementKind::VariableDeclaration;
                    variableStatement.name = state.allocateLocalName(varDecl->getNameAsString());
                    variableStatement.type = ensureFunctionTypeName(state, varDecl->getType(), varDecl->getLocation());
                    variableStatement.sourceLocation = makeSourceLocation(varDecl->getLocation());
                    state.localNames[varDecl] = variableStatement.name;
                    state.localTypeNamesByName[variableStatement.name] = variableStatement.type;
                    const bool isWorkgroupStatementType = state.module != nullptr && isWorkgroupTypeName(*state.module, variableStatement.type);
                    if (const clang::Expr *initializer = isWorkgroupStatementType ? nullptr : varDecl->getInit())
                    {
                        if (varDecl->getType()->isArrayType() &&
                            llvm::isa<clang::CXXConstructExpr>(initializer->IgnoreParenImpCasts()))
                        {
                            Expression destination;
                            destination.kind = ExpressionKind::DeclRef;
                            destination.name = variableStatement.name;
                            destination.type = variableStatement.type;
                            destination.sourceLocation = variableStatement.sourceLocation;
                            result.push_back(std::move(variableStatement));
                            appendConstructorInitializer(state, destination, *initializer->IgnoreParenImpCasts(), result);
                            continue;
                        }
                        bool omitTrivialRecordDefaultInitializer = false;
                        const bool isFramebufferVariable =
                            varRecordDecl != nullptr && isDerivedFromUGLBase(varRecordDecl, UGLC::CodeGen::mUGLFrameBufferBaseName);
                        if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(initializer->IgnoreParenImpCasts()))
                        {
                            omitTrivialRecordDefaultInitializer =
                                constructExpr->getNumArgs() == 0 && constructExpr->getConstructor()->isTrivial() &&
                                !constructExpr->requiresZeroInitialization() &&
                                recordDefinition(varDecl->getType().getNonReferenceType()->getAsCXXRecordDecl()) != nullptr;
                        }
                        if (!omitTrivialRecordDefaultInitializer && isFramebufferVariable)
                        {
                            omitTrivialRecordDefaultInitializer = isOmissibleFramebufferDefaultInitializer(initializer);
                        }
                        if (!omitTrivialRecordDefaultInitializer)
                        {
                            variableStatement.expressions.push_back(lowerExpression(state, initializer));
                        }
                    }
                    result.push_back(std::move(variableStatement));
                }
                return result;
            }
            if (const auto *returnStmt = llvm::dyn_cast<clang::ReturnStmt>(stmt))
            {
                Statement returnStatement;
                returnStatement.kind = StatementKind::Return;
                returnStatement.sourceLocation = makeSourceLocation(returnStmt->getBeginLoc());
                if (const clang::Expr *returnValue = returnStmt->getRetValue())
                {
                    returnStatement.expressions.push_back(lowerExpression(state, returnValue));
                }
                else if (!state.constructorResultName.empty())
                {
                    Expression receiver;
                    receiver.kind = ExpressionKind::DeclRef;
                    receiver.type = state.localTypeNamesByName.at(state.constructorResultName);
                    receiver.name = state.constructorResultName;
                    receiver.sourceLocation = returnStatement.sourceLocation;
                    returnStatement.expressions.push_back(std::move(receiver));
                }
                result.push_back(std::move(returnStatement));
                return result;
            }
            if (const auto *ifStmt = llvm::dyn_cast<clang::IfStmt>(stmt))
            {
                Statement scope;
                scope.kind = StatementKind::Block;
                scope.sourceLocation = makeSourceLocation(ifStmt->getBeginLoc());
                scope.children = lowerStatement(state, ifStmt->getInit());
                auto conditionDeclarations = lowerStatement(state, ifStmt->getConditionVariableDeclStmt());
                scope.children.insert(scope.children.end(), std::make_move_iterator(conditionDeclarations.begin()),
                                      std::make_move_iterator(conditionDeclarations.end()));
                if (ifStmt->isConstexpr())
                {
                    const auto selectedCase = ifStmt->getNondiscardedCase(mContext);
                    if (!selectedCase.has_value())
                        addDiagnostic(ifStmt->getBeginLoc(), "if constexpr remains dependent after shader semantic preparation.");
                    else
                        result = lowerStatement(state, *selectedCase);
                }
                else
                {
                    Statement ifStatement;
                    ifStatement.kind = StatementKind::If;
                    ifStatement.sourceLocation = scope.sourceLocation;
                    ifStatement.expressions.push_back(lowerExpression(state, ifStmt->getCond()));
                    ifStatement.children = lowerStatement(state, ifStmt->getThen());
                    ifStatement.elseChildren = lowerStatement(state, ifStmt->getElse());
                    result.push_back(std::move(ifStatement));
                }
                if (ifStmt->getInit() != nullptr || ifStmt->getConditionVariableDeclStmt() != nullptr)
                {
                    scope.children.insert(scope.children.end(), std::make_move_iterator(result.begin()),
                                          std::make_move_iterator(result.end()));
                    result.clear();
                    result.push_back(std::move(scope));
                }
                return result;
            }
            if (const auto *forStmt = llvm::dyn_cast<clang::ForStmt>(stmt))
            {
                Statement forStatement;
                forStatement.kind = StatementKind::For;
                forStatement.sourceLocation = makeSourceLocation(forStmt->getBeginLoc());
                if (const clang::Stmt *initStmt = forStmt->getInit())
                {
                    std::vector<Statement> initStatements = lowerStatement(state, initStmt);
                    forStatement.children.insert(forStatement.children.end(), std::make_move_iterator(initStatements.begin()), std::make_move_iterator(initStatements.end()));
                }
                if (const clang::Expr *condExpr = forStmt->getCond())
                {
                    forStatement.expressions.push_back(lowerExpression(state, condExpr));
                }
                if (const clang::Expr *incExpr = forStmt->getInc())
                {
                    forStatement.expressions.push_back(lowerExpression(state, incExpr));
                }
                std::vector<Statement> bodyStatements = lowerStatement(state, forStmt->getBody());
                forStatement.children.insert(forStatement.children.end(), std::make_move_iterator(bodyStatements.begin()), std::make_move_iterator(bodyStatements.end()));
                result.push_back(std::move(forStatement));
                return result;
            }
            if (const auto *whileStmt = llvm::dyn_cast<clang::WhileStmt>(stmt))
            {
                Statement whileStatement;
                whileStatement.kind = StatementKind::While;
                whileStatement.sourceLocation = makeSourceLocation(whileStmt->getBeginLoc());
                whileStatement.expressions.push_back(lowerExpression(state, whileStmt->getCond()));
                whileStatement.children = lowerStatement(state, whileStmt->getBody());
                result.push_back(std::move(whileStatement));
                return result;
            }
            if (const auto *doStmt = llvm::dyn_cast<clang::DoStmt>(stmt))
            {
                Statement doStatement;
                doStatement.kind = StatementKind::Do;
                doStatement.sourceLocation = makeSourceLocation(doStmt->getBeginLoc());
                doStatement.expressions.push_back(lowerExpression(state, doStmt->getCond()));
                doStatement.children = lowerStatement(state, doStmt->getBody());
                result.push_back(std::move(doStatement));
                return result;
            }
            if (const auto *switchStmt = llvm::dyn_cast<clang::SwitchStmt>(stmt))
            {
                Statement switchStatement;
                switchStatement.kind = StatementKind::Switch;
                switchStatement.sourceLocation = makeSourceLocation(switchStmt->getBeginLoc());
                switchStatement.expressions.push_back(lowerExpression(state, switchStmt->getCond()));
                SwitchCase *currentCase = nullptr;
                collectSwitchCasesFromStmt(state, switchStmt->getBody(), switchStatement.switchCases, currentCase);
                result.push_back(std::move(switchStatement));
                return result;
            }
            if (llvm::isa<clang::BreakStmt>(stmt))
            {
                result.push_back(Statement{.kind = StatementKind::Break, .sourceLocation = makeSourceLocation(stmt->getBeginLoc())});
                return result;
            }
            if (llvm::isa<clang::ContinueStmt>(stmt))
            {
                result.push_back(Statement{.kind = StatementKind::Continue, .sourceLocation = makeSourceLocation(stmt->getBeginLoc())});
                return result;
            }
            if (const auto *expr = llvm::dyn_cast<clang::Expr>(stmt))
            {
                if (std::optional<std::vector<Statement>> erasedDrawDataStatements = lowerErasedDrawDataExpressionStatement(state, expr))
                {
                    return std::move(*erasedDrawDataStatements);
                }
                Expression loweredExpression = lowerExpression(state, expr);
                if (loweredExpression.kind == ExpressionKind::Call && loweredExpression.name == "__uglir_noop")
                {
                    return result;
                }
                Statement exprStatement;
                exprStatement.kind = StatementKind::Expression;
                exprStatement.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                exprStatement.expressions.push_back(std::move(loweredExpression));
                result.push_back(std::move(exprStatement));
                return result;
            }

            addDiagnostic(stmt->getBeginLoc(), std::string(stmt->getStmtClassName()) + " is not supported in Phase 3.");
            return result;
        }

        void UGLIRLowerer::collectSwitchCasesFromStmt(FunctionLoweringState &state,
                                                      const clang::Stmt *stmt,
                                                      std::vector<SwitchCase> &cases,
                                                      SwitchCase *&currentCase)
        {
            if (stmt == nullptr)
            {
                return;
            }
            if (const auto *compoundStmt = llvm::dyn_cast<clang::CompoundStmt>(stmt))
            {
                for (const clang::Stmt *child : compoundStmt->body())
                {
                    collectSwitchCasesFromStmt(state, child, cases, currentCase);
                }
                return;
            }
            if (const auto *caseStmt = llvm::dyn_cast<clang::CaseStmt>(stmt))
            {
                SwitchCase switchCase;
                switchCase.sourceLocation = makeSourceLocation(caseStmt->getBeginLoc());
                if (const clang::Expr *lhs = caseStmt->getLHS())
                {
                    switchCase.labels.push_back(lowerExpression(state, lhs));
                }
                if (const clang::Expr *rhs = caseStmt->getRHS())
                {
                    switchCase.labels.push_back(lowerExpression(state, rhs));
                    addDiagnostic(rhs->getBeginLoc(), "case ranges are not supported in UGLIR switch lowering.");
                }
                cases.push_back(std::move(switchCase));
                currentCase = &cases.back();
                collectSwitchCasesFromStmt(state, caseStmt->getSubStmt(), cases, currentCase);
                return;
            }
            if (const auto *defaultStmt = llvm::dyn_cast<clang::DefaultStmt>(stmt))
            {
                SwitchCase switchCase;
                switchCase.isDefault = true;
                switchCase.sourceLocation = makeSourceLocation(defaultStmt->getBeginLoc());
                cases.push_back(std::move(switchCase));
                currentCase = &cases.back();
                collectSwitchCasesFromStmt(state, defaultStmt->getSubStmt(), cases, currentCase);
                return;
            }
            if (currentCase == nullptr)
            {
                addDiagnostic(stmt->getBeginLoc(), "switch statement body contains code before the first case/default label.");
                return;
            }
            std::vector<Statement> loweredStatements = lowerStatement(state, stmt);
            currentCase->body.insert(currentCase->body.end(),
                                     std::make_move_iterator(loweredStatements.begin()),
                                     std::make_move_iterator(loweredStatements.end()));
        }

        bool UGLIRLowerer::canCompareInBinary32(const clang::Expr &expr) const
        {
            if (!expr.getType()->isFloatingType())
            {
                return false;
            }
            const clang::QualType sourceType = expr.IgnoreParenImpCasts()->getType();
            if (sourceType->isFloatingType() && mContext.getTypeSize(sourceType) == 32u)
            {
                return true;
            }
            clang::Expr::EvalResult evaluated;
            if (!expr.isCXX11ConstantExpr(mContext) || !expr.EvaluateAsRValue(evaluated, mContext) || !evaluated.Val.isFloat())
            {
                return false;
            }
            bool losesInformation = false;
            evaluated.Val.getFloat().convert(llvm::APFloat::IEEEsingle(), llvm::APFloat::rmNearestTiesToEven, &losesInformation);
            return !losesInformation;
        }

        Expression UGLIRLowerer::lowerFloatingConversionOperand(FunctionLoweringState &state, const clang::Expr &expr, const std::string &targetTypeName)
        {
            const auto targetType = describeValueType(*state.module, targetTypeName);
            if (targetType.has_value() &&
                (targetType->scalarKind == ScalarKind::Float || targetType->scalarKind == ScalarKind::Half) &&
                expr.getType()->isFloatingType())
            {
                const bool isHalf = targetType->scalarKind == ScalarKind::Half;
                const auto scalarTypeName = ensureBuiltinTypeName(*state.module, isHalf ? "f16" : "f32", expr.getBeginLoc());
                if (const auto *conditional = llvm::dyn_cast<clang::ConditionalOperator>(expr.IgnoreParenImpCasts()))
                {
                    Expression result;
                    result.kind = ExpressionKind::Conditional;
                    result.type = scalarTypeName;
                    result.sourceLocation = makeSourceLocation(expr.getBeginLoc());
                    result.operands.push_back(lowerExpression(state, conditional->getCond()));
                    result.operands.push_back(lowerFloatingConversionOperand(state, *conditional->getTrueExpr(), scalarTypeName));
                    result.operands.push_back(lowerFloatingConversionOperand(state, *conditional->getFalseExpr(), scalarTypeName));
                    return result;
                }
                clang::Expr::EvalResult evaluated;
                if (expr.isCXX11ConstantExpr(mContext) && expr.EvaluateAsRValue(evaluated, mContext) && evaluated.Val.isFloat())
                {
                    bool losesInformation = false;
                    evaluated.Val.getFloat().convert(isHalf ? llvm::APFloat::IEEEhalf() : llvm::APFloat::IEEEsingle(),
                                                    llvm::APFloat::rmNearestTiesToEven, &losesInformation);
                    llvm::SmallString<32> literalText;
                    evaluated.Val.getFloat().toString(literalText);
                    Expression result;
                    result.kind = ExpressionKind::Literal;
                    result.type = scalarTypeName;
                    result.sourceLocation = makeSourceLocation(expr.getBeginLoc());
                    result.value = literalText.str().str();
                    return result;
                }
                const clang::Expr *operandExpr = &expr;
                if (const auto *promotion = llvm::dyn_cast<clang::ImplicitCastExpr>(expr.IgnoreParens());
                    promotion != nullptr && promotion->getCastKind() == clang::CK_FloatingCast &&
                    promotion->getSubExpr()->getType()->isFloatingType() &&
                    mContext.getTypeSize(promotion->getSubExpr()->getType()) == 32u &&
                    mContext.getTypeSize(promotion->getType()) > 32u)
                {
                    // A binary32 value widened without arithmetic can be read
                    // directly at this already-validated binary32 boundary.
                    operandExpr = promotion->getSubExpr();
                }
                Expression operand = lowerExpression(state, operandExpr);
                if (operand.type == scalarTypeName)
                {
                    return operand;
                }
                Expression result;
                result.kind = ExpressionKind::Cast;
                result.type = scalarTypeName;
                result.sourceLocation = makeSourceLocation(expr.getBeginLoc());
                result.operands.push_back(std::move(operand));
                return result;
            }
            return lowerExpression(state, &expr);
        }

        Expression UGLIRLowerer::lowerExpression(FunctionLoweringState &state, const clang::Expr *expr)
        {
            Expression result;
            if (expr == nullptr)
            {
                result.type = "void";
                return result;
            }
            const bool isValueRead = expr->isPRValue();
            if (const auto *cast = llvm::dyn_cast<clang::CastExpr>(expr->IgnoreParens());
                cast != nullptr && cast->getCastKind() == clang::CK_FloatingCast &&
                cast->getType()->isFloatingType() && mContext.getTypeSize(cast->getType()) == 32u)
            {
                const auto targetType = ensureFunctionTypeName(state, cast->getType(), cast->getBeginLoc());
                return lowerFloatingConversionOperand(state, *cast->getSubExpr(), targetType);
            }
            if (const auto *cast = llvm::dyn_cast<clang::ImplicitCastExpr>(expr->IgnoreParens()))
            {
                switch (cast->getCastKind())
                {
                case clang::CK_IntegralCast:
                case clang::CK_IntegralToBoolean:
                case clang::CK_IntegralToFloating:
                case clang::CK_FloatingToIntegral:
                case clang::CK_FloatingToBoolean:
                case clang::CK_FloatingCast:
                    result.kind = ExpressionKind::Cast;
                    result.type = ensureFunctionTypeName(state, cast->getType(), cast->getBeginLoc());
                    result.sourceLocation = makeSourceLocation(cast->getBeginLoc());
                    result.operands.push_back(lowerExpression(state, cast->getSubExpr()));
                    return result;
                default:
                    // Preserve value-category information when removing non-numeric casts.
                    break;
                }
            }
            expr = expr->IgnoreParenImpCasts();
            if (expr->isTypeDependent() || expr->isValueDependent())
            {
                addDiagnostic(expr->getBeginLoc(), "dependent expression remains after shader semantic preparation.");
                result.type = "void";
                return result;
            }
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());

            if (const auto *defaultArgExpr = llvm::dyn_cast<clang::CXXDefaultArgExpr>(expr))
            {
                return lowerExpression(state, defaultArgExpr->getExpr());
            }
            if (const auto *defaultInitExpr = llvm::dyn_cast<clang::CXXDefaultInitExpr>(expr))
            {
                if (const clang::Expr *initializer = defaultInitExpr->getExpr())
                {
                    return lowerExpression(state, initializer);
                }
                result.kind = ExpressionKind::Construct;
                result.name = result.type;
                normalizeConstructInfo(state, result, expr->getBeginLoc());
                return result;
            }
            if (const auto *integerLiteral = llvm::dyn_cast<clang::IntegerLiteral>(expr))
            {
                llvm::SmallString<32> literalText;
                integerLiteral->getValue().toString(literalText, 10, false);
                result.kind = ExpressionKind::Literal;
                result.value = literalText.str().str();
                return result;
            }
            if (const auto *floatingLiteral = llvm::dyn_cast<clang::FloatingLiteral>(expr))
            {
                llvm::SmallString<32> literalText;
                floatingLiteral->getValue().toString(literalText);
                result.kind = ExpressionKind::Literal;
                result.value = literalText.str().str();
                return result;
            }
            if (const auto *boolLiteral = llvm::dyn_cast<clang::CXXBoolLiteralExpr>(expr))
            {
                result.kind = ExpressionKind::Literal;
                result.value = boolLiteral->getValue() ? "true" : "false";
                return result;
            }
            if (llvm::isa<clang::CXXThisExpr>(expr))
            {
                if (!state.constructorResultName.empty())
                {
                    result.kind = ExpressionKind::DeclRef;
                    result.name = state.constructorResultName;
                    result.type = state.localTypeNamesByName.at(result.name);
                    return result;
                }
                result.kind = ExpressionKind::ThisRef;
                result.name.clear();
                if (const auto typeIter = state.localTypeNamesByName.find("this"); typeIter != state.localTypeNamesByName.end())
                {
                    result.type = typeIter->second;
                }
                return result;
            }
            if (const auto *declRef = llvm::dyn_cast<clang::DeclRefExpr>(expr))
            {
                return lowerDeclRef(state, declRef);
            }
            if (const auto *substExpr = llvm::dyn_cast<clang::SubstNonTypeTemplateParmExpr>(expr))
            {
                return lowerExpression(state, substExpr->getReplacement());
            }
            if (const auto *memberExpr = llvm::dyn_cast<clang::MemberExpr>(expr))
            {
                return lowerMemberExpr(state, memberExpr);
            }
            if (const auto *dependentMemberExpr = llvm::dyn_cast<clang::CXXDependentScopeMemberExpr>(expr))
            {
                return lowerDependentScopeMemberExpr(state, dependentMemberExpr);
            }
            if (const auto *arraySubscriptExpr = llvm::dyn_cast<clang::ArraySubscriptExpr>(expr))
            {
                if (std::optional<Expression> folded = isValueRead ? lowerConstantArraySubscript(state, arraySubscriptExpr) : std::nullopt)
                {
                    return *folded;
                }
                result.kind = ExpressionKind::Subscript;
                result.operands.push_back(lowerExpression(state, arraySubscriptExpr->getBase()));
                result.operands.push_back(lowerExpression(state, arraySubscriptExpr->getIdx()));
                return result;
            }
            if (const auto *operatorCall = llvm::dyn_cast<clang::CXXOperatorCallExpr>(expr))
            {
                return lowerOperatorCallExpr(state, operatorCall);
            }
            if (const auto *binaryOperator = llvm::dyn_cast<clang::BinaryOperator>(expr))
            {
                result.kind = binaryOperator->isAssignmentOp() ? ExpressionKind::Store : ExpressionKind::Binary;
                result.operatorName = getBinaryOperatorName(binaryOperator->getOpcode());
                if (const auto *compound = llvm::dyn_cast<clang::CompoundAssignOperator>(binaryOperator))
                    result.computationType = ensureFunctionTypeName(state, compound->getComputationResultType(), compound->getBeginLoc());
                if (binaryOperator->isComparisonOp() && canCompareInBinary32(*binaryOperator->getLHS()) &&
                    canCompareInBinary32(*binaryOperator->getRHS()))
                {
                    const auto comparisonType = ensureBuiltinTypeName(*state.module, "f32", binaryOperator->getBeginLoc());
                    result.operands.push_back(lowerFloatingConversionOperand(state, *binaryOperator->getLHS(), comparisonType));
                    result.operands.push_back(lowerFloatingConversionOperand(state, *binaryOperator->getRHS(), comparisonType));
                }
                else
                {
                    result.operands.push_back(lowerExpression(state, binaryOperator->getLHS()));
                    result.operands.push_back(lowerExpression(state, binaryOperator->getRHS()));
                }
                return result;
            }
            if (const auto *unaryOperator = llvm::dyn_cast<clang::UnaryOperator>(expr))
            {
                result.kind = ExpressionKind::Unary;
                result.operatorName = getUnaryOperatorName(unaryOperator->getOpcode());
                result.isPostfix = unaryOperator->isPostfix();
                result.operands.push_back(lowerExpression(state, unaryOperator->getSubExpr()));
                return result;
            }
            if (const auto *conditionalOperator = llvm::dyn_cast<clang::ConditionalOperator>(expr))
            {
                result.kind = ExpressionKind::Conditional;
                result.operands.push_back(lowerExpression(state, conditionalOperator->getCond()));
                result.operands.push_back(lowerExpression(state, conditionalOperator->getTrueExpr()));
                result.operands.push_back(lowerExpression(state, conditionalOperator->getFalseExpr()));
                return result;
            }
            if (const auto *callExpr = llvm::dyn_cast<clang::CallExpr>(expr))
            {
                return lowerCallExpr(state, callExpr);
            }
            if (const auto *initListExpr = llvm::dyn_cast<clang::InitListExpr>(expr))
            {
                result.kind = ExpressionKind::Construct;
                result.name = result.type;
                for (const clang::Expr *init : initListExpr->inits())
                {
                    result.operands.push_back(lowerFloatingConversionOperand(state, *init, result.type));
                }
                normalizeConstructInfo(state, result, initListExpr->getBeginLoc());
                return result;
            }
            if (llvm::isa<clang::ImplicitValueInitExpr>(expr))
            {
                result.kind = ExpressionKind::Construct;
                result.name = result.type;
                normalizeConstructInfo(state, result, expr->getBeginLoc());
                return result;
            }
            if (const auto *constructExpr = llvm::dyn_cast<clang::CXXConstructExpr>(expr))
                return lowerConstructExpression(state, *constructExpr, result.type);
            if (const auto *castExpr = llvm::dyn_cast<clang::ExplicitCastExpr>(expr))
            {
                result.kind = ExpressionKind::Cast;
                result.operands.push_back(lowerExpression(state, castExpr->getSubExpr()));
                return result;
            }

            result.kind = ExpressionKind::Literal;
            result.value = "<unsupported:" + std::string(expr->getStmtClassName()) + ">";
            addDiagnostic(expr->getBeginLoc(), std::string(expr->getStmtClassName()) + " expression is not supported in Phase 3.");
            return result;
        }

        Expression UGLIRLowerer::lowerDeclRef(FunctionLoweringState &state, const clang::DeclRefExpr *expr)
        {
            Expression result;
            const clang::ValueDecl *valueDecl = expr->getDecl();
            const std::string valueName = valueDecl == nullptr ? std::string() : valueDecl->getNameAsString();
            const auto substitutionIter = state.valueSubstitutions.find(valueDecl);
            if (substitutionIter != state.valueSubstitutions.end())
            {
                return substitutionIter->second;
            }
            const auto constantIter = state.integralTemplateArgumentsByName.find(valueName);
            if (constantIter != state.integralTemplateArgumentsByName.end())
            {
                result.kind = ExpressionKind::Literal;
                result.type = ensureBuiltinTypeName(*state.module, "u32", expr->getBeginLoc());
                result.value = constantIter->second;
                result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                return result;
            }
            const auto localIter = state.localNames.find(valueDecl);
            if (localIter != state.localNames.end())
            {
                result.kind = ExpressionKind::DeclRef;
                result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
                result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                result.name = localIter->second;
                const auto typeIter = state.localTypeNamesByName.find(result.name);
                if (typeIter != state.localTypeNamesByName.end())
                {
                    result.type = typeIter->second;
                }
                return result;
            }
            if (const auto *enumConstantDecl = llvm::dyn_cast_or_null<clang::EnumConstantDecl>(valueDecl))
            {
                llvm::SmallString<32> literalText;
                enumConstantDecl->getInitVal().toString(literalText, 10, enumConstantDecl->getType()->isSignedIntegerOrEnumerationType());
                result.kind = ExpressionKind::Literal;
                result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
                result.value = literalText.str().str();
                result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                return result;
            }
            if (const auto *varDecl = llvm::dyn_cast_or_null<clang::VarDecl>(valueDecl))
            {
                if (isSafeConstantVarDeclForUGLIR(varDecl, mContext))
                {
                    const clang::Expr *initializer = varDecl->getAnyInitializer();
                    if (initializer != nullptr)
                    {
                        clang::Expr::EvalResult evalResult;
                        if (initializer->EvaluateAsInt(evalResult, mContext))
                        {
                            llvm::SmallString<32> literalText;
                            evalResult.Val.getInt().toString(literalText, 10, expr->getType()->isSignedIntegerOrEnumerationType());
                            result.kind = ExpressionKind::Literal;
                            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
                            result.value = literalText.str().str();
                            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                            return result;
                        }
                        llvm::APFloat floatValue(0.0f);
                        if (initializer->EvaluateAsFloat(floatValue, mContext))
                        {
                            llvm::SmallString<32> literalText;
                            floatValue.toString(literalText);
                            result.kind = ExpressionKind::Literal;
                            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
                            result.value = literalText.str().str();
                            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
                            return result;
                        }
                        return lowerExpression(state, initializer);
                    }
                }
            }
            result.kind = ExpressionKind::DeclRef;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            result.name = valueDecl->getNameAsString();
            return result;
        }

        std::optional<Expression> UGLIRLowerer::lowerConstantArraySubscript(FunctionLoweringState &state, const clang::ArraySubscriptExpr *expr)
        {
            if (expr == nullptr)
            {
                return std::nullopt;
            }
            clang::Expr::EvalResult indexResult;
            if (!expr->getIdx()->isIntegerConstantExpr(mContext) ||
                !expr->getIdx()->EvaluateAsInt(indexResult, mContext))
            {
                return std::nullopt;
            }
            const uint64_t elementIndex = indexResult.Val.getInt().getZExtValue();
            const clang::Expr *base = expr->getBase()->IgnoreParenImpCasts();
            const auto *declRef = llvm::dyn_cast_or_null<clang::DeclRefExpr>(base);
            const auto *varDecl = declRef == nullptr ? nullptr : llvm::dyn_cast_or_null<clang::VarDecl>(declRef->getDecl());
            if (varDecl == nullptr || !isSafeConstantVarDeclForUGLIR(varDecl, mContext))
            {
                return std::nullopt;
            }
            const clang::Expr *initializer = varDecl->getAnyInitializer();
            const auto *initListExpr = llvm::dyn_cast_or_null<clang::InitListExpr>(initializer == nullptr ? nullptr : initializer->IgnoreParenImpCasts());
            if (initListExpr == nullptr || elementIndex >= initListExpr->getNumInits())
            {
                return std::nullopt;
            }
            return lowerExpression(state, initListExpr->getInit(static_cast<unsigned>(elementIndex)));
        }

        std::optional<Expression> UGLIRLowerer::lowerConstIntegralDefaultMember(FunctionLoweringState &state,
                                                                                const clang::FieldDecl *fieldDecl,
                                                                                const clang::MemberExpr *expr)
        {
            if (fieldDecl == nullptr || expr == nullptr || !fieldDecl->hasInClassInitializer())
            {
                return std::nullopt;
            }
            if (!fieldDecl->getType().isConstQualified() || !fieldDecl->getType()->isIntegralOrEnumerationType())
            {
                return std::nullopt;
            }

            const auto *recordDecl = llvm::dyn_cast_or_null<clang::CXXRecordDecl>(fieldDecl->getParent());
            if (recordDecl == nullptr)
            {
                return std::nullopt;
            }
            // A value-record aggregate initializer can override its member default.
            // Only the shader receiver's immutable configuration has no such instance override.
            if (state.module == nullptr || !llvm::isa<clang::CXXThisExpr>(expr->getBase()->IgnoreParenImpCasts()) ||
                makeUGLIRRecordSymbolName(*recordDecl) != state.module->reflection.shaderClassName)
                return std::nullopt;
            for (const clang::CXXConstructorDecl *constructorDecl : recordDecl->ctors())
            {
                if (constructorDecl != nullptr && !constructorDecl->isImplicit())
                {
                    return std::nullopt;
                }
            }

            const clang::Expr *initializer = fieldDecl->getInClassInitializer();
            if (initializer == nullptr)
            {
                return std::nullopt;
            }
            clang::Expr::EvalResult evalResult;
            if (!initializer->EvaluateAsInt(evalResult, mContext))
            {
                return std::nullopt;
            }

            llvm::SmallString<32> literalText;
            evalResult.Val.getInt().toString(literalText, 10, expr->getType()->isSignedIntegerOrEnumerationType());

            Expression result;
            result.kind = ExpressionKind::Literal;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.value = literalText.str().str();
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            return result;
        }

        Expression UGLIRLowerer::lowerMemberExpr(FunctionLoweringState &state, const clang::MemberExpr *expr)
        {
            Expression result;
            if (state.module != nullptr)
            {
                if (const std::optional<std::string> resourcePath = collectErasedResourceSelectorPath(state, expr);
                    resourcePath.has_value())
                {
                    if (const ResourceBinding *resource = findResourceByName(*state.module, *resourcePath))
                    {
                        return makeResourceDeclRefExpression(*resource);
                    }
                }
            }
            if (const auto *fieldDecl = llvm::dyn_cast_or_null<clang::FieldDecl>(expr->getMemberDecl()))
            {
                if (fieldDecl->isAnonymousStructOrUnion())
                {
                    if (const clang::Expr *base = expr->getBase())
                    {
                        return lowerExpression(state, base);
                    }
                }
                if (std::optional<Expression> foldedMember = lowerConstIntegralDefaultMember(state, fieldDecl, expr))
                {
                    return *foldedMember;
                }
            }
            result.kind = ExpressionKind::MemberRef;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.name = expr->getMemberNameInfo().getAsString();
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            if (const auto *fieldDecl = llvm::dyn_cast_or_null<clang::FieldDecl>(expr->getMemberDecl()))
            {
                const std::string fieldCanonicalName = canonicalRecordName(fieldDecl->getType());
                if (isFramebufferAttachmentName(fieldCanonicalName))
                {
                    result.type = ensureBuiltinTypeName(*state.module,
                                                        makeFramebufferTextureFormatValueTypeName(extractTextureFormatToken(typeSpelling(fieldDecl->getType()))),
                                                        fieldDecl->getLocation());
                }
                const auto captureIter = state.lambdaCaptureFieldNames.find(fieldDecl);
                if (captureIter != state.lambdaCaptureFieldNames.end())
                {
                    result.kind = ExpressionKind::DeclRef;
                    result.name = captureIter->second;
                    return result;
                }
                if (const clang::Expr *base = expr->getBase())
                {
                    if (llvm::isa<clang::CXXThisExpr>(base->IgnoreParenImpCasts()))
                    {
                        const auto substitutionIter = state.thisFieldSubstitutions.find(fieldDecl->getNameAsString());
                        if (substitutionIter != state.thisFieldSubstitutions.end())
                        {
                            return substitutionIter->second;
                        }
                    }
                }
            }
            if (const clang::Expr *base = expr->getBase())
            {
                Expression loweredBase = lowerExpression(state, base);
                if (state.module != nullptr)
                {
                    if (const Type *baseType = findLoweredTypeByName(*state.module, loweredBase.type))
                    {
                        const auto fieldIter = std::find_if(baseType->fields.begin(), baseType->fields.end(), [&result](const TypeField &field) {
                            return field.name == result.name;
                        });
                        if (fieldIter != baseType->fields.end())
                        {
                            result.type = fieldIter->type;
                        }
                    }
                    if (isVectorSwizzleName(result.name))
                    {
                        Expression *swizzleBase = &loweredBase;
                        std::optional<ValueTypeDescription> swizzleBaseType = describeValueType(*state.module, swizzleBase->type);
                        if ((!swizzleBaseType.has_value() || swizzleBaseType->vectorWidth <= 1u || swizzleBaseType->scalarKind == ScalarKind::None) &&
                            swizzleBase->kind == ExpressionKind::MemberRef &&
                            !swizzleBase->operands.empty())
                        {
                            Expression *candidateBase = &swizzleBase->operands.front();
                            std::optional<ValueTypeDescription> candidateBaseType = describeValueType(*state.module, candidateBase->type);
                            if (candidateBaseType.has_value() && candidateBaseType->vectorWidth > 1u && candidateBaseType->scalarKind != ScalarKind::None)
                            {
                                swizzleBase = candidateBase;
                                swizzleBaseType = candidateBaseType;
                            }
                        }
                        if (swizzleBaseType.has_value() && swizzleBaseType->vectorWidth > 1u && swizzleBaseType->scalarKind != ScalarKind::None)
                        {
                            result.type = ensureBuiltinTypeName(*state.module, makeVectorTypeName(swizzleBaseType->scalarKind, static_cast<uint32_t>(result.name.size())), expr->getBeginLoc());
                            loweredBase = *swizzleBase;
                        }
                    }
                }
                result.operands.push_back(std::move(loweredBase));
            }
            return result;
        }

        Expression UGLIRLowerer::lowerDependentScopeMemberExpr(FunctionLoweringState &state, const clang::CXXDependentScopeMemberExpr *expr)
        {
            Expression result;
            if (state.module != nullptr)
            {
                if (const std::optional<std::string> resourcePath = collectErasedResourceSelectorPath(state, expr);
                    resourcePath.has_value())
                {
                    if (const ResourceBinding *resource = findResourceByName(*state.module, *resourcePath))
                    {
                        return makeResourceDeclRefExpression(*resource);
                    }
                }
            }
            result.kind = ExpressionKind::MemberRef;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.name = expr->getMemberNameInfo().getAsString();
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            if (const clang::Expr *base = expr->getBase())
            {
                Expression loweredBase = lowerExpression(state, base);
                if (state.module != nullptr)
                {
                    if (const Type *baseType = findLoweredTypeByName(*state.module, loweredBase.type))
                    {
                        const auto fieldIter = std::find_if(baseType->fields.begin(), baseType->fields.end(), [&result](const TypeField &field) {
                            return field.name == result.name;
                        });
                        if (fieldIter != baseType->fields.end())
                        {
                            result.type = fieldIter->type;
                        }
                    }
                    if (isVectorSwizzleName(result.name))
                    {
                        const std::optional<ValueTypeDescription> swizzleBaseType = describeValueType(*state.module, loweredBase.type);
                        if (swizzleBaseType.has_value() && swizzleBaseType->vectorWidth > 1u && swizzleBaseType->scalarKind != ScalarKind::None)
                        {
                            result.type = ensureBuiltinTypeName(*state.module, makeVectorTypeName(swizzleBaseType->scalarKind, static_cast<uint32_t>(result.name.size())), expr->getBeginLoc());
                        }
                    }
                }
                result.operands.push_back(std::move(loweredBase));
            }
            return result;
        }

        Expression UGLIRLowerer::lowerCallExpr(FunctionLoweringState &state, const clang::CallExpr *expr)
        {
            Expression result;
            result.kind = ExpressionKind::Call;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            const clang::FunctionDecl *callee = expr->getDirectCallee();
            result.name = getFunctionName(callee);
            bool prependedObject = false;
            bool handledThisSpecialization = false;
            ErasedResourceCallKind erasedResourceCallKind = ErasedResourceCallKind::None;
            std::optional<Expression> erasedResourceSelector;
            const clang::Expr *userMemberObjectExpr = nullptr;
            clang::QualType userMemberObjectType;
            const auto *memberCallExpr = llvm::dyn_cast<clang::CXXMemberCallExpr>(expr);
            const auto *method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(callee);
            const bool operatorHasObject = llvm::isa<clang::CXXOperatorCallExpr>(expr) && method != nullptr && !method->isStatic();
            if (memberCallExpr != nullptr || operatorHasObject)
            {
                const clang::Expr *objectExpr = memberCallExpr != nullptr
                                                   ? memberCallExpr->getImplicitObjectArgument() : expr->getArg(0);
                const std::string objectTypeName = objectExpr == nullptr ? std::string() : canonicalRecordName(objectExpr->getType());
                const std::string calleeName = callee == nullptr ? std::string() : callee->getNameAsString();
                const bool isMemberConversionOperator = llvm::isa_and_nonnull<clang::CXXConversionDecl>(callee) ||
                                                        calleeName.rfind("operator ", 0) == 0;
                if (isMemberConversionOperator && objectExpr != nullptr && !isUserAuthoredFunction(callee))
                {
                    Expression objectExpression = lowerExpression(state, objectExpr);
                    if (objectExpression.type == result.type)
                    {
                        return objectExpression;
                    }
                    result.kind = ExpressionKind::Cast;
                    result.operands.push_back(std::move(objectExpression));
                    return result;
                }
                const ErasedResourceCallKind erasedBufferCallKind = isBufferComponentDataPackName(objectTypeName)
                                                                        ? classifyErasedBufferComponentCall(calleeName)
                                                                        : ErasedResourceCallKind::None;
                const ErasedResourceCallKind erasedTextureCallKind = isTextureComponentDataPackName(objectTypeName)
                                                                         ? classifyErasedTextureComponentCall(calleeName)
                                                                         : ErasedResourceCallKind::None;
                const ErasedResourceCallKind erasedDrawDataCallKind = isDrawDataPackName(objectTypeName)
                                                                          ? classifyErasedDrawDataCall(calleeName)
                                                                          : ErasedResourceCallKind::None;
                if (erasedBufferCallKind != ErasedResourceCallKind::None ||
                    erasedTextureCallKind != ErasedResourceCallKind::None ||
                    erasedDrawDataCallKind != ErasedResourceCallKind::None)
                {
                    erasedResourceCallKind = erasedBufferCallKind != ErasedResourceCallKind::None
                                                 ? erasedBufferCallKind
                                                 : (erasedTextureCallKind != ErasedResourceCallKind::None ? erasedTextureCallKind : erasedDrawDataCallKind);
                    result.name.clear();
                    if (objectExpr != nullptr)
                    {
                        const std::optional<std::string> erasedResourcePath = collectErasedResourceSelectorPath(state, objectExpr);
                        if (!erasedResourcePath.has_value() || erasedResourcePath->empty())
                        {
                            addDiagnostic(objectExpr->getBeginLoc(), "lowered indexed-resource call could not resolve its reflected resource selector.");
                        }
                        else
                        {
                            erasedResourceSelector = makeErasedResourceSelectorExpression(*erasedResourcePath, objectExpr->getBeginLoc());
                        }
                        prependedObject = true;
                    }
                }
                else if (isSampledTextureResourceName(objectTypeName) ||
                    isStorageTextureResourceName(objectTypeName) ||
                    isTextureAccessPackerName(objectTypeName) ||
                    isUniformBufferDataPackerName(objectTypeName) ||
                    isSamplerResourceName(objectTypeName) ||
                    isFramebufferAttachmentName(objectTypeName) ||
                    isBindGroupHandleName(objectTypeName) ||
                    isRenderSetHandleName(objectTypeName))
                {
                    result.name = callee == nullptr ? std::string() : callee->getNameAsString();
                    if (objectExpr != nullptr)
                    {
                        result.operands.push_back(lowerExpression(state, objectExpr));
                        prependedObject = true;
                    }
                    const IntrinsicCallKind resourceIntrinsic = classifyTextureIntrinsicCallKind(result.name);
                    if (resourceIntrinsic == IntrinsicCallKind::TextureRead && isUniformBufferDataPackerName(objectTypeName))
                    {
                        result.intrinsicCallKind = IntrinsicCallKind::UniformBufferRead;
                    }
                    else if (resourceIntrinsic == IntrinsicCallKind::TextureRead && isFramebufferAttachmentName(objectTypeName))
                    {
                        result.intrinsicCallKind = IntrinsicCallKind::InputAttachmentRead;
                    }
                    else
                    {
                        result.intrinsicCallKind = resourceIntrinsic;
                    }
                }
	                else if (objectExpr != nullptr && state.module != nullptr)
	                {
	                    const IntrinsicCallKind resourceIntrinsic = classifyTextureIntrinsicCallKind(calleeName);
	                    if (resourceIntrinsic != IntrinsicCallKind::None)
	                    {
                        if (const std::optional<std::string> resourcePath = collectErasedResourceSelectorPath(state, objectExpr);
                            resourcePath.has_value())
                        {
                            if (const ResourceBinding *resource = findResourceByName(*state.module, *resourcePath))
                            {
                                result.name = calleeName;
                                result.operands.push_back(makeResourceDeclRefExpression(*resource));
                                prependedObject = true;
                                result.intrinsicCallKind = resourceIntrinsic;
                            }
                        }
                        if (!prependedObject)
                        {
                            Expression objectExpression = lowerExpression(state, objectExpr);
                            if (isResourceLikeHelperParameterType(*state.module, objectExpression.type))
                            {
                                result.name = calleeName;
                                result.operands.push_back(std::move(objectExpression));
                                prependedObject = true;
                                result.intrinsicCallKind = resourceIntrinsic;
	                            }
	                        }
	                    }
	                    else if (getAnalyzableDefinition(callee) != nullptr && isUserAuthoredFunction(getAnalyzableDefinition(callee)))
	                    {
	                        result.name = getFunctionName(callee);
	                        userMemberObjectExpr = objectExpr;
	                        userMemberObjectType = objectExpr->getType();
	                    }
	                }
	                else if (objectExpr != nullptr && getAnalyzableDefinition(callee) != nullptr && isUserAuthoredFunction(getAnalyzableDefinition(callee)))
	                {
	                    result.name = getFunctionName(callee);
	                    userMemberObjectExpr = objectExpr;
                    userMemberObjectType = objectExpr->getType();
                }
            }
            if (callee != nullptr && result.type.find("dependent") != std::string::npos)
            {
                result.type = ensureFunctionTypeName(state, callee->getReturnType(), callee->getLocation());
            }
            std::vector<Expression> loweredArguments;
            loweredArguments.reserve(expr->getNumArgs());
            for (unsigned argumentIndex = operatorHasObject ? 1u : 0u; argumentIndex < expr->getNumArgs(); ++argumentIndex)
            {
                const clang::Expr *arg = expr->getArg(argumentIndex);
                const unsigned parameterIndex = argumentIndex - (operatorHasObject ? 1u : 0u);
                // Texture dimensions are DSL outputs despite their C++ value-parameter declarations.
                bool isOutput = result.intrinsicCallKind == IntrinsicCallKind::TextureGetDimensions;
                if (callee != nullptr && parameterIndex < callee->getNumParams())
                {
                    for (const std::string &attribute : getAnnotationStrings(callee->getParamDecl(parameterIndex)))
                    {
                        const ParameterPassingMode mode = parameterPassingModeFromToken(attribute);
                        isOutput = isOutput || mode == ParameterPassingMode::Out || mode == ParameterPassingMode::InOut;
                    }
                }
                if (isOutput)
                {
                    arg = arg->IgnoreParenImpCasts();
                    // OUT annotations use C++ value parameters; remove the implicit copy of the destination.
                    const auto *copy = llvm::dyn_cast<clang::CXXConstructExpr>(arg);
                    if (copy != nullptr && copy->getNumArgs() == 1 && copy->getConstructor()->isCopyOrMoveConstructor())
                    {
                        arg = copy->getArg(0)->IgnoreParenImpCasts();
                    }
                    if (!arg->isLValue() || arg->getType().isConstQualified())
                        addDiagnostic(arg->getBeginLoc(), "Shader output argument requires a writable lvalue.");
                }
                loweredArguments.push_back(lowerExpression(state, arg));
            }

            if (erasedResourceCallKind != ErasedResourceCallKind::None)
            {
                if (isErasedDrawDataStoreCall(erasedResourceCallKind))
                {
                    addDiagnostic(expr->getBeginLoc(), "draw-data out-parameter call must be lowered as a statement.");
                    return result;
                }
                if (state.module == nullptr || !erasedResourceSelector.has_value())
                {
                    addDiagnostic(expr->getBeginLoc(), "lowered indexed-resource call could not resolve its reflected resource selector.");
                    return result;
                }
                result.operands.clear();
                result.operands.push_back(std::move(*erasedResourceSelector));
                for (Expression &argument : loweredArguments)
                {
                    result.operands.push_back(std::move(argument));
                }
                return expandErasedResourceCallExpression(*state.module, erasedResourceCallKind, std::move(result));
            }

            if (userMemberObjectExpr != nullptr && callee != nullptr && state.module != nullptr)
            {
                const std::string calleeName = callee->getNameAsString();
                const IntrinsicCallKind resourceIntrinsic = classifyTextureIntrinsicCallKind(calleeName);
                if (resourceIntrinsic != IntrinsicCallKind::None)
                {
                    if (const std::optional<std::string> resourcePath = collectErasedResourceSelectorPath(state, userMemberObjectExpr);
                        resourcePath.has_value())
                    {
                        if (const ResourceBinding *resource = findResourceByName(*state.module, *resourcePath))
                        {
                            result.name = calleeName;
                            result.operands.push_back(makeResourceDeclRefExpression(*resource));
                            prependedObject = true;
                            result.intrinsicCallKind = resourceIntrinsic;
                            userMemberObjectExpr = nullptr;
                        }
                    }
                    if (userMemberObjectExpr != nullptr)
                    {
                        Expression objectExpression = lowerExpression(state, userMemberObjectExpr);
                        if (isResourceLikeHelperParameterType(*state.module, objectExpression.type))
                        {
                            result.name = calleeName;
                            result.operands.push_back(std::move(objectExpression));
                            prependedObject = true;
                            result.intrinsicCallKind = resourceIntrinsic;
                            userMemberObjectExpr = nullptr;
                        }
                    }
                }
            }

            if (userMemberObjectExpr != nullptr && callee != nullptr && state.module != nullptr)
            {
                Expression objectExpression = lowerExpression(state, userMemberObjectExpr);
                const std::string objectName = objectExpression.kind == ExpressionKind::DeclRef ? objectExpression.name : std::string();
                const std::vector<std::string> resourceFieldNames = collectResourceLikeFieldNames(state, userMemberObjectType);
                if (callee->getNameAsString() == "init" && !objectName.empty() && !resourceFieldNames.empty())
                {
                    const size_t substitutionCount = std::min(resourceFieldNames.size(), loweredArguments.size());
                    for (size_t index = 0; index < substitutionCount; ++index)
                    {
                        if (isResourceLikeHelperParameterType(*state.module, loweredArguments[index].type))
                        {
                            state.objectResourceFieldSubstitutions[objectName][resourceFieldNames[index]] = loweredArguments[index];
                        }
                    }
                    result.name = "__uglir_noop";
                    result.type = "void";
                    result.operands.clear();
                    return result;
                }

                const std::unordered_map<std::string, Expression> *fieldSubstitutions = nullptr;
                if (objectName == "this")
                {
                    fieldSubstitutions = &state.thisFieldSubstitutions;
                }
                else if (!objectName.empty())
                {
                    const auto substitutionIter = state.objectResourceFieldSubstitutions.find(objectName);
                    if (substitutionIter != state.objectResourceFieldSubstitutions.end())
                    {
                        fieldSubstitutions = &substitutionIter->second;
                    }
                }

                if (fieldSubstitutions != nullptr && !fieldSubstitutions->empty())
                {
                    const std::string specializedName = makeThisResourceSpecializedHelperName(callee, *fieldSubstitutions);
                    result.name = specializedName;
                    handledThisSpecialization = true;
                    if (mSyntheticFunctionNames.insert(specializedName).second)
                    {
                        mSyntheticFunctions.push_back(lowerFunction(*state.module,
                                                                    callee,
                                                                    false,
                                                                    ShaderStage::None,
                                                                    ShaderEntryKind::None,
                                                                    {1, 1, 1},
                                                                    specializedName,
                                                                    {},
                                                                    *fieldSubstitutions));
                    }
                }
                else if (!prependedObject)
                {
                    if (!isStatelessRecordType(userMemberObjectType))
                    {
                        result.operands.push_back(std::move(objectExpression));
                        prependedObject = true;
                    }
                    enqueueUserHelper(callee);
                }
            }

            bool usesResourceSpecialization = false;
            std::unordered_map<const clang::ValueDecl *, Expression> valueSubstitutions;
            std::vector<Expression> resourceArguments;
            std::vector<Expression> resourceCaptureArguments;
            const clang::FunctionDecl *definition = getAnalyzableDefinition(callee);
            if (!prependedObject && definition != nullptr && state.module != nullptr && isUserAuthoredFunction(definition))
            {
                const unsigned argumentCount = std::min<unsigned>(definition->getNumParams(), static_cast<unsigned>(loweredArguments.size()));
                for (unsigned index = 0; index < argumentCount; ++index)
                {
                    const Expression &argument = loweredArguments[index];
                    if (isResourceLikeHelperParameterType(*state.module, argument.type))
                    {
                        valueSubstitutions[definition->getParamDecl(index)] = argument;
                        resourceArguments.push_back(argument);
                        collectResourceSpecializationCaptures(*state.module, argument, resourceCaptureArguments);
                        usesResourceSpecialization = true;
                    }
                }
            }
            const std::vector<Expression> extraResourceCaptureArguments = usesResourceSpecialization
                                                                               ? getResourceSpecializationExtraCaptures(definition, valueSubstitutions, resourceCaptureArguments)
                                                                               : std::vector<Expression>();
            if (usesResourceSpecialization)
            {
                const std::string specializedName = makeResourceSpecializedHelperName(definition, resourceArguments);
                result.name = specializedName;
                if (mSyntheticFunctionNames.insert(specializedName).second)
                {
                    Function specializedFunction = lowerFunction(*state.module,
                                                                  definition,
                                                                  false,
                                                                  ShaderStage::None,
                                                                  ShaderEntryKind::None,
                                                                  {1, 1, 1},
                                                                  specializedName,
                                                                  valueSubstitutions);
                    appendResourceSpecializationCaptureParameters(specializedFunction, extraResourceCaptureArguments);
                    mSyntheticFunctions.push_back(std::move(specializedFunction));
                }
            }
            else if (!prependedObject && !handledThisSpecialization)
            {
                enqueueUserHelper(callee);
            }

            for (Expression &argument : loweredArguments)
            {
                if (state.module != nullptr &&
                    usesResourceSpecialization &&
                    isResourceLikeHelperParameterType(*state.module, argument.type))
                {
                    continue;
                }
                const Type *argumentType = nullptr;
                if (state.module != nullptr)
                {
                    const auto typeIter = std::find_if(state.module->types.begin(), state.module->types.end(), [&argument](const Type &type) {
                        return type.name == argument.type;
                    });
                    argumentType = typeIter == state.module->types.end() ? nullptr : &*typeIter;
                }
                if (argumentType != nullptr && argumentType->kind == TypeKind::Resource)
                {
                    continue;
                }
                result.operands.push_back(std::move(argument));
            }
            for (const Expression &capture : extraResourceCaptureArguments)
            {
                result.operands.push_back(capture);
            }
            if (result.intrinsicCallKind == IntrinsicCallKind::None && state.module != nullptr && !result.operands.empty())
            {
                const IntrinsicCallKind resourceIntrinsic = classifyTextureIntrinsicCallKind(result.name);
                if (resourceIntrinsic != IntrinsicCallKind::None &&
                    isResourceLikeHelperParameterType(*state.module, result.operands.front().type))
                {
                    result.intrinsicCallKind = resourceIntrinsic;
                }
            }
            if (result.intrinsicCallKind == IntrinsicCallKind::None)
            {
                result.intrinsicCallKind = classifyCallIntrinsicCallKind(result.name);
            }
            if ((result.intrinsicCallKind == IntrinsicCallKind::DiscardFragment || result.intrinsicCallKind == IntrinsicCallKind::Clip) &&
                state.module != nullptr &&
                !isFragmentKillCapableEntryKind(state.module->reflection.entryKind))
            {
                addDiagnostic(expr->getBeginLoc(),
                              std::string(getFragmentKillIntrinsicDiagnosticName(result.intrinsicCallKind)) +
                                  "() is only supported in fragment or pixel-local shaders, but \"" +
                                  state.module->reflection.entryName +
                                  "\" is a " +
                                  describeEntryKindForDiagnostic(state.module->reflection.entryKind) +
                                  " shader entry.");
            }
            return result;
        }

        /** Preserves the float arithmetic followed by half conversion defined by UGL half operators. */
        Expression UGLIRLowerer::preserveHalfArithmetic(FunctionLoweringState &state, Expression result, clang::SourceLocation location)
        {
            const std::string &op = result.operatorName;
            if (state.module == nullptr ||
                (op != "+" && op != "-" && op != "*" && op != "/" &&
                 op != "+=" && op != "-=" && op != "*=" && op != "/="))
                return result;
            ScalarKind scalarKind = ScalarKind::None;
            uint32_t width = 0;
            if (!describeConstructValueShape(*state.module, result.type, scalarKind, width) || scalarKind != ScalarKind::Half)
                return result;
            const std::string floatType = ensureBuiltinTypeName(*state.module,
                width == 1u ? "f32" : "float" + std::to_string(width), location);
            if (result.kind == ExpressionKind::Store)
            {
                result.computationType = floatType;
                return result;
            }
            Expression rounded;
            rounded.kind = ExpressionKind::Cast;
            rounded.type = result.type;
            rounded.sourceLocation = result.sourceLocation;
            result.type = floatType;
            for (Expression &operand : result.operands)
            {
                ScalarKind operandKind = ScalarKind::None;
                uint32_t operandWidth = 0;
                if (!describeConstructValueShape(*state.module, operand.type, operandKind, operandWidth))
                    continue;
                Expression converted;
                converted.kind = ExpressionKind::Cast;
                converted.type = ensureBuiltinTypeName(*state.module,
                    operandWidth == 1u ? "f32" : "float" + std::to_string(operandWidth), location);
                converted.sourceLocation = operand.sourceLocation;
                converted.operands.push_back(std::move(operand));
                operand = std::move(converted);
            }
            rounded.operands.push_back(std::move(result));
            return rounded;
        }

        Expression UGLIRLowerer::lowerOperatorCallExpr(FunctionLoweringState &state, const clang::CXXOperatorCallExpr *expr)
        {
            Expression result;
            result.type = ensureFunctionTypeName(state, expr->getType(), expr->getBeginLoc());
            result.sourceLocation = makeSourceLocation(expr->getBeginLoc());
            const auto *resolvedOperator = expr->getDirectCallee();
            const auto *method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(resolvedOperator);
            if (resolvedOperator != nullptr && isUserAuthoredFunction(resolvedOperator) &&
                !resolvedOperator->isDefaulted() &&
                (method == nullptr || !method->getParent()->isLambda()))
                return lowerCallExpr(state, expr);
            const clang::OverloadedOperatorKind op = expr->getOperator();
            if (expr->getNumArgs() == 0)
            {
                const clang::FunctionDecl *callee = expr->getDirectCallee();
                const std::string calleeName = callee == nullptr ? std::string() : callee->getNameAsString();
                const bool isMemberConversionOperator = llvm::isa_and_nonnull<clang::CXXConversionDecl>(callee) ||
                                                        calleeName.rfind("operator ", 0) == 0;
                const auto *calleeMemberExpr = llvm::dyn_cast_or_null<clang::MemberExpr>(
                    expr->getCallee() == nullptr ? nullptr : expr->getCallee()->IgnoreParenImpCasts());
                if (isMemberConversionOperator && calleeMemberExpr != nullptr && calleeMemberExpr->getBase() != nullptr)
                {
                    Expression objectExpression = lowerExpression(state, calleeMemberExpr->getBase());
                    if (objectExpression.type == result.type)
                    {
                        return objectExpression;
                    }
                    result.kind = ExpressionKind::Cast;
                    result.operands.push_back(std::move(objectExpression));
                    return result;
                }
            }
            if (op == clang::OO_Arrow && expr->getNumArgs() == 1)
            {
                return lowerExpression(state, expr->getArg(0));
            }
            if (op == clang::OO_Subscript && expr->getNumArgs() >= 2)
            {
                result.kind = ExpressionKind::Subscript;
                result.operands.push_back(lowerExpression(state, expr->getArg(0)));
                const clang::Expr *index = expr->getArg(1);
                const auto *method = llvm::dyn_cast_or_null<clang::CXXMethodDecl>(expr->getDirectCallee());
                const auto owner = method == nullptr ? std::string() : qualifiedName(method->getParent());
                if (owner == mUGLShaderStructuredBufferName || owner == mUGLShaderRWStructuredBufferName)
                {
                    if (const auto *promotion = llvm::dyn_cast<clang::ImplicitCastExpr>(index->IgnoreParens());
                        promotion != nullptr && promotion->getCastKind() == clang::CK_IntegralCast &&
                        mContext.getTypeSize(promotion->getType()) == 64u &&
                        mContext.getTypeSize(promotion->getSubExpr()->getType()) == 32u)
                    {
                        // The DSL buffer wrapper takes a host uint64_t index;
                        // its shader intrinsic consumes the original 32-bit index.
                        index = promotion->getSubExpr();
                    }
                }
                result.operands.push_back(lowerExpression(state, index));
                return result;
            }
            if (op == clang::OO_Equal && expr->getNumArgs() >= 2)
            {
                result.kind = ExpressionKind::Store;
                result.operatorName = "=";
                result.operands.push_back(lowerExpression(state, expr->getArg(0)));
                result.operands.push_back(lowerExpression(state, expr->getArg(1)));
                return result;
            }
            if (expr->getNumArgs() == 1)
            {
                std::string unaryOperator;
                switch (op)
                {
                case clang::OO_Minus:
                    unaryOperator = "-";
                    break;
                case clang::OO_Exclaim:
                    unaryOperator = "!";
                    break;
                case clang::OO_Tilde:
                    unaryOperator = "~";
                    break;
                case clang::OO_Plus:
                    return lowerExpression(state, expr->getArg(0));
                default:
                    break;
                }
                if (!unaryOperator.empty())
                {
                    result.kind = ExpressionKind::Unary;
                    result.operatorName = unaryOperator;
                    result.operands.push_back(lowerExpression(state, expr->getArg(0)));
                    return result;
                }
            }
            if (expr->getNumArgs() >= 2)
            {
                std::string compoundAssignmentOperator;
                switch (op)
                {
                case clang::OO_PlusEqual:
                    compoundAssignmentOperator = "+=";
                    break;
                case clang::OO_MinusEqual:
                    compoundAssignmentOperator = "-=";
                    break;
                case clang::OO_StarEqual:
                    compoundAssignmentOperator = "*=";
                    break;
                case clang::OO_SlashEqual:
                    compoundAssignmentOperator = "/=";
                    break;
                case clang::OO_PercentEqual:
                    compoundAssignmentOperator = "%=";
                    break;
                case clang::OO_CaretEqual:
                    compoundAssignmentOperator = "^=";
                    break;
                case clang::OO_AmpEqual:
                    compoundAssignmentOperator = "&=";
                    break;
                case clang::OO_PipeEqual:
                    compoundAssignmentOperator = "|=";
                    break;
                case clang::OO_LessLessEqual:
                    compoundAssignmentOperator = "<<=";
                    break;
                case clang::OO_GreaterGreaterEqual:
                    compoundAssignmentOperator = ">>=";
                    break;
                default:
                    break;
                }
                if (!compoundAssignmentOperator.empty())
                {
                    result.kind = ExpressionKind::Store;
                    result.operatorName = compoundAssignmentOperator;
                    result.operands.push_back(lowerExpression(state, expr->getArg(0)));
                    result.operands.push_back(lowerExpression(state, expr->getArg(1)));
                    return preserveHalfArithmetic(state, std::move(result), expr->getBeginLoc());
                }
            }
            if (expr->getNumArgs() >= 2)
            {
                std::string binaryOperator;
                switch (op)
                {
                case clang::OO_Plus:
                    binaryOperator = "+";
                    break;
                case clang::OO_Minus:
                    binaryOperator = "-";
                    break;
                case clang::OO_Star:
                    binaryOperator = "*";
                    break;
                case clang::OO_Slash:
                    binaryOperator = "/";
                    break;
                case clang::OO_Percent:
                    binaryOperator = "%";
                    break;
                case clang::OO_Amp:
                    binaryOperator = "&";
                    break;
                case clang::OO_Pipe:
                    binaryOperator = "|";
                    break;
                case clang::OO_Caret:
                    binaryOperator = "^";
                    break;
                case clang::OO_LessLess:
                    binaryOperator = "<<";
                    break;
                case clang::OO_GreaterGreater:
                    binaryOperator = ">>";
                    break;
                case clang::OO_AmpAmp:
                    binaryOperator = "&&";
                    break;
                case clang::OO_PipePipe:
                    binaryOperator = "||";
                    break;
                case clang::OO_EqualEqual:
                    binaryOperator = "==";
                    break;
                case clang::OO_ExclaimEqual:
                    binaryOperator = "!=";
                    break;
                case clang::OO_Less:
                    binaryOperator = "<";
                    break;
                case clang::OO_Greater:
                    binaryOperator = ">";
                    break;
                case clang::OO_LessEqual:
                    binaryOperator = "<=";
                    break;
                case clang::OO_GreaterEqual:
                    binaryOperator = ">=";
                    break;
                default:
                    break;
                }
                if (!binaryOperator.empty())
                {
                    result.kind = ExpressionKind::Binary;
                    result.operatorName = binaryOperator;
                    result.isEagerLogical = op == clang::OO_AmpAmp || op == clang::OO_PipePipe;
                    result.operands.push_back(lowerExpression(state, expr->getArg(0)));
                    result.operands.push_back(lowerExpression(state, expr->getArg(1)));
                    return preserveHalfArithmetic(state, std::move(result), expr->getBeginLoc());
                }
            }
            if (op == clang::OO_Call && expr->getNumArgs() >= 1)
            {
                if (const LambdaLocalLowering *lambdaInfo = resolveLambdaCallTarget(state, expr->getArg(0)))
                {
                    result.kind = ExpressionKind::Call;
                    result.name = lambdaInfo->functionName;
                    for (const LambdaCaptureLowering &capture : lambdaInfo->captures)
                    {
                        result.operands.push_back(capture.operand);
                    }
                    for (unsigned index = 1; index < expr->getNumArgs(); ++index)
                    {
                        result.operands.push_back(lowerExpression(state, expr->getArg(index)));
                    }
                    return result;
                }
            }

            result.kind = ExpressionKind::Call;
            if (const clang::FunctionDecl *callee = expr->getDirectCallee())
            {
                result.name = getFunctionName(callee);
                enqueueUserHelper(callee);
            }
            else
            {
                result.name = clang::getOperatorSpelling(op);
            }
            for (const clang::Expr *arg : expr->arguments())
            {
                Expression argument = lowerExpression(state, arg);
                const Type *argumentType = nullptr;
                if (state.module != nullptr)
                {
                    const auto typeIter = std::find_if(state.module->types.begin(), state.module->types.end(), [&argument](const Type &type) {
                        return type.name == argument.type;
                    });
                    argumentType = typeIter == state.module->types.end() ? nullptr : &*typeIter;
                }
                if (argumentType != nullptr && argumentType->kind == TypeKind::Resource)
                {
                    continue;
                }
                result.operands.push_back(std::move(argument));
            }
            if (result.intrinsicCallKind == IntrinsicCallKind::None)
            {
                result.intrinsicCallKind = classifyCallIntrinsicCallKind(result.name);
            }
            return result;
        }

        void UGLIRLowerer::enqueueUserHelper(const clang::FunctionDecl *functionDecl)
        {
            const clang::FunctionDecl *definition = getAnalyzableDefinition(functionDecl);
            const std::string key = getFunctionName(definition);
            if (definition == nullptr || key.empty() || !isUserAuthoredFunction(definition) || mQueuedHelpers.find(key) != mQueuedHelpers.end())
            {
                return;
            }
            mQueuedHelpers.insert(key);
            mHelperQueue.push_back(definition);
        }

        const clang::FunctionDecl *UGLIRLowerer::getAnalyzableDefinition(const clang::FunctionDecl *functionDecl) const
        {
            if (functionDecl == nullptr)
            {
                return nullptr;
            }
            const clang::FunctionDecl *definition = nullptr;
            if (functionDecl->hasBody(definition) && definition != nullptr)
            {
                return definition;
            }
            return nullptr;
        }

        std::string UGLIRLowerer::getFunctionName(const clang::FunctionDecl *functionDecl) const
        {
            if (functionDecl == nullptr)
            {
                return {};
            }
            return makeUGLIRFunctionSymbolName(*functionDecl);
        }

        std::string UGLIRLowerer::getBinaryOperatorName(clang::BinaryOperatorKind op) const
        {
            return clang::BinaryOperator::getOpcodeStr(op).str();
        }

        std::string UGLIRLowerer::getUnaryOperatorName(clang::UnaryOperatorKind op) const
        {
            return clang::UnaryOperator::getOpcodeStr(op).str();
        }
    } // namespace

    std::vector<ShaderClassRoot> discoverShaderRoots(clang::ASTContext &context)
    {
        UGLIRLowerer collector(context);
        return collector.collectRoots();
    }

    UGLIRLoweringResult lowerTranslationUnitToUGLIR(clang::ASTContext &context, llvm::ArrayRef<ShaderClassRoot> roots)
    {
        UGLIRLowerer lowerer(context);
        return lowerer.run(roots);
    }

    std::string formatUGLIRLoweringDiagnostics(const UGLIRLoweringResult &result)
    {
        std::string output;
        for (const UGLIRLoweringDiagnostic &diagnostic : result.diagnostics)
        {
            if (diagnostic.hasSourceLocation)
            {
                output += UGLC::CodeGen::formatClangStyleDiagnostic(diagnostic.filePath,
                                                                     diagnostic.line,
                                                                     diagnostic.column,
                                                                     diagnostic.message);
            }
            else
            {
                output += UGLC::CodeGen::formatUnlocatedDiagnostic(diagnostic.message);
            }
            output += '\n';
        }
        return output;
    }
} // namespace UGLC::CodeGen::UGLIR
