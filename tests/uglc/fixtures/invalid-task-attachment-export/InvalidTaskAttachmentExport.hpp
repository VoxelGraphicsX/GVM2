#pragma once
#include "UGL.h"
using namespace UGL;

using Image = Texture<TextureFormat::RGBA8Unorm, TextureUsage<RenderAttachment>, TextureDimension::e2D>;
using Attachment = ColorAttachment<TextureFormat::RGBA8Unorm>;

/// Exercises a render attachment that contains a borrowed view instead of an exportable allocation handle.
class ImageNode final : public IRenderNode<uint, Attachment>
{
public:
    /// Builds a valid graph-local attachment whose parent texture must be exported instead.
    Attachment build(IN uint extent) const
    {
        auto image = createTexture<Image>("OutputImage", uint2(extent, extent));
        Attachment attachment;
        attachment = view(image);
        return attachment;
    }
};

/// Rejects exporting internal pass data even though it is legal inside a RenderNode build.
[[Export]] auto buildInvalidGraph(uint extent)
{
    TaskGraph graph;
    return graph.add(ImageNode{}, extent);
}
