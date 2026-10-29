#pragma once
#include <inttypes.h>
namespace xGE::Math
{
	template<class X, class Y>
	constexpr X IntRoundUp(X _x, Y _y)
	{
		return (_x + _y - 1) / _y;
	}
	template<class X, class Y>
	constexpr X IntAlign(X x, Y y)noexcept
	{
		return (x + y - 1) & ~(y - 1);
	}
}
