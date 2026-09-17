#ifndef GVM_THREE_WEBGL_POSTPROCESSING_HPP
#define GVM_THREE_WEBGL_POSTPROCESSING_HPP

#include "UGL.h"
#include "WebglPostprocessingData.hpp"

using namespace UGL;

/** Stores one ordinary child Mesh transform in view and clip spaces. */
struct WebglPostprocessingRgbHalftoneObjectData
{
    float4x4 modelViewProjection;
    float4x4 modelView;
    float4x4 normalModelView;
};

/** Stores the mandatory identity instance payload. */
struct WebglPostprocessingRgbHalftoneInstanceData
{
    float4 reserved;
};

/** Stores the shared white Phong material constants. */
struct WebglPostprocessingRgbHalftoneMaterialData
{
    float4 diffuseAndShininess;
    float4 specular;
};

/** Stores the runtime DotScreen uniforms to preserve WebGL shader evaluation. */
struct WebglPostprocessingRgbHalftoneEffectParameters
{
    float4 dimensionsAndRadius;
    float4 channelAngles;
    float4 controls;
    float4 flags;
};

/** Defines the unique Scene RenderSet containing the floor and 50 box Mesh entities. */
struct WebglPostprocessingRgbHalftoneSceneRenderSet : public IRenderSet
{
    /** Declares the frozen five-component Scene ABI. */
    constructor(
        BufferComponent<WebglPostprocessingRgbHalftoneVertex>
            vertices [[RenderSetVertexBuffer]],
        BufferComponent<uint> indices [[RenderSetIndexBuffer]],
        BufferComponent<WebglPostprocessingRgbHalftoneObjectData> objects,
        BufferComponent<WebglPostprocessingRgbHalftoneInstanceData> instances,
        BufferComponent<WebglPostprocessingRgbHalftoneMaterialData> materials)
    {
    }
};

/** Binds one fullscreen half-float source texture. */
struct WebglPostprocessingRgbHalftoneScreenResources final : public IBindGroup
{
    /** Declares the source texture and linear clamp sampler. */
    constructor(
        Texture2D<float4> source [[Binding0]],
        Sampler sourceSampler [[Binding1]],
        UniformBuffer<WebglPostprocessingRgbHalftoneEffectParameters>
            parameters [[Binding2]])
    {
    }
};

/** Carries view-space Phong and fog inputs. */
struct WebglPostprocessingRgbHalftoneSceneOutput
{
    float4 position [[Position]];
    float3 normal [[Attribute0]];
    float2 uv [[Attribute1]];
    uint entityID [[Attribute2]];
    float3 viewPosition [[Attribute3]];
    float3 viewNormal [[Attribute4]];
};

/** Carries fullscreen coordinates. */
struct WebglPostprocessingRgbHalftoneScreenOutput
{
    float4 position [[Position]];
    float2 uv [[Attribute0]];
};

/** Defines the linear Scene and postprocess target. */
struct WebglPostprocessingRgbHalftoneLinearFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
    DepthStencilAttachment<TextureFormat::Depth32Float> depth;
};

/** Defines a color-only linear postprocess target. */
struct WebglPostprocessingRgbHalftoneLinearColorFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA16Float> color;
};

/** Defines the final ordinary single-sample RGBA8 output. */
struct WebglPostprocessingRgbHalftoneOutputFrameBuffer final : public IFrameBuffer
{
    ColorAttachment<TextureFormat::RGBA8Unorm> color;
};

/** Preserves one linear channel for the ordinary capture attachment. */
float webglPostprocessingLinearToSrgb(float value)
{
    return max(value, 0.0f);
}

/** Emits the shared fullscreen triangle. */
WebglPostprocessingRgbHalftoneScreenOutput webglPostprocessingFullscreen(uint vertexID)
{
    const float2 positionUv = float2(
        (vertexID << 1u) & 2u,
        vertexID & 2u);
    WebglPostprocessingRgbHalftoneScreenOutput outputValue;
    outputValue.position = float4(positionUv * 2.0f - 1.0f, 0.0f, 1.0f);
    outputValue.uv = positionUv;
    return outputValue;
}

