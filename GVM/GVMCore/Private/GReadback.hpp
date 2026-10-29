#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/span.h>
#include <cstdint>
#include <stdexcept>

namespace GVM::Core
{
    /// Typed view over one queued GPU readback; header-only template because the element type is erased at compile time.
    /// Wraps GVM::RHI::Readback and exposes the bytes as T or eastl::span<const T>.
    template <class T>
    class GpuReadback final
    {
    public:
        /// Constructs an empty readback view.
        GpuReadback() = default;
        /// Wraps an existing RHI readback handle.
        explicit GpuReadback(GVM::RHI::Readback readback)
            : mReadback(eastl::move(readback))
        {
        }

        /// Reports whether the readback data is available; callable from any thread.
        bool isReady() const
        {
            return mReadback != nullptr && mReadback->isReady();
        }

        /// Blocks until the executing submission completes; throws std::logic_error if the queue has not submitted the read yet.
        void wait()
        {
            if (mReadback == nullptr)
            {
                throw std::logic_error("GpuReadback::wait called on an empty readback.");
            }
            mReadback->wait();
        }

        /// Returns the readback bytes reinterpreted as a single T; throws std::logic_error before isReady() or when the byte count is smaller than sizeof(T).
        const T &get() const
        {
            const auto bytes = mReadback->getData();
            if (bytes.size() < sizeof(T))
            {
                throw std::logic_error("GpuReadback::get requires at least sizeof(T) bytes.");
            }
            return *reinterpret_cast<const T *>(bytes.data());
        }

        /// Returns the readback bytes reinterpreted as a span of T; the element count is bytes.size() / sizeof(T). Throws std::logic_error before isReady().
        eastl::span<const T> elements() const
        {
            const auto bytes = mReadback->getData();
            return eastl::span<const T>(reinterpret_cast<const T *>(bytes.data()), bytes.size() / sizeof(T));
        }

        /// Reports whether the view wraps a live RHI readback handle.
        bool isValid() const
        {
            return mReadback != nullptr;
        }

    private:
        GVM::RHI::Readback mReadback = nullptr;
    };

} // namespace GVM::Core
