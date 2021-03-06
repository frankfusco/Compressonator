// This file is part of meshoptimizer library; see meshoptimizer.h for version/license details
#include "meshoptimizer.h"

#include <assert.h>
#include <float.h>
#include <string.h>
#include <vector>
using namespace std;
// This work is based on:
// Nicolas Capens. Advanced Rasterization. 2004
namespace meshopt
{

//const int kViewport = 256;
struct float2
{
    float fdata[2];
};

struct uint2
{
    unsigned int uidata[2];
};

struct OverdrawBuffer
{
	vector<vector<float2>> z;
	vector<vector<uint2>> overdraw;
};

template <typename T>
static T min(T a, T b)
{
	return a < b ? a : b;
}

template <typename T>
static T max(T a, T b)
{
	return a > b ? a : b;
}

static float det2x2(float a, float b, float c, float d)
{
	// (a b)
	// (c d)
	return a * d - b * c;
}

static float computeDepthGradients(float& dzdx, float& dzdy, float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3)
{
	// z2 = z1 + dzdx * (x2 - x1) + dzdy * (y2 - y1)
	// z3 = z1 + dzdx * (x3 - x1) + dzdy * (y3 - y1)
	// (x2-x1 y2-y1)(dzdx) = (z2-z1)
	// (x3-x1 y3-y1)(dzdy)   (z3-z1)
	// we'll solve it with Cramer's rule
	float det = det2x2(x2 - x1, y2 - y1, x3 - x1, y3 - y1);
	float invdet = (det == 0) ? 0 : 1 / det;

	dzdx = det2x2(z2 - z1, y2 - y1, z3 - z1, y3 - y1) * invdet;
	dzdy = det2x2(x2 - x1, z2 - z1, x3 - x1, z3 - z1) * invdet;

	return det;
}

// half-space fixed point triangle rasterizer
static void rasterize(OverdrawBuffer* buffer, float v1x, float v1y, float v1z, float v2x, float v2y, float v2z, float v3x, float v3y, float v3z, size_t kViewport)
{
	// compute depth gradients
	float DZx, DZy;
	float det = computeDepthGradients(DZx, DZy, v1x, v1y, v1z, v2x, v2y, v2z, v3x, v3y, v3z);
	int sign = det > 0;

	// flip backfacing triangles to simplify rasterization logic
	if (sign)
	{
		// flipping v2 & v3 preserves depth gradients since they're based on v1
		float t;
		t = v2x, v2x = v3x, v3x = t;
		t = v2y, v2y = v3y, v3y = t;
		t = v2z, v2z = v3z, v3z = t;

		// flip depth since we rasterize backfacing triangles to second buffer with reverse Z; only v1z is used below
		v1z = kViewport - v1z;
		DZx = -DZx;
		DZy = -DZy;
	}

	// coordinates, 28.4 fixed point
	int X1 = int(16.0f * v1x + 0.5f);
	int X2 = int(16.0f * v2x + 0.5f);
	int X3 = int(16.0f * v3x + 0.5f);

	int Y1 = int(16.0f * v1y + 0.5f);
	int Y2 = int(16.0f * v2y + 0.5f);
	int Y3 = int(16.0f * v3y + 0.5f);

	// bounding rectangle, clipped against viewport
	// since we rasterize pixels with covered centers, min >0.5 should round up
	// as for max, due to top-left filling convention we will never rasterize right/bottom edges
	// so max >= 0.5 should round down
	int minx = max((min(X1, min(X2, X3)) + 7) >> 4, 0);
	int maxx = min((max(X1, max(X2, X3)) + 7) >> 4, (int)kViewport);
	int miny = max((min(Y1, min(Y2, Y3)) + 7) >> 4, 0);
	int maxy = min((max(Y1, max(Y2, Y3)) + 7) >> 4, (int)kViewport);

	// deltas, 28.4 fixed point
	int DX12 = X1 - X2;
	int DX23 = X2 - X3;
	int DX31 = X3 - X1;

	int DY12 = Y1 - Y2;
	int DY23 = Y2 - Y3;
	int DY31 = Y3 - Y1;

	// fill convention correction
	int TL1 = DY12 < 0 || (DY12 == 0 && DX12 > 0);
	int TL2 = DY23 < 0 || (DY23 == 0 && DX23 > 0);
	int TL3 = DY31 < 0 || (DY31 == 0 && DX31 > 0);

	// half edge equations, 24.8 fixed point
	// note that we offset minx/miny by half pixel since we want to rasterize pixels with covered centers
	int FX = (minx << 4) + 8;
	int FY = (miny << 4) + 8;
	int CY1 = DX12 * (FY - Y1) - DY12 * (FX - X1) + TL1 - 1;
	int CY2 = DX23 * (FY - Y2) - DY23 * (FX - X2) + TL2 - 1;
	int CY3 = DX31 * (FY - Y3) - DY31 * (FX - X3) + TL3 - 1;
	float ZY = v1z + (DZx * float(FX - X1) + DZy * float(FY - Y1)) * (1 / 16.f);

	for (int y = miny; y < maxy; y++)
	{
		int CX1 = CY1;
		int CX2 = CY2;
		int CX3 = CY3;
		float ZX = ZY;

		for (int x = minx; x < maxx; x++)
		{
			// check if all CXn are non-negative
			if ((CX1 | CX2 | CX3) >= 0)
			{
				if (ZX >= buffer->z[y][x].fdata[sign])
				{
					buffer->z[y][x].fdata[sign] = ZX;
					buffer->overdraw[y][x].uidata[sign]++;
				}
			}

			CX1 -= DY12 << 4;
			CX2 -= DY23 << 4;
			CX3 -= DY31 << 4;
			ZX += DZx;
		}

		CY1 += DX12 << 4;
		CY2 += DX23 << 4;
		CY3 += DX31 << 4;
		ZY += DZy;
	}
}

} // namespace meshopt

