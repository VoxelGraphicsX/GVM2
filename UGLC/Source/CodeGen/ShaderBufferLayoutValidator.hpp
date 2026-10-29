#pragma once

#include "ShaderBindGroupInfo.hpp"

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    void validateHLSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap);
    /** Validates existing Metal buffer element ABI rules for resources nested inside constructor Args. */
    void validateMSLConstructorArgumentBufferLayouts(const BaseASTVisitor &visitor, const clang::CXXRecordDecl &record);

    void validateMSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap);
} // namespace UGLC::CodeGen
