#include "UGL.h"
using namespace UGL;

/** Requires a scope-exit barrier that must not be silently discarded. */
struct BarrierGuard
{
    uint value;
    /** Synchronizes at scope exit to expose missing destructor lowering. */
    ~BarrierGuard() { GroupMemoryBarrierWithGroupSync(); }
};

/** Declares an entry with a shader-reachable unsupported cleanup. */
class [[LocalWorkGroupSize(1, 1, 1)]] InvalidDestructorPass : public IComputeClass
{
public:
    /** Creates the pass without resource bindings. */
    constructor() {}
private:
    /** Exercises local destruction through ordinary C++ scope exit. */
    void compute(uint3 threadID [[DispatchThreadID]]) { BarrierGuard guard; }
};
