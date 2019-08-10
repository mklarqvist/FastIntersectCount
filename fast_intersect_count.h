/*
* Copyright (c) 2019 Marcus D. R. Klarqvist
* Author(s): Marcus D. R. Klarqvist
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*   http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
*/
#ifndef FAST_INTERSECT_COUNT_H_
#define FAST_INTERSECT_COUNT_H_

#include <stdint.h>
#include <assert.h>
#include <memory.h>
#include <string.h>
#include <math.h>

#ifndef FIC_DEFAULT_BLOCK
#define FIC_DEFAULT_BLOCK 256e3
#endif

/*
 * Reference data transfer rates:
 *
 * DDR4 2133：17 GB/s
 * DDR4 2400：19.2 GB/s
 * DDR4 2666：21.3 GB/s
 * DDR4 3200：25.6 GB/s
 */

#if !(defined(__APPLE__)) && !(defined(__FreeBSD__))
#include <malloc.h>  // this should never be needed but there are some reports that it is needed.
#endif

/* *************************************
 *  Support.
 * 
 *  These subroutines and definitions are taken from the CRoaring repo
 *  by Daniel Lemire et al. available under the Apache 2.0 License
 *  (same as libintersect.h):
 *  https://github.com/RoaringBitmap/CRoaring/ 
 ***************************************/
#if defined(__SIZEOF_LONG_LONG__) && __SIZEOF_LONG_LONG__ != 8
#error This code assumes 64-bit long longs (by use of the GCC intrinsics). Your system is not currently supported.
#endif

/* ===   Compiler specifics   === */

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* >= C99 */
#  define FIC_RESTRICT   restrict
#else
/* note : it might be useful to define __restrict or __restrict__ for some C++ compilers */
#  define FIC_RESTRICT   /* disable */
#endif

#include <x86intrin.h>

/****************************
*  Memory management
* 
*  The subroutines aligned_malloc and aligned_free had to e renamed to
*  aligned_malloc_port and aligned_free_port to stop clashing with the
*  same subroutines in Roaring. These subroutines are included here
*  since there is no hard dependency on using Roaring bitmaps.
****************************/
// portable version of  posix_memalign
#ifndef aligned_malloc_port
static 
void* aligned_malloc_port(size_t alignment, size_t size) {
    void *p;
#ifdef _MSC_VER
    p = _aligned_malloc_port(size, alignment);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    p = __mingw_aligned_malloc_port(size, alignment);
#else
    // somehow, if this is used before including "x86intrin.h", it creates an
    // implicit defined warning.
    if (posix_memalign(&p, alignment, size) != 0) 
        return NULL;
#endif
    return p;
}
#endif

#ifndef aligned_free_port
static 
void aligned_free_port(void* memblock) {
#ifdef _MSC_VER
    _aligned_free_port(memblock);
#elif defined(__MINGW32__) || defined(__MINGW64__)
    __mingw_aligned_free_port(memblock);
#else
    free(memblock);
#endif
}
#endif

// portable alignment
#if defined(_MSC_VER)
#define ALIGNED(x) __declspec(align(x))
#else
#if defined(__GNUC__)
#define ALIGNED(x) __attribute__((aligned(x)))
#endif
#endif

// disable noise
#ifdef __GNUC__
#define WARN_UNUSED __attribute__((warn_unused_result))
#else
#define WARN_UNUSED
#endif

/*------ SIMD definitions --------*/

#define FIC_SSE_ALIGNMENT    16
#define FIC_AVX2_ALIGNMENT   32
#define FIC_AVX512_ALIGNMENT 64

/****************************
*  General checks
****************************/

#ifndef __has_builtin
  #define __has_builtin(x) 0
#endif

#ifndef __has_attribute
  #define __has_attribute(x) 0
#endif

#ifdef __GNUC__
  #define GNUC_PREREQ(x, y) \
      (__GNUC__ > x || (__GNUC__ == x && __GNUC_MINOR__ >= y))
#else
  #define GNUC_PREREQ(x, y) 0
#endif

#ifdef __clang__
  #define CLANG_PREREQ(x, y) \
      (__clang_major__ > x || (__clang_major__ == x && __clang_minor__ >= y))
#else
  #define CLANG_PREREQ(x, y) 0
#endif

#if (_MSC_VER < 1900) && \
    !defined(__cplusplus)
  #define inline __inline
#endif

#if (defined(__i386__) || \
     defined(__x86_64__) || \
     defined(_M_IX86) || \
     defined(_M_X64))
  #define X86_OR_X64
#endif

#if defined(X86_OR_X64) && \
   (defined(__cplusplus) || \
    defined(_MSC_VER) || \
   (GNUC_PREREQ(4, 2) || \
    __has_builtin(__sync_val_compare_and_swap)))
  #define HAVE_CPUID
#endif

#if GNUC_PREREQ(4, 2) || \
    __has_builtin(__builtin_popcount)
  #define HAVE_BUILTIN_POPCOUNT
#endif

#if GNUC_PREREQ(4, 2) || \
    CLANG_PREREQ(3, 0)
  #define HAVE_ASM_POPCNT
#endif

#if defined(HAVE_CPUID) && \
   (defined(HAVE_ASM_POPCNT) || \
    defined(_MSC_VER))
  #define HAVE_POPCNT
#endif

#if defined(HAVE_CPUID) && \
    GNUC_PREREQ(4, 9)
  #define HAVE_SSE41
  #define HAVE_AVX2
#endif

