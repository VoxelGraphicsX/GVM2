#ifndef CHECKER_BOARD_BACKGROUND_HPP
#define CHECKER_BOARD_BACKGROUND_HPP
// The MIT License
// Copyright © 2017 Inigo Quilez
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal IN the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be
// included IN all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Similar to https://www.shadertoy.com/view/XlXBWs, but with a triangular filter kernel (right),
// which produces less flickering animations that a box filter (center). Luckily, it's still easily
// integrable analytically!
//
// Info: https://iquilezles.org/articles/filterableprocedurals
//
// More filtered patterns:  https://www.shadertoy.com/playlist/l3KXR1
// From: https://www.shadertoy.com/view/llffWs

#include "UGL.h"
using namespace UGL;
#include "Camera.hpp"

// --- analytically triangle-filtered checkerboard ---
namespace Checker
{
    float3 pri(IN float3 x)
    {
        float3 h = frac(x / 2.0f) - 0.5f;
        return x * 0.5f + h * (1.0f - 2.0f * abs(h));
    }

    float checkersTextureGradTri(IN float3 p, IN float3 ddx, IN float3 ddy)
    {
        float3 w = max(abs(ddx), abs(ddy)) + 0.001f;
        float3 i = (pri(p + w) - 2.0f * pri(p) + pri(p - w)) / (w * w);
        return 0.5f - 0.5f * i.x * i.y * i.z;
    }

    float3 tri(IN float3 x)
    {
        float3 h = frac(x / 2.0f) - 0.5f;
        return 1.0f - 2.0f * abs(h);
    }

    float checkersTextureGradBox(IN float3 p, IN float3 ddx, IN float3 ddy)
    {
        float3 w = max(abs(ddx), abs(ddy)) + 0.001f;
        float3 i = (tri(p + 0.5f * w) - tri(p - 0.5f * w)) / w;
        return 0.5f - 0.5f * i.x * i.y * i.z;
    }

    float checkersTexture(IN float3 p)
    {
        float3 q = floor(p);
        return fmod(q.x + q.y + q.z, 2.0f);
    }

    float intersectLWC(float3 ro, float3 rd, OUT float3 pos, OUT float3 nor, OUT float occ, OUT float matid)
    {
        float tmin = 10000.0f;
        nor = float3(0.0f, 0.0f, 0.0f);
        occ = 1.0f;
        pos = float3(0.0f, 0.0f, 0.0f);

        float h = (0.01f - ro.y) / rd.y;
        if (h > 0.0f)
        {
            tmin = h;
            nor = float3(0.0f, 1.0f, 0.0f);
            pos = ro + h * rd;
            matid = 0.0f;
        }

        return tmin;
    }

    float3 texCoords(IN float3 p)
    {
        return 1.0f * p;
    }

    struct RayLWC
    {
        float3 origin;
        float3 direction;
        float tmin;
        float tmax;
    };

    RayLWC constructRayFromCamera(IN uint2 index, IN uint2 resolution, IN Camera cam)
    {
        const float2 pixelCenter = float2((float2)index.xy) + float2(0.5f, 0.5f);
        const float2 inUV = pixelCenter / float2(resolution.xy);
        float2 d = inUV * 2.0f - 1.0f;
        float4 target = mul(cam.projInv, float4(d.x, d.y, 1, 1));

        RayLWC rayDesc;
        rayDesc.origin = mul(cam.viewInv, float4(0, 0, 0, 1)).xyz;
        rayDesc.direction = mul(cam.viewInv, float4(normalize((float3)target.xyz), 0)).xyz;
        rayDesc.tmin = 0.001;
        rayDesc.tmax = 10000.0;

        return rayDesc;
    }