/** Evaluates Three r185's optimized Schlick Fresnel term. */
float3 webglPostprocessingFresnel(float3 f0, float dotViewHalf)
{
    const float fresnel =
        exp2((-5.55473f * dotViewHalf - 6.98316f) * dotViewHalf);
    return f0 * (1.0f - fresnel) + float3(fresnel);
}

/** Returns the non-negative fractional part used by HalftoneShader.rand. */
float webglPostprocessingRgbHalftoneFract(float value)
{
    return value - floor(value);
}

/** Reproduces HalftoneShader's deterministic pixel-cell random function. */
float webglPostprocessingRgbHalftoneRand(float2 seed)
{
    return webglPostprocessingRgbHalftoneFract(
        sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453f);
}

/** Returns a positive modulo for the rotated halftone grid. */
float webglPostprocessingRgbHalftonePositiveMod(float value, float step)
{
    return value - floor(value / step) * step;
}

/** Provides the two-argument atan used by the upstream GLSL shader. */
float webglPostprocessingRgbHalftoneAtan2(float y, float x)
{
    const float halfPi = 1.5707963267948966f;
    const float pi = 3.1415926535897932f;
    if (x > 0.0f)
        return atan(y / x);
    if (x < 0.0f && y >= 0.0f)
        return atan(y / x) + pi;
    if (x < 0.0f && y < 0.0f)
        return atan(y / x) - pi;
    return y >= 0.0f ? halfPi : -halfPi;
}

/** Stores the four samples and geometry of one RGB halftone cell. */
struct WebglPostprocessingRgbHalftoneCell
{
    float2 normal;
    float2 p1;
    float2 p2;
    float2 p3;
    float2 p4;
    float samp1;
    float samp2;
    float samp3;
    float samp4;
};

/** Evaluates the shape-specific distance used by HalftoneShader. */
float webglPostprocessingRgbHalftoneDistanceToRadius(
    float channel,
    float2 coordinate,
    float2 normal,
    float2 samplePoint,
    float angle,
    float radius,
    float shape)
{
    const float sqrt2MinusOne = 0.41421356f;
    const float sqrt2HalfMinusOne = 0.20710678f;
    const float pi = 3.14159265f;
    const float dx = coordinate.x - samplePoint.x;
    const float dy = coordinate.y - samplePoint.y;
    float distance = sqrt(dx * dx + dy * dy);
    float dotRadius = channel;
    if (shape < 1.5f)
    {
        dotRadius = pow(abs(dotRadius), 1.125f) * radius;
    }
    else if (shape < 2.5f)
    {
        dotRadius = pow(abs(dotRadius), 1.125f) * radius;
        if (distance != 0.0f)
        {
            const float dotPoint = abs(
                ((samplePoint.x - coordinate.x) / distance) * normal.x +
                ((samplePoint.y - coordinate.y) / distance) * normal.y);
            distance = distance * (1.0f - sqrt2HalfMinusOne) +
                dotPoint * distance * sqrt2MinusOne;
        }
    }
    else if (shape < 3.5f)
    {
        dotRadius = pow(abs(dotRadius), 1.5f) * radius;
        const float dotPoint =
            (samplePoint.x - coordinate.x) * normal.x +
            (samplePoint.y - coordinate.y) * normal.y;
        distance = sqrt(
            normal.x * dotPoint * normal.x * dotPoint +
            normal.y * dotPoint * normal.y * dotPoint);
    }
    else if (shape < 4.5f)
    {
        const float theta = webglPostprocessingRgbHalftoneAtan2(
            samplePoint.y - coordinate.y, samplePoint.x - coordinate.x) - angle;
        const float sineTheta = abs(sin(theta));
        const float cosineTheta = abs(cos(theta));
        dotRadius = pow(abs(dotRadius), 1.4f);
        dotRadius = radius * (dotRadius +
            ((sineTheta > cosineTheta)
                ? dotRadius - sineTheta * dotRadius
                : dotRadius - cosineTheta * dotRadius));
    }
    else
    {
        const float theta = webglPostprocessingRgbHalftoneAtan2(
            samplePoint.y - coordinate.y, samplePoint.x - coordinate.x) - angle -
            pi * 0.25f;
        const float sineTheta = abs(sin(theta));
        const float cosineTheta = abs(cos(theta));
        dotRadius = pow(abs(dotRadius), 1.4f);
        dotRadius = radius * (dotRadius +
            ((sineTheta > cosineTheta)
                ? dotRadius - sineTheta * dotRadius
                : dotRadius - cosineTheta * dotRadius));
    }
    return dotRadius - distance;
}

