#ifndef GVM_TEST_RHI_SPD_HIZ_READBACK_HPP
#define GVM_TEST_RHI_SPD_HIZ_READBACK_HPP

#include "UGL.h"

using namespace UGL;

namespace SpdHiZReadback
{
    static const uint TileSize = 64u;
    static const uint LocalSizeX = 16u;
    static const uint LocalSizeY = 16u;
    static const uint ThreadCount = 256u;
    static const uint MaxMipBindings = 13u;
    static const uint ReduceMin = 0u;
    static const uint ReduceMax = 1u;

    struct Params
    {
        uint2 sourceSize;
        uint mipCount = 0u;
        uint reduceMode = ReduceMin;

        uint2 workgroupCount;
        uint totalWorkgroupCount = 0u;
        uint pad0 = 0u;
    };

    struct SpdHiZBindGroup final : public IBindGroup
    {
        constructor(Texture2D<TextureFormat::Depth32Float> sourceDepth [[Binding0]],
                    RWTexture2D<TextureFormat::R16Float> mip0 [[Binding1]],
                    RWTexture2D<TextureFormat::R16Float> mip1 [[Binding2]],
                    RWTexture2D<TextureFormat::R16Float> mip2 [[Binding3]],
                    RWTexture2D<TextureFormat::R16Float> mip3 [[Binding4]],
                    RWTexture2D<TextureFormat::R16Float> mip4 [[Binding5]],
                    RWTexture2D<TextureFormat::R16Float> mip5 [[Binding6]],
                    RWTexture2D<TextureFormat::R16Float> mip6 [[Binding7]],
                    RWTexture2D<TextureFormat::R16Float> mip7 [[Binding8]],
                    RWTexture2D<TextureFormat::R16Float> mip8 [[Binding9]],
                    RWTexture2D<TextureFormat::R16Float> mip9 [[Binding10]],
                    RWTexture2D<TextureFormat::R16Float> mip10 [[Binding11]],
                    RWTexture2D<TextureFormat::R16Float> mip11 [[Binding12]],
                    RWTexture2D<TextureFormat::R16Float> mip12 [[Binding13]],
                    RWStructuredBuffer<uint> counter [[Binding14]],
                    UniformBuffer<Params> paramsBuffer [[Binding15]])
        {
        }
    };

    inline uint ceilDiv(uint value, uint divisor)
    {
        return (value + divisor - 1u) / divisor;
    }

    inline uint mipDim1D(uint value, uint mip)
    {
        const uint dim = value >> mip;
        return dim > 0u ? dim : 1u;
    }

    inline uint2 mipSize(uint2 sourceSize, uint mip)
    {
        return uint2(mipDim1D(sourceSize.x, mip), mipDim1D(sourceSize.y, mip));
    }

    inline half reducerIdentity(uint reduceMode)
    {
        return reduceMode == ReduceMax ? half(0.0f) : half(1.0f);
    }

    inline half reduceDepth(half a, half b, uint reduceMode)
    {
        return reduceMode == ReduceMax ? max(a, b) : min(a, b);
    }

    inline half reduceDepth4(half v0, half v1, half v2, half v3, uint reduceMode)
    {
        return reduceDepth(reduceDepth(v0, v1, reduceMode), reduceDepth(v2, v3, reduceMode), reduceMode);
    }

    inline half safeLoadDepth(Texture2D<TextureFormat::Depth32Float> depthTex, uint2 coord, uint2 sourceSize, uint reduceMode)
    {
        if (coord.x >= sourceSize.x || coord.y >= sourceSize.y)
        {
            return reducerIdentity(reduceMode);
        }
        return half(depthTex->read(coord).x);
    }

    inline half safeLoadMip(RWTexture2D<TextureFormat::R16Float> mipTex, uint2 coord, uint2 mipDim, uint reduceMode)
    {
        if (coord.x >= mipDim.x || coord.y >= mipDim.y)
        {
            return reducerIdentity(reduceMode);
        }
        return half(mipTex->read(coord).r);
    }

    inline void safeStoreMip(RWTexture2D<TextureFormat::R16Float> mipTex, uint2 coord, uint2 mipDim, half value)
    {
        if (coord.x < mipDim.x && coord.y < mipDim.y)
        {
            mipTex->write(coord, value);
        }
    }

