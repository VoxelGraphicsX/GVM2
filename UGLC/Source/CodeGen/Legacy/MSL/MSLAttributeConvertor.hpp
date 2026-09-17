#pragma once
#include <CodeGen/AbstractAttributeConvertor.hpp>
#include <CodeGen/UGLC.Constants.hpp>
namespace UGLC::CodeGen::MSL
{

    class MSLAttributeConvertor final : public AbstractAttributeConvertor
    {
    public:
        virtual std::string convertAttribute(const std::string &canonicalName) const override
        {
            std::string result = canonicalName;
            if (canonicalName.starts_with(mUGLAttributeVertexInputName))
            {
                result = "stage_in";
            }
            else if (canonicalName == mUGLAttributeVertexIDName)
            {
                result = "vertex_id";
            }
            else if (canonicalName == mUGLAttributePositionName)
            {
                result = "position";
            }
            else if (canonicalName == mUGLAttributeInstanceIDName)
            {
                result = "instance_id";
            }
            else if (canonicalName == mUGLAttributePrimitiveIDName)
            {
                result = "primitive_id";
            }
            else if (canonicalName.starts_with(mUGLAttributeAttributeName))
            {
                result = "attribute(" + canonicalName.substr(9) + ")";
            }
            else if (canonicalName == mUGLCTORName)
            {
                result = "";
            }
            else if (canonicalName == mUGLAttributeDomainLocationName)
            {
                return "position_in_patch";
            }
            else if (canonicalName == mUGLAttributeDispatchThreadIDName)
            {
                result = "thread_position_in_grid";
            }
            else if (canonicalName == mUGLAttributeGroupThreadIDName)
            {
                result = "thread_position_in_threadgroup";
            }
            else if (canonicalName == mUGLAttributeGroupIDName)
            {
                result = "threadgroup_position_in_grid";
            }
            else if (canonicalName == mUGLAttributeGroupIndexName)
            {
                result = "thread_index_in_threadgroup";
            }
            else if (canonicalName == mUGLAttributeBarycentricsName)
            {
                result = "barycentric_coord";
            }
            else if (canonicalName == mUGLAttributeWaveLaneIndexName)
            {
                result = "thread_index_in_simdgroup";
            }
            else if (canonicalName == mUGLAttributeWaveLaneCountName)
            {
                result = "threads_per_simdgroup";
            }
            else if (canonicalName == mUGLAttributePixelCoordName)
            {
                result = "position";
            }
            else if (canonicalName == mUGLAttributeSampleIndexName)
            {
                result = "sample_id";
            }
            else if (canonicalName == mUGLAttributePixelLocalInputName)
            {
                result = "";
            }
            return result;
        }

    private:
    };

} // namespace UGLC::CodeGen::MSL
