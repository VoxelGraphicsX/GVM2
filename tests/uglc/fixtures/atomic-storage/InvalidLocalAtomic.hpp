#pragma once
#include "UGL.h"
using namespace UGL;

/** Rejects an ordinary invocation-local scalar as an atomic storage target. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidLocalAtomic final : public IComputeClass
{
public:
    /** Creates the invalid shader without resource bindings. */
    constructor() {}
private:
    /** Attempts an atomic update on a plain local value. */
    void compute()
    {
        uint local = 0u;
        atomicAdd(local, 1u);
    }
};
