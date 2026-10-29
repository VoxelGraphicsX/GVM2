#pragma once

#include <cstdint>

// The Metal backend uses one fixed per-stage buffer argument table layout shared by render and
// compute passes: bind group g always occupies buffer(g), the Args root always occupies
// MetalArgumentRootBufferIndex, and RHI vertex buffer slots fill the table from the top down via
// MTLVertexDescriptor. UGLC emits the matching [[buffer(N)]] indices, so no backend-specific index
// has to travel through the backend-neutral RHI descriptors.
namespace GVM::RHI::Metal
{
    /** Number of entries in one Metal per-stage buffer argument table. */
    inline constexpr uint32_t MetalBufferTableSize = 31;
    /** Bind group g always uses Metal buffer(g); must equal UGLC MaxBindGroupCount. */
    inline constexpr uint32_t MetalBindGroupCount = 8;
    /** Fixed Metal buffer index of the Args root for render and compute; must equal UGLC MetalArgumentRootBufferIndex. */
    inline constexpr uint32_t MetalArgumentRootBufferIndex = MetalBindGroupCount;
    /** Vertex buffers fill the table from the top down and may not reach the Args root. */
    inline constexpr uint32_t MetalMaxVertexBufferCount = MetalBufferTableSize - MetalArgumentRootBufferIndex - 1;
    /** Returns the Metal buffer index of RHI vertex buffer slot `slot`; slot must be below MetalMaxVertexBufferCount. */
    inline constexpr uint32_t metalVertexBufferIndex(uint32_t slot)
    {
        return MetalBufferTableSize - 1 - slot;
    }
}
