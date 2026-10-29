#pragma once
#include "UGL.h"
using namespace UGL;
/** Rejects a pixel-local wrapper that erases the concrete constructor contract. */
inline void invalidInterfacePixelLocal(RenderClass<IPixelLocalRenderClass> pipeline) {}
