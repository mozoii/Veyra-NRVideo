#pragma once
// Dolby Vision Profile 7 dual-layer reconstruction (BL + EL), per ETSI GS CCM 001 V1.1.1 5.4.
//
// This is a **reference implementation**: pure functions, unit-testable, and line-for-line with the GPU version in PresentBlit.
// It was written on the CPU first because the spec only gives pseudo-code: one wrong shift or bit-depth and the picture is off,
// and it is hard to reconcile on the GPU step by step. Here the real RPU parameters run every branch first, and the GPU version follows.
//
// Parameter source: FFmpeg's AVDOVIRpuDataHeader / AVDOVIDataMapping (av_dovi_get_header /
// av_dovi_get_mapping). Measured on this sample (Dolby FEL Test): luma inverse mapping identity
// (polyOrder 1, slope 1.0), coef_log2_denom=23, bit depth 10/10/12, NLQ LINEAR_DZ.
//
// Custom addition for the P5 work; the playback path does not call it before the GPU version is done.
#include <algorithm>
#include <cmath>
#include <cstdint>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/dovi_meta.h>
}

namespace veyra::engine {

// The EL sample after inverse quantization: the spec requires a 17-bit signed value.
inline int64_t doviNlq(const AVDOVIDataMapping& mapping, int cmp, int elBitDepth,
                       int coefLog2Denom, int64_t e) {
    const auto& n = mapping.nlq[cmp];
    int64_t rr = e - int64_t(n.nlq_offset);
    if (rr == 0) return 0;
    const int64_t sign = rr < 0 ? -1 : 1;
    rr <<= 1;
    rr -= sign;
    rr <<= (10 - elBitDepth);
    int64_t dq = rr * int64_t(n.linear_deadzone_slope);
    dq += (int64_t(n.linear_deadzone_threshold) << (10 - elBitDepth + 1)) * sign;
    const int64_t limit = int64_t(n.vdr_in_max) << (10 - elBitDepth + 1);
    dq = std::clamp(dq, -limit, limit);
    return dq >> (coefLog2Denom - 5 - elBitDepth);
}

// Which segment the piecewise function lands in: pivot[i] <= s < pivot[i+1].
inline int doviPivotIndex(const AVDOVIReshapingCurve& curve, int64_t s) {
    const int pieces = curve.num_pivots > 0 ? int(curve.num_pivots) - 1 : 0;
    for (int i = 0; i < pieces; ++i)
        if (s >= int64_t(curve.pivots[i]) && s < int64_t(curve.pivots[i + 1])) return i;
    return s < int64_t(curve.pivots[0]) ? 0 : std::max(0, pieces - 1);
}

// 5.4.2.3.2 Polynomial mapping (luma).
inline uint16_t doviPolyMapping(const AVDOVIReshapingCurve& curve, int piece, int64_t s,
                                int blBitDepth, int coefLog2Denom) {
    // Spec pseudo-code: for (i = 0; i <= poly_order_minus1 + 1; i++)
    const int terms = int(curve.poly_order[piece]) + 2;
    int64_t ss = 1;
    int shift = 20;                                  // 2 * maximum BL bit depth
    int64_t vv = 0;
    for (int i = 0; i < terms && i < 3; ++i) {
        vv += curve.poly_coef[piece][i] * (ss << shift);
        ss *= s;
        shift -= blBitDepth;
    }
    vv = std::max<int64_t>(vv, 0);
    int64_t v = vv >> (4 + coefLog2Denom);
    return uint16_t(std::min<int64_t>(v, 0xffff));
}

// 5.4.2.3.3 MMR mapping (chroma). s0 must be the **downsampled** BL luma.
inline uint16_t doviMmrMapping(const AVDOVIReshapingCurve& curve, int piece,
                               int64_t s0, int64_t s1, int64_t s2,
                               int blBitDepth, int coefLog2Denom) {
    int64_t tt[22] = {};
    const int order = int(curve.mmr_order[piece]);
    tt[0] = int64_t(1) << 20;
    if (order >= 1) {
        tt[1] = s0 << (20 - blBitDepth);
        tt[2] = s1 << (20 - blBitDepth);
        tt[3] = s2 << (20 - blBitDepth);
        tt[4] = (s0 * s1) << (20 - 2 * blBitDepth);
        tt[5] = (s0 * s2) << (20 - 2 * blBitDepth);
        tt[6] = (s1 * s2) << (20 - 2 * blBitDepth);
        tt[7] = (tt[4] * tt[3]) >> 20;
    }
    if (order >= 2) {
        tt[8] = (s0 * s0) << (20 - 2 * blBitDepth);
        tt[9] = (s1 * s1) << (20 - 2 * blBitDepth);
        tt[10] = (s2 * s2) << (20 - 2 * blBitDepth);
        tt[11] = (tt[4] * tt[4]) >> 20;
        tt[12] = (tt[5] * tt[5]) >> 20;
        tt[13] = (tt[6] * tt[6]) >> 20;
        tt[14] = (tt[7] * tt[7]) >> 20;
    }
    if (order >= 3) {
        tt[15] = (tt[1] * tt[8]) >> 20;
        tt[16] = (tt[2] * tt[9]) >> 20;
        tt[17] = (tt[3] * tt[10]) >> 20;
        tt[18] = (tt[4] * tt[11]) >> 20;
        tt[19] = (tt[5] * tt[12]) >> 20;
        tt[20] = (tt[6] * tt[13]) >> 20;
        tt[21] = (tt[7] * tt[14]) >> 20;
    }
    int64_t rr = curve.mmr_constant[piece] * tt[0];
    int cnt = 1;
    for (int i = 1; i <= order; ++i)
        for (int j = 0; j < 7; ++j) {
            rr += curve.mmr_coef[piece][i - 1][j] * tt[cnt];
            ++cnt;
        }
    rr = std::max<int64_t>(rr, 0);
    int64_t v = rr >> (4 + coefLog2Denom);
    return uint16_t(std::min<int64_t>(v, 0xffff));
}

// 5.4.2: BL inverse mapping, returning v in the 16-bit domain.
inline uint16_t doviInverseMap(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                               int cmp, int64_t s0, int64_t s1, int64_t s2) {
    const auto& curve = mapping.curves[cmp];
    const int piece = doviPivotIndex(curve, s0);
    // Clip at the piece boundary (the spec does this before both mappings). A curve with
    // num_pivots 0 can only come from a failed RPU parse, so no clip is done to avoid reading pivots[-1].
    auto clampToCurve = [](int64_t v, const AVDOVIReshapingCurve& c) {
        if (c.num_pivots == 0) return v;
        return std::clamp<int64_t>(v, c.pivots[0], c.pivots[c.num_pivots - 1]);
    };
    const int64_t c0 = clampToCurve(s0, curve);
    const int64_t c1 = clampToCurve(s1, mapping.curves[1]);
    const int64_t c2 = clampToCurve(s2, mapping.curves[2]);
    if (curve.mapping_idc[piece] == AV_DOVI_MAPPING_POLYNOMIAL)
        return doviPolyMapping(curve, piece, c0, header.bl_bit_depth, header.coef_log2_denom);
    return doviMmrMapping(curve, piece, c0, c1, c2, header.bl_bit_depth, header.coef_log2_denom);
}

// 5.4.3.3: reconstruction. When BL is ST 2084 the whole 5.5 section is omitted (spec text), so the output is 12 bits directly.
inline uint16_t doviReconstruct(uint16_t v, int64_t residual, bool residualEnabled, int outBitDepth) {
    const int64_t maxOut = (int64_t(1) << outBitDepth) - 1;
    int64_t h = int64_t(v);
    if (residualEnabled) h += residual;
    h += int64_t(1) << (15 - outBitDepth);
    h >>= (16 - outBitDepth);
    return uint16_t(std::clamp<int64_t>(h, 0, maxOut));
}

// One sample at a time (luma). s0 = BL luma, el = EL luma.
inline uint16_t doviComposeLumaSample(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                                      int64_t blLuma, int64_t elLuma) {
    const uint16_t v = doviInverseMap(mapping, header, 0, blLuma, blLuma, blLuma);
    const int64_t r = doviNlq(mapping, 0, header.el_bit_depth, header.coef_log2_denom, elLuma);
    return doviReconstruct(v, r, header.disable_residual_flag == 0, header.vdr_bit_depth);
}

// One sample at a time (chroma). All three components are passed because MMR uses luma and the other component's chroma.
inline uint16_t doviComposeChromaSample(const AVDOVIDataMapping& mapping, const AVDOVIRpuDataHeader& header,
                                        int cmp, int64_t blLumaDown, int64_t blCb, int64_t blCr,
                                        int64_t elChroma) {
    const uint16_t v = doviInverseMap(mapping, header, cmp, blLumaDown, blCb, blCr);
    const int64_t r = doviNlq(mapping, cmp, header.el_bit_depth, header.coef_log2_denom, elChroma);
    return doviReconstruct(v, r, header.disable_residual_flag == 0, header.vdr_bit_depth);
}

} // namespace veyra::engine
