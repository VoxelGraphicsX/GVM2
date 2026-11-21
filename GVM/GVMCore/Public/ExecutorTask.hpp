#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace GVM::Core
{
    /// Owns one ready invocation; its callback must contain exception and terminal accounting.
    class ExecutorTask final
    {
    public:
        /// Creates an empty task for queue extraction without allocating a callback.
        ExecutorTask() noexcept;
        /// Creates an empty task from nullptr.
        ExecutorTask(std::nullptr_t) noexcept;
        /// Takes ownership of any void() callable, using inline storage up to 96 bytes.
        template <typename Fn, typename Decayed = std::decay_t<Fn>,
            typename = std::enable_if_t<
                !std::is_same_v<Decayed, ExecutorTask> &&
                !std::is_same_v<Decayed, std::nullptr_t> &&
                std::is_invocable_r_v<void, Decayed &>>>
        explicit ExecutorTask(Fn &&fn)
        {
            if constexpr (std::is_pointer_v<Decayed>)
            {
                if (fn == nullptr)
                {
                    return;
                }
            }
            else if constexpr (requires(const Decayed &f) { static_cast<bool>(!f); })
            {
                if (!fn)
                {
                    return;
                }
            }
            if constexpr (sizeof(Decayed) <= kInlineCapacity &&
                          alignof(Decayed) <= alignof(std::max_align_t) &&
                          std::is_nothrow_move_constructible_v<Decayed>)
            {
                ::new (static_cast<void *>(mStorage)) Decayed(std::forward<Fn>(fn));
                mOps = &inlineOps<Decayed>();
            }
            else
            {
                Decayed *heapPtr = new Decayed(std::forward<Fn>(fn));
                ::new (static_cast<void *>(mStorage)) Decayed *(heapPtr);
                mOps = &heapOps<Decayed>();
            }
        }
        /// Transfers the single right to execute the callback.
        ExecutorTask(ExecutorTask &&other) noexcept;
        /// Replaces an unexecuted callback by transferring ownership from another task.
        ExecutorTask &operator=(ExecutorTask &&other) noexcept;
        /// Releases captures after execution or rejection without invoking the callback.
        ~ExecutorTask();
        ExecutorTask(const ExecutorTask &) = delete;
        ExecutorTask &operator=(const ExecutorTask &) = delete;

        /// Returns whether this task still owns an invocation.
        explicit operator bool() const noexcept;
        /// Consumes the invocation exactly once; escaping exceptions violate the executor contract.
        void execute() noexcept;

    private:
        struct Ops
        {
            void (*invoke)(void *storage) noexcept;
            void (*moveConstruct)(void *dst, void *src) noexcept;
            void (*destroy)(void *storage) noexcept;
        };

        template <typename Fn>
        static const Ops &inlineOps() noexcept
        {
            static constexpr Ops kOps{
                [](void *storage) noexcept
                {
                    (*static_cast<Fn *>(storage))();
                },
                [](void *dst, void *src) noexcept
                {
                    Fn *srcFn = static_cast<Fn *>(src);
                    ::new (dst) Fn(std::move(*srcFn));
                    srcFn->~Fn();
                },
                [](void *storage) noexcept
                {
                    static_cast<Fn *>(storage)->~Fn();
                }
            };
            return kOps;
        }

        template <typename Fn>
        static const Ops &heapOps() noexcept
        {
            static constexpr Ops kOps{
                [](void *storage) noexcept
                {
                    Fn *ptr = *static_cast<Fn **>(storage);
                    (*ptr)();
                },
                [](void *dst, void *src) noexcept
                {
                    Fn **srcPtr = static_cast<Fn **>(src);
                    ::new (dst) Fn *(*srcPtr);
                    *srcPtr = nullptr;
                },
                [](void *storage) noexcept
                {
                    Fn *ptr = *static_cast<Fn **>(storage);
                    delete ptr;
                }
            };
            return kOps;
        }

        static constexpr size_t kInlineCapacity = 96;
        alignas(std::max_align_t) unsigned char mStorage[kInlineCapacity]{};
        const Ops *mOps = nullptr;
    };
}
