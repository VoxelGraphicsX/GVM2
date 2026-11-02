#pragma once
#include "UGL.h"
using namespace UGL;

using Image = Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding>, TextureDimension::e2D>;
using ImageView = TextureView<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding>, TextureDimension::e2D>;

/// Builds a view whose parent allocation must be present in the exported result.
class ImageNode final : public IRenderNode<uint, ImageView>
{
public:
    /// Returns only a view to exercise the allocation export diagnostic.
    ImageView build(IN uint extent) const
    {
        auto image = createTexture<Image>("OutputImage", uint2(extent, extent));
        return view(image);
    }
};

/// Rejects a standalone view that cannot express explicit parent allocation destruction.
[[Export]] auto buildInvalidGraph(uint extent)
{
    TaskGraph graph;
    return graph.add(ImageNode{}, extent);
}
