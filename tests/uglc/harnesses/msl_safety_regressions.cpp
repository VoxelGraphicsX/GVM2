#include <CodeGen/MSL/MSLTextureTypes.hpp>

#include <iostream>
#include <stdexcept>

int main()
{
    using namespace UGLC::CodeGen::MSL;

    if (MakeFormatToVectorTypeForFrameBuffer("UGL::TextureFormat::RG8Sint") != "short2")
    {
        throw std::runtime_error("RG8Sint framebuffer mapping regressed");
    }
    if (MakeFormatToVectorTypeForTexture("UGL::TextureFormat::RG8Sint") != "int")
    {
        throw std::runtime_error("RG8Sint texture scalar mapping regressed");
    }
    if (MakeFormatToVectorTypeForTexture("UGL::TextureFormat::RG8Uint") != "uint")
    {
        throw std::runtime_error("RG8Uint texture scalar mapping regressed");
    }
    if (MakeFormatToVectorTypeForFrameBuffer("UGL::TextureFormat::RGBA8Unorm_sRGB") != "half4")
    {
        throw std::runtime_error("sRGB framebuffer mapping regressed");
    }

    std::cout << "msl_format_mapping_ok\n";
    return 0;
}
