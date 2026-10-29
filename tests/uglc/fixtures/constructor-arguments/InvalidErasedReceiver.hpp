#pragma once
#include "ArgumentDiagnosticBase.hpp"
/** Erases the shader-class identity needed to determine the constructor argument prefix. */
inline void invalidErasedCall(IRenderClass &render) { render.draw(1, 1, 0, 0); }
