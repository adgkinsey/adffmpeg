/*
 * The simplest mpeg encoder (well, it was the simplest!)
 * Copyright (c) 2000,2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/cpu.h"
#include "libavutil/x86/asm.h"
#include "libavcodec/avcodec.h"
#include "libavcodec/dsputil.h"
#include "libavcodec/mpegvideo.h"
#include "dsputil_mmx.h"

#if HAVE_INLINE_ASM

extern uint16_t ff_inv_zigzag_direct16[64];

#if HAVE_SSSE3
#define HAVE_SSSE3_BAK
#endif
#undef HAVE_SSSE3
#define HAVE_SSSE3 0

#undef HAVE_SSE2
#undef HAVE_MMXEXT
#define HAVE_SSE2 0
#define HAVE_MMXEXT 0
#define RENAME(a) a ## _MMX
#define RENAMEl(a) a ## _mmx
#include "mpegvideoenc_template.c"

#undef HAVE_MMXEXT
#define HAVE_MMXEXT 1
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _MMX2
#define RENAMEl(a) a ## _mmx2
#include "mpegvideoenc_template.c"

#undef HAVE_SSE2
#define HAVE_SSE2 1
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSE2
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"

#ifdef HAVE_SSSE3_BAK
#undef HAVE_SSSE3
#define HAVE_SSSE3 1
#undef RENAME
#undef RENAMEl
#define RENAME(a) a ## _SSSE3
#define RENAMEl(a) a ## _sse2
#include "mpegvideoenc_template.c"
#endif

#endif /* HAVE_INLINE_ASM */

void ff_MPV_encode_init_x86(MpegEncContext *s)
{
#if HAVE_INLINE_ASM
    int mm_flags = av_get_cpu_flags();
    const int dct_algo = s->avctx->dct_algo;

    if (dct_algo == FF_DCT_AUTO || dct_algo == FF_DCT_MMX) {
#if HAVE_SSSE3
        if (mm_flags & AV_CPU_FLAG_SSSE3) {
            s->dct_quantize = dct_quantize_SSSE3;
        } else
#endif
        if (mm_flags & AV_CPU_FLAG_SSE2) {
            s->dct_quantize = dct_quantize_SSE2;
        } else if (mm_flags & AV_CPU_FLAG_MMXEXT) {
            s->dct_quantize = dct_quantize_MMX2;
        } else {
            s->dct_quantize = dct_quantize_MMX;
        }
    }
#endif /* HAVE_INLINE_ASM */
}