/** Draws the floor and 50 ordinary child Mesh entities through the unique Scene Set. */
class WebglPostprocessingRgbHalftoneMainPass final : public IRenderClass
{
public:
    /** Binds exactly one Scene RenderSet and conventional opaque depth state. */
    constructor(
        RenderSet<WebglPostprocessingRgbHalftoneSceneRenderSet> sceneSet [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(true);
        setDepthCompareFunction(CompareFunction::LessEqual);
    }

private:
    /** Resolves entity data and prepares flat-shaded view-space inputs. */
    WebglPostprocessingRgbHalftoneSceneOutput vertex(
        WebglPostprocessingRgbHalftoneVertex inputValue [[VertexInput0]],
        uint renderEntityID [[RenderEntityID]],
        uint renderEntityInstanceID [[RenderEntityInstanceID]])
    {
        const WebglPostprocessingRgbHalftoneObjectData objectData =
            sceneSet->objects->get(renderEntityID, 0u);
        const WebglPostprocessingRgbHalftoneInstanceData instanceData =
            sceneSet->instances->get(
                renderEntityID, renderEntityInstanceID);
        (void)instanceData;
        const float4 viewPosition =
            mul(objectData.modelView, inputValue.position);
        // The host stores the projection-only matrix in this component while
        // modelView is supplied separately; applying it to viewPosition gives
        // the intended projection * modelView transform.
        float4 clipPosition = mul(
            objectData.modelViewProjection, viewPosition);
        clipPosition.y = -clipPosition.y;
        clipPosition.z = (clipPosition.z + clipPosition.w) * 0.5f;
        WebglPostprocessingRgbHalftoneSceneOutput outputValue;
        outputValue.position = clipPosition;
        // The upstream ShaderMaterial deliberately uses the object-space
        // normal and UV directly; it is not a lit MeshPhong material.
        outputValue.normal = inputValue.normal.xyz;
        outputValue.uv = inputValue.uv.xy;
        outputValue.entityID = renderEntityID;
        outputValue.viewPosition = viewPosition.xyz;
        outputValue.viewNormal = mul(
            objectData.normalModelView,
            float4(inputValue.normal.xyz, 0.0f)).xyz;
        return outputValue;
    }

    /** Evaluates the r185 ShaderMaterial colour expression. */
    WebglPostprocessingRgbHalftoneLinearFrameBuffer fragment(
        WebglPostprocessingRgbHalftoneSceneOutput inputValue)
    {
        float3 linearColor;
        if (inputValue.entityID == 0u)
        {
            const float3 normal = normalize(inputValue.viewNormal);
            const float3 lightVector = float3(0.0f, 2.0f, -12.0f) -
                inputValue.viewPosition;
            const float distanceSquared = max(dot(lightVector, lightVector), 1.0f);
            const float diffuse = saturate(abs(dot(
                normal, normalize(lightVector))));
            // MeshPhongMaterial's default PointLight uses a zero-distance,
            // two-power attenuation and Lambert's 1/pi normalization. The
            // example has no ambient light, so do not inject a floor bias.
            linearColor = float3(diffuse * 79.57747155f / distanceSquared);
        }
        else
        {
            linearColor = abs(inputValue.normal) + float3(inputValue.uv, 0.0f);
        }
        // EffectComposer allocates a HalfFloatType target in r185.  Preserve
        // the ShaderMaterial's HDR values here; HalftoneShader samples those
        // values before the final default-framebuffer color conversion.
        WebglPostprocessingRgbHalftoneLinearFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(linearColor), half(0.0f));
        return frameBuffer;
    }
};

