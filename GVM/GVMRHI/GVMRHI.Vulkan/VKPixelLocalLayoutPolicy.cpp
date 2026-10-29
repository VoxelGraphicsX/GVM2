#include "VKPixelLocalLayoutPolicy.hpp"

namespace GVM::RHI::Vulkan
{
    vk::ImageLayout resolvePixelLocalColorInputLayout()
    {
        /*
         * Keep color pixel-local reads on the archived working Vulkan compatibility
         * path: the sparse color reference, input attachment reference and descriptor
         * image layout must all stay in GENERAL. Switching only the input side to
         * SHADER_READ_ONLY_OPTIMAL previously regressed the deferred sample into
         * magenta/black tiles, especially on the Vulkan/MoltenVK path.
         */
        return vk::ImageLayout::eGeneral;
    }
} // namespace GVM::RHI::Vulkan
