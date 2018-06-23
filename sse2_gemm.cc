#include "sse2_gemm.h"

#include "interleave.h"
#include "multiply.h"

#include <stdint.h>
#include <cassert>
#include <xmmintrin.h>
#include <emmintrin.h>

namespace intgemm {

#ifdef __SSE2__

namespace {
// Same implementation as AVX512, just width.  Grabs 4 32-bit values.
inline __m128i QuantizerGrab(const float *input, const __m128 quant_mult_reg) {
  return _mm_cvtps_epi32(_mm_mul_ps(*reinterpret_cast<const __m128*>(input), quant_mult_reg));
}

class QuantizeTile8 {
  public:
    typedef __m128i Integer;

    explicit QuantizeTile8(float mult) : mult_reg_(_mm_set1_ps(mult)) {}

    inline __m128i ForReshape(const float *input, int cols) {
      // Skip a row.
      return Tile(input, input + 2 * cols);
    }

    inline __m128i Consecutive(const float *input) {
      return Tile(input, input + 8);
    }

  private:
    // Quantize 16xfloat into 16xint8_t
    inline __m128i Tile(const float *input0, const float *input1) {
      const __m128i neg128 = _mm_set1_epi8(-128);
      __m128i g0 = QuantizerGrab(input0, mult_reg_);
      __m128i g1 = QuantizerGrab(input0 + 4, mult_reg_);
      __m128i g2 = QuantizerGrab(input1, mult_reg_);
      __m128i g3 = QuantizerGrab(input1 + 4, mult_reg_);
      __m128i packed0 = _mm_packs_epi32(g0, g1);
      __m128i packed1 = _mm_packs_epi32(g2, g3);
      __m128i packed = _mm_packs_epi16(packed0, packed1);
      /* Ban -128.
       * Don't use the SSE4.1 instruction _mm_max_epi8(packed, neg127).  Instead,
       * use SSE2 instructions _mm_cmpeq_epi8 and _mm_sub_epi8.
       * The first generates 0xff for fields -128.
       * The second subtracts 0xff from -128 which has the effect of converting
       * to -127.
       */
      // packed = _mm_max_epi8(packed, neg127);
      __m128i evils = _mm_cmpeq_epi8(packed, neg128);
      return _mm_sub_epi8(packed, evils);
      // No permute needed.  packs is in order for SSE.
    }

  private:
    const __m128 mult_reg_;
};

class QuantizeTile16 {
  public:
    typedef __m128i Integer;

    explicit QuantizeTile16(float mult) : mult_reg_(_mm_set1_ps(mult)) {}

		// Quantize 8xfloat into 8xint16_t
		inline __m128i Consecutive(const float *input) {
			__m128i g0 = QuantizerGrab(input, mult_reg_);
			__m128i g1 = QuantizerGrab(input + 4, mult_reg_);
			return _mm_packs_epi32(g0, g1);
		}

    inline __m128i ForReshape(const float *input, int) {
      return Consecutive(input);
    }

  private:
		const __m128 mult_reg_;
};

} // namespace


/* I also tried an implementation based on _mm_cvtps_pi16 but it was slower:
 * For size 1048576, run 10x in seconds on i7-6700:
 * This code: 0.00228409, 0.00204906
 * With _mm_cvtps_pi16 basis: 0.00391884, 0.00390869
 */
void SSE2_16bit::Quantize(const float *input, int16_t *output, float quant_mult, int size) {
  assert(size % 8 == 0);
  assert(reinterpret_cast<uintptr_t>(input) % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(output) % 16 == 0);
  QuantizeTile16 q(quant_mult);
  const float *end = input + size;
  for (; input != end; input += 8, output += 8) {
    *reinterpret_cast<__m128i*>(output) = q.Consecutive(input);
  }
}

void SSSE3_8bit::Quantize(const float *input, int8_t *output, float quant_mult, int size) {
  assert(size % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(input) % 16 == 0);
  assert(reinterpret_cast<uintptr_t>(output) % 16 == 0);
  QuantizeTile8 q(quant_mult);
  const float *end = input + size;
  for (; input != end; input += 16, output += 16) {
    *reinterpret_cast<__m128i*>(output) = q.Consecutive(input);
  }
}

void SSE2_16bit::PrepareB(const float *input, int16_t *output, float quant_mult, int rows, int cols) {
  PrepareBFor16(input, output, QuantizeTile16(quant_mult), rows, cols);
}

void SSSE3_8bit::PrepareB(const float *input, int8_t *output, float quant_mult, int rows, int cols) {
  PrepareBFor8(input, output, QuantizeTile8(quant_mult), rows, cols);
}

void SSE2_16bit::Multiply(const int16_t *A, const int16_t *B, float *C, float unquant_mult, int A_rows, int width, int B_cols) {
  Multiply16<__m128i, __m128>(A, B, C, unquant_mult, A_rows, width, B_cols);
}

void SSSE3_8bit::Multiply(const int8_t *A, const int8_t *B, float *C, float unquant_mult, int A_rows, int width, int B_cols) {
  Multiply8_SSE2OrAVX2<__m128i, __m128>(A, B, C, unquant_mult, A_rows, width, B_cols);
}

#endif // __SSE2__

} // namespace intgemm
