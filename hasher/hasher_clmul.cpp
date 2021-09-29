#include "../src/platform.h"

#define _FNCRC(f) f##_clmul

#define HasherInput HasherInput_ClMulSSE
#define _FNMD5x2(f) f##_sse

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
# include "crc_clmul.h"
# include "md5x2-sse.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif

#undef HasherInput
#undef _FNMD5x2
#define HasherInput HasherInput_ClMulScalar
#define _FNMD5x2(f) f##_scalar

#if defined(__PCLMUL__) && defined(__SSSE3__) && defined(__SSE4_1__)
# include "md5x2-scalar.h"
# include "hasher_base.h"
#else
# include "hasher_stub.h"
#endif