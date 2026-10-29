if(APPLE)
    target_compile_options(
        GVMRHI
        PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
endif()

target_sources(
    GVMRHI
    PRIVATE
        MAutoReleasePool.cpp
        MArguments.cpp
        MBindGroup.cpp
        MBindGroupLayout.cpp
        MBlitPassEncoder.cpp
        MBuffer.cpp
        MCommandEncoder.cpp
        MComputePassEncoder.cpp
        MComputePipeline.cpp
        MDevice.cpp
        MEnumUtils.cpp
        MInstance.cpp
        MPipelineLayout.cpp
        MQuerySet.cpp
        MQueue.cpp
    MReadback.cpp
        MRenderPassEncoder.cpp
        MRenderPipeline.cpp
        MSampler.cpp
        MShaderModule.cpp
        MSwapchain.cpp
        MTexture.cpp
        MTextureView.cpp
        MUploadAllocator.cpp
            MUtilShaders/MIndirectBufferConvertShaderExecutor.cpp
        MUtilShaders/MUtilShaders.cpp
        Metal.cpp
)

if(GVM_RHI_ENABLE_DIAGNOSTICS_OVERLAY)
    target_sources(GVMRHI PRIVATE MDiagnosticsOverlayShaders.cpp)
endif()

