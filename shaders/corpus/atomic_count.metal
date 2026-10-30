#include <metal_stdlib>
using namespace metal;

// Every thread adds one to a single counter.
kernel void atomic_count(device atomic_uint *counter [[buffer(0)]], uint id [[thread_position_in_grid]])
{
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
}
