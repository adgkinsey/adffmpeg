/*
 * Data exportable to clients for AD-Holdings data types
 * Copyright (c) 2006-2010 AD-Holdings plc
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

#ifndef AVFORMAT_DS_EXPORTS_H
#define AVFORMAT_DS_EXPORTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


typedef struct {
    uint16_t src_pixels;        ///< Input image size (horizontal)
    uint16_t src_lines;         ///< Input image size (vertical)
    uint16_t target_pixels;     ///< Output image size (horizontal)
    uint16_t target_lines;      ///< Output image size (vertical)
    uint16_t pixel_offset;      ///< Image start offset (horizontal)
    uint16_t line_offset;       ///< Image start offset (vertical)
} NetVuPicture;

typedef struct {
    uint32_t version;                ///<  structure version number */

    /** mode: in PIC_REVISION 0 this was the DFT style FULL_HI etc
     *        in PIC_REVISION 1 this is used to specify AD or JFIF format image
     */
    int32_t mode;
    int32_t cam;                ///< camera number
    int32_t vid_format;         ///< 422 or 411
    uint32_t start_offset;      ///< start of picture
    int32_t size;               ///< size of image
    int32_t max_size;           ///< maximum size allowed
    int32_t target_size;        ///< size wanted for compression
    int32_t factor;             ///< Q factor
    uint32_t alm_bitmask_hi;    ///< High 32 bits of the alarm bitmask
    int32_t status;             ///< status of last action performed on picture
    uint32_t session_time;      ///< playback time of image
    uint32_t milliseconds;      ///< sub-second count for playback speed control
    char res[4];                ///< picture size
    char title[31];             ///< camera title
    char alarm[31];             ///< alarm text - title, comment etc
    NetVuPicture format;        ///< NOTE: Do not assign to a pointer due to CW-alignment
    char locale[30];            ///< Timezone name
    int32_t utc_offset;         ///< Timezone difference in minutes
    uint32_t alm_bitmask;
} NetVuImageData;
#define NetVuImageDataHeaderSize 168

typedef struct _audioHeader {
    uint32_t            version;
    int32_t             mode;
    int32_t             channel;
    int32_t             sizeOfAdditionalData;
    int32_t             sizeOfAudioData;
    uint32_t            seconds;
    uint32_t            msecs;
    unsigned char *     additionalData;
} NetVuAudioData;
#define NetVuAudioDataHeaderSize (28 + sizeof(unsigned char *))

#define ID_LENGTH                       8
#define NUM_ACTIVITIES                  8
#define CAM_TITLE_LENGTH                24
#define ALARM_TEXT_LENGTH               24

typedef struct _dmImageData {
    char            identifier[ID_LENGTH];
    unsigned long   jpegLength;
    int64_t         imgSeq;
    int64_t         imgTime;
    unsigned char   camera;
    unsigned char   status;
    unsigned short  activity[NUM_ACTIVITIES];
    unsigned short  QFactor;
    unsigned short  height;
    unsigned short  width;
    unsigned short  resolution;
    unsigned short  interlace;
    unsigned short  subHeaderMask;
    char            camTitle[CAM_TITLE_LENGTH];
    char            alarmText[ALARM_TEXT_LENGTH];
} DMImageData;


typedef enum _frameType {
    FrameTypeUnknown = 0,
    NetVuVideo,
    NetVuAudio,
    DMVideo,
    DMNudge,
    NetVuDataInfo,
    NetVuDataLayout,
    RTPAudio
} ADFrameType;

/** This is the data structure that the ffmpeg parser fills in as part of the 
 * parsing routines. It will be shared between adpic and dspic so that our 
 * clients can be compatible with either stream more easily
 */
typedef struct {
    /// Type of frame we have. See ADFrameType enum for supported types
    ADFrameType         frameType;
    /// Pointer to structure holding the information for the frame. 
    void *              frameData;
    void *              additionalData;
} ADFrameData;

#define RTP_PAYLOAD_TYPE_8000HZ_ADPCM                       5
#define RTP_PAYLOAD_TYPE_11025HZ_ADPCM                      16
#define RTP_PAYLOAD_TYPE_16000HZ_ADPCM                      6
#define RTP_PAYLOAD_TYPE_22050HZ_ADPCM                      17
#define RTP_PAYLOAD_TYPE_32000HZ_ADPCM                      96
#define RTP_PAYLOAD_TYPE_44100HZ_ADPCM                      97
#define RTP_PAYLOAD_TYPE_48000HZ_ADPCM                      98
#define RTP_PAYLOAD_TYPE_8000HZ_PCM                         100
#define RTP_PAYLOAD_TYPE_11025HZ_PCM                        101
#define RTP_PAYLOAD_TYPE_16000HZ_PCM                        102
#define RTP_PAYLOAD_TYPE_22050HZ_PCM                        103
#define RTP_PAYLOAD_TYPE_32000HZ_PCM                        104
#define RTP_PAYLOAD_TYPE_44100HZ_PCM                        11
#define RTP_PAYLOAD_TYPE_48000HZ_PCM                        105

#ifdef __cplusplus
}
#endif

#endif
