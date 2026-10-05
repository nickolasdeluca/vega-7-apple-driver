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
//   Stage 4: after stage 3 succeeds, also offer a root-only diagnostic
//            IOUserClient that re-reads any stage 3 register on request. Each
//            read re-checks D0, decoding and BAR5, maps BAR5 read-only, reads
//            one allowlisted register and releases the mapping.
//   Stage 5: the diagnostic interface also reads 38 power, clock-gating,
//            engine and memory-hub state registers (never at boot); GC ones
//            only while SMUIO reports GFX on.
//   Stage 6: the first reviewed write. On request, in three ordered steps
//            (check, write, restore), write 0xCAFEDEAD to SCRATCH_REG0 and
//            then its original value, after read-checked preconditions. Only
//            the 4 KiB BAR5 page holding SCRATCH_REG0 is mapped writable, and
//            only during those steps. A connection closed after the write
//            has its original value restored.
//            Apart from the stage 6 scratch test, nothing is written to
//            configuration space, registers or memory, and every mapping and
//            the provider are released before start() returns.
//
// Hardware rules live in driver/core; this file only adapts IOKit to them.

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
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

// The writable 4 KiB page holding SCRATCH_REG0 (stage 6 only).
struct WritePage {
    volatile UInt32 *base;
};

// One device operation run by CezanneGPU::accessDevice with the checks done
// and BAR5 mapped; writer is null unless the operation asked for the page.
typedef cezanne::Status (*DeviceOperation)(UInt32 stage, const cezanne::RegisterReader &registers,
                                           UInt64 length, const cezanne::RegisterWriter *writer, void *argument);

class CezanneGPU : public IOService {
    OSDeclareDefaultStructors(CezanneGPU)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
    using IOService::newUserClient;
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties,
                           IOUserClient **handler) override;

    // Diagnostic interface (stage 4 on); scratch test (stage 6). owner is the
    // calling user client: a scratch test belongs to one connection.
    UInt32 stage() const { return stage_; }
    cezanne::Status diagnosticRead(uint32_t offset, uint32_t *value);
    cezanne::Status scratchCheck(const void *owner, cezanne::ScratchCheck *check);
    cezanne::Status scratchWrite(const void *owner, uint32_t *readback);
    cezanne::Status scratchRestore(const void *owner, uint32_t *readback);
    void scratchAbandon(const void *owner);

private:
    enum ScratchState { kScratchIdle, kScratchChecked, kScratchWritten };
    ScratchState scratchState_ = kScratchIdle;
    const void *scratchOwner_ = nullptr;
    uint32_t scratchOriginal_ = 0;
    cezanne::Status accessDevice(bool writable, DeviceOperation operation, void *argument);
    cezanne::Status restoreLocked();
    UInt32 stage_ = 0;
    bool diagnosticsReady_ = false; // stage 3 succeeded, so the GC bases are confirmed
    IOLock *lock_ = nullptr;        // serializes diagnostic reads
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

static bool registerWrite(void *context, uint32_t offset, uint32_t value)
{
    WritePage *page = static_cast<WritePage *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::writeAllowed(offset, cezanne::kScratchStage) || offset < cezanne::kScratchPageOffset ||
        offset + 4ull > uint64_t(cezanne::kScratchPageOffset) + cezanne::kScratchPageSize) {
        return false;
    }
    page->base[(offset - cezanne::kScratchPageOffset) / 4] = value;
    return true;
}

static bool refuseWrite(void *, uint32_t, uint32_t)
{
    return false;
}

static void pauseOneMillisecond(void *)
{
    IODelay(1000);
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
                diagnosticsReady_ = stage3 == cezanne::kOK && stage_ >= cezanne::kDiagnosticStage;
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
    if (diagnosticsReady_) {
        lock_ = IOLockAlloc();
        diagnosticsReady_ = lock_ != nullptr;
    }
    setProperty("CezanneGPU diagnostics", diagnosticsReady_);
    IOLog(LOG_PREFIX "attached at stage %u to %s; no register or configuration writes performed\n", stage_,
          provider->getName());
    registerService();
    return true;
}

