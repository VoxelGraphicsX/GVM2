#pragma once

#include <CodeGen/UGLIR/UGLIRCore.hpp>
#include <CodeGen/UGLIR/UGLIRShaderRoots.hpp>

#include <string>
#include <vector>

namespace clang { class ASTContext; class Sema; }

namespace UGLC::CodeGen
{
    /** Describes one logical argument field without imposing a host or native GPU byte layout. */
    struct ConstructorArgumentField
    {
        std::string name;
        std::string path;
        std::string canonicalType;
        UGLIR::TypeKind kind = UGLIR::TypeKind::Struct;
        UGLIR::ScalarKind scalarKind = UGLIR::ScalarKind::None;
        uint32_t bitWidth = 0;
        uint32_t vectorWidth = 0;
        uint32_t matrixRows = 0;
        uint32_t matrixColumns = 0;
        uint32_t arrayCount = 0;
        UGLIR::ResourceKind resourceKind = UGLIR::ResourceKind::Unknown;
        UGLIR::AccessMode accessMode = UGLIR::AccessMode::Read;
        UGLIR::TextureDimension textureDimension = UGLIR::TextureDimension::None;
        UGLIR::TextureFormat textureFormat = UGLIR::TextureFormat::Unknown;
        std::vector<ConstructorArgumentField> fields;
        UGLIR::SourceLocation sourceLocation;
    };

    /** Records the constructor arguments reachable from one entry without changing the class signature. */
    struct ConstructorArgumentEntryUsage
    {
        std::string name;
        UGLIR::ShaderEntryKind entryKind = UGLIR::ShaderEntryKind::None;
        std::vector<uint32_t> argumentIndices;
    };

    /** Owns a complete, versioned class argument contract independently of both Clang frontends. */
    struct ConstructorArgumentContract
    {
        uint32_t version = 1;
        std::string className;
        UGLIR::ShaderClassKind classKind = UGLIR::ShaderClassKind::Compute;
        std::vector<ConstructorArgumentField> parameters;
        std::vector<ConstructorArgumentEntryUsage> entries;
        UGLIR::SourceLocation sourceLocation;
    };

    /** Completes Args types, validates calls with C++ initialization rules, and exports owned contracts. */
    std::vector<ConstructorArgumentContract> prepareConstructorArgumentContracts(
        clang::ASTContext &context, clang::Sema &sema,
        llvm::ArrayRef<UGLIR::ShaderClassRoot> roots, bool includeZeroArgumentClasses);

    /** Serializes logical contracts and exact execution signatures; native offsets are deliberately absent. */
    std::string dumpConstructorArgumentContracts(const std::vector<ConstructorArgumentContract> &contracts);
}
