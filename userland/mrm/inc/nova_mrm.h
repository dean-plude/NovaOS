/* NovaOS: included first in each of MRT Core's sources (third_party/mrtcore)
 * by userland/mrm/build.py.  MinGW-w64's sal.h leaves the old annotations
 * __in and __out undefined in C++ compiled as GCC (they clashed with
 * libstdc++'s argument names; NovaOS's C++ library is Microsoft's STL) */
#pragma once
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
/* and these older ones MinGW-w64 lacks altogether */
#define __field_ecount(n)
#define __deref_opt_out_opt
#define __deref_out_bcount(n)
#define __out_ecount_opt(n)
#define __in_bcount_opt(n)
