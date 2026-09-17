#include <GVMSTL/unordered_set.h>
#include <GVMSTL/namespace_def.h>

/** Verifies the installed adapter supplies a usable unordered set in either supported namespace. */
int main()
{
    GVMSTL::unordered_set<int> values;
    values.insert(7);
    values.insert(7);
    values.insert(11);
    if (values.size() != 2 || values.count(7) != 1 || values.count(11) != 1)
        return 1;
    values.erase(7);
    return values.size() == 1 && values.count(7) == 0 ? 0 : 1;
}
