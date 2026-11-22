#pragma once
// Content dedup for live sources: the whole-picture comparison of a decoded
// frame's CPU planes against the previous new picture (ContentDedup.h decides
// what to do with the result). Hardware frames and bitstream carriers have no
// comparable planes and report none, so the caller keeps the sample.
#include "veyra/pipeline/PictureCompare.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

namespace veyra::pipeline {

inline bool captureFramePicture(const AVFrame* frame, PictureReference& reference, PictureReference::Result& out) {
    if (!frame || !frame->data[0] || frame->width <= 0 || frame->height <= 0) return false;
    const auto format = AVPixelFormat(frame->format);
    const auto* desc = av_pix_fmt_desc_get(format);
    if (!desc || (desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_PAL))) return false;
    // 8-bit components compare bytewise and little-endian 16-bit containers as
    // words; anything else (5/6-bit RGB, 10-bit fields packed in 32 bits) exactly.
    const unsigned depth = desc->comp[0].depth;
    bool words = depth > 8 && !(desc->flags & AV_PIX_FMT_FLAG_BE);
    for (int c = 0; c < desc->nb_components; ++c)
        words = words && desc->comp[c].depth + desc->comp[c].shift <= 16 && desc->comp[c].step % 2 == 0 && desc->comp[c].offset % 2 == 0;
    const auto kind = depth == 8 ? PictureComponents::Bytes : words ? PictureComponents::Words : PictureComponents::Exact;
    PicturePlane planes[4]; unsigned count = 0;
    for (int plane = 0, total = av_pix_fmt_count_planes(format); plane < total && plane < 4; ++plane) {
        const int bytes = av_image_get_linesize(format, frame->width, plane);
        const int rows = plane == 1 || plane == 2 ? -((-frame->height) >> desc->log2_chroma_h) : frame->height;
        if (bytes <= 0 || !frame->data[plane] || frame->linesize[plane] < bytes) return false;
        planes[count++] = {frame->data[plane], size_t(frame->linesize[plane]), size_t(bytes), unsigned(rows)};
    }
    out = reference.observe(planes, count, kind, PictureReference::bandFor(depth, desc->comp[0].shift));
    return true;
}

} // namespace veyra::pipeline
