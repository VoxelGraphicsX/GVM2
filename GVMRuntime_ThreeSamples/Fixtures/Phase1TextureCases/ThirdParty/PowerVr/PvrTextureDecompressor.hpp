// Copyright (c) Imagination Technologies Ltd.
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace pvr
{
    /// Decompresses one PVRTC1 payload into tightly packed RGBA8888 pixels.
    uint32_t PVRTDecompressPVRTC(
        const void *compressedData,
        uint32_t do2bitMode,
        uint32_t width,
        uint32_t height,
        uint8_t *resultImage);
} // namespace pvr
