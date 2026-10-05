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
//   Stage 2: also read GC MC_VM_FB_OFFSET, derive the carveout's physical
//            address, check it overlaps none of the device's BARs, map only
//            the 10 KiB IP discovery binary read-only, copy and validate it.
//   Stage 3: also read seven GC configuration registers and derive the
//            active CU and render-backend masks for SE 0 / SH 0, without
//            writing GRBM_GFX_INDEX to select them.
//            Nothing is written to configuration space, registers or memory,
//            and every mapping and the provider are released before start()
//            returns.
//
// Hardware rules live in driver/core; this file only adapts IOKit to them.

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#include "cezanne_core.h"

#define LOG_PREFIX "CezanneGPU: "

struct Aperture {
    const volatile UInt32 *base;
    UInt64 length;
    UInt32 stage;
};

class CezanneGPU : public IOService {
    OSDeclareDefaultStructors(CezanneGPU)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    UInt32 stage_ = 0;
    UInt32 table_[cezanne::kDiscoveryTmrSize / 4]; // stage 2 copy; too large for the stack
    void publish(const char *key, UInt64 value, UInt32 bits);
    void publishRegistryIdentity(IOService *provider);
    cezanne::Status runDevice(IOPCIDevice *pci);
    cezanne::Status runStage2(IOPCIDevice *pci, Aperture *aperture, const cezanne::BootState &boot,
                              cezanne::Discovery *discovery);
    cezanne::Status runStage3(Aperture *aperture, const cezanne::Discovery &discovery);
    cezanne::Status copyDiscovery(UInt64 physical);
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

static bool registerRead(void *context, uint32_t offset, uint32_t *value)
{
    const Aperture *aperture = static_cast<const Aperture *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::registerAllowed(offset, aperture->stage) || (offset & 3) != 0 || offset + 4ull > aperture->length) {
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

cezanne::Status CezanneGPU::copyDiscovery(UInt64 physical)
{
    IODeviceMemory *memory = IODeviceMemory::withRange(physical, cezanne::kDiscoveryTmrSize);
    if (memory == nullptr) {
        return cezanne::kTableUnavailable;
    }
    IOMemoryMap *map = memory->map(kIOMapInhibitCache | kIOMapReadOnly);
    cezanne::Status status = cezanne::kTableUnavailable;
    if (map != nullptr && map->getLength() >= cezanne::kDiscoveryTmrSize) {
        const volatile UInt32 *source = reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress());
        for (UInt32 i = 0; i < cezanne::kDiscoveryTmrSize / 4; i++) {
            table_[i] = source[i];
        }
        status = cezanne::kOK;
    }
    if (map != nullptr) {
        map->release();
    }
    memory->release();
    return status;
}

cezanne::Status CezanneGPU::runStage2(IOPCIDevice *pci, Aperture *aperture, const cezanne::BootState &boot,
                                      cezanne::Discovery *out)
{
    cezanne::Discovery &discovery = *out;
    discovery = cezanne::Discovery();
    cezanne::Carveout carveout;
    cezanne::RegisterReader registers = {registerRead, aperture};
    cezanne::Status status = cezanne::readCarveout(registers, aperture->length, boot, &carveout);
    publish("MC_VM_FB_OFFSET", carveout.fbOffset, 32);
    if (status != cezanne::kOK) {
        return status;
    }
    publish("carveout base", carveout.base, 64);
    publish("discovery address", carveout.table, 64);

    OSData *assigned = OSDynamicCast(OSData, pci->getProperty("assigned-addresses"));
    cezanne::Range ranges[8];
    uint32_t count = 0;
    status = cezanne::parseAssignedAddresses(
        assigned != nullptr ? static_cast<const uint8_t *>(assigned->getBytesNoCopy()) : nullptr,
        assigned != nullptr ? assigned->getLength() : 0, ranges, 8, &count);
    if (status == cezanne::kOK) {
        status = cezanne::checkCarveout(carveout, ranges, count);
    }
    if (status != cezanne::kOK) {
        return status;
    }

    status = copyDiscovery(carveout.table);
    if (status != cezanne::kOK) {
        IOLog(LOG_PREFIX "discovery mapping failed\n");
        return status;
    }
    status = cezanne::parseDiscovery(reinterpret_cast<const uint8_t *>(table_), sizeof(table_), &discovery);
    publish("discovery signature", table_[0], 32);
    // Publish the bytes only once the signature and checksum prove they are
    // the discovery binary, never arbitrary memory.
    if (discovery.binaryValid) {
        setProperty("CezanneGPU discovery binary", table_, discovery.binarySize);
        publish("discovery version", (UInt32(discovery.versionMajor) << 16) | discovery.versionMinor, 32);
        publish("discovery table version", discovery.tableVersion, 16);
        publish("discovery IP count", discovery.numIps, 16);
    }
    if (discovery.gcFound) {
        publish("discovery GC version",
                (UInt32(discovery.gcMajor) << 16) | (UInt32(discovery.gcMinor) << 8) | discovery.gcRevision, 32);
        publish("discovery GC base 0", discovery.gcBase0, 32);
        publish("discovery GC base 1", discovery.gcBase1, 32);
    }
    if (discovery.mp0Found) {
        publish("discovery MP0 base 0", discovery.mp0Base0, 32);
    }
    if (discovery.gcInfoFound) {
        publish("discovery GC info version", (UInt32(discovery.gcInfoMajor) << 16) | discovery.gcInfoMinor, 32);
        publish("discovery GC SE count", discovery.gcNumSe, 32);
        publish("discovery GC CUs per SH", discovery.gcCuPerSh, 32);
        publish("discovery GC SHs per SE", discovery.gcShPerSe, 32);
        publish("discovery GC RBs per SE", discovery.gcRbPerSe, 32);
    }
    IOLog(LOG_PREFIX "discovery %s: v%u.%u, %u IPs, GC %u.%u.%u\n", cezanne::statusName(status),
          discovery.versionMajor, discovery.versionMinor, discovery.numIps, discovery.gcMajor, discovery.gcMinor,
          discovery.gcRevision);
    return status;
}

cezanne::Status CezanneGPU::runStage3(Aperture *aperture, const cezanne::Discovery &discovery)
{
    cezanne::GfxConfig gfx;
    cezanne::RegisterReader registers = {registerRead, aperture};
    cezanne::Status status = cezanne::readGfxConfig(registers, aperture->length, discovery, &gfx);
    if (status == cezanne::kOK || status == cezanne::kGcInfoUnavailable || status == cezanne::kGfxIndexNotSe0Sh0 ||
        status == cezanne::kDeviceNotResponding) {
        publish("GRBM_STATUS", gfx.grbmStatus, 32);
        publish("GRBM_GFX_INDEX", gfx.grbmGfxIndex, 32);
        publish("CC_GC_SHADER_ARRAY_CONFIG", gfx.ccShaderArrayConfig, 32);
        publish("GC_USER_SHADER_ARRAY_CONFIG", gfx.userShaderArrayConfig, 32);
        publish("CC_RB_BACKEND_DISABLE", gfx.ccRbBackendDisable, 32);
        publish("GC_USER_RB_BACKEND_DISABLE", gfx.userRbBackendDisable, 32);
        publish("GB_ADDR_CONFIG", gfx.gbAddrConfig, 32);
    }
    if (status == cezanne::kOK) {
        publish("active CU mask", gfx.cuActiveMask, 32);
        publish("active CU count", gfx.cuActiveCount, 32);
        publish("active RB mask", gfx.rbActiveMask, 32);
        publish("active RB count", gfx.rbActiveCount, 32);
    }
    IOLog(LOG_PREFIX "gfx %s: %u CUs (mask 0x%x), %u RBs, GRBM_STATUS 0x%08x\n", cezanne::statusName(status),
          gfx.cuActiveCount, gfx.cuActiveMask, gfx.rbActiveCount, gfx.grbmStatus);
    return status;
}

cezanne::Status CezanneGPU::runDevice(IOPCIDevice *pci)
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
    Aperture aperture = {reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress()), map->getLength(),
                         stage_};
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
        if (status == cezanne::kOK && stage_ >= 2) {
            cezanne::Discovery discovery;
            cezanne::Status stage2 = runStage2(pci, &aperture, boot, &discovery);
            setProperty("CezanneGPU stage 2 result", cezanne::statusName(stage2));
            IOLog(LOG_PREFIX "stage 2 result: %s\n", cezanne::statusName(stage2));
            // Stage 3 trusts the GC bases only after the discovery cross-check.
            if (stage2 == cezanne::kOK && stage_ >= 3) {
                cezanne::Status stage3 = runStage3(&aperture, discovery);
                setProperty("CezanneGPU stage 3 result", cezanne::statusName(stage3));
                IOLog(LOG_PREFIX "stage 3 result: %s\n", cezanne::statusName(stage3));
            }
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
            status = runDevice(pci);
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
