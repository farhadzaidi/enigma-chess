#include <algorithm>
#include <array>
#include <cstring>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

#include "nnue.hpp"
#include "square.hpp"
#include "bitboard.hpp"

extern "C" const char _binary_nnue_bin_start[];

namespace {

// --- Weights  ---

alignas(64) int16_t l1_weight[FEATURE_SIZE][L1_SIZE];
alignas(64) int16_t l1_bias[L1_SIZE];
alignas(64) int8_t l2_weight[L2_SIZE][L1_SIZE * 2];
alignas(64) int32_t l2_bias[L2_SIZE];
alignas(64) int8_t l3_weight[L3_SIZE][L2_SIZE];
alignas(64) int32_t l3_bias[L3_SIZE];
alignas(64) int16_t output_weight[L3_SIZE];
int32_t output_bias;

bool weights_loaded = false;

void load_weights() {
    const char* data = _binary_nnue_bin_start;

    std::memcpy(l1_weight, data, sizeof(l1_weight));
    data += sizeof(l1_weight);
    std::memcpy(l1_bias, data, sizeof(l1_bias));
    data += sizeof(l1_bias);
    std::memcpy(l2_weight, data, sizeof(l2_weight));
    data += sizeof(l2_weight);
    std::memcpy(l2_bias, data, sizeof(l2_bias));
    data += sizeof(l2_bias);
    std::memcpy(l3_weight, data, sizeof(l3_weight));
    data += sizeof(l3_weight);
    std::memcpy(l3_bias, data, sizeof(l3_bias));
    data += sizeof(l3_bias);
    std::memcpy(output_weight, data, sizeof(output_weight));
    data += sizeof(output_weight);
    std::memcpy(&output_bias, data, sizeof(output_bias));

    weights_loaded = true;
}

/** Compute the HalfKP feature index for a piece from a given perspective.
 *  Black's perspective mirrors squares vertically and flips the piece's side. */
size_t get_feature_index(Side perspective, Square king_square, Side side, Piece piece, Square square) {
    if (perspective == BLACK) {
        king_square = flip_square(king_square);
        side = opposite_side(side);
        square = flip_square(square);
    }
    return king_square * 640 + side * 320 + piece * 64 + square;
}

#if defined(__AVX2__)

// Batched horizontal sum of 4 __m256i into 4 int32_t results.
// Uses hadd to interleave reductions, avoiding 4 separate extract+hadd+hadd chains.
void horizontal_sum_x4(
    __m256i a, __m256i b, __m256i c, __m256i d,
    int32_t& out_a, int32_t& out_b, int32_t& out_c, int32_t& out_d
) {
    // hadd pairs: a+b and c+d
    __m256i ab = _mm256_hadd_epi32(a, b);  // [a0+a1, a2+a3, b0+b1, b2+b3, a4+a5, a6+a7, b4+b5, b6+b7]
    __m256i cd = _mm256_hadd_epi32(c, d);
    __m256i abcd = _mm256_hadd_epi32(ab, cd); // [a_lo, b_lo, c_lo, d_lo, a_hi, b_hi, c_hi, d_hi]

    // Sum the two 128-bit lanes
    __m128i lo = _mm256_castsi256_si128(abcd);
    __m128i hi = _mm256_extracti128_si256(abcd, 1);
    __m128i sum = _mm_add_epi32(lo, hi); // [a_total, b_total, c_total, d_total]

    out_a += _mm_extract_epi32(sum, 0);
    out_b += _mm_extract_epi32(sum, 1);
    out_c += _mm_extract_epi32(sum, 2);
    out_d += _mm_extract_epi32(sum, 3);
}

int32_t horizontal_sum_epi32(__m256i value) {
    __m128i low = _mm256_castsi256_si128(value);
    __m128i high = _mm256_extracti128_si256(value, 1);
    __m128i sum = _mm_add_epi32(low, high);
    sum = _mm_hadd_epi32(sum, sum);
    sum = _mm_hadd_epi32(sum, sum);
    return _mm_cvtsi128_si32(sum);
}

void dot_product_u8s8_x4(
    const uint8_t* input,
    const int8_t* weights_0,
    const int8_t* weights_1,
    const int8_t* weights_2,
    const int8_t* weights_3,
    size_t size,
    int32_t& sum_0,
    int32_t& sum_1,
    int32_t& sum_2,
    int32_t& sum_3
) {
    const __m256i ones = _mm256_set1_epi16(1);
    __m256i acc_0 = _mm256_setzero_si256();
    __m256i acc_1 = _mm256_setzero_si256();
    __m256i acc_2 = _mm256_setzero_si256();
    __m256i acc_3 = _mm256_setzero_si256();

    for (size_t i = 0; i < size; i += 32) {
        __m256i input_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(input + i));

        __m256i weights_vec_0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights_0 + i));
        __m256i weights_vec_1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights_1 + i));
        __m256i weights_vec_2 = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights_2 + i));
        __m256i weights_vec_3 = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights_3 + i));

        acc_0 = _mm256_add_epi32(acc_0, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, weights_vec_0), ones));
        acc_1 = _mm256_add_epi32(acc_1, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, weights_vec_1), ones));
        acc_2 = _mm256_add_epi32(acc_2, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, weights_vec_2), ones));
        acc_3 = _mm256_add_epi32(acc_3, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, weights_vec_3), ones));
    }

    horizontal_sum_x4(acc_0, acc_1, acc_2, acc_3, sum_0, sum_1, sum_2, sum_3);
}

