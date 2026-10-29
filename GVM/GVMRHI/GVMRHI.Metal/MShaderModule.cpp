#include "MShaderModule.hpp"
#include "MDevice.hpp"
#include <string>
#include <dispatch/dispatch.h>
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

        if (!descriptor.metallib.empty())
        {
            // The default destructor requests a copy; descriptor storage may be released after init.
            dispatch_data_t data = dispatch_data_create(descriptor.metallib.data(), descriptor.metallib.size(),
                                                        nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            if (data == nullptr)
            {
                throw std::runtime_error("Failed to allocate Metal library data: " + std::string(descriptor.label.c_str()));
            }
            this->mNativeLibrary = device->getNativeDevice()->newLibrary(data, &error);
            dispatch_release(data);
        }
        else
        {
            MTL::CompileOptions *compileOptions = MTL::CompileOptions::alloc()->init();
            compileOptions->setLanguageVersion(MTL::LanguageVersion::LanguageVersion3_2);
            compileOptions->setFastMathEnabled(true);
            this->mNativeLibrary = device->getNativeDevice()->newLibrary(NS::String::string(descriptor.code.c_str(), NS::UTF8StringEncoding), compileOptions, &error);
            compileOptions->release();
        }
        if (this->mNativeLibrary == nullptr)
        {
            const std::string errorString = error != nullptr ? error->localizedDescription()->utf8String() : "unknown Metal error";
            throw std::runtime_error("Failed to create shader module '" + std::string(descriptor.label.c_str()) + "': " + errorString);
        }
    }
    MTL::Function *MShaderModule::makeLibrary(const eastl::string &functionName)
    {
        std::lock_guard<std::mutex> lock(mFunctionsMutex);
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
