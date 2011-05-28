#ifndef __TEMPLATES_H__
#define __TEMPLATES_H__

#include "utypes.h"
#include <assert.h>

#if defined(POSIX)
/* Allow over-writing FORCEINLINE from makefile because gcc 3.4.4 for buffalo
   doesn't seem to support __attribute__((always_inline)) in -O0 build
   (strangely, it works in -Os build) */
#ifndef FORCEINLINE
// The always_inline attribute asks gcc to inline the function even if no optimization is being requested.
// This macro should be used exclusive-or with the inline directive (use one or the other but not both)
// since Microsoft uses __forceinline to also mean inline,
// and this code is following a Microsoft compatibility model.
// Just setting the attribute without also specifying the inline directive apparently won't inline the function,
// as evidenced by multiply-defined symbols found at link time.
#define FORCEINLINE inline __attribute__((always_inline))
#endif
#endif

// Utility templates
#undef min
#undef max

template <typename T> static inline T min(T a, T b) { if (a < b) return a; return b; }
template <typename T> static inline T max(T a, T b) { if (a > b) return a; return b; }

template <typename T> static inline T min(T a, T b, T c) { return min(min(a,b),c); }
template <typename T> static inline T max(T a, T b, T c) { return max(max(a,b),c); }
template <typename T> static inline T clamp(T v, T mi, T ma)
{
	if (v > ma) v = ma;
	if (v < mi) v = mi;
	return v;
}

#ifdef __GNUC__
 #define PACKED_ATTRIBUTE __attribute__((__packed__))
#else
 #define PACKED_ATTRIBUTE
 #pragma pack(push,1)
#endif

namespace aux
{
	FORCEINLINE uint16 host_to_network(uint16 i) { return htons(i); }
	FORCEINLINE uint32 host_to_network(uint32 i) { return htonl(i); }
	FORCEINLINE int32 host_to_network(int32 i) { return htonl(i); }
	FORCEINLINE uint16 network_to_host(uint16 i) { return ntohs(i); }
	FORCEINLINE uint32 network_to_host(uint32 i) { return ntohl(i); }
	FORCEINLINE int32 network_to_host(int32 i) { return ntohl(i); }
}

template <class T>
struct PACKED_ATTRIBUTE big_endian
{
	T operator=(T i) { m_integer = aux::host_to_network(i); return i; }
	operator T() const { return aux::network_to_host(m_integer); }
private:
	T m_integer;
};

typedef big_endian<int32> int32_big;
typedef big_endian<uint32> uint32_big;
typedef big_endian<uint16> uint16_big;

#ifndef __GNUC__
 #pragma pack(pop)
#endif

template<typename T> static inline void zeromem(T *a, size_t count = 1) { memset(a, 0, count * sizeof(T)); }

typedef int SortCompareProc(const void *, const void *);

template<typename T> static FORCEINLINE void QuickSortT(T *base, size_t num, int (*comp)(const T *, const T *)) { qsort(base, num, sizeof(T), (SortCompareProc*)comp); }


// WARNING: The template parameter MUST be a POD type!
template <typename T, size_t minsize = 16> class Array {
protected:
	T *mem;
	size_t alloc,count;

public:
	Array(size_t init) { Init(init); }
	Array() { Init(); }
	~Array() { Free(); }

	void inline Init() { mem = NULL; alloc = count = 0; }
	void inline Init(size_t init) { Init(); if (init) Resize(init); }
	size_t inline GetCount() const { return count; }
	size_t inline GetAlloc() const { return alloc; }
	void inline SetCount(size_t c) { count = c; }

	inline T& operator[](size_t offset) { assert(offset ==0 || offset<alloc); return mem[offset]; }
	inline const T& operator[](size_t offset) const { assert(offset ==0 || offset<alloc); return mem[offset]; }

	void inline Resize(size_t a) {
		if (a == 0) { free(mem); Init(); }
		else { mem = (T*)realloc(mem, (alloc=a) * sizeof(T)); }
	}

	void Grow() { Resize(::max<size_t>(minsize, alloc * 2)); }

	inline size_t Append(const T &t) {
		if (count >= alloc) Grow();
		size_t r=count++;
		mem[r] = t;
		return r;
	}

	T inline &Append() {
		if (count >= alloc) Grow();
		return mem[count++];
	}

	void inline Compact() {
		Resize(count);
	}

	void inline Free() {
		free(mem);
		Init();
	}

	void inline Clear() {
		count = 0;
	}

	bool inline MoveUpLast(size_t index) {
		assert(index < count);
		size_t c = --count;
		if (index != c) {
			mem[index] = mem[c];
			return true;
		}
		return false;
	}

	bool inline MoveUpLastExist(const T &v) {
		return MoveUpLast(LookupElementExist(v));
	}

	size_t inline LookupElement(const T &v) const {
		for(size_t i = 0; i != count; i++)
			if (mem[i] == v)
				return i;
		return (size_t) -1;
	}

	bool inline HasElement(const T &v) const {
		return LookupElement(v) != -1;
	}

	typedef int SortCompareProc(const T *a, const T *b);

	void Sort(SortCompareProc* proc, size_t start, size_t end) {
		QuickSortT(&mem[start], end - start, proc);
	}

	void Sort(SortCompareProc* proc, size_t start) {
		Sort(proc, start, count);
	}

	void Sort(SortCompareProc* proc) {
		Sort(proc, 0, count);
	}
};

#endif //__TEMPLATES_H__
