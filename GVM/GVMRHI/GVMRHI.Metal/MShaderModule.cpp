#include "MShaderModule.hpp"
#include "MDevice.hpp"
#include <string>
namespace GVM::RHI::Metal
{

    MShaderModule::MShaderModule()
    {
    }

    void MShaderModule::init(MDevice *device, const ShaderModuleDescriptor &descriptor)
    {
        this->mLabelName = descriptor.label;
        this->mDevice = device;
        NS::Error *error = nullptr;

        MTL::CompileOptions *compileOptions = MTL::CompileOptions::alloc()->init();
        compileOptions->setLanguageVersion(MTL::LanguageVersion::LanguageVersion3_2);
        compileOptions->setFastMathEnabled(true);
        this->mNativeLibrary = device->getNativeDevice()->newLibrary(NS::String::string(descriptor.code.c_str(), NS::UTF8StringEncoding), compileOptions, &error);
        compileOptions->release();
        if (error)
        {
            const std::string &errorString = error->localizedDescription()->utf8String();
            // const std::string& errorReason = error->localizedFailureReason()->utf8String();
            // const std::string& errorSuggestion = error->localizedRecoverySuggestion()->utf8String();
            throw std::runtime_error("Failed to create shader module " + errorString);
        }
    }
    MTL::Function *MShaderModule::makeLibrary(const eastl::string &functionName)
    {
        if (mFunctions.find(functionName) != mFunctions.end())
        {
            return mFunctions.at(functionName);
        }
        auto function = this->mNativeLibrary->newFunction(NS::String::string(functionName.data(), NS::UTF8StringEncoding));
        mFunctions[functionName] = function;
        return function;
    }
    MShaderModule::~MShaderModule()
    {
        for (auto &[name, function] : mFunctions)
        {
            if (function != nullptr)
            {
                function->release();
            }
        }
        mFunctions.clear();
        if (mNativeLibrary != nullptr)
        {
            mNativeLibrary->release();
            mNativeLibrary = nullptr;
        }
    }
} // namespace GVM::RHI::Metal
