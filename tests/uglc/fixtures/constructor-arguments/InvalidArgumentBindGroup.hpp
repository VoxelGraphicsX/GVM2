#pragma once
#include "UGL.h"
using namespace UGL;

/** Contains ordinary resource bindings that belong on a Slot class instead of an Args field. */
struct NestedArgumentBindings final : public IBindGroup
{
    /** Defines a normal resource binding for the rejected group declarations. */
    constructor(RWStructuredBuffer<uint> output [[Binding0]]) {}
};

/** Rejects a BindGroup stored inside an ordinary Args structure. */
struct InvalidArgumentGroupField
{
    BindGroup<NestedArgumentBindings> bindings;
};

/** Rejects a BindGroup reachable through a nested Args field. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentNestedBindGroup final : public IComputeClass
{
public:
    /** Declares the aggregate whose field carries the forbidden group. */
    constructor(InvalidArgumentGroupField field [[Args0]]) {}
private:
    /** Intentionally reads the nested group so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { field.bindings->output[id.x] = id.x; }
};

/** Rejects a BindGroup declared as a direct constructor argument. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentBindGroup final : public IComputeClass
{
public:
    /** Declares the forbidden group parameter involved in this diagnostic. */
    constructor(BindGroup<NestedArgumentBindings> bindings [[Args0]]) {}
private:
    /** Intentionally reads the rejected group so its source location is diagnosed. */
    void compute(uint3 id [[DispatchThreadID]]) { bindings->output[id.x] = id.x; }
};
