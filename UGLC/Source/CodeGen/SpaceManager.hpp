#pragma once
#include <string>
namespace UGLC::CodeGen
{

    class SpaceManager
    {
        // std::string mSpace;
        const std::string mTab = "    ";

        int mSpaceCount = 0;

    public:
        void enter();

        void quit();
        std::string getSpace() const;
    };

} // namespace UGLC::CodeGen
