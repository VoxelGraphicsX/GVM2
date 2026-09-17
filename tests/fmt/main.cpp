#include <GVMRHI/GVMLogging.hpp>
#include <cstdio>

#if !GVM_LOGGING_ENABLED
#error "The fmt consumer requires a logging-enabled GVM build."
#endif

/** Verifies that the public logging API formats messages using the parent's fmt target. */
int main()
{
    const auto message = GVM::RHI::Logging::formatLogMessage("value={:04x}, ratio={:.2f}", 42, 1.25);
    if (message != "value=002a, ratio=1.25")
    {
        std::fprintf(stderr, "Unexpected formatted message: %s\n", message.c_str());
        return 1;
    }
    std::puts("Parent fmt integration and public logging passed.");
    return 0;
}
