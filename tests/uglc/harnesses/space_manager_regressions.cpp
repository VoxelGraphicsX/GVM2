#include <CodeGen/SpaceManager.hpp>

#include <iostream>
#include <string>

namespace
{
    int fail(const std::string &message)
    {
        std::cerr << message << '\n';
        return 1;
    }
}

int main()
{
    UGLC::CodeGen::SpaceManager manager;

    if (!manager.getSpace().empty())
    {
        return fail("SpaceManager should start at zero indentation");
    }

    manager.quit();
    if (!manager.getSpace().empty())
    {
        return fail("SpaceManager quit() should stay saturated at zero indentation");
    }

    manager.enter();
    if (manager.getSpace() != "    ")
    {
        return fail("SpaceManager enter() should still produce one indent after an extra quit()");
    }

    manager.quit();
    manager.quit();
    manager.enter();
    manager.enter();
    if (manager.getSpace() != "        ")
    {
        return fail("SpaceManager indentation depth should recover cleanly after repeated underflow attempts");
    }

    manager.quit();
    manager.quit();
    manager.quit();
    if (!manager.getSpace().empty())
    {
        return fail("SpaceManager should return to zero indentation after balanced quits");
    }

    std::cout << "space_manager_underflow_guard_ok\n";
    return 0;
}
