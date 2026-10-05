// Passive Cezanne probe: the first kext loaded only by the USB test EFI.
//
// It attaches to the Cezanne IOPCIDevice and records what the PCI registry
// already reports. It never opens the provider, reads or writes PCI
// configuration space, maps BARs, enables decoding or bus mastering, or
// registers interrupts. tests/test_probe_kext.py rejects builds whose
// undefined symbols would allow any of that.
//
// Interlock: probe() declines unless the boot arguments contain
// -cezanne-probe, which only the test EFI sets (docs/test-boot.md).

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#define LOG_PREFIX "CezanneProbe: "

static const UInt32 kVendorAMD = 0x1002;
static const UInt32 kDeviceCezanne = 0x1638;
static const UInt32 kProbeStage = 0; // stage 0: passive registry observation

class CezanneProbe : public IOService {
    OSDeclareDefaultStructors(CezanneProbe)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(CezanneProbe, IOService)

// Reads a little-endian 4-byte PCI registry property set by IOPCIFamily.
// Returns false when the property is missing or has an unexpected shape.
static bool registryU32(IOService *provider, const char *key, UInt32 *value)
{
    OSData *data = OSDynamicCast(OSData, provider->getProperty(key));
    if (data == nullptr || data->getLength() != sizeof(UInt32)) {
        return false;
    }
    const UInt8 *bytes = static_cast<const UInt8 *>(data->getBytesNoCopy());
    *value = static_cast<UInt32>(bytes[0]) | (static_cast<UInt32>(bytes[1]) << 8) |
             (static_cast<UInt32>(bytes[2]) << 16) | (static_cast<UInt32>(bytes[3]) << 24);
    return true;
}

static bool interlockSet()
{
    char flag = 0;
    return PE_parse_boot_argn("-cezanne-probe", &flag, sizeof(flag));
}

IOService *CezanneProbe::probe(IOService *provider, SInt32 *score)
{
    if (!interlockSet()) {
        IOLog(LOG_PREFIX "declining: boot argument -cezanne-probe is absent\n");
        return nullptr;
    }
    if (OSDynamicCast(IOPCIDevice, provider) == nullptr) {
        IOLog(LOG_PREFIX "declining: provider is not an IOPCIDevice\n");
        return nullptr;
    }
    UInt32 vendor = 0, device = 0;
    if (!registryU32(provider, "vendor-id", &vendor) || !registryU32(provider, "device-id", &device) ||
        (vendor & 0xFFFF) != kVendorAMD || (device & 0xFFFF) != kDeviceCezanne) {
        IOLog(LOG_PREFIX "declining: registry IDs are not %04x:%04x\n", kVendorAMD, kDeviceCezanne);
        return nullptr;
    }
    return IOService::probe(provider, score);
}

bool CezanneProbe::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        IOLog(LOG_PREFIX "IOService::start failed\n");
        return false;
    }
    // Missing registry values are reported as unavailable, never as zero.
    static const char *const keys[] = {"vendor-id", "device-id", "revision-id", "subsystem-vendor-id",
                                       "subsystem-id", "class-code"};
    for (const char *key : keys) {
        UInt32 value = 0;
        if (registryU32(provider, key, &value)) {
            IOLog(LOG_PREFIX "%s=0x%08x\n", key, value);
            OSNumber *number = OSNumber::withNumber(value, 32);
            if (number != nullptr) {
                char name[64];
                snprintf(name, sizeof(name), "CezanneProbe %s", key);
                setProperty(name, number);
                number->release();
            }
        } else {
            IOLog(LOG_PREFIX "%s unavailable\n", key);
        }
    }
    setProperty("CezanneProbe stage", kProbeStage, 32);
    IOLog(LOG_PREFIX "attached passively to %s; no hardware access performed\n", provider->getName());
    registerService();
    return true;
}

void CezanneProbe::stop(IOService *provider)
{
    IOLog(LOG_PREFIX "stopping\n");
    IOService::stop(provider);
}
