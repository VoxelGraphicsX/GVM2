#pragma once

#include "ShaderBindGroupInfo.hpp"

namespace UGLC::CodeGen
{
    class BaseASTVisitor;

    void validateHLSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap);
    void validateMSLShaderBufferLayouts(const BaseASTVisitor &visitor, const BindGroupInfoMap &bindGroupInfoMap);
} // namespace UGLC::CodeGen
