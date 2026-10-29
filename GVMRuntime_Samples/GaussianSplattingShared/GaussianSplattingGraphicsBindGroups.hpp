#pragma once

/**
 * Binds the sample-specific hot Gaussian payload used by graphics projection passes.
 *
 * Include this header inside namespace GsViewer after GaussianSplatHotGPU,
 * GaussianPositionOpacity, ProjectedGaussianRender, ViewerGlobals, and the
 * shared 3DGS GPU layouts are declared.
 */
struct SceneHotBindGroup final : public IBindGroup
{
    /**
     * Binds the read-only hot Gaussian stream for projection and traversal consumers.
     */
    constructor(StructuredBuffer<GaussianSplatHotGPU> splats [[Binding0]])
    {
    }
};

/**
 * Binds the shared cold Gaussian payload used by graphics-oriented samples.
 */
struct SceneColdBindGroup final : public IBindGroup
{
    /**
     * Binds the read-only cold Gaussian stream shared across graphics samples.
     */
    constructor(StructuredBuffer<GaussianSplatColdGPU> splats [[Binding0]])
    {
    }
};

/**
 * Binds only Gaussian position and opacity for passes that do not need full transform payloads.
 */
struct ScenePositionOpacityBindGroup final : public IBindGroup
{
    /**
     * Binds the compact position-opacity stream used by passes that only need centers and opacity.
     */
    constructor(StructuredBuffer<GaussianPositionOpacity> splats [[Binding0]])
    {
    }
};

/**
 * Binds read-only viewer globals for graphics passes.
 */
struct ViewerGlobalsBindGroup final : public IBindGroup
{
    /**
     * Binds viewer globals as read-only render-state input.
     */
    constructor(UniformBuffer<ViewerGlobals> globals [[Binding0]])
    {
    }
};

/**
 * Binds projected Gaussian billboard payloads written by projection passes.
 */
struct ProjectedRenderBindGroup final : public IBindGroup
{
    /**
     * Binds the writable projected billboard payload stream.
     */
    constructor(RWStructuredBuffer<ProjectedGaussianRender> projected [[Binding0]])
    {
    }
};

/**
 * Binds projected Gaussian billboard payloads read by graphics render passes.
 */
struct ProjectedRenderReadBindGroup final : public IBindGroup
{
    /**
     * Binds the read-only projected billboard payload stream for graphics rendering.
     */
    constructor(StructuredBuffer<ProjectedGaussianRender> projected [[Binding0]])
    {
    }
};
