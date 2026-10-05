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
//   Stage 7: the first SMU messages. On request, after a check that the MP1
//            mailbox is idle, send GetDriverIfVersion and GetSmuVersion as
//            Linux does first, writing only the allowlisted mailbox values.
//            Only the 4 KiB BAR5 page holding the mailbox is mapped writable,
//            and only during a query.
//   Stage 8: on request, after the same mailbox check, send DisallowGfxOff
//            and wait for SMUIO to report GFX on, as Linux does.
//   Stage 9: on request, in three ordered steps, check one fixed carveout
//            page is not being written (two reads ~1 s apart) and snapshot
//            it, give the SMU its GPU address and ask it to write its metrics
//            table there, then read the page back through a read-only mapping
//            and verify only the table bytes changed.
//            Apart from the stage 6 to 9 tests, nothing is written to
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

// The writable 4 KiB BAR5 page of a stage 6 or 7 test: SCRATCH_REG0's or the
// SMU mailbox's.
struct WritePage {
    volatile UInt32 *base;
    uint32_t pageOffset;
    UInt32 stage;
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
    cezanne::Status smuCheck(const void *owner, cezanne::SmuMailbox *mailbox);
    cezanne::Status smuQuery(const void *owner, uint32_t message, uint32_t *response, uint32_t *answer);
    cezanne::Status gfxOffDisallow(const void *owner, uint32_t *response, uint32_t *gfxMisc);
    cezanne::Status metricsCheck(const void *owner, cezanne::MetricsTarget *target);
    cezanne::Status metricsTransfer(const void *owner, uint32_t responses[3]);
    cezanne::Status metricsRead(const void *owner, cezanne::SmuMetrics *metrics);

private:
    enum ScratchState { kScratchIdle, kScratchChecked, kScratchWritten };
    ScratchState scratchState_ = kScratchIdle;
    const void *scratchOwner_ = nullptr;
    uint32_t scratchOriginal_ = 0;
    bool smuChecked_ = false;
    const void *smuOwner_ = nullptr;
    enum MetricsState { kMetricsIdle, kMetricsChecked, kMetricsTransferred };
    MetricsState metricsState_ = kMetricsIdle;
    const void *metricsOwner_ = nullptr;
    // The check region as read before the transfer; too large for the stack.
    uint32_t metricsSnapshot_[cezanne::kMetricsCheckSize / 4];
    // writablePage: 0 for none, else kScratchPageOffset or kSmuPageOffset.
    cezanne::Status accessDevice(uint32_t writablePage, DeviceOperation operation, void *argument);
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
    if (!cezanne::writeAllowed(offset, value, page->stage) || offset < page->pageOffset ||
        offset + 4ull > uint64_t(page->pageOffset) + cezanne::kPageSize) {
        return false;
    }
    page->base[(offset - page->pageOffset) / 4] = value;
    return true;
}

static bool refuseWrite(void *, uint32_t, uint32_t)
{
    return false;
}

static void pauseOneMillisecond(void *)
{
    // A sleeping wait: the stage 9 stability check pauses for about 1 s.
    IOSleep(1);
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

cezanne::Status CezanneGPU::accessDevice(uint32_t writablePage, DeviceOperation operation, void *argument)
{
    // Caller holds lock_.
    if (writablePage != 0 && writablePage != cezanne::kScratchPageOffset && writablePage != cezanne::kSmuPageOffset) {
        return cezanne::kRegisterNotAllowed;
    }
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
    WritePage page = {nullptr, writablePage, stage_};
    if (status == cezanne::kOK && writablePage != 0) {
        // The only writable mapping: one page of BAR5, for a stage 6 or 7 test.
        pageMemory = IODeviceMemory::withRange((state.bar5 & ~0xFull) + writablePage, cezanne::kPageSize);
        pageMap = pageMemory != nullptr ? pageMemory->map(kIOMapInhibitCache) : nullptr;
        if (pageMap == nullptr || pageMap->getLength() < cezanne::kPageSize) {
            status = cezanne::kApertureUnavailable;
        } else {
            page.base = reinterpret_cast<volatile UInt32 *>(pageMap->getVirtualAddress());
        }
    }
    if (status == cezanne::kOK) {
        cezanne::RegisterReader registers = {registerRead, &aperture};
        cezanne::RegisterWriter writer = {writablePage != 0 ? registerWrite : refuseWrite, pauseOneMillisecond, &page};
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
    cezanne::Status status = accessDevice(0, readOperation, &read);
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
        status = accessDevice(0, scratchCheckOperation, &scratch);
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
        status = accessDevice(cezanne::kScratchPageOffset, scratchWriteOperation, &scratch);
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
    cezanne::Status status = accessDevice(cezanne::kScratchPageOffset, scratchRestoreOperation, &scratch);
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
        status = accessDevice(cezanne::kScratchPageOffset, scratchRestoreOperation, &scratch);
        scratchState_ = kScratchIdle;
    }
    IOLockUnlock(lock_);
    return status;
}

struct SmuArgument {
    uint32_t message;
    uint32_t *response;
    uint32_t *answer;
    cezanne::SmuMailbox *mailbox;
};

static cezanne::Status smuCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                         const cezanne::RegisterWriter *, void *argument)
{
    return cezanne::checkSmu(registers, length, stage, static_cast<SmuArgument *>(argument)->mailbox);
}

