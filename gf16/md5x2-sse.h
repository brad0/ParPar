
#define ADD _mm_add_epi32
#define VAL(k) _mm_cvtsi64_si128(((uint64_t)k << 32) | k)
//#define VAL(k) _mm_set1_epi32
#define word_t __m128i
#define INPUT(k, set, ptr, offs, idx, var) ADD(var, VAL(k))
#define LOAD(k, set, ptr, offs, idx, var) ADD(var = _mm_unpacklo_epi32( \
	_mm_cvtsi32_si128(((uint32_t*)(ptr[0]))[idx]), \
	_mm_cvtsi32_si128(((uint32_t*)(ptr[1]))[idx]) \
), VAL(k))

#ifdef __SSE2__
#include <emmintrin.h>
// TODO: is there a way to use a 64-bit shift (by duplicating lanes) to emulate a 32-bit rotate?  probably useful, but only for SSE to avoid movdqa
#define ROTATE(a, r) (r == 16 ? \
	_mm_shufflelo_epi16(a, 0xb1) \
	: _mm_or_si128(_mm_slli_epi32(a, r), _mm_srli_epi32(a, 32-r)) \
)
#define _FN(f) f##_sse

#define F(b,c,d) _mm_xor_si128(_mm_and_si128(_mm_xor_si128(c, d), b), d)
// in theory, following OR could be replaced with ADD, which means it could be added to `a` before `b` is available
#define G(b,c,d) _mm_or_si128(_mm_and_si128(d, b), _mm_andnot_si128(d, c))
#define H(b,c,d) _mm_xor_si128(_mm_xor_si128(d, c), b)
#define I(b,c,d) _mm_xor_si128(_mm_or_si128(_mm_xor_si128(d, _mm_set1_epi8(-1)), b), c)

#include "md5x2-base.h"

#undef _FN

static HEDLEY_ALWAYS_INLINE void md5_extract_x2_sse(void* dst, void* state, const int idx) {
	__m128i* state_ = (__m128i*)state;
	// re-arrange into two hashes
	__m128i tmp1 = _mm_unpacklo_epi32(state_[0], state_[1]);
	__m128i tmp2 = _mm_unpacklo_epi32(state_[2], state_[3]);
	
	if(idx == 0) {
		_mm_storeu_si128((__m128i*)dst, _mm_unpacklo_epi64(tmp1, tmp2));
	} else {
		_mm_storeu_si128((__m128i*)dst, _mm_unpackhi_epi64(tmp1, tmp2));
	}
}
#define md5_init_lane_x2_sse(state, idx) { \
	__m128i* state_ = (__m128i*)state; \
	state_[0] = _mm_insert_epi16(state_[0], 0x2301, idx*2); \
	state_[0] = _mm_insert_epi16(state_[0], 0x6745, idx*2 + 1); \
	state_[1] = _mm_insert_epi16(state_[1], 0xab89, idx*2); \
	state_[1] = _mm_insert_epi16(state_[1], 0xefcd, idx*2 + 1); \
	state_[2] = _mm_insert_epi16(state_[2], 0xdcfe, idx*2); \
	state_[2] = _mm_insert_epi16(state_[2], 0x98ba, idx*2 + 1); \
	state_[3] = _mm_insert_epi16(state_[3], 0x5476, idx*2); \
	state_[3] = _mm_insert_epi16(state_[3], 0x1032, idx*2 + 1); \
}
#endif


#ifdef __AVX__
# undef LOAD
# define LOAD(k, set, ptr, offs, idx, var) ADD(var = _mm_insert_epi32( \
	_mm_cvtsi32_si128(((uint32_t*)(ptr[0]))[idx]), \
	((uint32_t*)(ptr[1]))[idx], 1 \
), VAL(k))
# define _FN(f) f##_avx
# include "md5x2-base.h"
# undef _FN
# define md5_extract_x2_avx md5_extract_x2_sse
#define md5_init_lane_x2_avx(state, idx) { \
	__m128i* state_ = (__m128i*)state; \
	state_[0] = _mm_insert_epi32(state_[0], 0x67452301L, idx); \
	state_[1] = _mm_insert_epi32(state_[1], 0xefcdab89L, idx); \
	state_[2] = _mm_insert_epi32(state_[2], 0x98badcfeL, idx); \
	state_[3] = _mm_insert_epi32(state_[3], 0x10325476L, idx); \
}
#endif
#ifdef ROTATE
# undef ROTATE
#endif

#ifdef __XOP__
#include <x86intrin.h>
#define ROTATE _mm_roti_epi32
#define _FN(f) f##_xop

#undef F
#undef G
#define F(b,c,d) _mm_cmov_si128(c, d, b)
#define G _mm_cmov_si128

#include "md5x2-base.h"

#undef _FN
#undef ROTATE

#define md5_extract_x2_xop md5_extract_x2_sse
#define md5_init_lane_x2_xop md5_init_lane_x2_avx
#endif


#ifdef __AVX512VL__
#include <immintrin.h>
#define ROTATE _mm_rol_epi32
#define _FN(f) f##_avx512

#undef F
#undef G
#undef H
#undef I
# define F(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xD8)
# define G(b,c,d) _mm_ternarylogic_epi32(d,c,b,0xAC)
# define H(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x96)
# define I(b,c,d) _mm_ternarylogic_epi32(d,c,b,0x63)

#include "md5x2-base.h"

#undef _FN
#undef ROTATE

#define md5_extract_x2_avx512 md5_extract_x2_sse
#define md5_init_lane_x2_avx512 md5_init_lane_x2_avx
#endif


#undef ADD
#undef VAL
#undef word_t
#undef INPUT
#undef LOAD

#ifdef F
# undef F
# undef G
# undef H
# undef I
#endif
