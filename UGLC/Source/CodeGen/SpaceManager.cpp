#include "SpaceManager.hpp"

namespace UGLC::CodeGen
{


    void SpaceManager::enter()
    {
        mSpaceCount += 1;
    }
    void SpaceManager::quit()
    {
        // Keep indentation depth saturated at zero so one mismatched quit does
        // not poison every later scope emission with a negative depth.
        if (mSpaceCount == 0)
        {
            return;
        }
        mSpaceCount -= 1;
    }
    std::string SpaceManager::getSpace() const
    {
        std::string space;
        for (int i = 0; i < mSpaceCount; i++)
        {
            space += mTab;
        }
        return space;
    }
} // namespace UGLC::CodeGen
