#pragma once

#include "ThreeSampleHostOptions.hpp"

#include <SampleApplication.hpp>

#include <EASTL/unique_ptr.h>

namespace GVM::ThreeSamples
{
    /// Creates a pipeline-generated sample application using explicit deterministic host settings.
    eastl::unique_ptr<GVM::Samples::ISampleApplication> createThreeGeneratedApplication(
        GVM::Samples::SampleWindowManager &window,
        const ThreeSampleHostOptions &options);
} // namespace GVM::ThreeSamples
