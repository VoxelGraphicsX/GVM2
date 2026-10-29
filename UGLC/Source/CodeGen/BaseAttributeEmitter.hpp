#pragma once

#include <clang/AST/Attr.h>
#include <clang/AST/DeclBase.h>

#include <string>
#include <vector>

namespace UGLC::CodeGen
{
    class AbstractAttributeConvertor;

    /**
     * @brief Handles UGL annotation lookup, formatting, and simple indexed-attribute parsing.
     *
     * UGL DSL declarations encode backend metadata with annotation attributes.
     * Example DSL:
     *
     * ```cpp
     * void create(UGL::BindGroup<Material> material [[Slot0]]);
     * UGL::Texture2D<UGL::float4> albedo [[Binding3]];
     * ```
     *
     * `hasAttribute()` answers semantic queries such as `Slot0`, while
     * `generateAttributes()` keeps ordinary C++ output paths able to re-emit
     * attributes after applying a backend-specific attribute convertor.
     */
    class BaseAttributeEmitter
    {
    public:
        /** @brief Returns true when a declaration carries an exact UGL annotation string. */
        [[nodiscard]] bool hasAttribute(const clang::Decl *decl, const std::string &name) const;

        /** @brief Builds the DSL slot attribute name for a zero-based shader resource slot. */
        [[nodiscard]] std::string getSlotNameByIndex(int index) const;

        /** @brief Builds the DSL vertex-input attribute name for a zero-based vertex buffer slot. */
        [[nodiscard]] std::string getVertexInputNameByIndex(int index) const;

        /** @brief Returns true when `rawAttribute` is exactly `attributeName` followed by decimal digits. */
        [[nodiscard]] bool isExactIndexedAttribute(const std::string &rawAttribute, const std::string &attributeName) const;

        /** @brief Returns the decimal suffix from an indexed attribute or `-1` when it does not match. */
        [[nodiscard]] int getIndexedAttributeNumber(const std::string &rawAttribute, const std::string &attributeName) const;

        /** @brief Emits all annotation attributes attached to a declaration. */
        [[nodiscard]] std::string generateAttributes(const clang::Decl *decl, const AbstractAttributeConvertor *convertor = nullptr) const;

        /** @brief Collects all Clang annotation attributes attached to a declaration. */
        [[nodiscard]] std::vector<const clang::AnnotateAttr *> getAllAttributes(const clang::Decl *decl) const;

        /** @brief Emits one annotation attribute after optional backend conversion. */
        [[nodiscard]] std::string generateAttribute(const clang::AnnotateAttr *attribute, const AbstractAttributeConvertor *convertor = nullptr) const;

        /** @brief Returns the converted annotation text without C++ attribute brackets. */
        [[nodiscard]] std::string generateRawAttribute(const clang::AnnotateAttr *attribute, const AbstractAttributeConvertor *convertor = nullptr) const;

        /** @brief Extracts comma-separated parameters from the first matching annotation string. */
        [[nodiscard]] std::vector<std::string> getAttributeParams(const clang::Decl *decl,
                                                                  const std::string &funcName,
                                                                  const AbstractAttributeConvertor *convertor = nullptr) const;
    };
} // namespace UGLC::CodeGen