meshopt_OverdrawStatistics meshopt_analyzeOverdraw(const unsigned int* indices, size_t index_count, const float* vertex_positions, size_t vertex_count, size_t vertex_positions_stride, size_t max_xyview)
{
	using namespace meshopt;

	assert(index_count % 3 == 0);
	assert(vertex_positions_stride > 0 && vertex_positions_stride <= 256);
	assert(vertex_positions_stride % sizeof(float) == 0);

	size_t vertex_stride_float = vertex_positions_stride / sizeof(float);

	meshopt_OverdrawStatistics result = {};

	float minv[3] = {FLT_MAX, FLT_MAX, FLT_MAX};
	float maxv[3] = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

	for (size_t i = 0; i < vertex_count; ++i)
	{
		const float* v = vertex_positions + i * vertex_stride_float;

		for (int j = 0; j < 3; ++j)
		{
			minv[j] = min(minv[j], v[j]);
			maxv[j] = max(maxv[j], v[j]);
		}
	}


	float extent = max(maxv[0] - minv[0], max(maxv[1] - minv[1], maxv[2] - minv[2]));
	float scale = max_xyview / extent;  //max_xyview = orthographic viewport max range

	meshopt_Buffer<float> triangles(index_count * 3);

	for (size_t i = 0; i < index_count; ++i)
	{
		unsigned int index = indices[i];
		assert(index < vertex_count);

		const float* v = vertex_positions + index * vertex_stride_float;

		triangles[i * 3 + 0] = (v[0] - minv[0]) * scale;
		triangles[i * 3 + 1] = (v[1] - minv[1]) * scale;
		triangles[i * 3 + 2] = (v[2] - minv[2]) * scale;
	}

	//meshopt_Buffer<OverdrawBuffer> buffer_storage(1);
    OverdrawBuffer buffer;
    buffer.z.resize(max_xyview);
    buffer.overdraw.resize(max_xyview);
    for (size_t y = 0; y < max_xyview; y++)
    {
        buffer.z[y].resize(max_xyview);
        buffer.overdraw[y].resize(max_xyview);
    }

	for (int axis = 0; axis < 3; ++axis)
	{
        //reset for each axis
        for (size_t i = 0; i < buffer.z.size(); i++) {
            for (size_t j = 0; j < buffer.z[i].size(); j++) {
                buffer.z[i][j].fdata[0] = 0.0;
                buffer.z[i][j].fdata[1] = 0.0;
                buffer.overdraw[i][j].uidata[0] = 0;
                buffer.overdraw[i][j].uidata[1] = 0;
            }
        }

		for (size_t i = 0; i < index_count; i += 3)
		{
			const float* vn0 = &triangles[3 * (i + 0)];
			const float* vn1 = &triangles[3 * (i + 1)];
			const float* vn2 = &triangles[3 * (i + 2)];

			switch (axis)
			{
			case 0:
				rasterize(&buffer, vn0[2], vn0[1], vn0[0], vn1[2], vn1[1], vn1[0], vn2[2], vn2[1], vn2[0], max_xyview);
				break;
			case 1:
				rasterize(&buffer, vn0[0], vn0[2], vn0[1], vn1[0], vn1[2], vn1[1], vn2[0], vn2[2], vn2[1], max_xyview);
				break;
			case 2:
				rasterize(&buffer, vn0[1], vn0[0], vn0[2], vn1[1], vn1[0], vn1[2], vn2[1], vn2[0], vn2[2], max_xyview);
				break;
			}
		}

		for (size_t y = 0; y < max_xyview; ++y)
			for (size_t x = 0; x < max_xyview; ++x)
				for (int s = 0; s < 2; ++s)
				{
					unsigned int overdraw = buffer.overdraw[y][x].uidata[s];
					result.pixels_covered += overdraw > 0;
					result.pixels_shaded += overdraw;
				}
	}

	result.overdraw = result.pixels_covered ? float(result.pixels_shaded) / float(result.pixels_covered) : 0.f;

	return result;
}