/** Samples the halftone source at one pixel-space position with r185's jitter. */
float4 webglPostprocessingRgbHalftoneSample(
    IN BindGroup<WebglPostprocessingRgbHalftoneScreenResources> resources,
    float2 samplePoint)
{
    const float width = resources->parameters->dimensionsAndRadius.x;
    const float height = resources->parameters->dimensionsAndRadius.y;
    const float radius = resources->parameters->dimensionsAndRadius.z;
    const float pi2 = 6.28318531f;
    float4 value = float4(resources->source->sample(
        resources->sourceSampler, float2(samplePoint.x / width, samplePoint.y / height)));
    const float base = webglPostprocessingRgbHalftoneRand(
        float2(floor(samplePoint.x), floor(samplePoint.y))) * pi2;
    const float step = pi2 / 8.0f;
    const float distance = radius * 0.66f;
    for (uint sampleIndex = 0u; sampleIndex < 8u; ++sampleIndex)
    {
        const float angle = base + step * float(sampleIndex);
        const float2 coordinate = samplePoint + float2(
            cos(angle) * distance, sin(angle) * distance);
        value += float4(resources->source->sample(
            resources->sourceSampler,
            float2(coordinate.x / width, coordinate.y / height)));
    }
    return value / 9.0f;
}

/** Finds the nearest rotated halftone grid cell and its four corners. */
WebglPostprocessingRgbHalftoneCell webglPostprocessingRgbHalftoneReferenceCell(
    float2 samplePoint,
    float originX,
    float originY,
    float gridAngle,
    float step,
    float scatter)
{
    WebglPostprocessingRgbHalftoneCell cell;
    const float2 normal = float2(cos(gridAngle), sin(gridAngle));
    const float threshold = step * 0.5f;
    const float2 relative = samplePoint - float2(originX, originY);
    const float dotNormal = dot(normal, relative);
    const float dotLine = -normal.y * relative.x + normal.x * relative.y;
    const float2 offset = normal * dotNormal;
    const float offsetNormal = webglPostprocessingRgbHalftonePositiveMod(
        sqrt(offset.x * offset.x + offset.y * offset.y), step);
    const float normalDirection = dotNormal < 0.0f ? 1.0f : -1.0f;
    const float normalScale = (offsetNormal < threshold
        ? -offsetNormal : step - offsetNormal) * normalDirection;
    const float2 lineRelative = relative - offset;
    const float offsetLine = webglPostprocessingRgbHalftonePositiveMod(
        sqrt(lineRelative.x * lineRelative.x + lineRelative.y * lineRelative.y),
        step);
    const float lineDirection = dotLine < 0.0f ? 1.0f : -1.0f;
    const float lineScale = (offsetLine < threshold
        ? -offsetLine : step - offsetLine) * lineDirection;
    cell.normal = normal;
    cell.p1 = samplePoint - normal * normalScale +
        float2(normal.y * lineScale, -normal.x * lineScale);
    if (scatter != 0.0f)
    {
        const float scatterMagnitude = scatter * threshold * 0.5f;
        const float scatterAngle = webglPostprocessingRgbHalftoneRand(
            float2(floor(cell.p1.x), floor(cell.p1.y))) * 6.28318531f;
        cell.p1 += float2(cos(scatterAngle), sin(scatterAngle)) *
            scatterMagnitude;
    }
    const float normalStep = normalDirection *
        (offsetNormal < threshold ? step : -step);
    const float lineStep = lineDirection *
        (offsetLine < threshold ? step : -step);
    cell.p2 = cell.p1 - normal * normalStep;
    cell.p3 = cell.p1 + float2(normal.y * lineStep, -normal.x * lineStep);
    cell.p4 = cell.p1 - normal * normalStep +
        float2(normal.y * lineStep, -normal.x * lineStep);
    return cell;
}