    inline half reduceDepth2x2FromSource(Texture2D<TextureFormat::Depth32Float> sourceDepth, uint2 coord, uint2 sourceSize, uint reduceMode)
    {
        half d00 = safeLoadDepth(sourceDepth, coord + uint2(0u, 0u), sourceSize, reduceMode);
        half d10 = safeLoadDepth(sourceDepth, coord + uint2(1u, 0u), sourceSize, reduceMode);
        half d01 = safeLoadDepth(sourceDepth, coord + uint2(0u, 1u), sourceSize, reduceMode);
        half d11 = safeLoadDepth(sourceDepth, coord + uint2(1u, 1u), sourceSize, reduceMode);
        return reduceDepth4(d00, d10, d01, d11, reduceMode);
    }

    inline half reduceDepth2x2FromMip(RWTexture2D<TextureFormat::R16Float> mip, uint2 coord, uint2 mipDim, uint reduceMode)
    {
        half d00 = safeLoadMip(mip, coord + uint2(0u, 0u), mipDim, reduceMode);
        half d10 = safeLoadMip(mip, coord + uint2(1u, 0u), mipDim, reduceMode);
        half d01 = safeLoadMip(mip, coord + uint2(0u, 1u), mipDim, reduceMode);
        half d11 = safeLoadMip(mip, coord + uint2(1u, 1u), mipDim, reduceMode);
        return reduceDepth4(d00, d10, d01, d11, reduceMode);
    }

    class [[LocalWorkGroupSize(16, 16, 1)]] Pass final : public IComputeClass
    {
    public:
        constructor(BindGroup<SpdHiZBindGroup> bindGroup [[Slot0]])
        {
        }

