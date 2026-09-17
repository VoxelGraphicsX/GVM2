#include "GObject.hpp"

void GVM::Core::UObject::AddRef()
{
    mRefCount++;
}

void GVM::Core::UObject::Release()
{
    if (--mRefCount == 0)
    {
        delete this;
    }
}
