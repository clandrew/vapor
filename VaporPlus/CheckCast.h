#pragma once

inline UINT CheckCastUint(size_t s)
{
	assert(s <= UINT_MAX);
	return static_cast<UINT>(s);
}

inline Index CheckCastIndex(size_t s)
{
	assert(s <= USHORT_MAX);
	return static_cast<Index>(s);
}
