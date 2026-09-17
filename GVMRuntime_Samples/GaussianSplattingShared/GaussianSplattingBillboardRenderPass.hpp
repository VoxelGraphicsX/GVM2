#pragma once

/**
 * Renders sorted Gaussian splats as alpha-blended screen-space billboards.
 *
 * Include this header inside namespace GsViewer for samples whose
 * ProjectedGaussianRender payload stores centerOpacitySupportScale and whose
 * GaussianRasterVertexOutput payload stores colorOpacity.
 */
class GaussianBillboardRenderPass final : public IRenderClass
{
public:
    /**
     * Configures additive alpha blending and binds the projected splat and sort-entry buffers.
     */
    constructor(BindGroup<ViewerGlobalsBindGroup> viewerGlobalsBindGroup [[Slot0]],
                BindGroup<ProjectedRenderReadBindGroup> projectedReadBindGroup [[Slot1]],
                BindGroup<EntryReadBindGroup> entryReadBindGroup [[Slot2]]
    )
    {
        setPrimitiveTopology(PrimitiveTopology::TriangleStrip);
        BlendState blendState = {};
        blendState.color.operation = BlendOperation::Add;
        blendState.color.srcFactor = BlendFactor::One;
        blendState.color.dstFactor = BlendFactor::OneMinusSrcAlpha;
        blendState.alpha.operation = BlendOperation::Add;
        blendState.alpha.srcFactor = BlendFactor::One;
        blendState.alpha.dstFactor = BlendFactor::OneMinusSrcAlpha;
        setBlendState(0u, blendState);
        setCullMode(CullMode::None);
    }

private:
    /**
     * Expands one sorted splat entry into one corner of its screen-space quad.
     */
    GaussianRasterVertexOutput vertex(uint vid [[VertexID]], uint instanceID [[InstanceID]])
    {
        GaussianRasterVertexOutput output;
        output.pos = float4(-2.0f, -2.0f, 0.0f, 1.0f);
        output.colorOpacity = float4(0.0f);
        output.localFragPos = float2(0.0f);

        const ViewerGlobals globals = viewerGlobalsBindGroup->globals->read();
        if (vid >= 4u)
        {
            return output;
        }

        const SortEntry entry = entryReadBindGroup->entries[instanceID];
        if (entry.splatIndex >= globals.sceneInfo.y)
        {
            return output;
        }

        const ProjectedGaussianRender projected = projectedReadBindGroup->projected[entry.splatIndex];
        const float opacity = projected.centerOpacitySupportScale.z;
        if (opacity <= MinVisibleAlpha)
        {
            return output;
        }

        const float supportScale = projected.centerOpacitySupportScale.w;
        if (supportScale <= 0.0f)
        {
            return output;
        }

        const float2 centerPx = projected.centerOpacitySupportScale.xy;
        const float2 majorAxis = projected.quadAxis01.xy;
        const float2 minorAxis = projected.quadAxis01.zw;
        float2 cornerSign = float2(-1.0f, -1.0f);
        if (vid == 1u)
        {
            cornerSign = float2(1.0f, -1.0f);
        }
        else if (vid == 2u)
        {
            cornerSign = float2(-1.0f, 1.0f);
        }
        else if (vid == 3u)
        {
            cornerSign = float2(1.0f, 1.0f);
        }

        const float2 pixelCoord = centerPx + cornerSign.x * majorAxis + cornerSign.y * minorAxis;

        output.pos = float4(
            pixelEdgeToNdc(pixelCoord.x, globals.imageInfo.x),
            pixelEdgeToNdc(pixelCoord.y, globals.imageInfo.y),
            0.0f,
            1.0f
        );
        output.colorOpacity = float4(projected.color.xyz, opacity);
        output.localFragPos = cornerSign * supportScale;
        return output;
    }

    /**
     * Evaluates the Gaussian alpha falloff and writes premultiplied color.
     */
    GaussianRasterFrameBuffer fragment(GaussianRasterVertexOutput input)
    {
        GaussianRasterFrameBuffer output;
        output.color = half4(0.0f);

        const float localDistanceSquared = dot(input.localFragPos, input.localFragPos);
        const float alpha = min(0.99f, input.colorOpacity.w * exp(-0.5f * localDistanceSquared));
        if (alpha < MinVisibleAlpha)
        {
            return output;
        }

        output.color = half4(input.colorOpacity.xyz * alpha, alpha);
        return output;
    }
};
