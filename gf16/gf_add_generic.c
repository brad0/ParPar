#include "gf16_global.h"
#include "gf16_muladd_multi.h"

static HEDLEY_ALWAYS_INLINE void gf_add_x_generic(
	const void *HEDLEY_RESTRICT scratch, uint8_t *HEDLEY_RESTRICT _dst, const unsigned srcScale,
	GF16_MULADD_MULTI_SRCLIST, size_t len,
	const uint16_t *HEDLEY_RESTRICT coefficients,
	const int doPrefetch, const char* _pf
) {
	assert(len > 0);
	
	GF16_MULADD_MULTI_SRC_UNUSED(8);
	UNUSED(coefficients);
	UNUSED(doPrefetch); UNUSED(_pf);
	
	int lookup3Rearrange = (int)((intptr_t)scratch); // abuse this variable
	
	for(intptr_t ptr = -(intptr_t)len; ptr; ptr += sizeof(uintptr_t)) {
		uintptr_t data;
		
		data = *(uintptr_t*)(_src1+ptr*srcScale);
		if(srcCount >= 2)
			data ^= *(uintptr_t*)(_src2+ptr*srcScale);
		if(srcCount >= 3)
			data ^= *(uintptr_t*)(_src3+ptr*srcScale);
		if(srcCount >= 4)
			data ^= *(uintptr_t*)(_src4+ptr*srcScale);
		if(srcCount >= 5)
			data ^= *(uintptr_t*)(_src5+ptr*srcScale);
		if(srcCount >= 6)
			data ^= *(uintptr_t*)(_src6+ptr*srcScale);
		if(srcCount >= 7)
			data ^= *(uintptr_t*)(_src7+ptr*srcScale);
		if(srcCount >= 8)
			data ^= *(uintptr_t*)(_src8+ptr*srcScale);
		
		if(lookup3Rearrange) {
			// revert bit rearrangement for LOOKUP3 method
			if(sizeof(uintptr_t) >= 8) {
				data = (data & 0xf80007fff80007ffULL) | ((data & 0x003ff800003ff800ULL) << 5) | ((data & 0x07c0000007c00000ULL) >> 11);
			} else {
				data = (data & 0xf80007ff) | ((data & 0x003ff800) << 5) | ((data & 0x07c00000) >> 11);
			}
		}
		
		*(uintptr_t*)(_dst+ptr) ^= data;
	}
}

unsigned gf_add_multi_generic(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
	return gf16_muladd_multi(NULL, &gf_add_x_generic, 4, regions, offset, dst, src, len, NULL);
}

// assumes word-size packing (for lookup algorithms)
#include "gf16_lookup.h"
unsigned gf_add_multi_packed_generic(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
	return gf16_muladd_multi_packed(NULL, &gf_add_x_generic, 1, 4, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
void gf_add_multi_packpf_generic(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	// no support for prefetching on generic implementation, so defer to regular function
	UNUSED(prefetchIn); UNUSED(prefetchOut);
	gf16_muladd_multi_packed(NULL, &gf_add_x_generic, 1, 4, regions, dst, src, len, gf16_lookup_stride(), NULL);
}

unsigned gf_add_multi_packed_lookup3(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) {
	return gf16_muladd_multi_packed((void*)1, &gf_add_x_generic, 1, 4, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
void gf_add_multi_packpf_lookup3(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) {
	UNUSED(prefetchIn); UNUSED(prefetchOut);
	gf16_muladd_multi_packed((void*)1, &gf_add_x_generic, 1, 4, regions, dst, src, len, gf16_lookup_stride(), NULL);
}
