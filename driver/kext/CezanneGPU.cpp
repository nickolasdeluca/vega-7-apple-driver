// IOKit adapter for the Cezanne driver: loaded only by the USB test EFI.
//
// The boot argument cezanne-stage=N selects how far it goes, up to
// cezanne::kMaxStage; without it the driver declines to attach. Each stage is
// authorized in docs/test-boot.md.
//
//   Stage 0: attach and record the registry's PCI identity. No device access.
//   Stage 1: also read PCI configuration space, then, only if the device is
//            already in D0 with memory decoding on, map the register BAR
//            read-only and read the two boot-state registers the core allows.
//            Nothing is written to configuration space or registers, and the
//            mapping and provider are released before start() returns.
//
// Hardware rules live in driver/core; this file only adapts IOKit to them.

#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#include "cezanne_core.h"

#define LOG_PREFIX "CezanneGPU: "

class CezanneGPU : public IOService {
    OSDeclareDefaultStructors(CezanneGPU)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    UInt32 stage_ = 0;
    void publish(const char *key, UInt64 value, UInt32 bits);
    void publishRegistryIdentity(IOService *provider);
    cezanne::Status runStage1(IOPCIDevice *pci);
};

OSDefineMetaClassAndStructors(CezanneGPU, IOService)

// Reads a little-endian 4-byte PCI registry property set by IOPCIFamily.
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

static bool requestedStage(UInt32 *stage)
{
    UInt32 value = 0;
    if (!PE_parse_boot_argn("cezanne-stage", &value, sizeof(value))) {
        return false;
    }
    *stage = value;
    return true;
}

static bool configRead(void *context, uint8_t offset, uint8_t width, uint32_t *value)
{
    IOPCIDevice *pci = static_cast<IOPCIDevice *>(context);
    switch (width) {
    case 1: *value = pci->configRead8(offset); return true;
    case 2: *value = pci->configRead16(offset); return true;
    case 4: *value = pci->configRead32(offset); return true;
    default: return false;
    }
}

struct Aperture {
    const volatile UInt32 *base;
    UInt64 length;
};

static bool registerRead(void *context, uint32_t offset, uint32_t *value)
{
    const Aperture *aperture = static_cast<const Aperture *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::registerAllowed(offset) || (offset & 3) != 0 || offset + 4ull > aperture->length) {
        return false;
    }
    *value = aperture->base[offset / 4];
    return true;
}

IOService *CezanneGPU::probe(IOService *provider, SInt32 *score)
{
    UInt32 stage = 0;
    if (!requestedStage(&stage)) {
        IOLog(LOG_PREFIX "declining: boot argument cezanne-stage is absent\n");
        return nullptr;
    }
    if (stage > cezanne::kMaxStage) {
        IOLog(LOG_PREFIX "declining: cezanne-stage=%u exceeds this build's stage %u\n", stage,
              cezanne::kMaxStage);
        return nullptr;
    }
    if (OSDynamicCast(IOPCIDevice, provider) == nullptr) {
        IOLog(LOG_PREFIX "declining: provider is not an IOPCIDevice\n");
        return nullptr;
    }
    UInt32 vendor = 0, device = 0, revision = 0;
    if (!registryU32(provider, "vendor-id", &vendor) || !registryU32(provider, "device-id", &device) ||
        !registryU32(provider, "revision-id", &revision) || (vendor & 0xFFFF) != cezanne::kVendorAMD ||
        (device & 0xFFFF) != cezanne::kDeviceCezanne || (revision & 0xFF) != cezanne::kRevisionTarget) {
        IOLog(LOG_PREFIX "declining: registry identity is not %04x:%04x rev %02x\n", cezanne::kVendorAMD,
              cezanne::kDeviceCezanne, cezanne::kRevisionTarget);
        return nullptr;
    }
    stage_ = stage;
    return IOService::probe(provider, score);
}

void CezanneGPU::publish(const char *key, UInt64 value, UInt32 bits)
{
    char name[64];
    snprintf(name, sizeof(name), "CezanneGPU %s", key);
    setProperty(name, value, bits);
}

