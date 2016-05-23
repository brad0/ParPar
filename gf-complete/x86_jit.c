#ifndef __GFC_JIT__
#define __GFC_JIT__

#include "gf_int.h"

/* registers */
#define AX 0
#define BX 3
#define CX 1
#define DX 2
#define DI 7
#define SI 6
#define BP 5
#define SP 4

/* conditional jumps */
#define JE  0x4
#define JNE 0x5
#define JL  0xC
#define JGE 0xD
#define JLE 0xE
#define JG  0xF


#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    )
	#define AMD64 1
	#define RXX_PREFIX size_t p = 1; *(jit++) = 0x48;
#else
	#define RXX_PREFIX size_t p = 0;
#endif

#ifdef _MSC_VER
#define inline __inline
#endif

static inline size_t _jit_rex_pref(uint8_t** jit, uint8_t xreg, uint8_t xreg2) {
#ifdef AMD64
	if(xreg > 7 || xreg2 > 7) {
		*((*jit)++) = 0x40 | (xreg2 >>3) | ((xreg >>1)&4);
		return 1;
	}
#endif
	return 0;
}

static inline size_t _jit_xorps_m(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80570F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40570F | (xreg <<19) | (mreg <<16) | (offs <<24);
		return p+4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)jit = 0x570F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_xorps_r(uint8_t* jit, uint8_t xreg2, uint8_t xreg1) {
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1);
	xreg1 &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	*(int32_t*)jit = 0xC0570F | (xreg2 <<19) | (xreg1 <<16);
	return p+3;
}
static inline size_t _jit_pxor_m(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80EF0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40EF0F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0xEF0F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_pxor_r(uint8_t* jit, uint8_t xreg2, uint8_t xreg1) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1) +1;
	xreg1 &= 7;
	xreg2 &= 7;
	*(int32_t*)jit = 0xC0EF0F | (xreg2 <<19) | (xreg1 <<16);
	return p+3;
}
static inline size_t _jit_xorpd_m(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80570F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40570F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0x570F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_xorpd_r(uint8_t* jit, uint8_t xreg2, uint8_t xreg1) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg2, xreg1) +1;
	xreg1 &= 7;
	xreg2 &= 7;
	*(int32_t*)jit = 0xC0570F | (xreg2 <<19) | (xreg1 <<16);
	return p+3;
}

static inline size_t _jit_movaps(uint8_t* jit, uint8_t xreg, uint8_t xreg2) {
	size_t p = _jit_rex_pref(&jit, xreg, xreg2);
	xreg &= 7;
	xreg2 &= 7;
	/* can overflow, but we don't care */
	*(int32_t*)jit = 0xC0280F | (xreg <<19) | (xreg2 <<16);
	return p+3;
}
static inline size_t _jit_movaps_load(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40280F | (xreg <<19) | (mreg <<16) | (offs <<24);
		return p+4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)jit = 0x280F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_movaps_store(uint8_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	size_t p = _jit_rex_pref(&jit, xreg, 0);
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40290F | (xreg <<19) | (mreg <<16) | (offs <<24);
		return p+4;
	} else {
		/* can overflow, but we don't care */
		*(int32_t*)jit = 0x290F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}

static inline size_t _jit_movdqa(uint8_t* jit, uint8_t xreg, uint8_t xreg2) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, xreg2) +1;
	xreg &= 7;
	xreg2 &= 7;
	*(int32_t*)jit = 0xC06F0F | (xreg <<19) | (xreg2 <<16);
	return p+3;
}
static inline size_t _jit_movdqa_load(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x806F0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x406F0F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0x6F0F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_movdqa_store(uint8_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x807F0F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x407F0F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0x7F0F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}

static inline size_t _jit_movapd(uint8_t* jit, uint8_t xreg, uint8_t xreg2) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, xreg2) +1;
	xreg &= 7;
	xreg2 &= 7;
	*(int32_t*)jit = 0xC0280F | (xreg <<19) | (xreg2 <<16);
	return p+3;
}
static inline size_t _jit_movapd_load(uint8_t* jit, uint8_t xreg, uint8_t mreg, int32_t offs) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80280F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40280F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0x280F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}
static inline size_t _jit_movapd_store(uint8_t* jit, uint8_t mreg, int32_t offs, uint8_t xreg) {
	*(jit++) = 0x66;
	size_t p = _jit_rex_pref(&jit, xreg, 0) +1;
	xreg &= 7;
	if((offs+128) & ~0xFF) {
		*(int32_t*)jit = 0x80290F | (xreg <<19) | (mreg <<16);
		*(int32_t*)(jit +3) = offs;
		return p+7;
	} else if(offs) {
		*(int32_t*)jit = 0x40290F | (xreg <<19) | (mreg <<16);
		jit[3] = (uint8_t)offs;
		return p+4;
	} else {
		*(int32_t*)jit = 0x290F | (xreg <<19) | (mreg <<16);
		return p+3;
	}
}