int32_t dot_product_s16s16_32(const int16_t* input, const int16_t* weights) {
    __m256i sum = _mm256_setzero_si256();

    for (size_t i = 0; i < L3_SIZE; i += 16) {
        __m256i input_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(input + i));
        __m256i weight_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights + i));
        __m256i pairwise_sums = _mm256_madd_epi16(input_vec, weight_vec);
        sum = _mm256_add_epi32(sum, pairwise_sums);
    }

    return horizontal_sum_epi32(sum);
}

void add_accumulator_row(int16_t* accumulator, const int16_t* weights) {
    for (size_t i = 0; i < L1_SIZE; i += 16) {
        __m256i acc = _mm256_load_si256(reinterpret_cast<const __m256i*>(accumulator + i));
        __m256i weight = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights + i));
        __m256i updated = _mm256_add_epi16(acc, weight);
        _mm256_store_si256(reinterpret_cast<__m256i*>(accumulator + i), updated);
    }
}

void sub_accumulator_row(int16_t* accumulator, const int16_t* weights) {
    for (size_t i = 0; i < L1_SIZE; i += 16) {
        __m256i acc = _mm256_load_si256(reinterpret_cast<const __m256i*>(accumulator + i));
        __m256i weight = _mm256_load_si256(reinterpret_cast<const __m256i*>(weights + i));
        __m256i updated = _mm256_sub_epi16(acc, weight);
        _mm256_store_si256(reinterpret_cast<__m256i*>(accumulator + i), updated);
    }
}

void clipped_relu_to_u8(const int16_t* input, uint8_t* output) {
    const __m256i zero = _mm256_setzero_si256();
    const __m256i q1 = _mm256_set1_epi16(static_cast<int16_t>(Q1));

    for (size_t i = 0; i < L1_SIZE; i += 32) {
        __m256i lo = _mm256_load_si256(reinterpret_cast<const __m256i*>(input + i));
        __m256i hi = _mm256_load_si256(reinterpret_cast<const __m256i*>(input + i + 16));

        lo = _mm256_min_epi16(_mm256_max_epi16(lo, zero), q1);
        hi = _mm256_min_epi16(_mm256_max_epi16(hi, zero), q1);

        // packus interleaves across lanes: we need permute to fix ordering
        __m256i packed = _mm256_packus_epi16(lo, hi);
        packed = _mm256_permute4x64_epi64(packed, 0xD8); // fix lane crossing: 0,2,1,3
        _mm256_store_si256(reinterpret_cast<__m256i*>(output + i), packed);
    }
}

// Vectorized clamp+scale+store for 4 int32 sums into uint8 output
void clamp_scale_store_u8(int32_t s0, int32_t s1, int32_t s2, int32_t s3, uint8_t* out) {
    __m128i v = _mm_set_epi32(s3, s2, s1, s0);
    // Arithmetic right-shift by 6 = divide by Q2 (64), truncating toward negative infinity.
    // For the non-negative values that survive the subsequent clamp this is equivalent to
    // the original `/ Q2` (which truncates toward zero) because negative values are clamped to 0.
    v = _mm_srai_epi32(v, 6);
    v = _mm_max_epi32(v, _mm_setzero_si128());
    v = _mm_min_epi32(v, _mm_set1_epi32(Q1));
    // Pack 4x int32 -> 8x int16 -> 16x uint8 (only low 4 bytes valid)
    v = _mm_packus_epi32(v, v);
    v = _mm_packus_epi16(v, v);
    // Store low 4 bytes
    *reinterpret_cast<uint32_t*>(out) = static_cast<uint32_t>(_mm_cvtsi128_si32(v));
}

