// Public API discovery only: no resources, command queues, shaders, or drawables.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach-o/dyld.h>
#include <stdio.h>

int main(void) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        NSMutableArray *inventory = [NSMutableArray array];
        for (id<MTLDevice> device in devices) {
            [inventory addObject:@{
                @"name": device.name,
                @"registry_id": @(device.registryID),
                @"low_power": @(device.lowPower),
                @"removable": @(device.removable),
                @"unified_memory": @(device.hasUnifiedMemory),
                @"recommended_max_working_set_bytes": @(device.recommendedMaxWorkingSetSize)
            }];
        }
        NSMutableArray *images = [NSMutableArray array];
        for (uint32_t i = 1; i < _dyld_image_count(); ++i) {
            const char *name = _dyld_get_image_name(i);
            if (!name) continue;
            NSString *path = [NSString stringWithUTF8String:name];
            NSString *lower = path.lowercaseString;
            if ([lower containsString:@"metal"] || [lower containsString:@"radeon"] ||
                [lower containsString:@"ioaccelerator"] || [lower containsString:@"iosurface"]) {
                [images addObject:path];
            }
        }
        NSMutableDictionary *report = [@{
            @"schema_version": @1,
            @"status": inventory.count ? @"available" : @"unavailable",
            @"devices": inventory,
            @"loaded_graphics_images": images,
            @"scope": @"Metal device discovery and properties; no GPU work submitted"
        } mutableCopy];
        if (!inventory.count) report[@"reason"] = @"MTLCopyAllDevices returned no devices; discovery may be restricted";
        NSError *error = nil;
        NSData *json = [NSJSONSerialization dataWithJSONObject:report options:NSJSONWritingPrettyPrinted error:&error];
        if (!json) {
            fprintf(stderr, "%s\n", error.description.UTF8String);
            return 1;
        }
        if (fwrite(json.bytes, 1, json.length, stdout) != json.length || fputc('\n', stdout) == EOF) return 1;
        return inventory.count ? 0 : 2;
    }
}