#if defined(HAVE_CPUID) && \
    GNUC_PREREQ(5, 0)
  #define HAVE_AVX512
#endif

#if defined(HAVE_CPUID) && \
    defined(_MSC_VER) && \
    defined(__AVX2__)
  #define HAVE_SSE41
  #define HAVE_AVX2
#endif

#if defined(HAVE_CPUID) && \
    defined(_MSC_VER) && \
    defined(__AVX512__)
  #define HAVE_AVX512
#endif

#if defined(HAVE_CPUID) && \
    CLANG_PREREQ(3, 8) && \
    __has_attribute(target) && \
   (!defined(_MSC_VER) || defined(__AVX2__)) && \
   (!defined(__apple_build_version__) || __apple_build_version__ >= 8000000)
  #define HAVE_SSE41
  #define HAVE_AVX2
  #define HAVE_AVX512
#endif

#ifdef __cplusplus
extern "C" {
#endif

/****************************
*  CPUID
****************************/
#if defined(HAVE_CPUID)

#if defined(_MSC_VER)
  #include <intrin.h>
  #include <immintrin.h>
#endif

// CPUID flags. See https://en.wikipedia.org/wiki/CPUID for more info.
/* %ecx bit flags */
#define bit_POPCNT   (1 << 23) // POPCNT instruction 
#define bit_SSE41    (1 << 19) // CPUID.01H:ECX.SSE41[Bit 19]
#define bit_SSE42    (1 << 20) // CPUID.01H:ECX.SSE41[Bit 20]

/* %ebx bit flags */
#define bit_AVX2     (1 << 5)  // CPUID.(EAX=07H, ECX=0H):EBX.AVX2[bit 5]
#define bit_AVX512BW (1 << 30) // AVX-512 Byte and Word Instructions

/* xgetbv bit flags */
#define XSTATE_SSE (1 << 1)
#define XSTATE_YMM (1 << 2)
#define XSTATE_ZMM (7 << 5)

static inline void run_cpuid(int eax, int ecx, int* abcd) {
#if defined(_MSC_VER)
  __cpuidex(abcd, eax, ecx);
#else
  int ebx = 0;
  int edx = 0;

  #if defined(__i386__) && \
      defined(__PIC__)
    /* in case of PIC under 32-bit EBX cannot be clobbered */
    __asm__ ("movl %%ebx, %%edi;"
             "cpuid;"
             "xchgl %%ebx, %%edi;"
             : "=D" (ebx),
               "+a" (eax),
               "+c" (ecx),
               "=d" (edx));
  #else
    __asm__ ("cpuid;"
             : "+b" (ebx),
               "+a" (eax),
               "+c" (ecx),
               "=d" (edx));
  #endif

  abcd[0] = eax;
  abcd[1] = ebx;
  abcd[2] = ecx;
  abcd[3] = edx;
#endif
}

#if defined(HAVE_AVX2) || \
    defined(HAVE_AVX512)

static inline int get_xcr0()
{
  int xcr0;

#if defined(_MSC_VER)
  xcr0 = (int) _xgetbv(0);
#else
  __asm__ ("xgetbv" : "=a" (xcr0) : "c" (0) : "%edx" );
#endif

  return xcr0;
}

#endif

static inline int get_cpuid() {
    int flags = 0;
    int abcd[4];

    run_cpuid(1, 0, abcd);

    // Check for POPCNT instruction
    if ((abcd[2] & bit_POPCNT) == bit_POPCNT)
        flags |= bit_POPCNT;

    // Check for SSE4.1 instruction set
    if ((abcd[2] & bit_SSE41) == bit_SSE41)
        flags |= bit_SSE41;

    // Check for SSE4.2 instruction set
    if ((abcd[2] & bit_SSE42) == bit_SSE42)
        flags |= bit_SSE42;

#if defined(HAVE_AVX2) || \
    defined(HAVE_AVX512)

    int osxsave_mask = (1 << 27);

    /* ensure OS supports extended processor state management */
    if ((abcd[2] & osxsave_mask) != osxsave_mask)
        return 0;

    int ymm_mask = XSTATE_SSE | XSTATE_YMM;
    int zmm_mask = XSTATE_SSE | XSTATE_YMM | XSTATE_ZMM;

    int xcr0 = get_xcr0();

    if ((xcr0 & ymm_mask) == ymm_mask) {
        run_cpuid(7, 0, abcd);

        if ((abcd[1] & bit_AVX2) == bit_AVX2)
            flags |= bit_AVX2;

        if ((xcr0 & zmm_mask) == zmm_mask) {
            if ((abcd[1] & bit_AVX512BW) == bit_AVX512BW)
            flags |= bit_AVX512BW;
        }
    }

#endif

  return flags;
}
#endif // defined(HAVE_CPUID)

/// TODO: Fixme
#ifdef _mm_popcnt_u64
#define FIC_POPCOUNT _mm_popcnt_u64
#else
#define FIC_POPCOUNT __builtin_popcountll
#endif

// Todo: fixme
static inline uint32_t fastrange32(uint32_t word, uint32_t p) {
	return (uint32_t)(((uint64_t)word * (uint64_t)p) >> 32);
}

static inline
uint64_t builtin_popcnt_unrolled(const __m128i val) {
    return FIC_POPCOUNT(*((uint64_t*)&val + 0)) + FIC_POPCOUNT(*((uint64_t*)&val + 1));
}

static inline
uint64_t popcount64_unrolled(const uint64_t* data, uint64_t size) {
    const uint64_t limit = size - size % 4;
    uint64_t cnt = 0;
    uint64_t i   = 0;

    for (/**/;i < limit; i += 4) {
        cnt += FIC_POPCOUNT(data[i+0]);
        cnt += FIC_POPCOUNT(data[i+1]);
        cnt += FIC_POPCOUNT(data[i+2]);
        cnt += FIC_POPCOUNT(data[i+3]);
    }

    for (/**/;i < size; ++i)
        cnt += FIC_POPCOUNT(data[i]);

    return cnt;
}

/****************************
*  SSE4.1 functions
****************************/

#if defined(HAVE_SSE41)
// #if SIMD_VERSION >= 3

#include <immintrin.h>

#ifndef FIC_POPCOUNT_SSE4
#define FIC_POPCOUNT_SSE4(A, B) {               \
    A += FIC_POPCOUNT(_mm_extract_epi64(B, 0)); \
    A += FIC_POPCOUNT(_mm_extract_epi64(B, 1)); \
}
#endif

