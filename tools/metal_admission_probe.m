// Metal track M1 probe (docs/test-boot.md): does Metal admit our device, and
// what does it ask of it?
//
// Default mode: MTLCopyAllDevices, report every device's class, name and
// registry ID; on each CezanneMtlDevice, try newLibraryWithSource: and
// newComputePipelineStateWithFunction: on a fixed kernel. --load-bundle PATH
// instead loads our bundle into this process directly, constructs the class
// with initWithAcceleratorPort:0 and makes the same calls, which exercises
// the post-admission path without Metal's resolver. Nothing is dispatched;
// no command queue or buffer is created. Output is JSON; exceptions are caught
// and reported. Set CEZANNE_M1_LOG to collect the bundle's message log.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <objc/message.h>
#import <objc/runtime.h>

// A per-run constant in the code keeps every compile out of Metal's caches.
static NSString *kernelSource(void)
{
    uint32_t salt = (uint32_t)[NSUUID UUID].UUIDString.hash;
    return [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n"
                                       "kernel void store_index(device uint *out [[buffer(0)]], uint id [[thread_position_in_grid]]) {\n"
                                       "    out[id] = id ^ %uu;\n}\n", salt];
}

static NSString *describe(id value)
{
    return value == nil ? @"nil" : [NSString stringWithFormat:@"%@ %p", NSStringFromClass([value class]), value];
}

static NSDictionary *exercise(id<MTLDevice> device)
{
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    @try {
        out[@"name"] = describe(device.name);
    } @catch (NSException *e) {
        out[@"name_exception"] = e.reason ?: e.name;
    }
    NSError *error = nil;
    id<MTLLibrary> library = nil;
    @try {
        library = [device newLibraryWithSource:kernelSource() options:[MTLCompileOptions new] error:&error];
        out[@"library"] = describe(library);
        out[@"library_error"] = error ? error.localizedDescription : [NSNull null];
    } @catch (NSException *e) {
        out[@"library_exception"] = [NSString stringWithFormat:@"%@: %@", e.name, e.reason];
    }
    if (library != nil) {
        @try {
            id<MTLFunction> function = [library newFunctionWithName:@"store_index"];
            out[@"function"] = describe(function);
            error = nil;
            id pipeline = [device newComputePipelineStateWithFunction:function error:&error];
            out[@"pipeline"] = describe(pipeline);
            out[@"pipeline_error"] = error ? error.localizedDescription : [NSNull null];
        } @catch (NSException *e) {
            out[@"pipeline_exception"] = [NSString stringWithFormat:@"%@: %@", e.name, e.reason];
        }
    }
    return out;
}

static void emit(NSDictionary *out)
{
    NSData *data = [NSJSONSerialization dataWithJSONObject:out
                                                   options:NSJSONWritingPrettyPrinted | NSJSONWritingSortedKeys
                                                     error:nil];
    printf("%s\n", [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding].UTF8String);
    fflush(stdout);
}

int main(int argc, const char *argv[])
{
    @autoreleasepool {
        NSMutableDictionary *out = [NSMutableDictionary dictionary];
        out[@"pid"] = @(getpid());
        if (argc == 3 && !strcmp(argv[1], "--load-bundle")) {
            NSBundle *bundle = [NSBundle bundleWithPath:@(argv[2])];
            NSError *error = nil;
            BOOL loaded = [bundle loadAndReturnError:&error];
            out[@"mode"] = @"load-bundle";
            out[@"loaded"] = @(loaded);
            out[@"load_error"] = error ? error.localizedDescription : [NSNull null];
            Class cls = loaded ? [bundle principalClass] : Nil;
            Class base = objc_getClass("_MTLDevice");
            out[@"principal_class"] = cls ? NSStringFromClass(cls) : [NSNull null];
            out[@"is_mtldevice_subclass"] = @(cls != Nil && base != Nil && [cls isSubclassOfClass:base]);
            if (cls == Nil) {
                emit(out);
                return 1;
            }
            id device = nil;
            @try {
                device = ((id(*)(id, SEL, unsigned int))objc_msgSend)([cls alloc], @selector(initWithAcceleratorPort:), 0);
                out[@"constructed"] = describe(device);
            } @catch (NSException *e) {
                out[@"construct_exception"] = [NSString stringWithFormat:@"%@: %@", e.name, e.reason];
            }
            if (device != nil) out[@"device"] = exercise(device);
            emit(out);
            return 0;
        }
        out[@"mode"] = @"enumerate";
        NSMutableArray *devices = [NSMutableArray array];
        for (id<MTLDevice> device in MTLCopyAllDevices()) {
            NSMutableDictionary *entry = [NSMutableDictionary dictionary];
            entry[@"class"] = NSStringFromClass([(id)device class]);
            entry[@"registry_id"] = @(device.registryID);
            if ([entry[@"class"] isEqualToString:@"CezanneMtlDevice"]) {
                [entry addEntriesFromDictionary:exercise(device)];
            } else {
                entry[@"name"] = device.name ?: @"";
            }
            [devices addObject:entry];
        }
        out[@"devices"] = devices;
        emit(out);
        return 0;
    }
}