static cezanne::Status smuQueryOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                         const cezanne::RegisterWriter *writer, void *argument)
{
    SmuArgument *smu = static_cast<SmuArgument *>(argument);
    return cezanne::sendSmuQuery(registers, length, *writer, stage, smu->message, smu->response, smu->answer);
}

static cezanne::Status gfxOffOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                       const cezanne::RegisterWriter *writer, void *argument)
{
    SmuArgument *smu = static_cast<SmuArgument *>(argument);
    return cezanne::disallowGfxOff(registers, length, *writer, stage, smu->response, smu->answer);
}

cezanne::Status CezanneGPU::smuCheck(const void *owner, cezanne::SmuMailbox *mailbox)
{
    *mailbox = cezanne::SmuMailbox();
    if (!diagnosticsReady_ || stage_ < cezanne::kSmuStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    SmuArgument smu = {0, nullptr, nullptr, mailbox};
    cezanne::Status status = accessDevice(0, smuCheckOperation, &smu);
    smuChecked_ = status == cezanne::kOK;
    smuOwner_ = owner;
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::smuQuery(const void *owner, uint32_t message, uint32_t *response, uint32_t *answer)
{
    *response = 0;
    *answer = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kSmuOutOfOrder;
    // A query follows a passing check by the same connection; any failure
    // ends the sequence, so nothing is retried without a new check.
    if (smuChecked_ && smuOwner_ == owner) {
        SmuArgument smu = {message, response, answer, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, smuQueryOperation, &smu);
        smuChecked_ = status == cezanne::kOK;
        IOLog(LOG_PREFIX "SMU query 0x%x: %s, response 0x%x, answer 0x%08x\n", message, cezanne::statusName(status),
              *response, *answer);
    }
    IOLockUnlock(lock_);
    return status;
}

// Reads a read-only mapping of carveout memory (stage 9).
struct MemoryWindow {
    const volatile UInt32 *base;
    UInt64 length;
};

static bool memoryRead(void *context, uint32_t offset, uint32_t *value)
{
    const MemoryWindow *window = static_cast<const MemoryWindow *>(context);
    if ((offset & 3) != 0 || offset + 4ull > window->length) {
        return false;
    }
    *value = window->base[offset / 4];
    return true;
}

// Maps length bytes of the metrics page's physical range read-only and
// uncached, runs check, and releases the mapping.
template <typename Check> static cezanne::Status withMetricsMemory(UInt32 length, Check check)
{
    IODeviceMemory *memory = IODeviceMemory::withRange(cezanne::kMetricsPhysical, length);
    if (memory == nullptr) {
        return cezanne::kTableNotWritten;
    }
    IOMemoryMap *map = memory->map(kIOMapReadOnly | kIOMapInhibitCache);
    cezanne::Status status = cezanne::kApertureUnavailable;
    if (map != nullptr && map->getLength() >= length) {
        MemoryWindow window = {reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress()), length};
        cezanne::MemoryReader reader = {memoryRead, &window};
        status = check(reader);
    }
    if (map != nullptr) {
        map->release();
    }
    memory->release();
    return status;
}

struct MetricsArgument {
    const cezanne::Range *ranges;
    uint32_t rangeCount;
    cezanne::MetricsTarget *target;
    uint32_t *responses;
    cezanne::SmuMetrics *metrics;
    uint32_t *snapshot;
};

static cezanne::Status metricsCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                             const cezanne::RegisterWriter *writer, void *argument)
{
    MetricsArgument *metrics = static_cast<MetricsArgument *>(argument);
    cezanne::SmuMailbox mailbox;
    cezanne::Status status = cezanne::checkSmu(registers, length, stage, &mailbox);
    if (status == cezanne::kOK) {
        status = cezanne::checkMetricsTarget(registers, length, stage, metrics->ranges, metrics->rangeCount,
                                             metrics->target);
    }
    if (status == cezanne::kOK) {
        uint32_t *snapshot = metrics->snapshot;
        status = withMetricsMemory(cezanne::kMetricsCheckSize, [writer, snapshot](const cezanne::MemoryReader &memory) {
            return cezanne::checkRegionStable(memory, cezanne::kMetricsCheckSize, *writer,
                                              cezanne::kMetricsStablePauses, snapshot);
        });
    }
    return status;
}