/** Evaluates one halftone channel's anti-aliased dot coverage. */
float webglPostprocessingRgbHalftoneDotColour(
    IN BindGroup<WebglPostprocessingRgbHalftoneScreenResources> resources,
    WebglPostprocessingRgbHalftoneCell cell,
    float2 samplePoint,
    float radius,
    float angle,
    float antiAlias,
    float shape,
    uint channelIndex)
{
    if (channelIndex == 0u)
    {
        cell.samp1 = webglPostprocessingRgbHalftoneSample(resources, cell.p1).r;
        cell.samp2 = webglPostprocessingRgbHalftoneSample(resources, cell.p2).r;
        cell.samp3 = webglPostprocessingRgbHalftoneSample(resources, cell.p3).r;
        cell.samp4 = webglPostprocessingRgbHalftoneSample(resources, cell.p4).r;
    }
    else if (channelIndex == 1u)
    {
        cell.samp1 = webglPostprocessingRgbHalftoneSample(resources, cell.p1).g;
        cell.samp2 = webglPostprocessingRgbHalftoneSample(resources, cell.p2).g;
        cell.samp3 = webglPostprocessingRgbHalftoneSample(resources, cell.p3).g;
        cell.samp4 = webglPostprocessingRgbHalftoneSample(resources, cell.p4).g;
    }
    else
    {
        cell.samp1 = webglPostprocessingRgbHalftoneSample(resources, cell.p1).b;
        cell.samp2 = webglPostprocessingRgbHalftoneSample(resources, cell.p2).b;
        cell.samp3 = webglPostprocessingRgbHalftoneSample(resources, cell.p3).b;
        cell.samp4 = webglPostprocessingRgbHalftoneSample(resources, cell.p4).b;
    }
    const float distance1 = webglPostprocessingRgbHalftoneDistanceToRadius(
        cell.samp1, cell.p1, cell.normal, samplePoint, angle, radius, shape);
    const float distance2 = webglPostprocessingRgbHalftoneDistanceToRadius(
        cell.samp2, cell.p2, cell.normal, samplePoint, angle, radius, shape);
    const float distance3 = webglPostprocessingRgbHalftoneDistanceToRadius(
        cell.samp3, cell.p3, cell.normal, samplePoint, angle, radius, shape);
    const float distance4 = webglPostprocessingRgbHalftoneDistanceToRadius(
        cell.samp4, cell.p4, cell.normal, samplePoint, angle, radius, shape);
    float result = distance1 > 0.0f ? clamp(distance1 / antiAlias, 0.0f, 1.0f) : 0.0f;
    result += distance2 > 0.0f ? clamp(distance2 / antiAlias, 0.0f, 1.0f) : 0.0f;
    result += distance3 > 0.0f ? clamp(distance3 / antiAlias, 0.0f, 1.0f) : 0.0f;
    result += distance4 > 0.0f ? clamp(distance4 / antiAlias, 0.0f, 1.0f) : 0.0f;
    return clamp(result, 0.0f, 1.0f);
}

/** Applies the selected r185 HalftoneShader blend mode. */
float webglPostprocessingRgbHalftoneBlendColour(
    float source, float original, float amount, float mode)
{
    if (mode < 1.5f)
        return source * amount + original * (1.0f - amount);
    if (mode < 2.5f)
        return source * (1.0f - amount) +
            max(0.0f, source * original) * amount;
    if (mode < 3.5f)
        return source * (1.0f - amount) +
            min(1.0f, source + original) * amount;
    if (mode < 4.5f)
        return source * (1.0f - amount) + max(source, original) * amount;
    return source * (1.0f - amount) + min(source, original) * amount;
}

