#include "gf16_global.h"
#include "platform.h"

#define _mword __m128i
#define _MM(f) _mm_ ## f
#define _MMI(f) _mm_ ## f ## _si128
#define _FN(f) f ## _sse2
#ifdef __SSE2__
# define _AVAILABLE
#endif

#include "gf_add_x86.h"

#ifdef _AVAILABLE
# undef _AVAILABLE
#endif
#undef _FN
#undef _MMI
#undef _MM
#undef _mword


unsigned gf_add_multi_sse2(unsigned regions, size_t offset, void *HEDLEY_RESTRICT dst, const void* const*HEDLEY_RESTRICT src, size_t len) {
#ifdef __SSE2__
	return gf16_muladd_multi((void*)1, &gf_add_x_sse2, 4, regions, offset, dst, src, len, NULL);
#else
	UNUSED(regions); UNUSED(offset); UNUSED(dst); UNUSED(src); UNUSED(len);
	return 0;
#endif
}


#ifdef __SSE2__
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_sse2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	return gf16_muladd_multi_packed((void*)vs, &gf_add_x_sse2, il, it, regions, dst, src, len, sizeof(__m128i)*vs, NULL); \
} \
void gf_add_multi_packpf_v##vs##i##il##_sse2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	gf16_muladd_multi_packpf((void*)vs, &gf_add_x_sse2, il, it, regions, dst, src, len, sizeof(__m128i)*vs, NULL, vs>1, prefetchIn, prefetchOut); \
}
#else
# define PACKED_FUNC(vs, il, it) \
unsigned gf_add_multi_packed_v##vs##i##il##_sse2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); \
	return 0; \
} \
void gf_add_multi_packpf_v##vs##i##il##_sse2(unsigned regions, void *HEDLEY_RESTRICT dst, const void* HEDLEY_RESTRICT src, size_t len, const void* HEDLEY_RESTRICT prefetchIn, const void* HEDLEY_RESTRICT prefetchOut) { \
	UNUSED(regions); UNUSED(dst); UNUSED(src); UNUSED(len); UNUSED(prefetchIn); UNUSED(prefetchOut); \
}
#endif

PACKED_FUNC(1, 2, 6)
PACKED_FUNC(1, 6, 6)
PACKED_FUNC(2, 1, 6)
PACKED_FUNC(2, 3, 6)
PACKED_FUNC(16, 1, 6)

#undef PACKED_FUNC