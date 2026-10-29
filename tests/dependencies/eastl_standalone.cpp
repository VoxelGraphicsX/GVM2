#include <EASTL/string.h>
#include <EASTL/vector.h>

/** Checks that the public EASTL target supplies its paired allocator without the runtime. */
int main()
{
    eastl::vector<eastl::string> values;
    for (unsigned index = 0; index < 257; ++index)
        values.push_back("standalone EASTL allocated string");
    return values.size() == 257 && values.back() == "standalone EASTL allocated string" ? 0 : 1;
}