    private:
        void compute(uint3 DTid [[DispatchThreadID]], uint GI [[GroupIndex]], uint3 GTid [[GroupThreadID]], uint3 Gid [[GroupID]])
        {
            GroupShared<half> lds[ThreadCount];
            GroupShared<uint> isLastGroupShared;

            const Params params = bindGroup->paramsBuffer->read();
            const uint reduceMode = params.reduceMode;
            const uint2 sourceSize = params.sourceSize;

            const uint localX = GTid.x;
            const uint localY = GTid.y;
            const uint2 tileBase = Gid.xy * TileSize;
            const uint2 threadBase = tileBase + uint2(localX * 4u, localY * 4u);

            const uint2 mip0Dim = mipSize(sourceSize, 0u);
            const uint2 mip1Dim = mipSize(sourceSize, 1u);
            const uint2 mip2Dim = mipSize(sourceSize, 2u);
            const uint2 mip3Dim = mipSize(sourceSize, 3u);
            const uint2 mip4Dim = mipSize(sourceSize, 4u);
            const uint2 mip5Dim = mipSize(sourceSize, 5u);
            const uint2 mip6Dim = mipSize(sourceSize, 6u);
            const uint2 mip7Dim = mipSize(sourceSize, 7u);
            const uint2 mip8Dim = mipSize(sourceSize, 8u);
            const uint2 mip9Dim = mipSize(sourceSize, 9u);
            const uint2 mip10Dim = mipSize(sourceSize, 10u);
            const uint2 mip11Dim = mipSize(sourceSize, 11u);
            const uint2 mip12Dim = mipSize(sourceSize, 12u);

            if (params.mipCount > 0u)
            {
                for (uint y = 0u; y < 4u; y++)
                {
                    for (uint x = 0u; x < 4u; x++)
                    {
                        const uint2 p = threadBase + uint2(x, y);
                        if (p.x < sourceSize.x && p.y < sourceSize.y)
                        {
                            const half d = half(bindGroup->sourceDepth->read(p).x);
                            safeStoreMip(bindGroup->mip0, p, mip0Dim, d);
                        }
                    }
                }
            }

            const half m1_00 = reduceDepth2x2FromSource(bindGroup->sourceDepth, threadBase + uint2(0u, 0u), sourceSize, reduceMode);
            const half m1_10 = reduceDepth2x2FromSource(bindGroup->sourceDepth, threadBase + uint2(2u, 0u), sourceSize, reduceMode);
            const half m1_01 = reduceDepth2x2FromSource(bindGroup->sourceDepth, threadBase + uint2(0u, 2u), sourceSize, reduceMode);
            const half m1_11 = reduceDepth2x2FromSource(bindGroup->sourceDepth, threadBase + uint2(2u, 2u), sourceSize, reduceMode);

            const uint2 mip1Base = (tileBase / 2u) + uint2(localX * 2u, localY * 2u);
            if (params.mipCount > 1u)
            {
                safeStoreMip(bindGroup->mip1, mip1Base + uint2(0u, 0u), mip1Dim, m1_00);
                safeStoreMip(bindGroup->mip1, mip1Base + uint2(1u, 0u), mip1Dim, m1_10);
                safeStoreMip(bindGroup->mip1, mip1Base + uint2(0u, 1u), mip1Dim, m1_01);
                safeStoreMip(bindGroup->mip1, mip1Base + uint2(1u, 1u), mip1Dim, m1_11);
            }

            const half m2 = reduceDepth4(m1_00, m1_10, m1_01, m1_11, reduceMode);
            const uint2 mip2Coord = (tileBase / 4u) + uint2(localX, localY);
            if (params.mipCount > 2u)
            {
                safeStoreMip(bindGroup->mip2, mip2Coord, mip2Dim, m2);
            }
            lds[GI] = m2;

            GroupMemoryBarrierWithGroupSync();

            if (GI < 64u)
            {
                const uint2 localOut = uint2(GI & 7u, GI >> 3u);
                const uint srcX = localOut.x * 2u;
                const uint srcY = localOut.y * 2u;
                const half v = reduceDepth4(
                    lds[srcY * 16u + srcX],
                    lds[srcY * 16u + srcX + 1u],
                    lds[(srcY + 1u) * 16u + srcX],
                    lds[(srcY + 1u) * 16u + srcX + 1u],
                    reduceMode);

                if (params.mipCount > 3u)
                {
                    safeStoreMip(bindGroup->mip3, (tileBase / 8u) + localOut, mip3Dim, v);
                }
                lds[GI] = v;
            }

            GroupMemoryBarrierWithGroupSync();

            if (GI < 16u)
            {
                const uint2 localOut = uint2(GI & 3u, GI >> 2u);
                const uint srcX = localOut.x * 2u;
                const uint srcY = localOut.y * 2u;
                const half v = reduceDepth4(
                    lds[srcY * 8u + srcX],
                    lds[srcY * 8u + srcX + 1u],
                    lds[(srcY + 1u) * 8u + srcX],
                    lds[(srcY + 1u) * 8u + srcX + 1u],
                    reduceMode);

                if (params.mipCount > 4u)
                {
                    safeStoreMip(bindGroup->mip4, (tileBase / 16u) + localOut, mip4Dim, v);
                }
                lds[GI] = v;
            }

            GroupMemoryBarrierWithGroupSync();

            if (GI < 4u)
            {
                const uint2 localOut = uint2(GI & 1u, GI >> 1u);
                const uint srcX = localOut.x * 2u;
                const uint srcY = localOut.y * 2u;
                const half v = reduceDepth4(
                    lds[srcY * 4u + srcX],
                    lds[srcY * 4u + srcX + 1u],
                    lds[(srcY + 1u) * 4u + srcX],
                    lds[(srcY + 1u) * 4u + srcX + 1u],
                    reduceMode);

                if (params.mipCount > 5u)
                {
                    safeStoreMip(bindGroup->mip5, (tileBase / 32u) + localOut, mip5Dim, v);
                }
                lds[GI] = v;
            }

            GroupMemoryBarrierWithGroupSync();
            if (GI == 0u)
            {
                const half v = reduceDepth4(lds[0], lds[1], lds[2], lds[3], reduceMode);
                if (params.mipCount > 6u)
                {
                    safeStoreMip(bindGroup->mip6, Gid.xy, mip6Dim, v);
                }
                lds[0] = v;
            }

            DeviceMemoryBarrierWithGroupSync();

            if (GI == 0u)
            {
                const uint previous = atomicAdd(bindGroup->counter[0], 1u);
                isLastGroupShared = (previous + 1u == params.totalWorkgroupCount) ? 1u : 0u;
            }

            GroupMemoryBarrierWithGroupSync();

            if (isLastGroupShared != 0u && params.mipCount > 7u)
            {
                const uint2 mip7Base = uint2(localX * 2u, localY * 2u);

                const half t7_00 = reduceDepth2x2FromMip(bindGroup->mip6, (mip7Base + uint2(0u, 0u)) * 2u, mip6Dim, reduceMode);
                const half t7_10 = reduceDepth2x2FromMip(bindGroup->mip6, (mip7Base + uint2(1u, 0u)) * 2u, mip6Dim, reduceMode);
                const half t7_01 = reduceDepth2x2FromMip(bindGroup->mip6, (mip7Base + uint2(0u, 1u)) * 2u, mip6Dim, reduceMode);
                const half t7_11 = reduceDepth2x2FromMip(bindGroup->mip6, (mip7Base + uint2(1u, 1u)) * 2u, mip6Dim, reduceMode);

                safeStoreMip(bindGroup->mip7, mip7Base + uint2(0u, 0u), mip7Dim, t7_00);
                safeStoreMip(bindGroup->mip7, mip7Base + uint2(1u, 0u), mip7Dim, t7_10);
                safeStoreMip(bindGroup->mip7, mip7Base + uint2(0u, 1u), mip7Dim, t7_01);
                safeStoreMip(bindGroup->mip7, mip7Base + uint2(1u, 1u), mip7Dim, t7_11);

                const half t8 = reduceDepth4(t7_00, t7_10, t7_01, t7_11, reduceMode);
                if (params.mipCount > 8u)
                {
                    safeStoreMip(bindGroup->mip8, uint2(localX, localY), mip8Dim, t8);
                }
                lds[GI] = t8;

                GroupMemoryBarrierWithGroupSync();
                if (GI < 64u)
                {
                    const uint2 localOut = uint2(GI & 7u, GI >> 3u);
                    const uint srcX = localOut.x * 2u;
                    const uint srcY = localOut.y * 2u;
                    const half v = reduceDepth4(
                        lds[srcY * 16u + srcX],
                        lds[srcY * 16u + srcX + 1u],
                        lds[(srcY + 1u) * 16u + srcX],
                        lds[(srcY + 1u) * 16u + srcX + 1u],
                        reduceMode);

                    if (params.mipCount > 9u)
                    {
                        safeStoreMip(bindGroup->mip9, localOut, mip9Dim, v);
                    }
                    lds[GI] = v;
                }

                GroupMemoryBarrierWithGroupSync();
                if (GI < 16u)
                {
                    const uint2 localOut = uint2(GI & 3u, GI >> 2u);
                    const uint srcX = localOut.x * 2u;
                    const uint srcY = localOut.y * 2u;
                    const half v = reduceDepth4(
                        lds[srcY * 8u + srcX],
                        lds[srcY * 8u + srcX + 1u],
                        lds[(srcY + 1u) * 8u + srcX],
                        lds[(srcY + 1u) * 8u + srcX + 1u],
                        reduceMode);

                    if (params.mipCount > 10u)
                    {
                        safeStoreMip(bindGroup->mip10, localOut, mip10Dim, v);
                    }
                    lds[GI] = v;
                }

                GroupMemoryBarrierWithGroupSync();
                if (GI < 4u)
                {
                    const uint2 localOut = uint2(GI & 1u, GI >> 1u);
                    const uint srcX = localOut.x * 2u;
                    const uint srcY = localOut.y * 2u;
                    const half v = reduceDepth4(
                        lds[srcY * 4u + srcX],
                        lds[srcY * 4u + srcX + 1u],
                        lds[(srcY + 1u) * 4u + srcX],
                        lds[(srcY + 1u) * 4u + srcX + 1u],
                        reduceMode);

                    if (params.mipCount > 11u)
                    {
                        safeStoreMip(bindGroup->mip11, localOut, mip11Dim, v);
                    }
                    lds[GI] = v;
                }

                GroupMemoryBarrierWithGroupSync();
                if (GI == 0u)
                {
                    const half v = reduceDepth4(lds[0], lds[1], lds[2], lds[3], reduceMode);
                    if (params.mipCount > 12u)
                    {
                        safeStoreMip(bindGroup->mip12, uint2(0u, 0u), mip12Dim, v);
                    }
                }
            }
        }
    };
} // namespace SpdHiZReadback

#endif // GVM_TEST_RHI_SPD_HIZ_READBACK_HPP
