#pragma once
#include "UGL.h"
using namespace UGL;
/** Rejects a render wrapper that erases the concrete constructor contract. */
inline void invalidInterfaceRender(RenderClass<IRenderClass> pipeline) {}
