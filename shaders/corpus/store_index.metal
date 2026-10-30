#include <metal_stdlib>
using namespace metal;

// Each thread stores its index (stage 22's kernel).
kernel void store_index(device uint *out [[buffer(0)]], uint id [[thread_position_in_grid]])
{
    out[id] = id;
}
