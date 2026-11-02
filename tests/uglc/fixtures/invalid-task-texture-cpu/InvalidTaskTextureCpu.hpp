#pragma once
#include "UGL.h"
using namespace UGL;

/// Carries a GPU texture through an otherwise ordinary record.
struct Input
{
    Texture<TextureFormat::RGBA8Unorm, TextureUsage<TextureBinding>, TextureDimension::e2D> image;
};

/// Cannot consume GPU resources even when the function does not dereference the handle.
uint consume(Input input)
{
    return 1u;
}

/// Requires the transitive CPU resource restriction to remain enforced after enabling GPU exports.
[[Export]] auto buildInvalidGraph(Input input)
{
    TaskGraph graph;
    return graph.add(consume, input);
}