#if !defined(_MSC_VER)
  __attribute__ ((target ("sse4.1")))
#endif
static inline 
uint64_t FIC_POPCOUNT_SSE(const __m128i n) {
    return(FIC_POPCOUNT(_mm_cvtsi128_si64(n)) + FIC_POPCOUNT(_mm_cvtsi128_si64(_mm_unpackhi_epi64(n, n))));
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("sse4.1")))
#endif
static inline
void CSA128(__m128i* h, __m128i* l, __m128i a, __m128i b, __m128i c) {
    __m128i u = _mm_xor_si128(a, b);
    *h = _mm_or_si128(_mm_and_si128(a, b), _mm_and_si128(u, c));
    *l = _mm_xor_si128(u, c);
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("sse4.1")))
#endif
static 
uint64_t popcnt_sse4_csa_intersect(const __m128i* FIC_RESTRICT data1, 
                                   const __m128i* FIC_RESTRICT data2, 
                                   uint64_t size)
{
    __m128i ones     = _mm_setzero_si128();
    __m128i twos     = _mm_setzero_si128();
    __m128i fours    = _mm_setzero_si128();
    __m128i eights   = _mm_setzero_si128();
    __m128i sixteens = _mm_setzero_si128();
    __m128i twosA, twosB, foursA, foursB, eightsA, eightsB;

    uint64_t i = 0;
    uint64_t limit = size - size % 16;
    uint64_t cnt64 = 0;

    for (/**/; i < limit; i += 16) {
        CSA128(&twosA,   &ones,   ones,  (data1[i+0]  & data2[i+0]), (data1[i+1] & data2[i+1]));
        CSA128(&twosB,   &ones,   ones,  (data1[i+2]  & data2[i+2]), (data1[i+3] & data2[i+3]));
        CSA128(&foursA,  &twos,   twos,  twosA,  twosB);
        CSA128(&twosA,   &ones,   ones,  (data1[i+4]  & data2[i+4]), (data1[i+5] & data2[i+5]));
        CSA128(&twosB,   &ones,   ones,  (data1[i+6]  & data2[i+6]), (data1[i+7] & data2[i+7]));
        CSA128(&foursB,  &twos,   twos,  twosA,  twosB);
        CSA128(&eightsA, &fours,  fours, foursA, foursB);
        CSA128(&twosA,   &ones,   ones,  (data1[i+8]  & data2[i+8]),  (data1[i+9]  & data2[i+9]));
        CSA128(&twosB,   &ones,   ones,  (data1[i+10] & data2[i+10]), (data1[i+11] & data2[i+11]));
        CSA128(&foursA,  &twos,   twos,  twosA,  twosB);
        CSA128(&twosA,   &ones,   ones,  (data1[i+12] & data2[i+12]), (data1[i+13] & data2[i+13]));
        CSA128(&twosB,   &ones,   ones,  (data1[i+14] & data2[i+14]), (data1[i+15] & data2[i+15]));
        CSA128(&foursB,  &twos,   twos,  twosA,  twosB);
        CSA128(&eightsB, &fours,  fours, foursA, foursB);
        CSA128(&sixteens,&eights, eights,eightsA,eightsB);

        cnt64 += FIC_POPCOUNT_SSE(sixteens);
    }

    cnt64 <<= 4;
    cnt64 += FIC_POPCOUNT_SSE(eights) << 3;
    cnt64 += FIC_POPCOUNT_SSE(fours)  << 2;
    cnt64 += FIC_POPCOUNT_SSE(twos)   << 1;
    cnt64 += FIC_POPCOUNT_SSE(ones)   << 0;

    for (/**/; i < size; ++i)
        cnt64 = FIC_POPCOUNT_SSE(data1[i] & data2[i]);

    return cnt64;
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("sse4.1")))
#endif
static 
uint64_t intersect_bitmaps_sse4(const uint64_t* FIC_RESTRICT b1, 
                                const uint64_t* FIC_RESTRICT b2, 
                                const uint32_t n_ints) 
{
    uint64_t count = 0;
    const __m128i* r1 = (__m128i*)b1;
    const __m128i* r2 = (__m128i*)b2;
    const uint32_t n_cycles = n_ints / 2;

    count += popcnt_sse4_csa_intersect(r1, r2, n_cycles);

    for (int i = n_cycles*2; i < n_ints; ++i) {
        count += _mm_popcnt_u64(b1[i] & b2[i]);
    }

    return(count);
}

// #endif
#endif