/** Applies the legacy EffectComposer DotScreenShader at scale four. */
class WebglPostprocessingRgbHalftoneDotScreenPass final : public IRenderClass
{
public:
    /** Binds the Scene texture without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingRgbHalftoneScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingRgbHalftoneScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Produces the complete r185 RGB Halftone dots and blend modes. */
    WebglPostprocessingRgbHalftoneLinearColorFrameBuffer fragment(
        WebglPostprocessingRgbHalftoneScreenOutput inputValue)
    {
        const float4 sourceColor = float4(resources->source->sample(
            resources->sourceSampler, inputValue.uv));
        const float shape = resources->parameters->controls.x;
        const float scatter = resources->parameters->controls.y;
        const float blending = resources->parameters->controls.z;
        const float blendingMode = resources->parameters->controls.w;
        const float disable = resources->parameters->flags.x;
        const float greyscale = resources->parameters->flags.y;
        if (disable > 0.5f)
        {
            WebglPostprocessingRgbHalftoneLinearColorFrameBuffer frameBuffer;
            frameBuffer.color = half4(sourceColor);
            return frameBuffer;
        }
        const float2 samplePoint = float2(
            inputValue.uv.x * resources->parameters->dimensionsAndRadius.x,
            inputValue.uv.y * resources->parameters->dimensionsAndRadius.y);
        const float radius =
            resources->parameters->dimensionsAndRadius.z;
        const float antiAlias = radius < 2.5f ? radius * 0.5f : 1.25f;
        const float angleR = resources->parameters->channelAngles.x;
        const float angleG = resources->parameters->channelAngles.y;
        const float angleB = resources->parameters->channelAngles.z;
        const WebglPostprocessingRgbHalftoneCell cellR =
            webglPostprocessingRgbHalftoneReferenceCell(
            samplePoint, 0.0f, 0.0f, angleR, radius, scatter);
        const WebglPostprocessingRgbHalftoneCell cellG =
            webglPostprocessingRgbHalftoneReferenceCell(
            samplePoint, 0.0f, 0.0f, angleG, radius, scatter);
        const WebglPostprocessingRgbHalftoneCell cellB =
            webglPostprocessingRgbHalftoneReferenceCell(
            samplePoint, 0.0f, 0.0f, angleB, radius, scatter);
        float red = webglPostprocessingRgbHalftoneDotColour(
            resources, cellR, samplePoint, radius, angleR, antiAlias, shape, 0u);
        float green = webglPostprocessingRgbHalftoneDotColour(
            resources, cellG, samplePoint, radius, angleG, antiAlias, shape, 1u);
        float blue = webglPostprocessingRgbHalftoneDotColour(
            resources, cellB, samplePoint, radius, angleB, antiAlias, shape, 2u);
        red = webglPostprocessingRgbHalftoneBlendColour(
            red, sourceColor.r, blending, blendingMode);
        green = webglPostprocessingRgbHalftoneBlendColour(
            green, sourceColor.g, blending, blendingMode);
        blue = webglPostprocessingRgbHalftoneBlendColour(
            blue, sourceColor.b, blending, blendingMode);
        if (greyscale > 0.5f)
        {
            const float average = (red + green + blue) / 3.0f;
            red = average;
            green = average;
            blue = average;
        }
        WebglPostprocessingRgbHalftoneLinearColorFrameBuffer frameBuffer;
        frameBuffer.color = half4(half3(red, green, blue), half(1.0f));
        return frameBuffer;
    }
};

/** Performs the explicit r185 output color conversion. */
class WebglPostprocessingRgbHalftoneOutputPass final : public IRenderClass
{
public:
    /** Binds the halftone linear result without depth or blending. */
    constructor(
        BindGroup<WebglPostprocessingRgbHalftoneScreenResources> resources [[Slot0]])
    {
        setCullMode(CullMode::None);
        setDepthWriteEnabled(false);
    }

private:
    /** Emits one fullscreen triangle. */
    WebglPostprocessingRgbHalftoneScreenOutput vertex(uint vertexID [[VertexID]])
    {
        return webglPostprocessingFullscreen(vertexID);
    }

    /** Stores each linear channel directly in the ordinary RGBA8 capture. */
    WebglPostprocessingRgbHalftoneOutputFrameBuffer fragment(
        WebglPostprocessingRgbHalftoneScreenOutput inputValue)
    {
        // EffectComposer writes the halftone pass directly to the default
        // framebuffer.  Read the already-computed pixel instead of applying
        // a second filtered texture lookup in the explicit resolve pass;
        // this preserves the one-fragment-per-canvas-pixel contract.
        const uint2 extent = uint2(
            uint(resources->parameters->dimensionsAndRadius.x),
            uint(resources->parameters->dimensionsAndRadius.y));
        const uint2 texel = uint2(clamp(
            floor(inputValue.uv * float2(extent)),
            float2(0.0f), float2(extent - uint2(1u))));
        const float4 sourceColor = float4(resources->source->read(texel));
        WebglPostprocessingRgbHalftoneOutputFrameBuffer frameBuffer;
        frameBuffer.color = half4(
            half(webglPostprocessingLinearToSrgb(sourceColor.r)),
            half(webglPostprocessingLinearToSrgb(sourceColor.g)),
            half(webglPostprocessingLinearToSrgb(sourceColor.b)),
            half(1.0f));
        return frameBuffer;
    }
};

