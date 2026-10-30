/*
 * compat.h - Portability shim for building Arboretum with MinGW-w64 / GCC.
 *
 * This header is NEVER included by the library sources themselves. It is
 * force-included from the command line (`g++ -include .../compat.h ...`), so
 * that not a single file tracked by git needs to be modified in either of the
 * two versions under comparison (baseline and query-gravity).
 *
 * Why it is needed:
 *   Arboretum uses the BSD/POSIX spellings u_int8_t / u_int16_t / u_int32_t in
 *   ~3100 places (e.g. src/include/arboretum/stPage.h:51), but its headers never
 *   include <sys/types.h> themselves. On glibc they get these typedefs
 *   transitively; MinGW-w64 does not provide them at all, so the build fails
 *   with "'u_int32_t' does not name a type".
 *
 *   On glibc, <sys/types.h> already declares them and __GLIBC__ is defined, so
 *   we leave the system definitions alone. Everywhere else we declare them
 *   ourselves as exact aliases of the fixed-width <stdint.h> types.
 *
 * IMPORTANT: this flag is applied identically to both versions under test, so
 * it cannot bias the comparison.
 */
#ifndef BENCH_COMPAT_H
#define BENCH_COMPAT_H

/*
 * Second portability gap: src/include/arboretum/stMetricHistogram.h:386 uses
 * M_PI, which is not part of standard C++ and which MinGW's <math.h> only
 * exposes when _USE_MATH_DEFINES is defined *before* <math.h> is first seen.
 * Because this header is force-included ahead of everything else, defining the
 * macro here is enough.
 */
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <math.h>

#include <sys/types.h> /* defines __GLIBC__ on Linux; harmless on MinGW */
#include <stdint.h>    /* global-namespace uint8_t/uint16_t/uint32_t/uint64_t */

#if !defined(__GLIBC__)
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#endif

#endif /* BENCH_COMPAT_H */
