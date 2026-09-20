// Exercise the actual NEON lighting code against the scalar lighting contract.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "Neon/3DMathNeon.cpp"
#include "Neon/gSPNeon.cpp"

gSPInfo gSP{};
static bool hardwareLighting;
bool isHWLightingAllowed() { return hardwareLighting; }

// Lighting tests do not need a renderer; catch accidental renderer access.
DisplayWindow& DisplayWindow::get() { std::abort(); }

static void check(bool condition, const char* message)
{
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s (numLights=%u)\n", message, gSP.numLights);
		std::exit(1);
	}
}

int main()
{
	// Include zero lights, the 4/7 light SIMD batches, and their scalar tails.
	for (gSP.numLights = 0; gSP.numLights < 12; ++gSP.numLights) {
		for (unsigned i = 0; i < 12; ++i) {
			for (unsigned c = 0; c < 3; ++c) {
				gSP.lights.rgb[i][c] = 0.005f * (i + 1) * (c + 1);
				gSP.lights.rgb2[i][c] = 0.009f * (i + 1) * (3 - c);
			}
			gSP.lights.i_xyz[i][0] = (i % 3 == 0) ? -1.0f : 0.6f;
			gSP.lights.i_xyz[i][1] = 0.0f;
			gSP.lights.i_xyz[i][2] = 0.8f;
		}
		for (unsigned start = 0; start < 2; ++start) {
			SPVertex vertices[6]{};
			for (auto& vertex : vertices) {
				vertex.nx = 0.8f;
				vertex.nz = 0.6f;
				vertex.a = 0.75f;
				vertex.r = vertex.g = vertex.b = -9.0f;
				vertex.HWLight = 99;
			}
			gSPLightVertex_NEON(4, start, vertices);
			for (unsigned v = start; v < start + 4; ++v) {
				const float (*colors)[3] = (v & 1) == 0 ? gSP.lights.rgb : gSP.lights.rgb2;
				for (unsigned c = 0; c < 3; ++c) {
					float expected = colors[gSP.numLights][c];
					for (unsigned i = 0; i < gSP.numLights; ++i) {
						const float intensity = 0.8f * gSP.lights.i_xyz[i][0] +
							0.6f * gSP.lights.i_xyz[i][2];
						if (intensity > 0.0f)
							expected += colors[i][c] * intensity;
					}
					check(std::fabs((&vertices[v].r)[c] - std::min(1.0f, expected)) < 1e-5f,
						"ambient/directional color matches scalar lighting");
				}
				check(vertices[v].a == 0.75f && vertices[v].HWLight == 0,
					"software lighting preserves alpha and clears HWLight");
			}
			check(vertices[start + 4].r == -9.0f, "batch stays within vertex bounds");
			if (start != 0)
				check(vertices[0].r == -9.0f, "batch preserves vertices before its start");
		}
	}
	std::puts("all NEON lighting checks passed");
}
