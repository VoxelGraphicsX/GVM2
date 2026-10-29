#pragma once

// UGL / UGLC source contract:
// 1. User-authored DSL code must not declare, store, pass, return, or manipulate raw pointers.
//    This includes T*, void*, pointer arrays, pointer arithmetic, and nullptr-based pointer flow.
// 2. The only pointer semantics allowed in user-authored DSL code is the implicit current-object
//    `this` inside a class member function. User code must not rebind, store, or expose that
//    pointer as part of the DSL surface.
// 3. Raw pointers that appear in generated code or backend/runtime implementation are lowering
//    details and are not part of the public DSL language model.

#ifdef DISABLE_UGL
#include "Details/UGL.Attributes.h"
#include "Details/UGL.Types-glm.h"

#else
#include "Details/UGL.Attributes.h"
#include "Details/UGL.Device.h"
#include "Details/UGL.Format.h"
#include "Details/UGL.Resources.h"
#include "Details/UGL.Shaders.h"
// #include "Details/UGL.Types.h"
#include "Details/UGL.Renderer.h"
#include "Details/UGL.Types.h"

#include "Details/UGL.Atomic.h"
#include "Details/UGL.Constructor.h"
#include "Details/UGL.Function.h"
#include "Details/UGL.ImGui.h"
#include "Details/UGL.Object.h"
#include "Details/UGL.RawData.h"
#include "Details/UGL.RenderSet.h"
#include "Details/UGL.Swapchain.h"
#include "Details/UGL.Sync.h"
#endif
