/*
 * copyright (c) 2010 AD-Holdings plc
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

#ifndef AVFORMAT_LIBPAR_H
#define AVFORMAT_LIBPAR_H

#include "avformat.h"


#ifdef AD_SIDEDATA_IN_PRIV
#include <parreader_types.h>

typedef struct {
    int indexInfoCount;
    ParFrameInfo *frameInfo;
    ParKeyValuePair *indexInfo;
    int fileChanged;
} LibparFrameExtra;

#endif // AD_SIDEDATA_IN_PRIV

#endif /* AVFORMAT_LIBPAR_H */
