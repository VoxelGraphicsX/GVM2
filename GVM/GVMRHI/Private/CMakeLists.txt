set(GVMRHI_PRIVATE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CpuProbeFrontend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GBackendFactory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/GEnumUtils.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoggerProvidersStub.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoggingCoreDisabled.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/LoggingCoreEnabled.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PixelLocalPassAccess.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PublicLoggerAdapter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/RHIDiagnosticsOverlay.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpdlogLoggerProvider.cpp"
)

if(GVM_RHI_ENABLE_LOGGING)
    list(REMOVE_ITEM GVMRHI_PRIVATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LoggingCoreDisabled.cpp")
else()
    list(REMOVE_ITEM GVMRHI_PRIVATE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/LoggingCoreEnabled.cpp")
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
