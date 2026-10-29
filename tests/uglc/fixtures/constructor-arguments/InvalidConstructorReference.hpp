#pragma once
#include "ArgumentDiagnosticBase.hpp"

/** Intentionally declares a shader-visible constructor with an ordinary reference parameter. */
struct ArgumentReferenceConstructor
{
    uint value;
    /** Declares a forbidden reference signature without a constructor body. */
    ArgumentReferenceConstructor(uint &value);
};

/** Demonstrates that declaration-only constructors cannot hide reference parameters. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidConstructorReference final : public IComputeClass
{
public:
    /** Requires one ordinary input value. */
    constructor(uint value [[Args0]]) {}
private:
    /** Intentionally constructs a local object using a forbidden constructor signature. */
    void compute(uint3 id [[DispatchThreadID]]) { uint local = value; ArgumentReferenceConstructor result(local); }
};
