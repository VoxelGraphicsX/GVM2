#include <gtest/gtest.h>

#include <GVMTestCommon.hpp>

#include <filesystem>

namespace
{
    TEST(DslAtomicCodegenTests, EmitsMetalAtomicGroupSharedScalars)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_TRUE(contents.find("threadgroup atomic<unsigned int> scalarCounter") != std::string::npos ||
                    contents.find("threadgroup atomic_uint scalarCounter") != std::string::npos ||
                    contents.find("threadgroup uint scalarCounter") != std::string::npos);
        EXPECT_TRUE(contents.find("threadgroup atomic<unsigned int> scratch[1]") != std::string::npos ||
                    contents.find("threadgroup atomic_uint scratch[1]") != std::string::npos ||
                    contents.find("threadgroup uint scratch[1]") != std::string::npos);
    }

    TEST(DslAtomicCodegenTests, EmitsAtomicReturnExpressionsForActiveBackend)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_EQ(contents.find("const uint scalarBase = {"), std::string::npos);
        EXPECT_EQ(contents.find("assignedBase = {"), std::string::npos);
        EXPECT_EQ(contents.find("const uint andBase = {"), std::string::npos);
        EXPECT_EQ(contents.find("assignedMask = {"), std::string::npos);

        const auto directSpirvDisassembly = generatedDir / "spv" / "GroupSharedAtomicRegressionPass.spvasm";
        if (std::filesystem::exists(directSpirvDisassembly))
        {
            EXPECT_FALSE(std::filesystem::exists(generatedDir / "hlsl"));
            EXPECT_NE(contents.find("eastl::string{},"), std::string::npos);
            EXPECT_EQ(contents.find("InterlockedAdd("), std::string::npos);
            EXPECT_EQ(contents.find("InterlockedAnd("), std::string::npos);

            const auto spirv = GVM::Tests::readTextFile(directSpirvDisassembly);
            const auto firstAtomicAdd = spirv.find("OpAtomicIAdd %uint");
            ASSERT_NE(firstAtomicAdd, std::string::npos);
            EXPECT_NE(spirv.find("OpAtomicIAdd %uint", firstAtomicAdd + 1), std::string::npos);

            const auto firstAtomicAnd = spirv.find("OpAtomicAnd %uint");
            ASSERT_NE(firstAtomicAnd, std::string::npos);
            EXPECT_NE(spirv.find("OpAtomicAnd %uint", firstAtomicAnd + 1), std::string::npos);

            EXPECT_NE(spirv.find("OpBitwiseOr %uint"), std::string::npos);
            EXPECT_NE(spirv.find("OpAtomicLoad %uint"), std::string::npos);
            return;
        }

        EXPECT_NE(contents.find("InterlockedAdd("), std::string::npos);
        EXPECT_NE(contents.find("InterlockedAnd("), std::string::npos);

        const bool hasLegacyReturnTemporaries =
            contents.find("const uint scalarBase = __uglc_scalarBase_atomic_result") != std::string::npos &&
            contents.find("const uint andBase = __uglc_andBase_atomic_result") != std::string::npos &&
            contents.find("assignedBase = __uglc_atomic_assign_result") != std::string::npos &&
            contents.find("assignedMask = __uglc_atomic_assign_result") != std::string::npos;
        const bool hasUGLIRReturnCalls =
            contents.find("uint scalarBase = UGL__atomicAdd__Tunsigned_int_Tunsigned_int(scalarCounter, 1u)") != std::string::npos &&
            contents.find("uint andBase = UGL__atomicAnd__Tunsigned_int_Tunsigned_int(scalarCounter, 4294967280u)") != std::string::npos &&
            contents.find("assignedBase = UGL__atomicAdd__Tunsigned_int_Tunsigned_int(scalarCounter, 1u)") != std::string::npos &&
            contents.find("assignedMask = UGL__atomicAnd__Tunsigned_int_Tunsigned_int(scalarCounter, 4294967279u)") != std::string::npos;
        EXPECT_TRUE(hasLegacyReturnTemporaries || hasUGLIRReturnCalls);
    }

    TEST(DslAtomicCodegenTests, EmitsMetalDeviceAtomicFenceOrdering)
    {
        const auto generatedDir = GVM::Tests::requireEnvPath("GVM_TEST_DSL_GENERATED_DIR");
        const auto generatedHeader = generatedDir / "generate_result.hpp";

        ASSERT_TRUE(std::filesystem::exists(generatedHeader));

        const auto contents = GVM::Tests::readTextFile(generatedHeader);
        EXPECT_NE(contents.find("atomic_store_explicit(&a, value, memory_order_relaxed)"), std::string::npos);
        EXPECT_NE(contents.find("atomic_load_explicit(&a, memory_order_relaxed)"), std::string::npos);
        EXPECT_NE(contents.find("atomic_fetch_add_explicit(&atom,b,memory_order_relaxed)"), std::string::npos);
        EXPECT_NE(contents.find("atomic_fetch_and_explicit(&a, (T)b, memory_order_relaxed)"), std::string::npos);
        EXPECT_TRUE(contents.find("atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst)") != std::string::npos ||
                    contents.find("atomic_thread_fence(mem_flags::mem_device, memory_order_relaxed)") != std::string::npos);
    }
} // namespace
