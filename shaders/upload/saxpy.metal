#include <metal_stdlib>
using namespace metal;

// y = a * x + y, one element per thread; a from a constant buffer.
kernel void saxpy(constant float &a [[buffer(0)]], device const float *x [[buffer(1)]], device float *y [[buffer(2)]],
                  uint id [[thread_position_in_grid]])
{
    y[id] = a * x[id] + y[id];
}
