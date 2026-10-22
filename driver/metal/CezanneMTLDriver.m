// Metal track M1 (docs/test-boot.md): an observation-only Metal device bundle.
//
// Metal's resolver loads this bundle from /Library/GPUBundles when an
// IOAccelerator node names it (MetalPluginName CezanneMTLDriver) and asks for
// MetalPluginClassName CezanneMtlDevice, which must be a subclass of the
// private _MTLDevice. The class is therefore built at load time on top of
// whatever _MTLDevice the running Metal provides; nothing links against it.
//
// The device does no GPU work. It records every message Metal sends it that
// _MTLDevice does not implement itself (those are what Metal expects of a
// vendor), answers them with nil/0/NO, and records respondsToSelector:
// queries. Records go to the unified log (prefix "CezanneM1:") and, when
// CEZANNE_M1_LOG names a writable file, to that file as well.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#import <objc/runtime.h>
#include <os/log.h>
#include <stdio.h>
#include <stdlib.h>

static os_log_t m1Log(void)
{
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ log = os_log_create("org.cezanne-driver.mtl", "M1"); });
    return log;
}

static void record(NSString *line)
{
    os_log(m1Log(), "CezanneM1: %{public}s", line.UTF8String);
    const char *path = getenv("CEZANNE_M1_LOG");
    if (path != NULL) {
        FILE *file = fopen(path, "a");
        if (file != NULL) {
            fprintf(file, "%s\n", line.UTF8String);
            fclose(file);
        }
    }
}

static Class gSuper;

static id initWithAcceleratorPort(id self, SEL _cmd, unsigned int port)
{
    record([NSString stringWithFormat:@"initWithAcceleratorPort: %u (class %@, superclass %@)", port,
                                      NSStringFromClass(object_getClass(self)), NSStringFromClass(gSuper)]);
    struct objc_super sup = {self, gSuper};
    id result = ((id(*)(struct objc_super *, SEL))objc_msgSendSuper)(&sup, @selector(init));
    record([NSString stringWithFormat:@"[_MTLDevice init] returned %p", result]);
    return result;
}

static NSString *deviceName(id self, SEL _cmd)
{
    return @"Cezanne M1 (observation only)";
}

static BOOL resolveInstanceMethod(id cls, SEL _cmd, SEL selector)
{
    // Reached for respondsToSelector: as well as for real sends; never add a
    // method, so the answer to respondsToSelector: stays NO.
    record([NSString stringWithFormat:@"resolve -%@", NSStringFromSelector(selector)]);
    return NO;
}

static NSMethodSignature *methodSignature(id self, SEL _cmd, SEL selector)
{
    struct objc_super sup = {self, gSuper};
    NSMethodSignature *signature =
        ((NSMethodSignature * (*)(struct objc_super *, SEL, SEL)) objc_msgSendSuper)(&sup, _cmd, selector);
    // Unknown to the hierarchy: answer as if it returned an object (nil).
    return signature ? signature : [NSMethodSignature signatureWithObjCTypes:"@@:"];
}

static void forwardInvocation(id self, SEL _cmd, NSInvocation *invocation)
{
    record([NSString stringWithFormat:@"sent -%@ (answered nil/0)", NSStringFromSelector(invocation.selector)]);
    NSUInteger length = invocation.methodSignature.methodReturnLength;
    if (length > 0) {
        void *zero = calloc(1, length);
        [invocation setReturnValue:zero];
        free(zero);
    }
}

__attribute__((constructor)) static void registerDeviceClass(void)
{
    gSuper = objc_getClass("_MTLDevice");
    if (gSuper == Nil) {
        record(@"load: _MTLDevice not found; CezanneMtlDevice not created");
        return;
    }
    Class cls = objc_allocateClassPair(gSuper, "CezanneMtlDevice", 0);
    if (cls == Nil) {
        record(@"load: objc_allocateClassPair failed (name taken?)");
        return;
    }
    class_addMethod(cls, @selector(initWithAcceleratorPort:), (IMP)initWithAcceleratorPort, "@20@0:8I16");
    class_addMethod(cls, @selector(name), (IMP)deviceName, "@16@0:8");
    class_addMethod(cls, @selector(methodSignatureForSelector:), (IMP)methodSignature, "@24@0:8:16");
    class_addMethod(cls, @selector(forwardInvocation:), (IMP)forwardInvocation, "v24@0:8@16");
    class_addMethod(object_getClass((id)cls), @selector(resolveInstanceMethod:), (IMP)resolveInstanceMethod,
                    "c24@0:8:16");
    objc_registerClassPair(cls);
    record([NSString stringWithFormat:@"load: CezanneMtlDevice registered on %@ in pid %d", NSStringFromClass(gSuper),
                                      getpid()]);
}
