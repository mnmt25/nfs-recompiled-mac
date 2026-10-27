#ifndef MMX_H_
#define MMX_H_

#include <x86.h>
#include <cstring>
//#include <intrin.h>
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
# include <immintrin.h>
#else
// Non-x86 hosts (e.g. Apple Silicon): SIMDe maps the _mm_* intrinsics to NEON
# define SIMDE_ENABLE_NATIVE_ALIASES
# include <simde/x86/sse2.h>
#endif

namespace x86
{

struct regmmx
{
    __m128i reg;
    operator x86::reg64() const
    {
        return _mm_cvtsi128_si64(reg);
    }
    operator __m128i() const
    {
        return reg;
    }
};

static inline regmmx from_reg64(const x86::reg64& value)
{
    regmmx result;
    result.reg = _mm_cvtsi64_si128(value);
    return result;
}

struct MMX
{
    regmmx mm0;
    regmmx mm1;
    regmmx mm2;
    regmmx mm3;
    regmmx mm4;
    regmmx mm5;
    regmmx mm6;
    regmmx mm7;
    void init()
    {
        memset(this, 0, sizeof(*this));
    }
};

}

#endif /* !MMX_H_ */