/****************************
*  AVX256 functions
****************************/

#if defined(HAVE_AVX2)
// #if SIMD_VERSION >= 5

#include <immintrin.h>

#ifndef FIC_POPCOUNT_AVX2
#define FIC_POPCOUNT_AVX2(A, B) {                  \
    A += FIC_POPCOUNT(_mm256_extract_epi64(B, 0)); \
    A += FIC_POPCOUNT(_mm256_extract_epi64(B, 1)); \
    A += FIC_POPCOUNT(_mm256_extract_epi64(B, 2)); \
    A += FIC_POPCOUNT(_mm256_extract_epi64(B, 3)); \
}
#endif

#if !defined(_MSC_VER)
  __attribute__ ((target ("avx2")))
#endif
static inline
void CSA256(__m256i* h, __m256i* l, __m256i a, __m256i b, __m256i c) {
    __m256i u = _mm256_xor_si256(a, b);
    *h = _mm256_or_si256(_mm256_and_si256(a, b), _mm256_and_si256(u, c));
    *l = _mm256_xor_si256(u, c);
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("avx2")))
#endif
static inline
__m256i popcnt256(__m256i v) {
    __m256i lookup1 = _mm256_setr_epi8(
        4, 5, 5, 6, 5, 6, 6, 7,
        5, 6, 6, 7, 6, 7, 7, 8,
        4, 5, 5, 6, 5, 6, 6, 7,
        5, 6, 6, 7, 6, 7, 7, 8
    );

    __m256i lookup2 = _mm256_setr_epi8(
        4, 3, 3, 2, 3, 2, 2, 1,
        3, 2, 2, 1, 2, 1, 1, 0,
        4, 3, 3, 2, 3, 2, 2, 1,
        3, 2, 2, 1, 2, 1, 1, 0
    );

    __m256i low_mask = _mm256_set1_epi8(0x0f);
    __m256i lo = _mm256_and_si256(v, low_mask);
    __m256i hi = _mm256_and_si256(_mm256_srli_epi16(v, 4), low_mask);
    __m256i popcnt1 = _mm256_shuffle_epi8(lookup1, lo);
    __m256i popcnt2 = _mm256_shuffle_epi8(lookup2, hi);

    return _mm256_sad_epu8(popcnt1, popcnt2);
}

/*
 * AVX2 Harley-Seal popcount (4th iteration).
 * The algorithm is based on the paper "Faster Population Counts
 * using AVX2 Instructions" by Daniel Lemire, Nathan Kurz and
 * Wojciech Mula (23 Nov 2016).
 * @see https://arxiv.org/abs/1611.07612
 */
// In this version we perform the operation A&B as input into the CSA operator.
#if !defined(_MSC_VER)
  __attribute__ ((target ("avx2")))
#endif
static 
uint64_t popcnt_avx2_csa_intersect(const __m256i* FIC_RESTRICT data1, 
                                   const __m256i* FIC_RESTRICT data2, 
                                   uint64_t size)
{
    __m256i cnt      = _mm256_setzero_si256();
    __m256i ones     = _mm256_setzero_si256();
    __m256i twos     = _mm256_setzero_si256();
    __m256i fours    = _mm256_setzero_si256();
    __m256i eights   = _mm256_setzero_si256();
    __m256i sixteens = _mm256_setzero_si256();
    __m256i twosA, twosB, foursA, foursB, eightsA, eightsB;

    uint64_t i = 0;
    uint64_t limit = size - size % 16;
    uint64_t* cnt64;

    for (/**/; i < limit; i += 16) {
        CSA256(&twosA,   &ones,   ones,  (data1[i+0]  & data2[i+0]), (data1[i+1] & data2[i+1]));
        CSA256(&twosB,   &ones,   ones,  (data1[i+2]  & data2[i+2]), (data1[i+3] & data2[i+3]));
        CSA256(&foursA,  &twos,   twos,  twosA, twosB);
        CSA256(&twosA,   &ones,   ones,  (data1[i+4]  & data2[i+4]), (data1[i+5] & data2[i+5]));
        CSA256(&twosB,   &ones,   ones,  (data1[i+6]  & data2[i+6]), (data1[i+7] & data2[i+7]));
        CSA256(&foursB,  &twos,   twos,  twosA, twosB);
        CSA256(&eightsA, &fours,  fours, foursA, foursB);
        CSA256(&twosA,   &ones,   ones,  (data1[i+8]  & data2[i+8]), (data1[i+9]   & data2[i+9]));
        CSA256(&twosB,   &ones,   ones,  (data1[i+10] & data2[i+10]), (data1[i+11] & data2[i+11]));
        CSA256(&foursA,  &twos,   twos,  twosA, twosB);
        CSA256(&twosA,   &ones,   ones,  (data1[i+12] & data2[i+12]), (data1[i+13] & data2[i+13]));
        CSA256(&twosB,   &ones,   ones,  (data1[i+14] & data2[i+14]), (data1[i+15] & data2[i+15]));
        CSA256(&foursB,  &twos,   twos,  twosA, twosB);
        CSA256(&eightsB, &fours,  fours, foursA, foursB);
        CSA256(&sixteens,&eights, eights,eightsA, eightsB);

        cnt = _mm256_add_epi64(cnt, popcnt256(sixteens));
    }

    cnt = _mm256_slli_epi64(cnt, 4);
    cnt = _mm256_add_epi64(cnt, _mm256_slli_epi64(popcnt256(eights), 3));
    cnt = _mm256_add_epi64(cnt, _mm256_slli_epi64(popcnt256(fours),  2));
    cnt = _mm256_add_epi64(cnt, _mm256_slli_epi64(popcnt256(twos),   1));
    cnt = _mm256_add_epi64(cnt, popcnt256(ones));

    for (/**/; i < size; ++i)
        cnt = _mm256_add_epi64(cnt, popcnt256(data1[i] & data2[i]));

    cnt64 = (uint64_t*) &cnt;

    return cnt64[0] +
            cnt64[1] +
            cnt64[2] +
            cnt64[3];
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("avx2")))
#endif
static uint64_t intersect_bitmaps_avx2(const uint64_t* FIC_RESTRICT b1, 
                                       const uint64_t* FIC_RESTRICT b2, 
                                       const uint32_t n_ints)
{
    uint64_t count = 0;
    const __m256i* r1 = (__m256i*)b1;
    const __m256i* r2 = (__m256i*)b2;
    const uint32_t n_cycles = n_ints / 4;

    count += popcnt_avx2_csa_intersect(r1, r2, n_cycles);

    for (int i = n_cycles*4; i < n_ints; ++i) {
        count += _mm_popcnt_u64(b1[i] & b2[i]);
    }

    return(count);
}
// #endif
#endif

