#include "CPPFunctionConvertor.hpp"
#include <CodeGen/ShaderBarrierBuiltinUtils.hpp>
#include <CodeGen/UGLC.Constants.hpp>

namespace UGLC::CodeGen::CPP
{


    std::string CPPFunctionConvertor::convertFunc(const std::string &canonicalName, const std::vector<std::string> &templateArgs) const
    {
        (void)templateArgs;
        std::string result = canonicalName;
        if (canonicalName == "UGL::MakeSPTR")
        {
            result = "GVM::Core::MakeSPTR";
        }
        else if (canonicalName == "UGL::fillBuffer")
        {
            result = "GVM::Core::fillBuffer";
        }
        else if (canonicalName == "UGL::copyBufferToBuffer")
        {
            result = "GVM::Core::copyBufferToBuffer";
        }
        else if (canonicalName == "UGL::pixelLocalPass")
        {
            result = "GVM::Core::pixelLocalPass";
        }
        else if (canonicalName == "UGL::nextPixelLocalPass")
        {
            result = "GVM::Core::nextPixelLocalPass";
        }
        else if (canonicalName == mUGLFunctionWaveGetLaneIndexName ||
                 canonicalName == mUGLFunctionWaveGetLaneCountName ||
                 canonicalName == mUGLFunctionWaveReadLaneAtName ||
                 canonicalName == mUGLFunctionWaveReadLaneFirstName ||
                 canonicalName == mUGLFunctionWaveActiveBallotName ||
                 canonicalName == mUGLFunctionWaveActiveCountBitsName ||
                 canonicalName == mUGLFunctionWavePrefixCountBitsName ||
                 canonicalName == mUGLFunctionWavePrefixSumName ||
                 canonicalName == mUGLFunctionWaveMatchName ||
                 canonicalName == mUGLFunctionWaveReadAcrossXName ||
                 canonicalName == mUGLFunctionWaveReadAcrossYName ||
                 canonicalName == mUGLFunctionWaveReadAcrossDiagonalName ||
                 canonicalName == mUGLFunctionQuadReadLaneAtName ||
                 canonicalName == mUGLFunctionQuadReadAcrossXName ||
                 canonicalName == mUGLFunctionQuadReadAcrossYName ||
                 canonicalName == mUGLFunctionQuadReadAcrossDiagonalName ||
                 canonicalName == mUGLFunctionDiscardFragmentName ||
                 canonicalName == mUGLFunctionClipName ||
                 isShaderBarrierBuiltin(classifyShaderBarrierBuiltin(canonicalName)))
        {
            // Preserve shader-only UGL spellings so generated host-side
            // deletion/stub diagnostics can report the source-level builtin.
            result = canonicalName;
        }
        else if (canonicalName == "UGL::RawData")
        {
            result = "&";
        }
        else if (canonicalName.starts_with("UGL::Matrix")) // && templateArgs.size() == 3)
        {
            // result = "float" + canonicalName.substr(std::string("UGL::Matrix<float,").size(), 1) + "x" + canonicalName.substr(canonicalName.find_last_of(","), 1);
            result = "";
        }
        else if (canonicalName.starts_with("UGL::RenderImGui"))
        {
            result = "GVM::Core::RenderPassTaskDescriptor{.drawFn=[](GVM::RHI::RenderPassEncoder passEncoder){ ImGui_ImplGVM_RenderDrawData(ImGui::GetDrawData(), passEncoder);}}";
        }
        else if (canonicalName.find("UGL::") != canonicalName.npos)
        {
            result = canonicalName.substr(5);
        }
        return result;
    }

    bool CPPFunctionConvertor::checkShouldIgnoreParams(const std::string &canonicalName) const
    {
        if (canonicalName.starts_with("UGL::RenderImGui"))
        {
            return true;
        }
        return false;
    }

} // namespace UGLC::CodeGen::CPP
