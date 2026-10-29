set(GVMRHI_PRIVATE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/ArgumentValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BlitPassValidation.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ComputePassValidator.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BufferCopyMultipleRegionExecutor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuProbeFrontend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DeviceLostState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/DeviceObjectCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FullscreenTextureExecutor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GBackendFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GEnumUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoggerProvidersStub.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoggingCore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PixelLocalPassAccess.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicLoggerAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RenderPassRecorder.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpdlogLoggerProvider.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TextureCopyFootprint.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TextureViewResolution.cpp"
)

if(NOT GVM_RHI_ENABLE_LOGGING)
    list(REMOVE_ITEM GVMRHI_PRIVATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SpdlogLoggerProvider.cpp")
endif()

if(GVM_RHI_USE_SPDLOG_LOGGER)
    list(REMOVE_ITEM GVMRHI_PRIVATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LoggerProvidersStub.cpp")
else()
    list(REMOVE_ITEM GVMRHI_PRIVATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/SpdlogLoggerProvider.cpp")
endif()

target_sources(
    GVMRHI
    PRIVATE
        ${GVMRHI_PRIVATE_SOURCES}
)

if(GVM_RHI_ENABLE_DIAGNOSTICS_OVERLAY)
    target_sources(GVMRHI PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/RHIDiagnosticsOverlay.cpp")
endif()