/****************************
*  AVX512BW functions
****************************/

#if defined(HAVE_AVX512)
// #if SIMD_VERSION >= 6

#include <immintrin.h>

#if !defined(_MSC_VER)
  __attribute__ ((target ("avx512bw")))
#endif
static inline __m512i popcnt512(__m512i v) {
    __m512i m1 = _mm512_set1_epi8(0x55);
    __m512i m2 = _mm512_set1_epi8(0x33);
    __m512i m4 = _mm512_set1_epi8(0x0F);
    __m512i t1 = _mm512_sub_epi8(v,       (_mm512_srli_epi16(v, 1)   & m1));
    __m512i t2 = _mm512_add_epi8(t1 & m2, (_mm512_srli_epi16(t1, 2)  & m2));
    __m512i t3 = _mm512_add_epi8(t2,       _mm512_srli_epi16(t2, 4)) & m4;

    return _mm512_sad_epu8(t3, _mm512_setzero_si512());
}

#if !defined(_MSC_VER)
  __attribute__ ((target ("avx512bw")))
#endif
static inline void CSA512(__m512i* h, __m512i* l, __m512i a, __m512i b, __m512i c) {
    *l = _mm512_ternarylogic_epi32(c, b, a, 0x96);
    *h = _mm512_ternarylogic_epi32(c, b, a, 0xe8);
}

/*
 * AVX512 Harley-Seal popcount (4th iteration).
 * The algorithm is based on the paper "Faster Population Counts
 * using AVX2 Instructions" by Daniel Lemire, Nathan Kurz and
 * Wojciech Mula (23 Nov 2016).
 * @see https://arxiv.org/abs/1611.07612
 */
#if !defined(_MSC_VER)
  __attribute__ ((target ("avx512bw")))
#endif
static inline
uint64_t popcnt_avx512_csa_intersect(const __m512i* FIC_RESTRICT data1, 
                                     const __m512i* FIC_RESTRICT data2, 
                                     uint64_t size)
{
    __m512i cnt      = _mm512_setzero_si512();
    __m512i ones     = _mm512_setzero_si512();
    __m512i twos     = _mm512_setzero_si512();
    __m512i fours    = _mm512_setzero_si512();
    __m512i eights   = _mm512_setzero_si512();
    __m512i sixteens = _mm512_setzero_si512();
    __m512i twosA, twosB, foursA, foursB, eightsA, eightsB;

    uint64_t i = 0;
    uint64_t limit = size - size % 16;
    uint64_t* cnt64;

    for (/**/; i < limit; i += 16) {
        CSA512(&twosA,   &ones,   ones,  (data1[i+0]  & data2[i+0]), (data1[i+1] & data2[i+1]));
        CSA512(&twosB,   &ones,   ones,  (data1[i+2]  & data2[i+2]), (data1[i+3] & data2[i+3]));
        CSA512(&foursA,  &twos,   twos,  twosA, twosB);
        CSA512(&twosA,   &ones,   ones,  (data1[i+4]  & data2[i+4]), (data1[i+5] & data2[i+5]));
        CSA512(&twosB,   &ones,   ones,  (data1[i+6]  & data2[i+6]), (data1[i+7] & data2[i+7]));
        CSA512(&foursB,  &twos,   twos,  twosA, twosB);
        CSA512(&eightsA, &fours,  fours, foursA, foursB);
        CSA512(&twosA,   &ones,   ones,  (data1[i+8]  & data2[i+8]), (data1[i+9]   & data2[i+9]));
        CSA512(&twosB,   &ones,   ones,  (data1[i+10] & data2[i+10]), (data1[i+11] & data2[i+11]));
        CSA512(&foursA,  &twos,   twos,  twosA, twosB);
        CSA512(&twosA,   &ones,   ones,  (data1[i+12] & data2[i+12]), (data1[i+13] & data2[i+13]));
        CSA512(&twosB,   &ones,   ones,  (data1[i+14] & data2[i+14]), (data1[i+15] & data2[i+15]));
        CSA512(&foursB,  &twos,   twos,  twosA, twosB);
        CSA512(&eightsB, &fours,  fours, foursA, foursB);
        CSA512(&sixteens,&eights, eights,eightsA, eightsB);

        cnt = _mm512_add_epi64(cnt, popcnt512(sixteens));
    }

    cnt = _mm512_slli_epi64(cnt, 4);
    cnt = _mm512_add_epi64(cnt, _mm512_slli_epi64(popcnt512(eights), 3));
    cnt = _mm512_add_epi64(cnt, _mm512_slli_epi64(popcnt512(fours), 2));
    cnt = _mm512_add_epi64(cnt, _mm512_slli_epi64(popcnt512(twos), 1));
    cnt = _mm512_add_epi64(cnt,  popcnt512(ones));

    for (/**/; i < size; ++i)
        cnt = _mm512_add_epi64(cnt, popcnt512(data1[i] & data2[i]));

    cnt64 = (uint64_t*)&cnt;

    return cnt64[0] +
            cnt64[1] +
            cnt64[2] +
            cnt64[3] +
            cnt64[4] +
            cnt64[5] +
            cnt64[6] +
            cnt64[7];
}

