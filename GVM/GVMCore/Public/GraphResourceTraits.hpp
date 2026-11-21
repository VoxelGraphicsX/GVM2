#pragma once

#include <GVMRHI/GVMRHI.hpp>
#include <EASTL/type_traits.h>

namespace GVM::Core
{
    namespace Private
    {
        template <class T, class Visitor>
        void visitGraphResources(const T &value, const Visitor &visitor);
    }

    /// Specialize this trait for native C++ structs containing RHI resource handles
    /// (Buffer, BufferRange, Texture, TextureView) so TaskGraph compile-time checks
    /// and TaskRuntime export lifetimes track them automatically.
    template <class T>
    struct GraphResourceTraits
    {
        static constexpr bool containsResources = false;

        template <class Visitor>
        static void visit(const T &, const Visitor &)
        {
        }
    };

    template <>
    struct GraphResourceTraits<void>
    {
        static constexpr bool containsResources = false;
    };

    /// Visits any RHI resource identities contained inside value (handles, containers, or GraphResourceTraits types).
    template <class T, class Visitor>
    void visitGraphResource(const T &value, const Visitor &visitor)
    {
        Private::visitGraphResources(value, visitor);
    }
}
