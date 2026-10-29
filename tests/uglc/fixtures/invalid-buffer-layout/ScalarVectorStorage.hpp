#pragma once
#include "UGL.h"
using namespace UGL;
/** Exposes a buffer layout that cannot preserve the shared Host and shader ABI. */
struct LayoutPayload { uint tag; float4 value; };
/** Binds input and independent readback output. */
struct LayoutBindings final : public IBindGroup {
/** Creates the layout probe resources. */
constructor(StructuredBuffer<LayoutPayload> data [[Binding0]], RWStructuredBuffer<uint> output [[Binding1]]) {}
};
/** Reads one field from dynamically selected input records. */
class [[LocalWorkGroupSize(1,1,1)]] LayoutProbe final : public IComputeClass {
public:
/** Binds the probe resources. */
constructor(BindGroup<LayoutBindings> bindings [[Slot0]]) {}
private:
/** Writes the observed input field without arithmetic transformations. */
void compute(uint3 threadID [[DispatchThreadID]]) { uint lane = threadID.x; bindings->output[lane] = uint(bindings->data[lane].value.x); }
};