// Functions
// AVX512
#if !defined(_MSC_VER)
  __attribute__ ((target ("avx512bw")))
#endif
static uint64_t intersect_bitmaps_avx512_csa(const uint64_t* FIC_RESTRICT b1, 
                                             const uint64_t* FIC_RESTRICT b2, 
                                             const uint32_t n_ints) 
{
    uint64_t count = 0;
    const __m512i* r1 = (__m512i*)b1;
    const __m512i* r2 = (__m512i*)b2;
    const uint32_t n_cycles = n_ints / 8;

    count += popcnt_avx512_csa_intersect(r1, r2, n_cycles);

    for (int i = n_cycles*8; i < n_ints; ++i) {
        count += _mm_popcnt_u64(b1[i] & b2[i]);
    }

    return(count);
}
// #endif
#endif

/****************************
*  Scalar functions
****************************/

static
uint64_t intersect_bitmaps_scalar(const uint64_t* FIC_RESTRICT b1, const uint64_t* FIC_RESTRICT b2, const uint32_t n_ints) {
    uint64_t count = 0;
    int i = 0;
    for(/**/; i + 4 < n_ints; i += 4) {
        count += FIC_POPCOUNT(b1[i+0] & b2[i+0]);
        count += FIC_POPCOUNT(b1[i+1] & b2[i+1]);
        count += FIC_POPCOUNT(b1[i+2] & b2[i+2]);
        count += FIC_POPCOUNT(b1[i+3] & b2[i+3]);
    }

    for (/**/; i < n_ints; ++i) {
        count += FIC_POPCOUNT(b1[i] & b2[i]);
    }

    return(count);
}

static
uint64_t intersect_bitmaps_scalar_list(const uint64_t* FIC_RESTRICT b1, const uint64_t* FIC_RESTRICT b2, 
    const uint32_t* l1, const uint32_t* l2,
    const uint32_t n1, const uint32_t n2) 
{
    uint64_t count = 0;

#define MOD(x) (( (x) * 64 ) >> 6)
    if(n1 < n2) {
        for(int i = 0; i < n1; ++i) {
            count += ((b2[l1[i] >> 6] & (1L << MOD(l1[i]))) != 0); 
            __builtin_prefetch(&b2[l1[i] >> 6], 0, _MM_HINT_T0);
        }
    } else {
        for(int i = 0; i < n2; ++i) {
            count += ((b1[l2[i] >> 6] & (1L << MOD(l2[i]))) != 0);
            __builtin_prefetch(&b1[l2[i] >> 6], 0, _MM_HINT_T0);
        }
    }
#undef MOD
    return(count);
}

/****************************
*  Wrappers
****************************/

typedef uint64_t (*ftype)(const uint64_t*, const uint64_t*, const uint32_t);
typedef uint64_t (*fltype)(const uint64_t*, const uint64_t*, 
    const uint32_t*, const uint32_t*,
    const uint32_t, const uint32_t);

static
uint64_t c_fwrapper(const uint32_t n_vectors, 
                    const uint64_t* vals, 
                    const uint32_t n_ints, 
                    const ftype f)
{
    uint32_t offset = 0;
    uint32_t inner_offset = 0;
    uint64_t total = 0;
    
    for (int i = 0; i < n_vectors; ++i) {
        inner_offset = offset + n_ints;
        for (int j = i + 1; j < n_vectors; ++j, inner_offset += n_ints) {
            total += (*f)(&vals[offset], &vals[inner_offset], n_ints);
        }
        offset += n_ints;
    }

    return total;
}

static
uint64_t c_flwrapper(const uint32_t n_vectors, 
                     const uint64_t* FIC_RESTRICT vals,
                     const uint32_t n_ints,
                     const uint32_t* FIC_RESTRICT n_alts,
                     const uint32_t* FIC_RESTRICT alt_positions,
                     const uint32_t* FIC_RESTRICT alt_offsets, 
                     const ftype f, 
                     const fltype fl, 
                     const uint32_t cutoff)
{
    uint64_t count = 0;
    uint64_t offset1 = 0;
    uint64_t offset2 = n_ints;

    for (int i = 0; i < n_vectors; ++i, offset1 += n_ints) {
        offset2 = offset1 + n_ints;
        for (int j = i+1; j < n_vectors; ++j, offset2 += n_ints) {
            if (n_alts[i] < cutoff || n_alts[j] < cutoff) {
                count += (*fl)(&vals[offset1], &vals[offset2], &alt_positions[alt_offsets[i]], &alt_positions[alt_offsets[j]], n_alts[i], n_alts[j]);
            } else {
                count += (*f)(&vals[offset1], &vals[offset2], n_ints);
            }
        }
    }
    return count;
}

