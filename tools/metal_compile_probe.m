// Observe Metal's runtime shader compilation path from our own process.
//
// Compiles one fixed MSL compute kernel on the default device, creates its
// compute pipeline state, and optionally serializes a binary archive holding
// it. It creates no command queue or command buffer and dispatches nothing.
// Output is JSON: the device, timings, the pipeline's reported properties, and
// the images loaded into this process before and after each step (public dyld
// API on our own process only). With --pause N it waits N seconds after
// compiling so other read-only tools can look at the system meanwhile. With
// --big N the kernel instead runs N dependent math steps, so that the native
// compile takes long enough to be observed from outside.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>
#include <time.h>
#include <unistd.h>

static NSArray<NSString *> *loadedImages(void) {
    NSMutableArray *images = [NSMutableArray array];
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (name) [images addObject:@(name)];
    }
    return images;
}

static NSArray<NSString *> *added(NSArray<NSString *> *before, NSArray<NSString *> *after) {
    NSMutableOrderedSet *set = [NSMutableOrderedSet orderedSetWithArray:after];
    [set minusSet:[NSSet setWithArray:before]];
    return set.array;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        unsigned pause = 0, big = 0;
        NSString *archivePath = nil;
        for (int i = 1; i + 1 < argc; i += 2) {
            if (!strcmp(argv[i], "--pause")) pause = (unsigned)atoi(argv[i + 1]);
            else if (!strcmp(argv[i], "--archive")) archivePath = @(argv[i + 1]);
            else if (!strcmp(argv[i], "--big")) big = (unsigned)atoi(argv[i + 1]);
        }
        NSMutableDictionary *out = [NSMutableDictionary dictionary];
        out[@"pid"] = @(getpid());
        NSArray *start = loadedImages();
        out[@"images_at_start"] = @(start.count);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) {
            out[@"error"] = @"MTLCreateSystemDefaultDevice returned nil";
            printf("%s\n", [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted error:nil] encoding:NSUTF8StringEncoding].UTF8String);
            return 2;
        }
        NSArray *afterDevice = loadedImages();
        out[@"device"] = @{@"name": device.name, @"class": NSStringFromClass([device class]),
                           @"registry_id": @(device.registryID),
                           @"supports_family_mac2": @([device supportsFamily:MTLGPUFamilyMac2])};
        out[@"images_added_by_device"] = added(start, afterDevice);

        // The nonce must reach the code, not only a comment: the native compile is
        // cached by AIR, so a comment-only change still hits that cache.
        NSString *nonce = [NSUUID UUID].UUIDString;
        uint32_t salt = (uint32_t)nonce.hash;
        NSMutableString *source = [NSMutableString stringWithFormat:
            @"#include <metal_stdlib>\nusing namespace metal;\n"
             "kernel void store_index(device uint *out [[buffer(0)]], uint id [[thread_position_in_grid]]) {\n"
             "    uint v = id ^ %uu;\n    float f = float(v);\n", salt];
        for (unsigned i = 0; i < big; i++)
            [source appendFormat:@"    f = sin(f * %u.5f + %u.25f);\n", (i * 2654435761u) % 997u + 1u, (i ^ salt) % 991u];
        [source appendString:@"    out[id] = v + uint(f);\n}\n"];
        out[@"nonce"] = nonce;
        out[@"big"] = @(big);

        NSError *error = nil;
        double t0 = now();
        id<MTLLibrary> library = [device newLibraryWithSource:source options:[MTLCompileOptions new] error:&error];
        double t1 = now();
        NSArray *afterLibrary = loadedImages();
        out[@"library"] = @{@"ok": @(library != nil), @"seconds": @(t1 - t0),
                            @"error": error ? error.localizedDescription : [NSNull null],
                            @"class": library ? NSStringFromClass([library class]) : [NSNull null]};
        out[@"images_added_by_library"] = added(afterDevice, afterLibrary);
        if (!library) {
            printf("%s\n", [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted error:nil] encoding:NSUTF8StringEncoding].UTF8String);
            return 1;
        }

        id<MTLFunction> function = [library newFunctionWithName:@"store_index"];
        error = nil;
        t0 = now();
        id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
        t1 = now();
        NSArray *afterPipeline = loadedImages();
        out[@"pipeline"] = @{@"ok": @(pipeline != nil), @"seconds": @(t1 - t0),
                             @"error": error ? error.localizedDescription : [NSNull null],
                             @"class": pipeline ? NSStringFromClass([pipeline class]) : [NSNull null],
                             @"thread_execution_width": pipeline ? @(pipeline.threadExecutionWidth) : [NSNull null],
                             @"max_total_threads_per_threadgroup": pipeline ? @(pipeline.maxTotalThreadsPerThreadgroup) : [NSNull null],
                             @"static_threadgroup_memory_length": pipeline ? @(pipeline.staticThreadgroupMemoryLength) : [NSNull null]};
        out[@"images_added_by_pipeline"] = added(afterLibrary, afterPipeline);

        if (archivePath && pipeline) {
            MTLBinaryArchiveDescriptor *desc = [MTLBinaryArchiveDescriptor new];
            error = nil;
            id<MTLBinaryArchive> archive = [device newBinaryArchiveWithDescriptor:desc error:&error];
            MTLComputePipelineDescriptor *pd = [MTLComputePipelineDescriptor new];
            pd.computeFunction = function;
            BOOL added_ok = archive && [archive addComputePipelineFunctionsWithDescriptor:pd error:&error];
            BOOL saved = added_ok && [archive serializeToURL:[NSURL fileURLWithPath:archivePath] error:&error];
            out[@"binary_archive"] = @{@"path": archivePath, @"added": @(added_ok), @"serialized": @(saved),
                                       @"error": error ? error.localizedDescription : [NSNull null]};
            out[@"images_added_by_archive"] = added(afterPipeline, loadedImages());
        }

        out[@"images_at_end"] = @(loadedImages().count);
        printf("%s\n", [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:out options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys error:nil] encoding:NSUTF8StringEncoding].UTF8String);
        fflush(stdout);
        if (pause) sleep(pause);
        return 0;
    }
}
