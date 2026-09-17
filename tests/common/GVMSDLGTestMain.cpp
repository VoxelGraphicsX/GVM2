#include <SDL.h>
#include <gtest/gtest.h>
#include "GVMRenderTestOptions.hpp"
#include <cstdio>

/** Initializes explicit render options and executes the SDL-backed GoogleTest suite. */
int main(int argc, char **argv)
{
    try { GVM::Tests::configureRenderTestOptions(argc, argv); }
    catch (const std::exception &error)
    {
        std::fprintf(stderr, "%s\n", error.what());
        return 2;
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