static
uint64_t c_fwrapper_blocked(const uint32_t n_vectors, 
                            const uint64_t* vals, 
                            const uint32_t n_ints, 
                            const ftype f,
                            uint32_t block_size)
{
    uint64_t total = 0;

    block_size = (block_size == 0 ? 3 : block_size);
    const uint32_t n_blocks1 = n_vectors / block_size;
    const uint32_t n_blocks2 = n_vectors / block_size;

    uint32_t i  = 0;
    uint32_t tt = 0;
    for (/**/; i + block_size <= n_vectors; i += block_size) {
        // diagonal component
        uint32_t left = i*n_ints;
        uint32_t right = 0;
        for (uint32_t j = 0; j < block_size; ++j, left += n_ints) {
            right = left + n_ints;
            for (uint32_t jj = j + 1; jj < block_size; ++jj, right += n_ints) {
                total += (*f)(&vals[left], &vals[right], n_ints);
            }
        }

        // square component
        uint32_t curi = i;
        uint32_t j = curi + block_size;
        for (/**/; j + block_size <= n_vectors; j += block_size) {
            left = curi*n_ints;
            for (uint32_t ii = 0; ii < block_size; ++ii, left += n_ints) {
                right = j*n_ints;
                for (uint32_t jj = 0; jj < block_size; ++jj, right += n_ints) {
                    total += (*f)(&vals[left], &vals[right], n_ints);
                }
            }
        }

        // residual
        right = j*n_ints;
        for (/**/; j < n_vectors; ++j, right += n_ints) {
            left = curi*n_ints;
            for (uint32_t jj = 0; jj < block_size; ++jj, left += n_ints) {
                total += (*f)(&vals[left], &vals[right], n_ints);
            }
        }
    }
    // residual tail
    uint32_t left = i*n_ints;
    for (/**/; i < n_vectors; ++i, left += n_ints) {
        uint32_t right = left + n_ints;
        for (uint32_t j = i + 1; j < n_vectors; ++j, right += n_ints) {
            total += (*f)(&vals[left], &vals[right], n_ints);
        }
    }

    return total;
}

static
uint64_t c_flwrapper_blocked(const uint32_t n_vectors, 
                             const uint64_t* FIC_RESTRICT vals,
                             const uint32_t n_ints,
                             const uint32_t* FIC_RESTRICT n_alts,
                             const uint32_t* FIC_RESTRICT alt_positions,
                             const uint32_t* FIC_RESTRICT alt_offsets, 
                             const ftype f, 
                             const fltype fl, 
                             const uint32_t cutoff,
                             uint32_t block_size)
{
    uint64_t total = 0;

    block_size = (block_size == 0 ? 3 : block_size);
    const uint32_t n_blocks1 = n_vectors / block_size;
    const uint32_t n_blocks2 = n_vectors / block_size;

    uint32_t i  = 0;
    uint32_t tt = 0;

    for (/**/; i + block_size <= n_vectors; i += block_size) {
        // diagonal component
        uint32_t left = i*n_ints;
        uint32_t right = 0;
        for (uint32_t j = 0; j < block_size; ++j, left += n_ints) {
            right = left + n_ints;
            for (uint32_t jj = j + 1; jj < block_size; ++jj, right += n_ints) {
                if (n_alts[i+j] < cutoff || n_alts[i+jj] < cutoff) {
                    total += (*fl)(&vals[left], &vals[right], 
                        &alt_positions[alt_offsets[i+j]], &alt_positions[alt_offsets[i+jj]], 
                        n_alts[i+j], n_alts[i+jj]);
                } else {
                    total += (*f)(&vals[left], &vals[right], n_ints);
                }
            }
        }

        // square component
        uint32_t curi = i;
        uint32_t j = curi + block_size;
        for (/**/; j + block_size <= n_vectors; j += block_size) {
            left = curi*n_ints;
            for (uint32_t ii = 0; ii < block_size; ++ii, left += n_ints) {
                right = j*n_ints;
                for (uint32_t jj = 0; jj < block_size; ++jj, right += n_ints) {
                    if (n_alts[curi+ii] < cutoff || n_alts[j+jj] < cutoff) {
                        total += (*fl)(&vals[left], &vals[right], 
                            &alt_positions[alt_offsets[curi+ii]], &alt_positions[alt_offsets[j+jj]], 
                            n_alts[curi+ii], n_alts[j+jj]);
                    } else {
                        total += (*f)(&vals[left], &vals[right], n_ints);
                    }
                }
            }
        }

        // residual
        right = j*n_ints;
        for (/**/; j < n_vectors; ++j, right += n_ints) {
            left = curi*n_ints;
            for (uint32_t jj = 0; jj < block_size; ++jj, left += n_ints) {
                if (n_alts[curi+jj] < cutoff || n_alts[j] < cutoff) {
                    total += (*fl)(&vals[left], &vals[right], 
                        &alt_positions[alt_offsets[curi+jj]], &alt_positions[alt_offsets[j]], 
                        n_alts[curi+jj], n_alts[j]);
                } else {
                    total += (*f)(&vals[left], &vals[right], n_ints);
                }
            }
        }
    }
    // residual tail
    uint32_t left = i*n_ints;
    for (/**/; i < n_vectors; ++i, left += n_ints) {
        uint32_t right = left + n_ints;
        for (uint32_t j = i + 1; j < n_vectors; ++j, right += n_ints) {
            if (n_alts[i] < cutoff || n_alts[j] < cutoff) {
                total += (*fl)(&vals[left], &vals[right], 
                    &alt_positions[alt_offsets[i]], &alt_positions[alt_offsets[j]], 
                    n_alts[i], n_alts[j]);
            } else {
                total += (*f)(&vals[left], &vals[right], n_ints);
            }
        }
    }

    return total;
}