void CezanneGPU::free()
{
    if (lock_ != nullptr) {
        IOLockFree(lock_);
        lock_ = nullptr;
    }
    IOService::free();
}

cezanne::Status CezanneGPU::accessDevice(bool writable, DeviceOperation operation, void *argument)
{
    // Caller holds lock_.
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, getProvider());
    if (pci == nullptr || !pci->open(this)) {
        return cezanne::kProviderOpenFailed;
    }
    // Re-check every time: the device may have changed state since start().
    cezanne::PciState state;
    cezanne::ConfigReader config = {configRead, pci};
    cezanne::Status status = cezanne::readPciState(config, &state);
    if (status == cezanne::kOK) {
        status = cezanne::checkPciState(state);
    }
    IOMemoryMap *map = nullptr;
    if (status == cezanne::kOK) {
        map = pci->mapDeviceMemoryWithRegister(cezanne::kRegisterBar, kIOMapInhibitCache | kIOMapReadOnly);
        status = map == nullptr ? cezanne::kApertureUnavailable : cezanne::kOK;
    }
    Aperture aperture = {nullptr, 0, stage_};
    if (status == cezanne::kOK) {
        aperture.base = reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress());
        aperture.length = map->getLength();
        status = cezanne::checkAperture(state, map->getPhysicalAddress(), aperture.length);
    }
    IODeviceMemory *pageMemory = nullptr;
    IOMemoryMap *pageMap = nullptr;
    WritePage page = {nullptr};
    if (status == cezanne::kOK && writable) {
        // The only writable mapping: one page of BAR5, for the scratch test.
        pageMemory = IODeviceMemory::withRange((state.bar5 & ~0xFull) + cezanne::kScratchPageOffset,
                                               cezanne::kScratchPageSize);
        pageMap = pageMemory != nullptr ? pageMemory->map(kIOMapInhibitCache) : nullptr;
        if (pageMap == nullptr || pageMap->getLength() < cezanne::kScratchPageSize) {
            status = cezanne::kApertureUnavailable;
        } else {
            page.base = reinterpret_cast<volatile UInt32 *>(pageMap->getVirtualAddress());
        }
    }
    if (status == cezanne::kOK) {
        cezanne::RegisterReader registers = {registerRead, &aperture};
        cezanne::RegisterWriter writer = {writable ? registerWrite : refuseWrite, pauseOneMillisecond, &page};
        status = operation(stage_, registers, aperture.length, &writer, argument);
    }
    if (pageMap != nullptr) {
        pageMap->release();
    }
    if (pageMemory != nullptr) {
        pageMemory->release();
    }
    if (map != nullptr) {
        map->release();
    }
    pci->close(this);
    return status;
}

struct ReadArgument {
    uint32_t offset;
    uint32_t *value;
};

static cezanne::Status readOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                     const cezanne::RegisterWriter *, void *argument)
{
    ReadArgument *read = static_cast<ReadArgument *>(argument);
    return cezanne::readDiagnosticRegister(registers, length, stage, read->offset, read->value);
}

cezanne::Status CezanneGPU::diagnosticRead(uint32_t offset, uint32_t *value)
{
    *value = 0;
    if (!diagnosticsReady_ || !cezanne::registerAllowed(offset, stage_)) {
        return cezanne::kRegisterNotAllowed;
    }
    ReadArgument read = {offset, value};
    IOLockLock(lock_);
    cezanne::Status status = accessDevice(false, readOperation, &read);
    IOLockUnlock(lock_);
    return status;
}

struct ScratchArgument {
    uint32_t original;
    uint32_t *readback;
    cezanne::ScratchCheck *check;
};

static cezanne::Status scratchCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                             const cezanne::RegisterWriter *writer, void *argument)
{
    return cezanne::checkScratch(registers, length, *writer, stage, static_cast<ScratchArgument *>(argument)->check);
}