static cezanne::Status metricsTransferOperation(UInt32 stage, const cezanne::RegisterReader &registers,
                                                UInt64 length, const cezanne::RegisterWriter *writer, void *argument)
{
    return cezanne::requestMetrics(registers, length, *writer, stage, static_cast<MetricsArgument *>(argument)->responses);
}

static cezanne::Status metricsReadOperation(UInt32, const cezanne::RegisterReader &, UInt64,
                                            const cezanne::RegisterWriter *, void *argument)
{
    MetricsArgument *read = static_cast<MetricsArgument *>(argument);
    cezanne::SmuMetrics *metrics = read->metrics;
    const uint32_t *snapshot = read->snapshot;
    return withMetricsMemory(cezanne::kPageSize, [metrics, snapshot](const cezanne::MemoryReader &page) {
        return cezanne::verifyMetricsPage(page, snapshot, metrics);
    });
}

cezanne::Status CezanneGPU::metricsCheck(const void *owner, cezanne::MetricsTarget *target)
{
    *target = cezanne::MetricsTarget();
    if (!diagnosticsReady_ || stage_ < cezanne::kMetricsStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, getProvider());
    OSData *assigned = pci != nullptr ? OSDynamicCast(OSData, pci->getProperty("assigned-addresses")) : nullptr;
    cezanne::Range ranges[8];
    uint32_t count = 0;
    cezanne::Status status = cezanne::parseAssignedAddresses(
        assigned != nullptr ? static_cast<const uint8_t *>(assigned->getBytesNoCopy()) : nullptr,
        assigned != nullptr ? assigned->getLength() : 0, ranges, 8, &count);
    if (status != cezanne::kOK) {
        return status;
    }
    IOLockLock(lock_);
    MetricsArgument metrics = {ranges, count, target, nullptr, nullptr, metricsSnapshot_};
    status = accessDevice(0, metricsCheckOperation, &metrics);
    metricsState_ = status == cezanne::kOK ? kMetricsChecked : kMetricsIdle;
    metricsOwner_ = owner;
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::metricsTransfer(const void *owner, uint32_t responses[3])
{
    responses[0] = responses[1] = responses[2] = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kMetricsOutOfOrder;
    if (metricsState_ == kMetricsChecked && metricsOwner_ == owner) {
        MetricsArgument metrics = {nullptr, 0, nullptr, responses, nullptr, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, metricsTransferOperation, &metrics);
        metricsState_ = status == cezanne::kOK ? kMetricsTransferred : kMetricsIdle;
        IOLog(LOG_PREFIX "metrics transfer: %s, responses 0x%x 0x%x 0x%x\n", cezanne::statusName(status),
              responses[0], responses[1], responses[2]);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::metricsRead(const void *owner, cezanne::SmuMetrics *metrics)
{
    *metrics = cezanne::SmuMetrics();
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kMetricsOutOfOrder;
    if (metricsState_ == kMetricsTransferred && metricsOwner_ == owner) {
        MetricsArgument argument = {nullptr, 0, nullptr, nullptr, metrics, metricsSnapshot_};
        status = accessDevice(0, metricsReadOperation, &argument);
        metricsState_ = kMetricsIdle;
        IOLog(LOG_PREFIX "metrics read: %s\n", cezanne::statusName(status));
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::gfxOffDisallow(const void *owner, uint32_t *response, uint32_t *gfxMisc)
{
    *response = 0;
    *gfxMisc = 0;
    if (stage_ < cezanne::kGfxOffStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kSmuOutOfOrder;
    // Like a query: only after a passing check by the same connection.
    if (smuChecked_ && smuOwner_ == owner) {
        SmuArgument smu = {cezanne::kSmuMsgDisableGfxOff, response, gfxMisc, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, gfxOffOperation, &smu);
        smuChecked_ = status == cezanne::kOK;
        IOLog(LOG_PREFIX "DisallowGfxOff: %s, response 0x%x, SMUIO_GFX_MISC_CNTL 0x%08x\n",
              cezanne::statusName(status), *response, *gfxMisc);
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
    if (smuOwner_ == owner) {
        smuChecked_ = false;
        smuOwner_ = nullptr;
    }
    if (metricsOwner_ == owner) {
        metricsState_ = kMetricsIdle;
        metricsOwner_ = nullptr;
    }
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
    static IOReturn smuCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn smuQuery(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn gfxOffDisallow(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn metricsCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn metricsTransfer(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn metricsRead(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
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

IOReturn CezanneGPUUserClient::smuCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::SmuMailbox mailbox;
    arguments->scalarOutput[0] = self->gpu_->smuCheck(self, &mailbox);
    arguments->scalarOutput[1] = mailbox.message;
    arguments->scalarOutput[2] = mailbox.argument;
    arguments->scalarOutput[3] = mailbox.response;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::smuQuery(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint64_t message = arguments->scalarInput[0];
    if (message != cezanne::kSmuMsgGetSmuVersion && message != cezanne::kSmuMsgGetDriverIfVersion) {
        return kIOReturnBadArgument;
    }
    uint32_t response = 0, answer = 0;
    arguments->scalarOutput[0] = self->gpu_->smuQuery(self, static_cast<uint32_t>(message), &response, &answer);
    arguments->scalarOutput[1] = response;
    arguments->scalarOutput[2] = answer;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::gfxOffDisallow(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t response = 0, gfxMisc = 0;
    arguments->scalarOutput[0] = self->gpu_->gfxOffDisallow(self, &response, &gfxMisc);
    arguments->scalarOutput[1] = response;
    arguments->scalarOutput[2] = gfxMisc;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::metricsCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::MetricsTarget metrics;
    arguments->scalarOutput[0] = self->gpu_->metricsCheck(self, &metrics);
    arguments->scalarOutput[1] = metrics.fbLocationBase;
    arguments->scalarOutput[2] = metrics.fbOffset;
    arguments->scalarOutput[3] = metrics.gpuAddress;
    arguments->scalarOutput[4] = metrics.physical;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::metricsTransfer(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t responses[3];
    arguments->scalarOutput[0] = self->gpu_->metricsTransfer(self, responses);
    for (uint32_t i = 0; i < 3; i++) {
        arguments->scalarOutput[i + 1] = responses[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::metricsRead(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::SmuMetrics metrics;
    arguments->scalarOutput[0] = self->gpu_->metricsRead(self, &metrics);
    // The dispatch table fixes the structure size at sizeof(SmuMetrics).
    UInt8 *out = static_cast<UInt8 *>(arguments->structureOutput);
    const UInt8 *in = reinterpret_cast<const UInt8 *>(metrics.words);
    for (uint32_t i = 0; i < sizeof(metrics); i++) {
        out[i] = in[i];
    }
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
        {smuCheck, 0, 0, 4, 0},       // kDiagnosticSmuCheck
        {smuQuery, 1, 0, 3, 0},       // kDiagnosticSmuQuery
        {gfxOffDisallow, 0, 0, 3, 0}, // kDiagnosticGfxOffDisallow
        {metricsCheck, 0, 0, 5, 0},    // kDiagnosticMetricsCheck
        {metricsTransfer, 0, 0, 4, 0}, // kDiagnosticMetricsTransfer
        {metricsRead, 0, 0, 1, sizeof(cezanne::SmuMetrics)}, // kDiagnosticMetricsRead
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
