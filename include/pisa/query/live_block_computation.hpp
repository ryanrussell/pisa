#pragma once

#include "bit_vector.hpp"
#include <immintrin.h>
namespace pisa {

bit_vector compute_live_quant16(std::vector<std::vector<uint8_t>> const& scores, uint16_t threshold)
{
    bit_vector_builder bv;
    bv.reserve(scores[0].size());
    for (size_t i = 0; i < scores[0].size(); i += 1) {
        uint16_t sum = scores[0][i];
        for (size_t term = 1; term < scores.size(); ++term) {
            sum += scores[term][i];
        }
        bv.append_bits(static_cast<uint64_t>(sum >= threshold), 1);
    }
    return bit_vector(&bv);
}

#ifdef __AVX__
__m128i load64(__m128i* data)
{
    return _mm_unpacklo_epi8(_mm_loadu_si128(data), _mm_setzero_si128());
}

bit_vector avx_compute_live_quant16(std::vector<std::vector<uint8_t>> const& scores, uint16_t threshold)
{
    bit_vector_builder bv;
    bv.reserve(scores[0].size());
    __m128i thresholds = _mm_set1_epi16(threshold);
    size_t i = 0;
    for (; i < scores[0].size() and scores[0].size() - i >= 8; i += 8) {
        __m128i sum = load64((__m128i*)(scores[0].data() + i));
        for (size_t term = 1; term < scores.size(); ++term) {
            sum = _mm_adds_epu16(sum, load64((__m128i*)(scores[term].data() + i)));
        }

        __m128i masksL1 = _mm_cmpeq_epi16(_mm_max_epu16(sum, thresholds), sum);
        __m128i lensAll = _mm_shuffle_epi8(
            masksL1, _mm_setr_epi8(0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1));
        uint32_t maskBitsL1 = _mm_movemask_epi8(lensAll);
        bv.append_bits(maskBitsL1, 8);
    }

    for (; i < scores[0].size(); i += 1) {
        uint16_t sum = scores[0][i];
        for (size_t term = 1; term < scores.size(); ++term) {
            sum += scores[term][i];
        }
        bv.append_bits(static_cast<uint64_t>(sum >= threshold), 1);
    }
    return bit_vector(&bv);
}

#endif

#ifdef __AVX2__

__m256i load(__m128i* data)
{
    __m256i _a = _mm256_set_m128i(_mm_loadu_si128(data), _mm_loadu_si128(data));
    __m256i _b;
    _b = _mm256_unpacklo_epi8(_a, _mm256_setzero_si256());
    _a = _mm256_unpackhi_epi8(_a, _mm256_setzero_si256());
    return _mm256_set_m128i(_mm256_extractf128_si256(_a, 0), _mm256_extractf128_si256(_b, 0));
}

bit_vector avx2_compute_live_quant16(std::vector<std::vector<uint8_t>> const& scores, uint16_t threshold)
{
    bit_vector_builder bv;
    bv.reserve(scores[0].size());
    __m256i thresholds = _mm256_set1_epi16(threshold);
    size_t i = 0;
    for (; i < scores[0].size() and scores[0].size() - i >= 16; i += 16) {
        __m256i sum = load((__m128i*)(scores[0].data() + i));
        for (size_t term = 1; term < scores.size(); ++term) {
            sum = _mm256_adds_epu16(sum, load((__m128i*)(scores[term].data() + i)));
        }
        __m256i masksL1 = _mm256_cmpeq_epi16(_mm256_max_epu16(sum, thresholds), sum);
        uint32_t maskBitsL1 = _mm_movemask_epi8(_mm_packs_epi16(
            _mm256_extractf128_si256(masksL1, 0), _mm256_extractf128_si256(masksL1, 1)));
        bv.append_bits(maskBitsL1, 16);
    }

    for (; i < scores[0].size(); i += 1) {
        uint16_t sum = scores[0][i];
        for (size_t term = 1; term < scores.size(); ++term) {
            sum += scores[term][i];
        }
        bv.append_bits(static_cast<uint64_t>(sum >= threshold), 1);
    }

    return bit_vector(&bv);
}

#endif

}  // namespace pisa
