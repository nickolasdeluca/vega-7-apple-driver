#include <metal_stdlib>
using namespace metal;

// c = a + b, one element per thread.
kernel void vector_add(device const float *a [[buffer(0)]], device const float *b [[buffer(1)]],
                       device float *c [[buffer(2)]], uint id [[thread_position_in_grid]])
{
    c[id] = a[id] + b[id];
}
