#pragma once

namespace clang
{
    class Sema;
    class TagDecl;
}

namespace UGLC::CodeGen
{
    /// Adds typed deferred fields to source-only Task<Record> specializations before member lookup.
    class TaskProjectionBuilder final
    {
    public:
        /// Completes public record projections in both shader and host frontends without changing resource types.
        static void complete(clang::Sema &sema, clang::TagDecl &declaration);
    };
}