// Intersect
static 
uint64_t intersect(const void* data, const uint32_t n_vectors, const uint32_t n_bitmaps_vector) {

#if defined(HAVE_CPUID)
  #if defined(__cplusplus)
    /* C++11 thread-safe singleton */
    static const int cpuid = get_cpuid();
  #else
    static int cpuid_ = -1;
    int cpuid = cpuid_;
    if (cpuid == -1) {
      cpuid = get_cpuid();

      #if defined(_MSC_VER)
        _InterlockedCompareExchange(&cpuid_, cpuid, -1);
      #else
        __sync_val_compare_and_swap(&cpuid_, -1, cpuid);
      #endif
    }
  #endif
#endif

#if defined(HAVE_AVX512)
    if ((cpuid & bit_AVX512BW) && n_bitmaps_vector >= 128) { // 16*512
        return c_fwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, &intersect_bitmaps_avx512_csa, FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

#if defined(HAVE_AVX2)
    if ((cpuid & bit_AVX2) && n_bitmaps_vector >= 64) { // 16*256
        return c_fwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, &intersect_bitmaps_avx2, FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

#if defined(HAVE_SSE41)
    if ((cpuid & bit_SSE41) && n_bitmaps_vector >= 32) { // 16*128
        return c_fwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, &intersect_bitmaps_sse4, FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

    return c_fwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, &intersect_bitmaps_scalar, FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
}

static 
uint32_t get_alignment() {

#if defined(HAVE_CPUID)
  #if defined(__cplusplus)
    /* C++11 thread-safe singleton */
    static const int cpuid = get_cpuid();
  #else
    static int cpuid_ = -1;
    int cpuid = cpuid_;
    if (cpuid == -1) {
      cpuid = get_cpuid();

      #if defined(_MSC_VER)
        _InterlockedCompareExchange(&cpuid_, cpuid, -1);
      #else
        __sync_val_compare_and_swap(&cpuid_, -1, cpuid);
      #endif
    }
  #endif
#endif

        uint32_t alignment = 0;
#if defined(HAVE_AVX512)
        if ((cpuid & bit_AVX512BW)) { // 16*512
            alignment = FIC_AVX512_ALIGNMENT;
        }
#endif

#if defined(HAVE_AVX2)
        if ((cpuid & bit_AVX2) && alignment == 0) { // 16*256
            alignment = FIC_AVX2_ALIGNMENT;
        }
#endif

#if defined(HAVE_SSE41)
        if ((cpuid & bit_SSE41) && alignment == 0) { // 16*128
            alignment = FIC_SSE_ALIGNMENT;
        }
#endif

        if (alignment == 0) alignment = 8;
        return alignment;
}

static 
uint64_t intersect_list(const void* data, 
                        const uint32_t n_vectors, 
                        const uint32_t n_bitmaps_vector, 
                        const uint32_t* n_alts, 
                        const uint32_t* alt_pos, 
                        const uint32_t* alt_offsets,
                        const uint32_t cutoff)
{

#if defined(HAVE_CPUID)
  #if defined(__cplusplus)
    /* C++11 thread-safe singleton */
    static const int cpuid = get_cpuid();
  #else
    static int cpuid_ = -1;
    int cpuid = cpuid_;
    if (cpuid == -1) {
      cpuid = get_cpuid();

      #if defined(_MSC_VER)
        _InterlockedCompareExchange(&cpuid_, cpuid, -1);
      #else
        __sync_val_compare_and_swap(&cpuid_, -1, cpuid);
      #endif
    }
  #endif
#endif


#if defined(HAVE_AVX512)
    if ((cpuid & bit_AVX512BW) && n_bitmaps_vector >= 128) { // 16*512
        return c_flwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, 
            n_alts, alt_pos, alt_offsets, 
            &intersect_bitmaps_avx512_csa, 
            &intersect_bitmaps_scalar_list, 
            50,
            FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

#if defined(HAVE_AVX2)
    if ((cpuid & bit_AVX2) && n_bitmaps_vector >= 64) { // 16*256
        return c_flwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, 
            n_alts, alt_pos, alt_offsets, 
            &intersect_bitmaps_avx2, 
            &intersect_bitmaps_scalar_list, 
            50,
            FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

#if defined(HAVE_SSE41)
    if ((cpuid & bit_SSE41) && n_bitmaps_vector >= 32) { // 16*128
        return c_flwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, 
            n_alts, alt_pos, alt_offsets, 
            &intersect_bitmaps_sse4, 
            &intersect_bitmaps_scalar_list, 
            50,
            FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
    }
#endif

    return c_flwrapper_blocked(n_vectors, (uint64_t*)data, n_bitmaps_vector, 
            n_alts, alt_pos, alt_offsets, 
            &intersect_bitmaps_scalar, 
            &intersect_bitmaps_scalar_list, 
            50,
            FIC_DEFAULT_BLOCK/(n_bitmaps_vector*8));
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FAST_INTERSECT_COUNT_H_ */