// Vectorized clamp+scale+store for 4 int32 sums into int16 output
void clamp_scale_store_s16(int32_t s0, int32_t s1, int32_t s2, int32_t s3, int16_t* out) {
    __m128i v = _mm_set_epi32(s3, s2, s1, s0);
    v = _mm_srai_epi32(v, 6);
    v = _mm_max_epi32(v, _mm_setzero_si128());
    v = _mm_min_epi32(v, _mm_set1_epi32(Q1));
    v = _mm_packs_epi32(v, v);
    // Store low 8 bytes (4x int16)
    _mm_storel_epi64(reinterpret_cast<__m128i*>(out), v);
}

#endif

}  // namespace

// --- Initialization ---

NNUE::NNUE() {
    if (!weights_loaded) load_weights();
    accumulator_stack.reserve(MAX_GAME_PLY);
    reset_accumulators();
}

void NNUE::reset_accumulators() {
    for (size_t i = 0; i < L1_SIZE; i++) {
        accumulators[WHITE][i] = static_cast<int16_t>(l1_bias[i]);
        accumulators[BLACK][i] = static_cast<int16_t>(l1_bias[i]);
    }
}

// --- Feature Updates ---

void NNUE::add_feature(const std::array<Square, NUM_SIDES>& king_squares, Side side, Piece piece, Square square) {
    for (Side s = WHITE; s < NUM_SIDES; s++) {
        size_t feature_index = get_feature_index(s, king_squares[s], side, piece, square);
#if defined(__AVX2__)
        add_accumulator_row(accumulators[s].data(), l1_weight[feature_index]);
#else
        for (size_t i = 0; i < L1_SIZE; i++) {
            accumulators[s][i] = static_cast<int16_t>(accumulators[s][i] + l1_weight[feature_index][i]);
        }
#endif
    }
}

void NNUE::remove_feature(const std::array<Square, NUM_SIDES>& king_squares, Side side, Piece piece, Square square) {
    for (Side s = WHITE; s < NUM_SIDES; s++) {
        size_t feature_index = get_feature_index(s, king_squares[s], side, piece, square);
#if defined(__AVX2__)
        sub_accumulator_row(accumulators[s].data(), l1_weight[feature_index]);
#else
        for (size_t i = 0; i < L1_SIZE; i++) {
            accumulators[s][i] = static_cast<int16_t>(accumulators[s][i] - l1_weight[feature_index][i]);
        }
#endif
    }
}

void NNUE::refresh_features(
    const std::array<Square, NUM_SIDES>& king_squares,
    const std::array<std::array<Bitboard, NUM_PIECES>, NUM_SIDES>& pieces
) {
    reset_accumulators();
    for (Side side = 0; side < NUM_SIDES; side++) {
        for (Piece piece = PAWN; piece < KING; piece++) {
            Bitboard bb = pieces[side][piece];
            while (bb) {
                Square square = pop_lsb(bb);
                add_feature(king_squares, side, piece, square);
            }
        }
    }
}

// --- Stack Operations ---

void NNUE::push() {
    accumulator_stack.push_back(accumulators);
}

void NNUE::pop() {
    accumulators = accumulator_stack.back();
    accumulator_stack.pop_back();
}

void NNUE::clear_history() {
    accumulator_stack.clear();
}

// --- Evaluation ---

/** Forward pass through the NNUE network using quantized integer arithmetic.
 *  The L1 output (accumulators) is already computed incrementally via add/remove_feature.
 *  This function runs the remaining layers: L2 (512->32), L3 (32->32), output (32->1).
 *  All intermediate values are in Q1*Q2 scale after matmul, divided by Q2 to return to Q1 scale.
 *  The output layer has the centipawn conversion baked into its weights via quantization. */
