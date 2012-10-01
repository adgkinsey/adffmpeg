/*
 * Copyright (c) 2009 Stefano Sabatini
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

/**
 * @file
 * pixdesc test filter
 */

#include "libavutil/eval.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "video.h"


typedef struct {
    const AVClass *class;
    char *comp_expr_str[4];
    int rgba_map[4];
    int copyplane[4];
    int skipplane[4];
    const AVPixFmtDescriptor *pix_desc;
} CopyPlaneContext;

#define R 0
#define G 1
#define B 2
#define A 3

#define OFFSET(x) offsetof(CopyPlaneContext, x)

static const AVOption copyplane_options[] = {
    {"r",  "set R expression", OFFSET(comp_expr_str[0]),  FF_OPT_TYPE_STRING, {.str="r"}, CHAR_MIN, CHAR_MAX},
    {"g",  "set G expression", OFFSET(comp_expr_str[1]),  FF_OPT_TYPE_STRING, {.str="g"}, CHAR_MIN, CHAR_MAX},
    {"b",  "set B expression", OFFSET(comp_expr_str[2]),  FF_OPT_TYPE_STRING, {.str="b"}, CHAR_MIN, CHAR_MAX},
    {"a",  "set A expression", OFFSET(comp_expr_str[3]),  FF_OPT_TYPE_STRING, {.str="a"}, CHAR_MIN, CHAR_MAX},
    {NULL},
};

static const char *copyplane_get_name(void *ctx)
{
        return "copyplane";
}

static const AVClass copyplane_class = {
        "CopyPlaneContext",
        copyplane_get_name,
        copyplane_options
};


static av_cold int init(AVFilterContext *ctx, const char *args)
{
    CopyPlaneContext *copyplane = ctx->priv;
    int ret;
    
    copyplane->class = &copyplane_class;
    av_opt_set_defaults(copyplane);
    
    if (args && (ret = av_set_options_string(copyplane, args, "=", ":")) < 0)
        return ret;
    
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    CopyPlaneContext *copyplane = ctx->priv;
    
    for (i = 0; i < 4; i++) {
        av_freep(&copyplane->comp_expr_str[i]);
    }
}

#define RGB_FORMATS                             \
    PIX_FMT_ARGB,         PIX_FMT_RGBA,         \
    PIX_FMT_ABGR,         PIX_FMT_BGRA

static enum PixelFormat rgb_pix_fmts[] = { RGB_FORMATS, PIX_FMT_NONE };

