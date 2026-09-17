#ifndef GVM_THREE_PHASE1_BATCH_DATA_HPP
#define GVM_THREE_PHASE1_BATCH_DATA_HPP

#include "UGL.h"

using namespace UGL;

/** Stores one standalone simple-scene vertex shared by generated and host code. */
struct Phase1BatchSimpleVertex
{
    float4 position [[Attribute0]];
    float4 color [[Attribute1]];
    float4 uvAndNormal [[Attribute2]];
};

#endif
