#include "ThreeGeneratedApplication.hpp"

#include "UGLBin/generate_result.hpp"

#ifndef GVM_THREE_RENDERER_TYPE
#error "GVM_THREE_RENDERER_TYPE must name the generated AbstractRenderer type."
#endif

#ifndef GVM_THREE_RENDERER_FACTORY
#error "GVM_THREE_RENDERER_FACTORY must name the generated AbstractRenderer factory."
#endif

#ifndef GVM_THREE_RUNTIME_ADAPTER_HEADER
#error "GVM_THREE_RUNTIME_ADAPTER_HEADER must name the shard runtime adapter header."
#endif

#ifndef GVM_THREE_RUNTIME_ADAPTER_TYPE
#error "GVM_THREE_RUNTIME_ADAPTER_TYPE must name the shard runtime adapter type."
#endif

#include GVM_THREE_RUNTIME_ADAPTER_HEADER

#ifndef GVM_THREE_SAMPLE_NAME
#define GVM_THREE_SAMPLE_NAME "GVM Three.js Sample"
#endif

namespace GVM::ThreeSamples
{
    /// Hosts one generated Three.js compatibility renderer on the shared sample runtime.
    class ThreeGeneratedApplication final : public GVM::Samples::SampleApplication
    {
    public:
        /// Creates and initializes the generated renderer for the requested backend.
        ThreeGeneratedApplication(
            GVM::Samples::SampleWindowManager &window,
            const ThreeSampleHostOptions &options);

        /// Releases the generated renderer before the shared RHI objects are destroyed.
        ~ThreeGeneratedApplication() override;

    private:
        /// Renders one deterministic frame through the generated DSL renderer.
        void renderSampleFrame() override;

        struct Impl;
        eastl::unique_ptr<Impl> mImpl;
    };

    /// Owns renderer state generated independently by one UGLC pipeline.
    struct ThreeGeneratedApplication::Impl
    {
        /// Creates per-run host state from fully validated command-line options.
        explicit Impl(const ThreeSampleHostOptions &hostOptions)
            : options(hostOptions)
        {
        }

        ThreeSampleHostOptions options;
        GVM_THREE_RENDERER_TYPE renderer;
        GVM_THREE_RUNTIME_ADAPTER_TYPE runtimeAdapter;
        uint32_t frameIndex = 0;
    };

    ThreeGeneratedApplication::ThreeGeneratedApplication(
        GVM::Samples::SampleWindowManager &window,
        const ThreeSampleHostOptions &options)
        : SampleApplication(
              window,
              GVM::Samples::SampleApplicationCreateInfo{
                  .sampleName = GVM_THREE_SAMPLE_NAME,
                  .preferredGraphicsBackend = options.backend,
                  .diagnosticsOverlayEnabled = false,
              })
        , mImpl(eastl::make_unique<Impl>(options))
    {
        mImpl->renderer = GVM_THREE_RENDERER_FACTORY();
        mImpl->renderer->init(device(), swapchain());
        mImpl->renderer->configureOutput(options.width, options.height);
        mImpl->runtimeAdapter.initialize(*mImpl->renderer, device(), mImpl->options);
    }

    ThreeGeneratedApplication::~ThreeGeneratedApplication()
    {
        if (mImpl != nullptr && mImpl->renderer != nullptr)
        {
            mImpl->runtimeAdapter.shutdown(*mImpl->renderer, mImpl->options);
            mImpl->renderer->destroy();
        }
        mImpl = nullptr;
    }

    void ThreeGeneratedApplication::renderSampleFrame()
    {
        mImpl->runtimeAdapter.beforeFrame(
            *mImpl->renderer,
            mImpl->options,
            mImpl->frameIndex);
        mImpl->renderer->render();
        mImpl->runtimeAdapter.afterFrame(
            *mImpl->renderer,
            mImpl->options,
            mImpl->frameIndex,
            mImpl->renderer->getReadbackTextureHandle(),
            mImpl->renderer->getReadbackWidth(),
            mImpl->renderer->getReadbackHeight());
        ++mImpl->frameIndex;
    }

    eastl::unique_ptr<GVM::Samples::ISampleApplication> createThreeGeneratedApplication(
        GVM::Samples::SampleWindowManager &window,
        const ThreeSampleHostOptions &options)
    {
        return eastl::make_unique<ThreeGeneratedApplication>(window, options);
    }
} // namespace GVM::ThreeSamples
