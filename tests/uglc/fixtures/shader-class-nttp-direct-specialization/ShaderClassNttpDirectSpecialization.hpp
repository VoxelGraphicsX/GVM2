#ifndef UGLC_TEST_SHADER_CLASS_NTTP_DIRECT_SPECIALIZATION_HPP
#define UGLC_TEST_SHADER_CLASS_NTTP_DIRECT_SPECIALIZATION_HPP

#include "UGL.h"

using namespace UGL;

/** Provides the writable storage buffer used by direct shader class NTTP specializations. */
struct ShaderClassNttpDirectSpecializationBindGroup final : public IBindGroup
{
    /** Creates the bind group with the output buffer written by the compute variants. */
    constructor(RWStructuredBuffer<uint> values [[Binding0]])
    {
    }
};

namespace ShaderClassNttpDirectSpecializationRecords
{
    /** Stores uniform data read by a direct shader class template specialization. */
    struct DirectNttpConfig
    {
        uint4 value;
    };

    /** Provides uniform config and writable storage for the direct config specialization. */
    struct DirectNttpConfigBindGroup final : public IBindGroup
    {
        /** Creates the bind group with the uniform config and output buffer. */
        constructor(UniformBuffer<DirectNttpConfig> config [[Binding0]],
                    RWStructuredBuffer<uint> values [[Binding1]])
        {
        }
    };

    /** Reads the first config lane so helper signatures depend on DirectNttpConfig. */
    inline uint readConfig(DirectNttpConfig config)
    {
        return config.value.x;
    }
} // namespace ShaderClassNttpDirectSpecializationRecords

/** Exercises direct compute shader class template specialization with literal non-type template arguments. */
template <uint A, uint B>
class [[LocalWorkGroupSize(1, 1, 1)]] DirectNttpPassT final : public IComputeClass
{
public:
    /** Binds the output buffer used to keep the substituted values visible in generated shader code. */
    constructor(BindGroup<ShaderClassNttpDirectSpecializationBindGroup> bindGroup [[Slot0]])
    {
    }

private:
    /** Writes both non-type template parameters after shader class specialization erases them to constants. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const uint x = A;
        const uint y = B;
        bindGroup->values[threadID.x] = x * 10u + y;
    }
};

/** Exercises direct compute shader class template specialization with record dependencies in uniforms and helpers. */
template <uint Mode>
class [[LocalWorkGroupSize(1, 1, 1)]] DirectNttpConfigPassT final : public IComputeClass
{
public:
    /** Binds the config uniform and output buffer used by the record dependency regression. */
    constructor(BindGroup<ShaderClassNttpDirectSpecializationRecords::DirectNttpConfigBindGroup> configBindGroup [[Slot0]])
    {
    }

private:
    /** Reads a uniform record, passes it to a helper, and adds the substituted mode value. */
    void compute(uint3 threadID [[DispatchThreadID]])
    {
        const ShaderClassNttpDirectSpecializationRecords::DirectNttpConfig config = configBindGroup->config->read();
        configBindGroup->values[threadID.x] = ShaderClassNttpDirectSpecializationRecords::readConfig(config) + Mode;
    }
};

namespace ShaderClassNttpDirectSpecializationValues
{
    /** Supplies a namespace-scoped constexpr-like value used as a shader class template argument. */
    static const uint NamespaceA = 3u;

    /** Supplies a second namespace-scoped constexpr-like value used as a shader class template argument. */
    static const uint NamespaceB = 4u;

    /** Exercises direct compute shader class specialization when template arguments are namespace constants. */
    template <uint A, uint B>
    class [[LocalWorkGroupSize(1, 1, 1)]] NamespacedNttpPassT final : public IComputeClass
    {
    public:
        /** Binds the output buffer used by the namespace-constant specialization. */
        constructor(BindGroup<ShaderClassNttpDirectSpecializationBindGroup> bindGroup [[Slot0]])
        {
        }

    private:
        /** Writes namespace-provided non-type template parameters after specialization substitution. */
        void compute(uint3 threadID [[DispatchThreadID]])
        {
            const uint x = A;
            const uint y = B;
            bindGroup->values[threadID.x] = x * 10u + y;
        }
    };
} // namespace ShaderClassNttpDirectSpecializationValues

/** Hosts direct shader class template specializations without using aliases. */
class ShaderClassNttpDirectSpecializationRenderer final : public AbstractRenderer
{
    Device device;
    Buffer<uint, BufferUsage<Storage, CopyDst>> values;
    Buffer<ShaderClassNttpDirectSpecializationRecords::DirectNttpConfig, BufferUsage<Uniform, CopyDst>> config;
    BindGroup<ShaderClassNttpDirectSpecializationBindGroup> bindGroup;
    BindGroup<ShaderClassNttpDirectSpecializationRecords::DirectNttpConfigBindGroup> configBindGroup;
    ComputeClass<DirectNttpPassT<1u, 2u>> literalPass;
    ComputeClass<ShaderClassNttpDirectSpecializationValues::NamespacedNttpPassT<ShaderClassNttpDirectSpecializationValues::NamespaceA, ShaderClassNttpDirectSpecializationValues::NamespaceB>> namespacePass;
    ComputeClass<DirectNttpConfigPassT<1u>> configPass;

public:
    /** Creates the shared buffers and all directly-specialized compute classes. */
    void init(Device inDevice, Swapchain inSwapchain) override
    {
        device = inDevice;
        values = device->createBuffer("ShaderClassNttpDirectSpecializationValues", 128);
        config = device->createBuffer("ShaderClassNttpDirectSpecializationConfig", 1);
        bindGroup = device->createBindGroup<ShaderClassNttpDirectSpecializationBindGroup>(values);
        configBindGroup = device->createBindGroup<ShaderClassNttpDirectSpecializationRecords::DirectNttpConfigBindGroup>(config, values);
        literalPass = device->createComputeClass<DirectNttpPassT<1u, 2u>>(bindGroup);
        namespacePass = device->createComputeClass<ShaderClassNttpDirectSpecializationValues::NamespacedNttpPassT<ShaderClassNttpDirectSpecializationValues::NamespaceA, ShaderClassNttpDirectSpecializationValues::NamespaceB>>(bindGroup);
        configPass = device->createComputeClass<DirectNttpConfigPassT<1u>>(configBindGroup);
    }

    /** Leaves rendering empty because this fixture validates generated compute shader code only. */
    void render() override
    {
    }

    /** Leaves teardown empty because the fixture owns only reference-counted DSL resources. */
    void destroy() override
    {
    }
};

#endif