PositionScore NNUE::evaluate(Side us) {
    auto& ours = accumulators[us];
    auto& theirs = accumulators[us ^ 1];

    alignas(32) std::array<uint8_t, L1_SIZE * 2> layer1_input;
#if defined(__AVX2__)
    clipped_relu_to_u8(ours.data(), layer1_input.data());
    clipped_relu_to_u8(theirs.data(), layer1_input.data() + L1_SIZE);
#else
    for (size_t i = 0; i < L1_SIZE; i++) {
        layer1_input[i] = static_cast<uint8_t>(std::clamp(ours[i], static_cast<int16_t>(0), static_cast<int16_t>(Q1)));
        layer1_input[L1_SIZE + i] = static_cast<uint8_t>(std::clamp(theirs[i], static_cast<int16_t>(0), static_cast<int16_t>(Q1)));
    }
#endif

    // L2: concatenated L1 output [ours, theirs] (512) -> L2_SIZE
    alignas(32) std::array<uint8_t, L2_SIZE> layer2_output;
#if defined(__AVX2__)
    for (size_t i = 0; i < L2_SIZE; i += 8) {
        const __m256i ones = _mm256_set1_epi16(1);
        __m256i acc_0 = _mm256_setzero_si256();
        __m256i acc_1 = _mm256_setzero_si256();
        __m256i acc_2 = _mm256_setzero_si256();
        __m256i acc_3 = _mm256_setzero_si256();
        __m256i acc_4 = _mm256_setzero_si256();
        __m256i acc_5 = _mm256_setzero_si256();
        __m256i acc_6 = _mm256_setzero_si256();
        __m256i acc_7 = _mm256_setzero_si256();

        for (size_t j = 0; j < L1_SIZE * 2; j += 32) {
            __m256i input_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(layer1_input.data() + j));

            acc_0 = _mm256_add_epi32(acc_0, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i]     + j))), ones));
            acc_1 = _mm256_add_epi32(acc_1, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 1] + j))), ones));
            acc_2 = _mm256_add_epi32(acc_2, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 2] + j))), ones));
            acc_3 = _mm256_add_epi32(acc_3, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 3] + j))), ones));
            acc_4 = _mm256_add_epi32(acc_4, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 4] + j))), ones));
            acc_5 = _mm256_add_epi32(acc_5, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 5] + j))), ones));
            acc_6 = _mm256_add_epi32(acc_6, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 6] + j))), ones));
            acc_7 = _mm256_add_epi32(acc_7, _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l2_weight[i + 7] + j))), ones));
        }

        int32_t sum_0 = l2_bias[i],     sum_1 = l2_bias[i + 1], sum_2 = l2_bias[i + 2], sum_3 = l2_bias[i + 3];
        int32_t sum_4 = l2_bias[i + 4], sum_5 = l2_bias[i + 5], sum_6 = l2_bias[i + 6], sum_7 = l2_bias[i + 7];
        horizontal_sum_x4(acc_0, acc_1, acc_2, acc_3, sum_0, sum_1, sum_2, sum_3);
        horizontal_sum_x4(acc_4, acc_5, acc_6, acc_7, sum_4, sum_5, sum_6, sum_7);
        clamp_scale_store_u8(sum_0, sum_1, sum_2, sum_3, &layer2_output[i]);
        clamp_scale_store_u8(sum_4, sum_5, sum_6, sum_7, &layer2_output[i + 4]);
    }
#else
    for (size_t i = 0; i < L2_SIZE; i++) {
        int32_t sum = l2_bias[i];
        const int8_t* weights = l2_weight[i];
        for (size_t j = 0; j < L1_SIZE * 2; j++) {
            sum += static_cast<int32_t>(weights[j]) * layer1_input[j];
        }

        // Rescale from Q1*Q2 back to Q1 and apply ClippedReLU
        layer2_output[i] = static_cast<uint8_t>(std::clamp(sum / Q2, static_cast<int32_t>(0), Q1));
    }
#endif

    // L3: L2_SIZE -> L3_SIZE
    alignas(32) std::array<int16_t, L3_SIZE> layer3_output;
#if defined(__AVX2__)
    {
        const __m256i ones = _mm256_set1_epi16(1);
        const __m256i input_vec = _mm256_load_si256(reinterpret_cast<const __m256i*>(layer2_output.data()));

        for (size_t i = 0; i < L3_SIZE; i += 4) {
            __m256i a0 = _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l3_weight[i]))), ones);
            __m256i a1 = _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l3_weight[i + 1]))), ones);
            __m256i a2 = _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l3_weight[i + 2]))), ones);
            __m256i a3 = _mm256_madd_epi16(_mm256_maddubs_epi16(input_vec, _mm256_load_si256(reinterpret_cast<const __m256i*>(l3_weight[i + 3]))), ones);

            int32_t sum_0 = l3_bias[i], sum_1 = l3_bias[i + 1], sum_2 = l3_bias[i + 2], sum_3 = l3_bias[i + 3];
            horizontal_sum_x4(a0, a1, a2, a3, sum_0, sum_1, sum_2, sum_3);
            clamp_scale_store_s16(sum_0, sum_1, sum_2, sum_3, &layer3_output[i]);
        }
    }
#else
    for (size_t i = 0; i < L3_SIZE; i++) {
        int32_t sum = l3_bias[i];
        const int8_t* weights = l3_weight[i];
        for (size_t j = 0; j < L2_SIZE; j++) {
            sum += static_cast<int32_t>(weights[j]) * layer2_output[j];
        }

        layer3_output[i] = static_cast<int16_t>(std::clamp(sum / Q2, static_cast<int32_t>(0), Q1));
    }
#endif

    // Output: L3_SIZE -> 1 (centipawn conversion baked into weight quantization)
    int32_t output = output_bias;
#if defined(__AVX2__)
    output += dot_product_s16s16_32(layer3_output.data(), output_weight);
#else
    for (size_t i = 0; i < L3_SIZE; i++) {
        output += output_weight[i] * layer3_output[i];
    }
#endif

    return output / Q2;
}
