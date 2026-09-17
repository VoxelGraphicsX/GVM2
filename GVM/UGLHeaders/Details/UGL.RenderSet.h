#pragma once
#include "UGL.Resources.h"
#include "UGL.Types.h"
#include <functional>
#include <type_traits>
#include <vector>
namespace UGL
{
    /*  struct IRenderComponent
     {
     };

     template <typename T>
     concept IsRenderComponent = std::is_base_of_v<IRenderComponent, T>;

     template <typename T>
     concept IsRenderComponentArray = std::is_array_v<T> && std::is_base_of_v<IRenderComponent, std::remove_extent_t<T>>;
  */
    // template <class T>
    //   requires std::is_base_of_v<IRenderComponent, T>

    /*  class IRenderEntity
     {
     public:
     }; */

    // template <class T>
    //     requires std::is_base_of_v<IRenderEntity, T>
    /*  class RenderEntity final
     {
     public:
         void addComponent(IsRenderComponent auto component)
         {
             // Add the component to the entity
         }
         void addComponents(IsRenderComponentArray auto components)
         {
             // Add the components to the entity
         }
         void updateComponent(IsRenderComponent auto component, uint instanceIndex = 1)
         {
             // Update the component in the entity
         }
         void updateComponents(IsRenderComponentArray auto components, uint instanceStartIndex = 0)
         {
             // Update the components in the entity
         }
     }; */
    template <class T>
    struct RenderEntity
    {
    };

    template <class T>
    struct BufferComponentDataPack
    {
        [[nodiscard]]
        T get(uint renderEntityIndex, uint instanceIndex)
        {
            return T();
        }
        [[nodiscard]]
        T getRaw(uint index)
        {
            return T();
        }
        [[nodiscard]]
        bool checkValid(uint renderEntityIndex)
        {
            return false;
        }

        /* template <class R>
        void set(RenderEntity<R> entity, string dataName, const T *value, uint64_t dataStorageSize, uint instanceStartIndex, uint instanceCount)
        {
            // Set the value in the buffer
        } */
        /*
        template <class R>
        void set(RenderEntity<R> entity, const T *value, uint64_t dataStorageSize, uint instanceStartIndex, uint instanceCount)
        {
            // Set the value in the buffer
        } */
    };

    template <class T>
    struct TextureComponentDataPack
    {
        Texture2D<T> get(uint renderEntityIndex, uint textureIndex)
        {
            (void)renderEntityIndex;
            (void)textureIndex;
            return {};
        }
    };

    template <class T>
    struct BufferComponent
    {
        BufferComponentDataPack<T> *operator->()
        {
            return new BufferComponentDataPack<T>();
        }
    };

    template <class T, uint32_t MaxN>
    struct TextureComponent
    {
        TextureComponentDataPack<T> *operator->()
        {
            return new TextureComponentDataPack<T>();
        }
    };

    struct IRenderSet
    {
    };

    /*  using RenderComponentSetDataCallBack = std::function<void()>;
     template <class T>
         requires std::is_base_of_v<IRenderSet, T>
     class RenderSetCommandEncoderDataPack
     {
     public:
         RenderEntity<T> alloc(uint verticesCount, uint indicesCount, uint instanceCount = 1)
         {
             return {};
         }
         template <class B>
         void setBuffer(BufferComponent<B> bufferComponent, RenderEntity<T> entity, const string &bufferName, const void *value, uint64_t dataStorageSize, uint instanceStartIndex, uint instanceCount, const RenderComponentSetDataCallBack &callBack = nullptr) {}

         void remove(RenderEntity<T> entity) {}
         void commitAndWait() {}
     };

     template <class T>
         requires std::is_base_of_v<IRenderSet, T>
     class RenderSetCommandEncoder
     {
     public:
         RenderSetCommandEncoderDataPack<T> *operator->()
         {
             return new RenderSetCommandEncoderDataPack<T>();
         }
     }; */

    /*     enum class RenderSetCommandType
        {
            Alloc,
            SetBuffer,
        }; */

    struct RenderSetAllocInfo
    {
        uint32_t verticesCount = 0;
        uint32_t indicesCount = 0;
        uint32_t instanceCount = 1;
        template <class B>
        void addBuffer(BufferComponent<B> bufferComponent, const string &bufferName, const B *value, uint64_t dataStorageSize, uint32_t instanceCount)
        {
        }
    };

    template <class T>
    struct RenderSetDataPack : public T
    {
        RenderEntity<T> alloc(const RenderSetAllocInfo &info)
        {

            // Allocate the entity
            return {};
        }

        template <class B>
        void set(BufferComponent<B> bufferComponent, RenderEntity<T> entity, string dataName, const B *value, uint64_t dataStorageSize, uint instanceStartIndex, uint instanceCount)
        {
            // Set the value in the buffer
        }

        void remove(RenderEntity<T> entity)
        {
            // Destroy the entity
        }
        /* RenderSetCommandEncoder<T> createCommandEncoder()
        {
            return {};
        } */
        uint getRenderEntityIndexCount(uint renderEntityID) const
        {
            return 0;
        }
        uint getRenderEntityInstanceCount(uint renderEntityID) const
        {
            return 0;
        }
        uint getRenderEntityFirstIndex(uint renderEntityID) const
        {
            return 0;
        }
        int getRenderEntityVertexOffset(uint renderEntityID) const
        {
            return 0;
        }
        uint getRenderEntityGlobalInstanceBase(uint renderEntityID) const
        {
            return 0;
        }
        void getRenderEntityInfo(uint renderEntityID, uint &indexCount, uint &instanceCount, uint &firstIndex, int &vertexOffset, uint &globalInstanceBase)
        {
        }
        uint getRenderEntityVersion(uint renderEntityID) const
        {
            return 0;
        }
        void getRenderEntityCMDParams(uint cmdIndex, uint &entityID, uint &instanceIndex) const
        {
        }
        int getMaxEntityCount()
        {
            return 0;
        }
        uint64_t getCMDParamsCount() const
        {
            return 0;
        }
        bool checkValid(uint renderEntityID) const
        {
            return false;
        }
        void update()
        {
        }
        void destroy()
        {
            // Destroy the render set
        }
    };

    template <class T>
        requires std::is_base_of_v<IRenderSet, T>
    class RenderSet final
    {
    public:
        RenderSetDataPack<T> *operator->()
        {
            return new RenderSetDataPack<T>();
        }
    };
} // namespace UGL