static int query_formats(AVFilterContext *ctx)
{
    //CopyPlaneContext *copyplane = ctx->priv;
    ff_set_common_formats(ctx, ff_make_format_list(rgb_pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CopyPlaneContext *cp = ctx->priv;
    int comp;

    cp->pix_desc = &av_pix_fmt_descriptors[inlink->format];
    
    switch (inlink->format) {
        case PIX_FMT_ARGB:  cp->rgba_map[A] = 0; cp->rgba_map[R] = 1; cp->rgba_map[G] = 2; cp->rgba_map[B] = 3; break;
        case PIX_FMT_ABGR:  cp->rgba_map[A] = 0; cp->rgba_map[B] = 1; cp->rgba_map[G] = 2; cp->rgba_map[R] = 3; break;
        case PIX_FMT_RGBA:
        case PIX_FMT_RGB24: cp->rgba_map[R] = 0; cp->rgba_map[G] = 1; cp->rgba_map[B] = 2; cp->rgba_map[A] = 3; break;
        case PIX_FMT_BGRA:
        case PIX_FMT_BGR24: cp->rgba_map[B] = 0; cp->rgba_map[G] = 1; cp->rgba_map[R] = 2; cp->rgba_map[A] = 3; break;
    }

    for (comp = 0; comp < cp->pix_desc->nb_components; comp++) {
        int plane = cp->rgba_map[comp];
        cp->copyplane[plane] = -1;
        cp->skipplane[plane] = 0;
        switch (cp->comp_expr_str[comp][0])  {
            case('r'):
                if (plane != cp->rgba_map[R])  {
                    cp->copyplane[plane] = cp->rgba_map[R];
                    cp->skipplane[plane] = 1;
                }
                break;
            case('g'):
                if (plane != cp->rgba_map[G])  {
                    cp->copyplane[plane] = cp->rgba_map[G];
                    cp->skipplane[plane] = 1;
                }
                break;
            case('b'):
                if (plane != cp->rgba_map[B])  {
                    cp->copyplane[plane] = cp->rgba_map[B];
                    cp->skipplane[plane] = 1;
                }
                break;
            case('a'):
                if (plane != cp->rgba_map[A])  {
                    cp->copyplane[plane] = cp->rgba_map[A];
                    cp->skipplane[plane] = 1;
                }
                break;
            case('n'):
                cp->skipplane[comp] = 1;
                break;
        }
    }

    return 0;
}

static int start_frame(AVFilterLink *inlink, AVFilterBufferRef *picref)
{
    CopyPlaneContext *priv = inlink->dst->priv;
    AVFilterLink *outlink  = inlink->dst->outputs[0];
    AVFilterBufferRef *outpicref;
    int i;

    outlink->out_buf = ff_get_video_buffer(outlink, AV_PERM_WRITE,
                                           outlink->w, outlink->h);
    outpicref = outlink->out_buf;
    avfilter_copy_buffer_ref_props(outpicref, picref);

    for (i = 0; i < 4; i++) {
        int h = outlink->h;
        h = i == 1 || i == 2 ? h>>priv->pix_desc->log2_chroma_h : h;
        if (outpicref->data[i]) {
            uint8_t *data = outpicref->data[i] +
                (outpicref->linesize[i] > 0 ? 0 : outpicref->linesize[i] * (h-1));
            memset(data, 0, FFABS(outpicref->linesize[i]) * h);
        }
    }

    /* copy palette */
    if (priv->pix_desc->flags & PIX_FMT_PAL)
        memcpy(outpicref->data[1], outpicref->data[1], 256*4);

    return ff_start_frame(outlink, avfilter_ref_buffer(outpicref, ~0));
}

static int draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir)
{
    CopyPlaneContext *cp      = inlink->dst->priv;
    AVFilterBufferRef *inpic  = inlink->cur_buf;
    AVFilterBufferRef *outpic = inlink->dst->outputs[0]->out_buf;
    int i, c, w = inlink->w;
    uint16_t *line = NULL;
    
    if (!(line = av_malloc(sizeof(uint16_t) * w)))
        return AVERROR(ENOMEM);

    for (c = 0; c < cp->pix_desc->nb_components; c++) {
        int w1 = c == 1 || c == 2 ? w>>cp->pix_desc->log2_chroma_w : w;
        int h1 = c == 1 || c == 2 ? h>>cp->pix_desc->log2_chroma_h : h;
        int y1 = c == 1 || c == 2 ? y>>cp->pix_desc->log2_chroma_h : y;

        for (i = y1; i < y1 + h1; i++) {
            if (cp->copyplane[c] >= 0)  {
                av_read_image_line(line, 
                                   (const uint8_t **)inpic->data,
                                   inpic->linesize,
                                   cp->pix_desc,
                                   0, i, cp->copyplane[c], w1, 0);
                av_write_image_line(line,
                                    outpic->data,
                                    outpic->linesize,
                                    cp->pix_desc,
                                    0, i, c, w1);
            }
            if (!cp->skipplane[c])  {
                av_read_image_line(line, 
                                   (const uint8_t **)inpic->data,
                                   inpic->linesize,
                                   cp->pix_desc,
                                   0, i, c, w1, 0);
                av_write_image_line(line,
                                    outpic->data,
                                    outpic->linesize,
                                    cp->pix_desc,
                                    0, i, c, w1);
            }                
        }
    }
    av_free(line);

    return ff_draw_slice(inlink->dst->outputs[0], y, h, slice_dir);
}

AVFilter avfilter_vf_copyplane = {
    .name          = "copyplane",
    .description   = NULL_IF_CONFIG_SMALL("Copy R,G or B plane to A."),

    .priv_size     = sizeof(CopyPlaneContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs        = (AVFilterPad[]) {{ .name            = "default",
                                        .type            = AVMEDIA_TYPE_VIDEO,
                                        .start_frame     = start_frame,
                                        .draw_slice      = draw_slice,
                                        .config_props    = config_props,
                                        .min_perms       = AV_PERM_READ, },
                                      { .name = NULL}},

    .outputs       = (AVFilterPad[]) {{ .name            = "default",
                                        .type            = AVMEDIA_TYPE_VIDEO, },
                                      { .name = NULL}},
};
