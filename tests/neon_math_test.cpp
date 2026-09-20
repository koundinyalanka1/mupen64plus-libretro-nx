/* Degenerate vector handling in the NEON geometry path.
 *
 * GLideN64/src/Neon/3DMathNeon.cpp replaces GLideN64/src/3DMath.cpp whenever
 * HAVE_NEON is set, and it normalizes with vrsqrte_f32 + Newton-Raphson
 * instead of sqrtf(). vrsqrte_f32(0.0f) is +Inf and the refinement turns that
 * into NaN, so a degenerate (zero-length) vector used to come back as NaN
 * where the scalar path returns it unchanged -- and those NaNs feed straight
 * into vertex lighting.
 *
 * This compiles the REAL NEON translation unit, not a copy of it, so the
 * check cannot drift away from the shipped code.
 */
#include <cmath>
#include <cfenv>
#include <cstdio>
#include <limits>

#if defined(__clang__) && defined(__aarch64__)
#pragma STDC FENV_ACCESS ON
#endif

#include "Neon/3DMathNeon.cpp"

static int g_failures = 0;

static void expectNoInvalid(const char* what)
{
	if (std::fetestexcept(FE_INVALID) != 0) {
		std::printf("FAIL %s: raised FE_INVALID\n", what);
		++g_failures;
	}
	std::feclearexcept(FE_ALL_EXCEPT);
}

static void expect(const char* what, const float got[3], const float want[3])
{
	for (int i = 0; i < 3; ++i) {
		const bool gotNaN = std::isnan(got[i]) != 0;
		if (gotNaN || std::fabs(got[i] - want[i]) > 1e-4f) {
			std::printf("FAIL %s: lane %d got %g, want %g\n",
				what, i, got[i], want[i]);
			++g_failures;
			return;
		}
	}
	std::printf("ok   %s\n", what);
}

int main(void)
{
	float identity[4][4] = {{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1}};
	const float zero[3] = {0.0f, 0.0f, 0.0f};
	std::feclearexcept(FE_ALL_EXCEPT);

	/* The case that regressed: a zero vector must survive as a zero vector. */
	float n0[3] = {0.0f, 0.0f, 0.0f};
	Normalize(n0);
	expectNoInvalid("Normalize zero");
	expect("Normalize(0,0,0) stays zero", n0, zero);

	float t0[3] = {0.0f, 0.0f, 0.0f};
	TransformVectorNormalize(t0, identity);
	expectNoInvalid("TransformVectorNormalize zero");
	expect("TransformVectorNormalize(0,0,0) stays zero", t0, zero);

	float i0src[3] = {0.0f, 0.0f, 0.0f};
	float i0dst[3] = {9.0f, 9.0f, 9.0f};
	InverseTransformVectorNormalize(i0src, i0dst, identity);
	expectNoInvalid("InverseTransformVectorNormalize zero");
	expect("InverseTransformVectorNormalize(0,0,0) stays zero", i0dst, zero);

	/* Non-degenerate vectors must still normalize correctly. */
	const float want345[3] = {0.6f, 0.8f, 0.0f};
	float n1[3] = {3.0f, 4.0f, 0.0f};
	Normalize(n1);
	expect("Normalize(3,4,0) is unit", n1, want345);

	const float wantThirds[3] = {-1.0f/3.0f, -2.0f/3.0f, 2.0f/3.0f};
	float n2[3] = {-1.0f, -2.0f, 2.0f};
	Normalize(n2);
	expect("Normalize(-1,-2,2) is unit", n2, wantThirds);

	const float wantY[3] = {0.0f, 1.0f, 0.0f};
	float t1[3] = {0.0f, 5.0f, 0.0f};
	TransformVectorNormalize(t1, identity);
	expect("TransformVectorNormalize(0,5,0) is unit", t1, wantY);

	/* Batched path: one degenerate member must not poison its neighbours,
	 * which is why the guard is per lane rather than an early return. */
	float src[4][3] = {{1,0,0},{0,0,0},{0,2,0},{0,0,-3}};
	float dst[4][3];
	InverseTransformVectorNormalizeN(src, dst, identity, 4);
	const float wantX[3]  = {1.0f, 0.0f,  0.0f};
	const float wantNZ[3] = {0.0f, 0.0f, -1.0f};
	expect("batch vector 0 is unit", dst[0], wantX);
	expect("batch degenerate member stays zero", dst[1], zero);
	expect("batch neighbour after degenerate is unit", dst[2], wantY);
	expect("batch last member is unit", dst[3], wantNZ);
	expectNoInvalid("four-vector batch");

	// A nonzero vector can also have a zero squared length after underflow.
	// Scalar Normalize leaves it unchanged, so a zero reciprocal scale loses
	// information even though it fixes the NaN output for an all-zero vector.
	const float tiny[3] = {1e-30f, -2e-30f, 3e-30f};
	for (unsigned operation = 0; operation < 3; ++operation) {
		float value[3] = {tiny[0], tiny[1], tiny[2]};
		if (operation == 0)
			Normalize(value);
		else if (operation == 1)
			TransformVectorNormalize(value, identity);
		else
			InverseTransformVectorNormalize(value, value, identity);
		for (unsigned c = 0; c < 3; ++c) {
			if (value[c] != tiny[c]) {
				std::printf("FAIL tiny vector: operation %u, lane %u got %g, want %g\n",
					operation, c, value[c], tiny[c]);
				++g_failures;
			}
		}
		expectNoInvalid("tiny vector");
	}

	// Exercise every batch/tail combination, including the unused eighth
	// reciprocal-sqrt lane in the seven-vector path and multiple seven batches.
	for (unsigned count = 0; count <= 17; ++count) {
		float input[18][3];
		float output[18][3];
		for (unsigned i = 0; i < 18; ++i) {
			for (unsigned c = 0; c < 3; ++c) {
				input[i][c] = i % 3 == 0 ? zero[c] : i % 3 == 1 ? tiny[c] : want345[c] * 5.0f;
				output[i][c] = 99.0f;
			}
		}
		InverseTransformVectorNormalizeN(input, output, identity, count);
		expectNoInvalid("mixed vector batch");
		for (unsigned i = 0; i < 18; ++i) {
			for (unsigned c = 0; c < 3; ++c) {
				const float expected = i >= count ? 99.0f : i % 3 == 0 ? zero[c] : i % 3 == 1 ? tiny[c] : want345[c];
				const bool valid = i < count && i % 3 == 2 ?
					std::isfinite(output[i][c]) && std::fabs(output[i][c] - expected) < 1e-4f :
					output[i][c] == expected;
				if (!valid) {
					std::printf("FAIL mixed batch: count %u, vector %u, lane %u got %g, want %g\n",
						count, i, c, output[i][c], expected);
					++g_failures;
				}
			}
		}
	}

	// Match scalar propagation when the length itself is NaN.
	float nanVector[3] = {std::numeric_limits<float>::quiet_NaN(), 1.0f, 2.0f};
	Normalize(nanVector);
	for (unsigned c = 0; c < 3; ++c) {
		if (!std::isnan(nanVector[c])) {
			std::printf("FAIL NaN propagation: lane %u got %g\n", c, nanVector[c]);
			++g_failures;
		}
	}

	if (g_failures != 0) {
		std::printf("%d failure(s)\n", g_failures);
		return 1;
	}
	std::printf("all NEON normalize checks passed\n");
	return 0;
}