void CezanneGPU::publishRegistryIdentity(IOService *provider)
{
    // Missing registry values are reported as unavailable, never as zero.
    static const char *const keys[] = {"vendor-id", "device-id", "revision-id", "subsystem-vendor-id",
                                       "subsystem-id", "class-code"};
    for (const char *key : keys) {
        UInt32 value = 0;
        if (registryU32(provider, key, &value)) {
            IOLog(LOG_PREFIX "registry %s=0x%08x\n", key, value);
            char name[64];
            snprintf(name, sizeof(name), "registry %s", key);
            publish(name, value, 32);
        } else {
            IOLog(LOG_PREFIX "registry %s unavailable\n", key);
        }
    }
}

cezanne::Status CezanneGPU::runStage1(IOPCIDevice *pci)
{
    cezanne::PciState state;
    cezanne::ConfigReader config = {configRead, pci};
    cezanne::Status status = cezanne::readPciState(config, &state);
    if (status == cezanne::kOK) {
        IOLog(LOG_PREFIX "config %04x:%04x rev %02x class %06x command %04x status %04x bar5 %08x "
              "pm@%02x pmcsr %04x\n", state.vendor, state.device, state.revision, state.classCode,
              state.command, state.status, state.bar5, state.powerCapability, state.powerControl);
        publish("config command", state.command, 16);
        publish("config status", state.status, 16);
        publish("config bar5", state.bar5, 32);
        publish("config pmcsr", state.powerControl, 16);
        status = cezanne::checkPciState(state);
    }
    if (status != cezanne::kOK) {
        return status;
    }

    IOMemoryMap *map = pci->mapDeviceMemoryWithRegister(cezanne::kRegisterBar, kIOMapInhibitCache | kIOMapReadOnly);
    if (map == nullptr) {
        IOLog(LOG_PREFIX "register BAR mapping failed\n");
        return cezanne::kApertureUnavailable;
    }
    Aperture aperture = {reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress()), map->getLength()};
    publish("bar5 length", aperture.length, 64);
    status = cezanne::checkAperture(state, map->getPhysicalAddress(), aperture.length);
    if (status == cezanne::kOK) {
        cezanne::BootState boot;
        cezanne::RegisterReader registers = {registerRead, &aperture};
        status = cezanne::readBootState(registers, aperture.length, &boot);
        IOLog(LOG_PREFIX "C2PMSG_33=0x%08x RCC_CONFIG_MEMSIZE=0x%08x (%llu MiB) ifwi-ready=%d\n", boot.c2pmsg33,
              boot.configMemsize, boot.vramBytes >> 20, boot.ifwiReady);
        if (status == cezanne::kOK || status == cezanne::kDeviceNotResponding) {
            publish("MP0_SMN_C2PMSG_33", boot.c2pmsg33, 32);
            publish("RCC_CONFIG_MEMSIZE", boot.configMemsize, 32);
        }
    }
    map->release();
    return status;
}

bool CezanneGPU::start(IOService *provider)
{
    if (!IOService::start(provider)) {
        IOLog(LOG_PREFIX "IOService::start failed\n");
        return false;
    }
    publish("stage", stage_, 32);
    publishRegistryIdentity(provider);
    if (stage_ >= 1) {
        IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
        cezanne::Status status = cezanne::kProviderOpenFailed;
        if (pci != nullptr && pci->open(this)) {
            status = runStage1(pci);
            pci->close(this);
        } else {
            // Another client (the boot framebuffer or AMDSupport also attach
            // to this device) may hold it open; record that, read nothing.
            IOLog(LOG_PREFIX "could not open the PCI device\n");
        }
        // A failed check is a result to record, not a reason to unload.
        setProperty("CezanneGPU stage 1 result", cezanne::statusName(status));
        IOLog(LOG_PREFIX "stage 1 result: %s\n", cezanne::statusName(status));
    }
    IOLog(LOG_PREFIX "attached at stage %u to %s; no register or configuration writes performed\n", stage_,
          provider->getName());
    registerService();
    return true;
}

void CezanneGPU::stop(IOService *provider)
{
    IOLog(LOG_PREFIX "stopping\n");
    IOService::stop(provider);
}
