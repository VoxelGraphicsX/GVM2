#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <GVMRHI/GVMRHI.hpp>

namespace GVM::RHI::Private
{
    /** Returns the texture's label for validation messages, or "unnamed texture" when the label is empty. */
    eastl::string describeTextureLabel(const TextureDescriptor &descriptor);
    /** Returns the label of a texture object for validation messages: "<null>" for a null pointer and "<unnamed>" when the label is empty. */
    eastl::string describeTextureLabel(const TextureImpl *texture);
    /** Returns a short name for a texture dimension used in validation messages. */
    const char *describeTextureDimension(TextureDimension dimension);
    /** Returns a short name for a texture view dimension used in validation messages. */
    const char *describeTextureViewDimension(TextureViewDimension dimension);
    /** Describes dimension, size, layer count, and mip count of a texture for validation messages. */
    eastl::string describeTextureShape(const TextureDescriptor &descriptor);
    /** Describes view dimension, format, and mip/layer ranges of a view for validation messages. */
    eastl::string describeTextureViewShape(const TextureViewDescriptor &descriptor);

    /** Resolves defaulted fields (format, dimension, mip/layer counts) of a view against its texture and validates mip/layer ranges and view dimension; the aspect passes through unchanged. Throws std::out_of_range for ranges and std::invalid_argument for dimensions. */
    TextureViewDescriptor resolveTextureViewDescriptor(const TextureDescriptor &texture, const TextureViewDescriptor &view);

    /** Returns true when every view-identity field of two view descriptors matches; the label is not compared. */
    bool equalTextureViewDescriptors(const TextureViewDescriptor &lhs, const TextureViewDescriptor &rhs);

    /** Texture views of one texture keyed by resolved descriptor; not thread-safe, callers hold their own lock. */
    class TextureViewCache
    {
    public:
        /** Returns the cached view for the resolved descriptor, or a null TextureView when absent. */
        TextureView find(const TextureViewDescriptor &resolved) const;
        /** Caches view under the resolved descriptor. */
        void insert(const TextureViewDescriptor &resolved, TextureView view);
        /** Drops every cached entry without destroying the views. */
        void clear();

    private:
        /** One resolved descriptor and the view created for it. */
        struct Entry
        {
            TextureViewDescriptor descriptor = {};
            TextureView view = {};
        };

        eastl::vector<Entry> mEntries;
    };
} // namespace GVM::RHI::Private
