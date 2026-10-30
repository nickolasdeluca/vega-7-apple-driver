#include <metal_stdlib>
using namespace metal;

// A 2-D grid: each thread stores its coordinates, row-major.
kernel void grid_2d(device uint *out [[buffer(0)]], uint2 pos [[thread_position_in_grid]],
                    uint2 size [[threads_per_grid]])
{
    out[pos.y * size.x + pos.x] = (pos.y << 16) | pos.x;
}
