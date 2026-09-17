#ifndef EASTL_HELPER_EASTL_MAKE_INTRUSIVE_H
#define EASTL_HELPER_EASTL_MAKE_INTRUSIVE_H
#include <EASTL/intrusive_ptr.h>
namespace eastl
{

    template <class T, typename... Args>
    [[nodiscard]]
    eastl::intrusive_ptr<T> make_intrusive(Args &&...args) noexcept
    {
        return new T(std::forward<Args>(args)...);
    }

}

#endif