static cezanne::Status scratchWriteOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                             const cezanne::RegisterWriter *writer, void *argument)
{
    ScratchArgument *scratch = static_cast<ScratchArgument *>(argument);
    return cezanne::writeScratchPattern(registers, length, *writer, stage, scratch->original, scratch->readback);
}

static cezanne::Status scratchRestoreOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                               const cezanne::RegisterWriter *writer, void *argument)
{
    ScratchArgument *scratch = static_cast<ScratchArgument *>(argument);
    return cezanne::restoreScratch(registers, length, *writer, stage, scratch->original, scratch->readback);
}

cezanne::Status CezanneGPU::scratchCheck(const void *owner, cezanne::ScratchCheck *check)
{
    *check = cezanne::ScratchCheck();
    if (!diagnosticsReady_ || stage_ < cezanne::kScratchStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kScratchOutOfOrder;
    // A written value must be restored before another test starts.
    if (scratchState_ != kScratchWritten) {
        ScratchArgument scratch = {0, nullptr, check};
        status = accessDevice(false, scratchCheckOperation, &scratch);
        scratchState_ = status == cezanne::kOK ? kScratchChecked : kScratchIdle;
        scratchOwner_ = owner;
        scratchOriginal_ = check->original;
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::scratchWrite(const void *owner, uint32_t *readback)
{
    *readback = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kScratchOutOfOrder;
    if (scratchState_ == kScratchChecked && scratchOwner_ == owner) {
        ScratchArgument scratch = {scratchOriginal_, readback, nullptr};
        status = accessDevice(true, scratchWriteOperation, &scratch);
        // A failed precondition or read stops before writing; anything after
        // the write call leaves the register to be restored.
        bool wrote = status == cezanne::kOK || status == cezanne::kScratchReadbackMismatch ||
                     status == cezanne::kRegisterWriteFailed;
        scratchState_ = wrote ? kScratchWritten : kScratchIdle;
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::restoreLocked()
{
    uint32_t readback = 0;
    ScratchArgument scratch = {scratchOriginal_, &readback, nullptr};
    cezanne::Status status = accessDevice(true, scratchRestoreOperation, &scratch);
    scratchState_ = kScratchIdle;
    return status;
}

cezanne::Status CezanneGPU::scratchRestore(const void *owner, uint32_t *readback)
{
    *readback = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kScratchOutOfOrder;
    if (scratchState_ == kScratchWritten && scratchOwner_ == owner) {
        ScratchArgument scratch = {scratchOriginal_, readback, nullptr};
        status = accessDevice(true, scratchRestoreOperation, &scratch);
        scratchState_ = kScratchIdle;
    }
    IOLockUnlock(lock_);
    return status;
}

void CezanneGPU::scratchAbandon(const void *owner)
{
    if (lock_ == nullptr) {
        return;
    }
    IOLockLock(lock_);
    if (scratchOwner_ == owner) {
        if (scratchState_ == kScratchWritten) {
            cezanne::Status status = restoreLocked();
            IOLog(LOG_PREFIX "scratch test abandoned after the write; restore: %s\n", cezanne::statusName(status));
            setProperty("CezanneGPU scratch abandoned restore", cezanne::statusName(status));
        }
        scratchState_ = kScratchIdle;
        scratchOwner_ = nullptr;
    }
    IOLockUnlock(lock_);
}

// Root-only diagnostic connection to CezanneGPU: reads from stage 4, the
// scratch test from stage 6.
class CezanneGPUUserClient : public IOUserClient {
    OSDeclareDefaultStructors(CezanneGPUUserClient)

public:
    bool start(IOService *provider) override;
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                            IOExternalMethodDispatch *dispatch, OSObject *target, void *reference) override;

private:
    CezanneGPU *gpu_ = nullptr;
    static IOReturn getInfo(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn readRegister(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn scratchCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn scratchWrite(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn scratchRestore(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
};

OSDefineMetaClassAndStructors(CezanneGPUUserClient, IOUserClient)

bool CezanneGPUUserClient::start(IOService *provider)
{
    gpu_ = OSDynamicCast(CezanneGPU, provider);
    return gpu_ != nullptr && IOUserClient::start(provider);
}

IOReturn CezanneGPUUserClient::clientClose()
{
    gpu_->scratchAbandon(this);
    terminate();
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::getInfo(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    arguments->scalarOutput[0] = cezanne::kDiagnosticVersion;
    arguments->scalarOutput[1] = self->gpu_->stage();
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::readRegister(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint64_t offset = arguments->scalarInput[0];
    if (offset > 0xFFFFFFFFull) {
        return kIOReturnBadArgument;
    }
    uint32_t value = 0;
    cezanne::Status status = self->gpu_->diagnosticRead(static_cast<uint32_t>(offset), &value);
    arguments->scalarOutput[0] = status;
    arguments->scalarOutput[1] = value;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::scratchCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::ScratchCheck check;
    cezanne::Status status = self->gpu_->scratchCheck(self, &check);
    const uint64_t out[] = {status, check.gfxMisc, check.cpMeCntl, check.cpMecCntl, check.rlcCntl, check.grbmStatus,
                            check.original};
    for (uint32_t i = 0; i < 7; i++) {
        arguments->scalarOutput[i] = out[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::scratchWrite(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t readback = 0;
    arguments->scalarOutput[0] = self->gpu_->scratchWrite(self, &readback);
    arguments->scalarOutput[1] = readback;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::scratchRestore(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t readback = 0;
    arguments->scalarOutput[0] = self->gpu_->scratchRestore(self, &readback);
    arguments->scalarOutput[1] = readback;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
                                              IOExternalMethodDispatch *, OSObject *, void *reference)
{
    // Fixed scalar counts; IOUserClient::externalMethod rejects any mismatch.
    static IOExternalMethodDispatch methods[cezanne::kDiagnosticSelectorCount] = {
        {getInfo, 0, 0, 2, 0},        // kDiagnosticGetInfo
        {readRegister, 1, 0, 2, 0},   // kDiagnosticReadRegister
        {scratchCheck, 0, 0, 7, 0},   // kDiagnosticScratchCheck
        {scratchWrite, 0, 0, 2, 0},   // kDiagnosticScratchWrite
        {scratchRestore, 0, 0, 2, 0}, // kDiagnosticScratchRestore
    };
    if (selector >= cezanne::kDiagnosticSelectorCount) {
        return kIOReturnUnsupported;
    }
    return IOUserClient::externalMethod(selector, arguments, &methods[selector], this, reference);
}

IOReturn CezanneGPU::newUserClient(task_t owningTask, void *securityID, UInt32 type, OSDictionary *properties,
                                   IOUserClient **handler)
{
    *handler = nullptr;
    if (!diagnosticsReady_ || stage_ < cezanne::kDiagnosticStage || type != 0) {
        return kIOReturnUnsupported;
    }
    if (IOUserClient::clientHasPrivilege(securityID, kIOClientPrivilegeAdministrator) != kIOReturnSuccess) {
        return kIOReturnNotPrivileged;
    }
    CezanneGPUUserClient *client = OSTypeAlloc(CezanneGPUUserClient);
    if (client == nullptr) {
        return kIOReturnNoMemory;
    }
    if (!client->initWithTask(owningTask, securityID, type, properties) || !client->attach(this)) {
        client->release();
        return kIOReturnError;
    }
    if (!client->start(this)) {
        client->detach(this);
        client->release();
        return kIOReturnError;
    }
    *handler = client;
    return kIOReturnSuccess;
}

void CezanneGPU::stop(IOService *provider)
{
    IOLog(LOG_PREFIX "stopping\n");
    IOService::stop(provider);
}