/** Owns the unique Scene Set and the three ordered fullscreen effects. */
class WebglPostprocessingRgbHalftoneRenderer final : public AbstractRenderer
{
    Device device;
    Swapchain swapchain;
    Queue graphicsQueue;
    [[Export]] RenderSet<WebglPostprocessingRgbHalftoneSceneRenderSet> sceneSet;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> sceneTexture;
    Texture<TextureFormat::RGBA16Float,
            TextureUsage<RenderAttachment, TextureBinding>,
            TextureDimension::e2D> dotTexture;
    Texture<TextureFormat::Depth32Float,
            TextureUsage<RenderAttachment>,
            TextureDimension::e2D> sceneDepth;
    Texture<TextureFormat::RGBA8Unorm,
            TextureUsage<RenderAttachment, TextureBinding, CopySrc>,
            TextureDimension::e2D> outputTexture;
    Sampler linearSampler;
    Sampler nearestSampler;
    Buffer<WebglPostprocessingRgbHalftoneEffectParameters,
           BufferUsage<Uniform, CopyDst>> effectParameterBuffer;
    BindGroup<WebglPostprocessingRgbHalftoneScreenResources> sceneScreenResources;
    BindGroup<WebglPostprocessingRgbHalftoneScreenResources> dotScreenResources;
    RenderClass<WebglPostprocessingRgbHalftoneMainPass> mainPass;
    RenderClass<WebglPostprocessingRgbHalftoneDotScreenPass> dotPass;
    RenderClass<WebglPostprocessingRgbHalftoneOutputPass> outputPass;
    uint width = 800u;
    uint height = 500u;
    uint effectMode = 0u;

public:
    /** Creates the unique Scene RenderSet and its Scene RenderClass. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        swapchain = inSwapchain;
        graphicsQueue = device->graphicsQueue(0);
        sceneSet =
            device->createRenderSet<WebglPostprocessingRgbHalftoneSceneRenderSet>();
        mainPass = device->createRenderClass<WebglPostprocessingRgbHalftoneMainPass>(
            sceneSet);
        linearSampler = device->createSampler({
            .label = "WebglPostprocessingRgbHalftoneLinearSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Linear,
            .minFilter = FilterMode::Linear,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        nearestSampler = device->createSampler({
            .label = "WebglPostprocessingRgbHalftoneNearestSampler",
            .addressModeU = AddressMode::ClampToEdge,
            .addressModeV = AddressMode::ClampToEdge,
            .addressModeW = AddressMode::ClampToEdge,
            .magFilter = FilterMode::Nearest,
            .minFilter = FilterMode::Nearest,
            .mipmapFilter = MipmapFilterMode::Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 0.0f,
            .maxAnisotropy = 1u,
        });
        effectParameterBuffer = device->createBuffer(
            "WebglPostprocessingRgbHalftoneEffectParameters", 1u);
    }

    /** Allocates all ordinary single-sample intermediate and output textures. */
    void configureOutput(uint inWidth, uint inHeight, uint inEffectMode)
    {
        width = inWidth;
        height = inHeight;
        effectMode = inEffectMode;
        sceneTexture = device->createTexture(
            "WebglPostprocessingRgbHalftoneScene", width, height, 1u);
        dotTexture = device->createTexture(
            "WebglPostprocessingRgbHalftoneDot", width, height, 1u);
        sceneDepth = device->createTexture(
            "WebglPostprocessingRgbHalftoneDepth", width, height, 1u);
        outputTexture = device->createTexture(
            "WebglPostprocessingRgbHalftoneOutput", width, height, 1u);
        sceneScreenResources = device->createBindGroup<
            WebglPostprocessingRgbHalftoneScreenResources>(
                sceneTexture->createView(), linearSampler,
                effectParameterBuffer);
        dotScreenResources = device->createBindGroup<
            WebglPostprocessingRgbHalftoneScreenResources>(
                dotTexture->createView(), nearestSampler,
                effectParameterBuffer);
        setEffectMode(effectMode);
        dotPass = device->createRenderClass<
            WebglPostprocessingRgbHalftoneDotScreenPass>(sceneScreenResources);
        outputPass = device->createRenderClass<
            WebglPostprocessingRgbHalftoneOutputPass>(dotScreenResources);
    }

