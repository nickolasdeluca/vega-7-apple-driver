#include <metal_stdlib>
using namespace metal;

// Each threadgroup (a power of two) sums its inputs in threadgroup memory.
kernel void group_sum(device const uint *in [[buffer(0)]], device uint *out [[buffer(1)]],
                      threadgroup uint *partial [[threadgroup(0)]], uint id [[thread_position_in_grid]],
                      uint tid [[thread_position_in_threadgroup]], uint group [[threadgroup_position_in_grid]],
                      uint size [[threads_per_threadgroup]])
{
    partial[tid] = in[id];
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint s = size / 2; s > 0; s >>= 1) {
        if (tid < s) partial[tid] += partial[tid + s];
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0) out[group] = partial[0];
}
