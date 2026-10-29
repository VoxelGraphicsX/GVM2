#pragma once
#include "UGL.h"
using namespace UGL;

/** Exposes a normal C++ mutator that requires a writable object. */
struct ArgumentMethodSettings
{
    uint value;
    /** Modifies this settings instance when it is an invocation-local copy. */
    void mutate() { value = 1; }
};

/** Attempts to invoke a non-const mutator on a readonly Args object. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidArgumentMethodWrite final : public IComputeClass
{
public:
    /** Receives ordinary settings as one readonly shader input. */
    constructor(ArgumentMethodSettings settings [[Args0]]) {}
private:
    /** Requires Clang to diagnose the const implicit object instead of losing its Args origin. */
    void compute(uint3 id [[DispatchThreadID]])
    {
        auto local = settings;
        local.mutate();
        settings.mutate();
    }
};
