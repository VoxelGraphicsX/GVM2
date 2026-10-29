#pragma once

#if GVM_USE_STD

#ifndef GVMSTL
#define GVMSTL std
#endif

#elif GVM_USE_EASTL

#ifndef GVMSTL
#define GVMSTL eastl
#endif

#endif
