/* wf_asin.c -- float version of w_asin.c.
 * Conversion to float by Ian Lance Taylor, Cygnus Support, ian@cygnus.com.
 */

/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 *
 */

/* 
 * wrapper asinf(x)
 */


#include "fdlibm.h"

#ifdef _LIBM_REENT
#define asinf _asinf_r
#endif

#if defined (_LIBM_REENT) || ! defined (_REENT_ONLY)

#ifdef __STDC__
	float asinf(_R1 float x)		/* wrapper asinf */
#else
	float asinf(_R2 x)			/* wrapper asinf */
	_R3 float x;
#endif
{
#ifdef _IEEE_LIBM
	return __ieee754_asinf(x);
#else
	float z;
	z = __ieee754_asinf(x);
	if(_LIB_VERSION == _IEEE_ || isnanf(x)) return z;
	if(fabsf(x)>(float)1.0) {
	    /* asinf(|x|>1) */
	    return (float)__kernel_standard(_R4,(double)x,(double)x,102);
	} else
	    return z;
#endif
}

#endif /* defined (_LIBM_REENT) || ! defined (_REENT_ONLY) */