static inline size_t _jit_push(uint8_t* jit, uint8_t reg) {
	jit[0] = 0x50 | reg;
	return 1;
}
static inline size_t _jit_pop(uint8_t* jit, uint8_t reg) {
	jit[0] = 0x58 | reg;
	return 1;
}
static inline size_t _jit_jmp(uint8_t* jit, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit -2);
	if((target+128) & ~0xFF) {
		*(jit++) = 0xE9;
		*(int32_t*)jit = target -3;
		return 5;
	} else {
		*(int16_t*)jit = 0xEB | ((int8_t)target << 8);
		return 2;
	}
}
static inline size_t _jit_jcc(uint8_t* jit, char op, uint8_t* addr) {
	int32_t target = (int32_t)(addr - jit -2);
	if((target+128) & ~0xFF) {
		*(jit++) = 0x0F;
		*(jit++) = 0x80 | op;
		*(int32_t*)jit = target -4;
		return 6;
	} else {
		*(int16_t*)jit = 0x70 | op | ((int8_t)target << 8);
		return 2;
	}
}
static inline size_t _jit_cmp_r(uint8_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC039 | (reg2 << 11) | (reg << 8);
	return p+2;
}
static inline size_t _jit_add_i(uint8_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC081 | (reg << 8);
	jit += 2;
	*(int32_t*)jit = val;
	return p+6;
}
static inline size_t _jit_sub_i(uint8_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC083 | (reg << 8);
	jit += 2;
	*(int32_t*)jit = val;
	return p+6;
}
static inline size_t _jit_sub_r(uint8_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC029 | (reg2 << 11) | (reg << 8);
	return p+2;
}
static inline size_t _jit_and_i(uint8_t* jit, uint8_t reg, int32_t val) {
	RXX_PREFIX
	*(int16_t*)jit = 0xE081 | (reg << 11);
	jit += 2;
	*(int32_t*)jit = val;
	return p+6;
}
static inline size_t _jit_xor_r(uint8_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC031 | (reg2 << 11) | (reg << 8);
	return p+2;
}
static inline size_t _jit_xor_m(uint8_t* jit, uint8_t reg, uint8_t mreg, int32_t offs) {
	RXX_PREFIX
	if((offs+128) & ~0xFF) {
		*(int16_t*)jit = 0x8033 | (reg <<11) | (mreg << 8);
		*(int32_t*)(jit +2) = offs;
		return p+6;
	} else if(offs) {
		*(int16_t*)jit = 0x4033 | (reg <<11) | (mreg << 8);
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		*(int16_t*)jit = 0x0033 | (reg <<11) | (mreg << 8);
		return p+2;
	}
}
static inline size_t _jit_xor_rm(uint8_t* jit, uint8_t mreg, int32_t offs, uint8_t reg) {
	RXX_PREFIX
	if((offs+128) & ~0xFF) {
		*(int16_t*)jit = 0x8031 | (reg <<11) | (mreg << 8);
		*(int32_t*)(jit +2) = offs;
		return p+6;
	} else if(offs) {
		*(int16_t*)jit = 0x4031 | (reg <<11) | (mreg << 8);
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		*(int16_t*)jit = 0x0031 | (reg <<11) | (mreg << 8);
		return p+2;
	}
}
static inline size_t _jit_mov_i(uint8_t* jit, uint8_t reg, intptr_t val) {
#ifdef AMD64
	if(val > 0x3fffffff || val < 0x40000000) {
		*(int16_t*)jit = 0xB848 | (reg << 8);
		*(int64_t*)(jit +2) = val;
		return 10;
	} else {
		*(int32_t*)jit = 0xC0C748 | (reg << 16);
		*(int32_t*)(jit +3) = (int32_t)val;
		return 7;
	}
#else
	*(jit++) = 0xB8 | reg;
	*(int32_t*)jit = (int32_t)val;
	return 5;
#endif
}
static inline size_t _jit_mov_r(uint8_t* jit, uint8_t reg, uint8_t reg2) {
	RXX_PREFIX
	*(int16_t*)jit = 0xC089 | (reg2 << 11) | (reg << 8);
	return p+2;
}
static inline size_t _jit_mov_load(uint8_t* jit, uint8_t reg, uint8_t mreg, int32_t offs) {
	RXX_PREFIX
	if((offs+128) & ~0xFF) {
		*(int16_t*)jit = 0x808B | (reg <<11) | (mreg << 8);
		*(int32_t*)(jit +2) = offs;
		return p+6;
	} else if(offs) {
		*(int16_t*)jit = 0x408B | (reg <<11) | (mreg << 8);
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		*(int16_t*)jit = 0x008B | (reg <<11) | (mreg << 8);
		return p+2;
	}
}
static inline size_t _jit_mov_store(uint8_t* jit, uint8_t mreg, int32_t offs, uint8_t reg) {
	RXX_PREFIX
	if((offs+128) & ~0xFF) {
		*(int16_t*)jit = 0x8089 | (reg <<11) | (mreg << 8);
		*(int32_t*)(jit +2) = offs;
		return p+6;
	} else if(offs) {
		*(int16_t*)jit = 0x4089 | (reg <<11) | (mreg << 8);
		jit[2] = (uint8_t)offs;
		return p+3;
	} else {
		*(int16_t*)jit = 0x0089 | (reg <<11) | (mreg << 8);
		return p+2;
	}
}
static inline size_t _jit_nop(uint8_t* jit) {
	jit[0] = 0x90;
	return 1;
}
static inline size_t _jit_align32(uint8_t* jit) {
	size_t p = 0;
	while((intptr_t)jit & 0x1F) {
		p += _jit_nop(jit++);
	}
	return p;
}
static inline size_t _jit_ret(uint8_t* jit) {
	jit[0] = 0xC3;
	return 1;
}


#if defined(_WINDOWS) || defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64)
#include <windows.h>
static void* jit_alloc(size_t len) {
	return VirtualAlloc(NULL, len, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}
static void jit_free(void* mem, size_t len) {
	VirtualFree(mem, 0, MEM_RELEASE);
}
#else
#include <sys/mman.h>
static void* jit_alloc(size_t len) {
	return mmap(NULL, len, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
}
static void jit_free(void* mem, size_t len) {
	munmap(mem, len); /* TODO: needs to be aligned?? */
}
#endif

#endif /*__GFC_JIT__*/