    float3 doLighting(IN float3 pos, IN float3 nor, IN float occ, IN float3 rd)
    {
        float sh = 1.0f;
        float dif = saturate(dot(nor, float3(0.57703f, 0.57703f, 0.57703f)));
        float bac = saturate(dot(nor, float3(-0.707f, 0.0f, -0.707f)));

        float3 lin = dif * float3(1.50f, 1.40f, 1.30f) * sh;
        lin += occ * float3(0.15f, 0.20f, 0.30f);
        lin += bac * float3(0.10f, 0.10f, 0.10f);

        return lin;
    }

} // namespace Checker
struct CheckBoardGBufferBindGroup final : public IBindGroup
{
    constructor(RWTexture2D<UGL::TextureFormat::RGBA8Unorm> albedoTexture [[Binding0]], RWTexture2D<UGL::TextureFormat::R32Float> depthTexture [[Binding1]])
    {
    }
};

class [[LocalWorkGroupSize(8, 8, 1)]] CheckerBoardBackground final : public IComputeClass
{
public:
    constructor(BindGroup<CameraBindGroup> camBindGroup [[Slot0]], BindGroup<CheckBoardGBufferBindGroup> frameBufferBindGroup [[Slot1]])
    {
    }

private:
    void compute(uint3 ThreadID [[DispatchThreadID]])
    {
        const Camera cam = camBindGroup->camBuffer->read();
        uint2 resolution;
        frameBufferBindGroup->albedoTexture->getDimensions(resolution.x, resolution.y);
        if (ThreadID.x >= resolution.x || ThreadID.y >= resolution.y)
        {
            return;
        }
        Checker::RayLWC ray = Checker::constructRayFromCamera(ThreadID.xy, resolution, cam);
        Checker::RayLWC ray_ddx = Checker::constructRayFromCamera(ThreadID.xy + uint2(1, 0), resolution, cam);
        Checker::RayLWC ray_ddy = Checker::constructRayFromCamera(ThreadID.xy + uint2(0, 1), resolution, cam);
        float3 pos;
        float3 nor;
        float occ;
        float matid;
        float t = Checker::intersectLWC(ray.origin, ray.direction, pos, nor, occ, matid);

        float4 clipPos = mul(camBindGroup->camBuffer->proj, mul(camBindGroup->camBuffer->view, float4(pos, 1.0)));
        float depth = .0; //

        float3 col = float3(0.9);
        if (t < 10000.0)
        {

            // -----------------------------------------------------------------------
            // compute ray differentials by intersecting the tangent plane to the
            // surface.
            // -----------------------------------------------------------------------

            // computer ray differentials
            float3 ddx_pos = ray_ddx.origin - ray_ddx.direction * dot(ray_ddx.origin - pos, nor) / dot(ray_ddx.direction, nor);
            float3 ddy_pos = ray_ddy.origin - ray_ddy.direction * dot(ray_ddy.origin - pos, nor) / dot(ray_ddy.direction, nor);

            // calc texture sampling footprint
            float3 uvw = Checker::texCoords(pos);
            float3 ddx_uvw = Checker::texCoords(ddx_pos) - uvw;
            float3 ddy_uvw = Checker::texCoords(ddy_pos) - uvw;

            // shading
            float3 mate = float3(0.0);

            mate = float3(1.0) * Checker::checkersTextureGradTri(uvw, ddx_uvw, ddy_uvw);

            // lighting
            float3 lin = Checker::doLighting(pos, nor, occ, ray.direction);

            // combine lighting with material
            col = mate * lin;

            // fog
            col = lerp(col, float3(0.9f), 1.0f - exp(-0.000001f * t * t));
            depth = clipPos.z / clipPos.w;
        }

        // gamma correction
        col = pow(col, float3(0.4545f));

        // depth = depth * .5 + .5;
        frameBufferBindGroup->albedoTexture->write(ThreadID.xy, half4(col.x, col.y, col.z, 1.0f));

        frameBufferBindGroup->depthTexture->write(ThreadID.xy, depth);
    }
};

#endif // CHECKER_BOARD_BACKGROUND_HPP