    /** Preserves the host-selected effect mode when the generated host reapplies dimensions. */
    void configureOutput(uint inWidth, uint inHeight)
    {
        configureOutput(inWidth, inHeight, effectMode);
    }

    /** Uploads one locked HalftonePass parameter set without changing attachments. */
    void setEffectMode(uint inEffectMode)
    {
        effectMode = inEffectMode;
        WebglPostprocessingRgbHalftoneEffectParameters parameters;
        parameters.dimensionsAndRadius =
            float4(float(width), float(height), 4.0f, 0.0f);
        parameters.channelAngles =
            float4(3.14159265358979323846f / 12.0f,
                   3.14159265358979323846f / 4.0f,
                   3.14159265358979323846f / 6.0f, 0.0f);
        parameters.controls = effectMode == 1u
            ? float4(5.0f, 0.35f, 0.65f, 2.0f)
            : float4(1.0f, 0.0f, 1.0f, 1.0f);
        parameters.flags = effectMode == 2u
            ? float4(1.0f, 0.0f, 0.0f, 0.0f)
            : (effectMode == 1u
                ? float4(0.0f, 1.0f, 0.0f, 0.0f)
                : float4(0.0f, 0.0f, 0.0f, 0.0f));
        graphicsQueue->writeBuffer(
            BufferRange(effectParameterBuffer),
            &parameters, sizeof(parameters))->submit();
    }

    /** Renders the Set once, then Halftone, output, and Present. */
    void render() override
    {
        sceneSet->update();
        WebglPostprocessingRgbHalftoneLinearFrameBuffer sceneFrame;
        sceneFrame.color = sceneTexture->createView();
        sceneFrame.color.loadOp = LoadOp::Clear;
        sceneFrame.color.storeOp = StoreOp::Store;
        // Three's Scene background is Color(0x444444), represented in the
        // linear RGBA16Float target before the final sRGB conversion.
        sceneFrame.color.clearValue = {0.0578054f, 0.0578054f,
                                       0.0578054f, 1.0f};
        sceneFrame.depth = sceneDepth->createView();
        sceneFrame.depth.depthLoadOp = LoadOp::Clear;
        sceneFrame.depth.depthStoreOp = StoreOp::Store;
        sceneFrame.depth.depthClearValue = 1.0f;
        WebglPostprocessingRgbHalftoneLinearColorFrameBuffer dotFrame;
        dotFrame.color = dotTexture->createView();
        dotFrame.color.loadOp = LoadOp::Clear;
        dotFrame.color.storeOp = StoreOp::Store;
        WebglPostprocessingRgbHalftoneOutputFrameBuffer outputFrame;
        outputFrame.color = outputTexture->createView();
        outputFrame.color.loadOp = LoadOp::Clear;
        outputFrame.color.storeOp = StoreOp::Store;
        const auto nextTexture = swapchain->queryNextTexture();
        graphicsQueue
            ->renderPass("WebglPostprocessingRgbHalftoneMain", sceneFrame, mainPass())
            ->renderPass(
                "WebglPostprocessingRgbHalftoneDotScreen", dotFrame,
                dotPass(3u, 1u, 0u, 0u))
            ->renderPass(
                "WebglPostprocessingRgbHalftoneOutput", outputFrame,
                outputPass(3u, 1u, 0u, 0u))
            ->renderToSwapchain(
                nextTexture, outputTexture, RenderToSwapchainDescriptor{})
            ->submit();
        swapchain->present();
    }

    /** Returns the final DSL-owned RGBA8 texture. */
    auto getReadbackTextureHandle() const
    {
        return outputTexture;
    }

    /** Returns the configured output width. */
    uint getReadbackWidth() const
    {
        return width;
    }

    /** Returns the configured output height. */
    uint getReadbackHeight() const
    {
        return height;
    }

    /** Releases the Scene Set and every postprocessing attachment. */
    void destroy() override
    {
        sceneSet->destroy();
        device->freeTexture(sceneTexture);
        device->freeTexture(dotTexture);
        device->freeTexture(sceneDepth);
        device->freeTexture(outputTexture);
        device->freeBuffer(effectParameterBuffer);
    }
};

#endif
