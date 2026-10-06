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
//   Stage 10: 21 more read-only diagnostic registers (PSP mailbox, memory
//            hub and GC hub apertures); nothing else changes.
//   Stage 11: on request, in ordered steps, check the PSP is ready with no
//            ring and one fixed carveout page is not being written, create
//            the PSP kernel-mode ring at that page, compare the page region
//            with its snapshot, and destroy the ring (also on an abandoned
//            connection after any create attempt). No frame is submitted.
//   Stage 12: after the stage 11 check (which then also checks the 4 MiB TMR
//            region) and create, write a SETUP_TMR command, fence and ring
//            frame into the three-page work area (the only writable memory
//            mapping), advance the write pointer and wait for the fence;
//            compare the 64 KiB around it with the snapshot; then send
//            DESTROY_TMR the same way and destroy the ring (also on an
//            abandoned connection after the submit).
//   Stage 13: after the stage 12 SETUP_TMR, copy the embedded SDMA0 image
//            into a five-page firmware buffer (the second writable memory
//            mapping), send LOAD_IP_FW as frame 1, compare both regions and
//            check SDMA0 is still halted; DESTROY_TMR then moves to frame 2.
//   Stage 14: after that load, read 31 SDMA registers, send PowerUpSdma,
//            read them, send PowerDownSdma, read them again. No SDMA
//            register is written.
//   Stage 15: after that load, check SDMA0 still holds its boot 19 values,
//            write the copy work area (the third writable memory mapping),
//            power SDMA up, apply the golden settings and the start writes
//            (exact values), run the ring test and one 4 KiB copy with a
//            fence, verify, then halt, power down and tear down (also on an
//            abandoned connection after the start).
//   Stage 17: after a verified stage 15 copy, check the GART and IH
//            registers and display pipe 0 hold their boot 22 values, write
//            the GART work area (the fourth writable memory mapping) and
//            frame 2, enable MMHUB VM context 0 and IH ring 0 with exact
//            values and an engine 17 flush, copy through the GART with a
//            fence and a TRAP, verify, and restore every register (also
//            before the stage 15 stop, and on an abandoned connection).
//            Apart from the stage 6 to 17 tests, nothing is written to
//            configuration space, registers or memory, and every mapping and
//            the provider are released before start() returns.
//
// Hardware rules live in driver/core; this file only adapts IOKit to them.

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/clock.h>
#include <libkern/OSAtomic.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <pexpert/pexpert.h>

#include "cezanne_core.h"

#define LOG_PREFIX "CezanneGPU: "

// The pinned SDMA0 image, generated by build.sh into the build directory.
extern const uint8_t kCezanneSdmaImage[];
extern const uint32_t kCezanneSdmaImageLength;

struct Aperture {
    const volatile UInt32 *base;
    UInt64 length;
    UInt32 stage;
    bool semaphore; // the stage 17 flush may read VM_INVALIDATE_ENG17_SEM
};

// Up to three writable BAR5 pages: one for a stage 6 to 14 test, the
// kSdmaPages set for stage 15, the kGartPages set for stage 17, the
// kIntrPages set (two) for stage 18.
struct WritePage {
    volatile UInt32 *base[3];
    uint32_t pageOffset[3];
    uint32_t count;
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
    cezanne::Status pspRingCheck(const void *owner, cezanne::PspMailbox *mailbox);
    cezanne::Status pspRingCreate(const void *owner, uint32_t *response, bool *written);
    cezanne::Status pspRingObserve(const void *owner, cezanne::PspMailbox *mailbox, uint32_t *changedInPage,
                                   uint32_t *changedOutside);
    cezanne::Status pspRingDestroy(const void *owner, uint32_t *response, cezanne::PspMailbox *mailbox);
    cezanne::Status pspTmrSubmit(const void *owner, uint32_t *fence, cezanne::PspResponse *response,
                                 uint32_t *writePointer);
    cezanne::Status pspTmrObserve(const void *owner, uint32_t *unexpected, uint32_t *firstOffset);
    cezanne::Status pspTmrTeardown(const void *owner, uint32_t *fence, uint32_t *tmrStatus, uint32_t *ringResponse,
                                   cezanne::PspMailbox *mailbox);
    cezanne::Status sdmaLoad(const void *owner, uint32_t *fence, cezanne::PspResponse *response,
                             uint32_t *writePointer);
    cezanne::Status sdmaObserve(const void *owner, uint32_t *workUnexpected, uint32_t *workFirst,
                                uint32_t *firmwareUnexpected, uint32_t *firmwareFirst, uint32_t *checksum,
                                uint32_t *f32Cntl);
    cezanne::Status sdmaInventory(const void *owner, cezanne::SdmaInventory *inventory, uint32_t *upResponse,
                                  uint32_t *downResponse);
    cezanne::Status sdmaCopyCheck(const void *owner, uint32_t *index, uint32_t *value);
    cezanne::Status sdmaStart(const void *owner, uint32_t *progress, uint32_t *upResponse);
    cezanne::Status sdmaSubmit(const void *owner, uint32_t frame, uint32_t *observed, uint32_t *rptr, uint32_t *wptr,
                               uint32_t *f32Cntl, uint32_t *status);
    cezanne::Status sdmaVerify(const void *owner, uint32_t *rptr, uint32_t *unexpected, uint32_t *firstOffset,
                               uint32_t *status);
    cezanne::Status sdmaStop(const void *owner, uint32_t *f32Cntl, uint32_t *downResponse, uint32_t *fence,
                             uint32_t *ringResponse);
    cezanne::Status gartCheck(const void *owner, uint32_t *index, uint32_t *value);
    cezanne::Status gartEnable(const void *owner, uint32_t *progress, uint32_t *ack);
    cezanne::Status gartVerify(const void *owner, cezanne::GartReport *report);
    cezanne::Status gartRestore(const void *owner, uint32_t *index, uint32_t *value, uint32_t *ihWptr);
    // Stage 18. msi: the MSI capability's control, address low and high, data.
    cezanne::Status intrCheck(const void *owner, uint32_t *index, uint32_t *value, uint32_t *msiIndex, uint32_t msi[4]);
    cezanne::Status intrEnable(const void *owner, uint32_t *progress, uint32_t msi[4]);
    cezanne::Status intrVerify(const void *owner, cezanne::IntrReport *report);
    cezanne::Status intrAck(const void *owner, uint32_t *rptr, uint32_t *countBefore, uint32_t *countAfter,
                            uint32_t *writeback);
    cezanne::Status intrRestore(const void *owner, uint32_t *index, uint32_t *value, uint32_t msi[4]);

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
    // Stage 11: a ring may exist from the create command's write until the
    // destroy, whatever the create's response.
    enum PspState {
        kPspIdle,
        kPspChecked,
        kPspCreated,
        kPspObserved,
        kPspTmrSubmitted,
        kPspTmrObserved,
        kPspSdmaLoaded,
        kPspSdmaObserved,
    };
    PspState pspState_ = kPspIdle;
    bool pspCreateOk_ = false;   // the create was answered with status 0
    bool pspTmrOk_ = false;      // SETUP_TMR fenced with status 0
    uint32_t pspFrames_ = 0;     // frames whose write pointer was written
    bool pspLastFenced_ = false; // the last of them fenced
    uint32_t sdmaSnapshot_[cezanne::kSdmaFwCheckSize / 4];
    // Stage 15: the copy's steps on this connection, and how far the start
    // got (0 nothing, 1 powered up, 2 registers written).
    enum CopyState { kCopyIdle, kCopyChecked, kCopyStarted, kCopyTested, kCopySubmitted };
    CopyState copyState_ = kCopyIdle;
    uint32_t sdmaProgress_ = 0;
    uint32_t sdmaWorkSnapshot_[cezanne::kSdmaWorkCheckSize / 4];
    bool copyVerified_ = false; // the stage 15 verify passed
    // Stage 17: the steps on this connection, and how far the enable got (0
    // nothing, 1 GART registers, 2 IH registers, 3 SDMA0_CNTL); non-zero
    // until the restore ran.
    enum GartState {
        kGartIdle,
        kGartChecked,
        kGartEnabled,   // the enable passed
        kGartFailed,    // the enable failed after any write
        kGartSubmitted, // frame 2 fenced
        kGartVerified,
        kGartRestored,
    };
    GartState gartState_ = kGartIdle;
    bool gartVerified_ = false; // the stage 17 verify passed
    uint32_t gartProgress_ = 0;
    uint32_t gartSnapshot_[cezanne::kGartWorkCheckSize / 4];
    uint32_t gartDisplay_[cezanne::kDisplayInventoryCount];
    cezanne::Status gartRestoreLocked(uint32_t *index, uint32_t *value, uint32_t *ihWptr);
    // Stage 18: the steps on this connection; how far the enable got (0
    // nothing, 1 IH armed and INTERRUPT_CNTL2 written, 2 ENABLE_INTR); the
    // MSI handler while registered. msiCount_ and the times are written only
    // by intrFilter, in primary interrupt context.
    enum IntrState {
        kIntrIdle,
        kIntrChecked,
        kIntrEnabled,   // the enable passed
        kIntrFailed,    // the enable failed after any write or registration
        kIntrSubmitted, // frame 3 fenced
        kIntrVerified,
        kIntrRestored,
    };
    IntrState intrState_ = kIntrIdle;
    bool intrVerified_ = false; // the stage 18 verify passed
    uint32_t intrProgress_ = 0;
    int msiIndex_ = -1;
    IOWorkLoop *intrLoop_ = nullptr;
    IOFilterInterruptEventSource *intrSource_ = nullptr;
    SInt32 msiCount_ = 0;
    uint32_t msiAtSubmit_ = 0;                 // the count when frame 3 was submitted
    UInt64 intrEnableTime_ = 0, intrSubmitTime_ = 0; // before the ENABLE_INTR write and the frame 3 submit
    UInt64 msiTimes_[cezanne::kIntrTimes] = {};   // of the first MSIs, written by intrFilter
    uint32_t sinceEnable(UInt64 time) const;
    static bool intrFilter(OSObject *owner, IOFilterInterruptEventSource *source);
    static void intrAction(OSObject *owner, IOInterruptEventSource *source, int count);
    static uint32_t intrCount(void *context);
    cezanne::Status addIntrSourceLocked();
    void removeIntrSourceLocked();
    void readMsiLocked(uint32_t msi[4]);
    cezanne::Status intrRestoreLocked(uint32_t *index, uint32_t *value, uint32_t msi[4]);
    cezanne::Status sdmaStopLocked(uint32_t *f32Cntl, uint32_t *downResponse, uint32_t *fence, uint32_t *ringResponse);
    cezanne::Status pspTeardownLocked(uint32_t *fence, uint32_t *tmrStatus, uint32_t *ringResponse,
                                      cezanne::PspMailbox *mailbox);
    const void *pspOwner_ = nullptr;
    uint32_t pspSnapshot_[cezanne::kPspRingCheckSize / 4];
    cezanne::Status pspDestroyLocked(uint32_t *response, cezanne::PspMailbox *mailbox);
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
    // The core already checks this; the adapter refuses independently. The
    // semaphore only through a stage 17 page-set access (its flush).
    bool allowed = cezanne::registerAllowed(offset, aperture->stage) ||
                   (aperture->semaphore && cezanne::semaphoreReadAllowed(offset, aperture->stage));
    if (!allowed || (offset & 3) != 0 || offset + 4ull > aperture->length) {
        return false;
    }
    *value = aperture->base[offset / 4];
    return true;
}

static bool registerWrite(void *context, uint32_t offset, uint32_t value)
{
    WritePage *page = static_cast<WritePage *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::writeAllowed(offset, value, page->stage)) {
        return false;
    }
    for (uint32_t i = 0; i < page->count; i++) {
        if (offset >= page->pageOffset[i] && offset + 4ull <= uint64_t(page->pageOffset[i]) + cezanne::kPageSize) {
            page->base[i][(offset - page->pageOffset[i]) / 4] = value;
            return true;
        }
    }
    return false;
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
                         stage_, false};
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
    if (writablePage != 0 && writablePage != cezanne::kScratchPageOffset && writablePage != cezanne::kSmuPageOffset &&
        writablePage != cezanne::kSdmaPageSet && writablePage != cezanne::kGartPageSet &&
        writablePage != cezanne::kIntrPageSet) {
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
    Aperture aperture = {nullptr, 0, stage_, writablePage == cezanne::kGartPageSet};
    if (status == cezanne::kOK) {
        aperture.base = reinterpret_cast<const volatile UInt32 *>(map->getVirtualAddress());
        aperture.length = map->getLength();
        status = cezanne::checkAperture(state, map->getPhysicalAddress(), aperture.length);
    }
    IODeviceMemory *pageMemories[3] = {nullptr, nullptr, nullptr};
    IOMemoryMap *pageMaps[3] = {nullptr, nullptr, nullptr};
    WritePage page = {{nullptr, nullptr, nullptr}, {0, 0, 0}, 0, stage_};
    if (writablePage == cezanne::kSdmaPageSet || writablePage == cezanne::kGartPageSet) {
        page.count = 3;
        for (uint32_t i = 0; i < 3; i++) {
            page.pageOffset[i] =
                writablePage == cezanne::kSdmaPageSet ? cezanne::kSdmaPages[i] : cezanne::kGartPages[i];
        }
    } else if (writablePage == cezanne::kIntrPageSet) {
        page.count = 2;
        for (uint32_t i = 0; i < 2; i++) {
            page.pageOffset[i] = cezanne::kIntrPages[i];
        }
    } else if (writablePage != 0) {
        page.count = 1;
        page.pageOffset[0] = writablePage;
    }
    for (uint32_t i = 0; status == cezanne::kOK && i < page.count; i++) {
        // The only writable register mappings: these pages of BAR5.
        IODeviceMemory *pageMemory =
            IODeviceMemory::withRange((state.bar5 & ~0xFull) + page.pageOffset[i], cezanne::kPageSize);
        pageMemories[i] = pageMemory;
        IOMemoryMap *pageMap = pageMemory != nullptr ? pageMemory->map(kIOMapInhibitCache) : nullptr;
        pageMaps[i] = pageMap;
        if (pageMap == nullptr || pageMap->getLength() < cezanne::kPageSize) {
            status = cezanne::kApertureUnavailable;
        } else {
            page.base[i] = reinterpret_cast<volatile UInt32 *>(pageMap->getVirtualAddress());
        }
    }
    if (status == cezanne::kOK) {
        cezanne::RegisterReader registers = {registerRead, &aperture};
        cezanne::RegisterWriter writer = {writablePage != 0 ? registerWrite : refuseWrite, pauseOneMillisecond, &page};
        status = operation(stage_, registers, aperture.length, &writer, argument);
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (pageMaps[i] != nullptr) {
            pageMaps[i]->release();
        }
        if (pageMemories[i] != nullptr) {
            pageMemories[i]->release();
        }
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

// Maps length bytes of a fixed carveout page's physical range (the stage 9
// metrics page or the stage 11 ring page) read-only and uncached, runs check,
// and releases the mapping.
template <typename Check> static cezanne::Status withCarveoutMemory(UInt64 physical, UInt32 length, Check check)
{
    if (physical != cezanne::kMetricsPhysical && physical != cezanne::kPspRingPhysical &&
        physical != cezanne::kPspTmrPhysical && physical != cezanne::kSdmaFwPhysical &&
        physical != cezanne::kSdmaWorkPhysical && physical != cezanne::kGartWorkPhysical) {
        return cezanne::kRegisterNotAllowed;
    }
    IODeviceMemory *memory = IODeviceMemory::withRange(physical, length);
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

// The stage 12 work area: the ring, command and fence pages, the only
// carveout memory the driver writes.
struct WorkWindow {
    volatile UInt32 *base;
    UInt32 stage;
};

static bool workRead(void *context, uint32_t offset, uint32_t *value)
{
    const WorkWindow *work = static_cast<const WorkWindow *>(context);
    if ((offset & 3) != 0 || offset + 4ull > cezanne::kPspWorkSize) {
        return false;
    }
    *value = work->base[offset / 4];
    return true;
}

static bool workWrite(void *context, uint32_t offset, uint32_t value)
{
    WorkWindow *work = static_cast<WorkWindow *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::pspWorkWriteAllowed(offset, value, work->stage)) {
        return false;
    }
    work->base[offset / 4] = value;
    return true;
}

// Maps the work area writable and uncached, runs use, and releases it.
template <typename Use> static cezanne::Status withPspWork(UInt32 stage, Use use)
{
    if (stage < cezanne::kPspTmrStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IODeviceMemory *workMemory = IODeviceMemory::withRange(cezanne::kPspRingPhysical, cezanne::kPspWorkSize);
    if (workMemory == nullptr) {
        return cezanne::kApertureUnavailable;
    }
    IOMemoryMap *workMap = workMemory->map(kIOMapInhibitCache);
    cezanne::Status status = cezanne::kApertureUnavailable;
    if (workMap != nullptr && workMap->getLength() >= cezanne::kPspWorkSize) {
        WorkWindow window = {reinterpret_cast<volatile UInt32 *>(workMap->getVirtualAddress()), stage};
        cezanne::MemoryReader reader = {workRead, &window};
        cezanne::MemoryWriter writer = {workWrite, &window};
        status = use(reader, writer);
    }
    if (workMap != nullptr) {
        workMap->release();
    }
    workMemory->release();
    return status;
}

// The stage 13 firmware buffer: the second carveout region the driver
// writes, only with the embedded image.
struct FirmwareWindow {
    volatile UInt32 *base;
    UInt32 stage;
};

static bool firmwareRead(void *context, uint32_t offset, uint32_t *value)
{
    const FirmwareWindow *firmware = static_cast<const FirmwareWindow *>(context);
    if ((offset & 3) != 0 || offset + 4ull > cezanne::kSdmaFwBufferSize) {
        return false;
    }
    *value = firmware->base[offset / 4];
    return true;
}

static bool firmwareWrite(void *context, uint32_t offset, uint32_t value)
{
    FirmwareWindow *firmware = static_cast<FirmwareWindow *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::sdmaFirmwareWriteAllowed(kCezanneSdmaImage, offset, value, firmware->stage)) {
        return false;
    }
    firmware->base[offset / 4] = value;
    return true;
}

// Maps the firmware buffer writable and uncached, runs use, and releases it.
template <typename Use> static cezanne::Status withSdmaFirmware(UInt32 stage, Use use)
{
    if (stage < cezanne::kPspSdmaStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IODeviceMemory *firmwareMemory = IODeviceMemory::withRange(cezanne::kSdmaFwPhysical, cezanne::kSdmaFwBufferSize);
    if (firmwareMemory == nullptr) {
        return cezanne::kApertureUnavailable;
    }
    IOMemoryMap *firmwareMap = firmwareMemory->map(kIOMapInhibitCache);
    cezanne::Status status = cezanne::kApertureUnavailable;
    if (firmwareMap != nullptr && firmwareMap->getLength() >= cezanne::kSdmaFwBufferSize) {
        FirmwareWindow window = {reinterpret_cast<volatile UInt32 *>(firmwareMap->getVirtualAddress()), stage};
        cezanne::MemoryReader reader = {firmwareRead, &window};
        cezanne::MemoryWriter writer = {firmwareWrite, &window};
        status = use(reader, writer);
    }
    if (firmwareMap != nullptr) {
        firmwareMap->release();
    }
    firmwareMemory->release();
    return status;
}

// The stage 15 copy work area: ring, write-back, source and destination;
// from stage 17 also the second destination.
struct SdmaWorkWindow {
    volatile UInt32 *base;
    UInt32 stage;
    UInt32 length;
};

static bool sdmaWorkRead(void *context, uint32_t offset, uint32_t *value)
{
    const SdmaWorkWindow *sdmaWork = static_cast<const SdmaWorkWindow *>(context);
    if ((offset & 3) != 0 || offset + 4ull > sdmaWork->length) {
        return false;
    }
    *value = sdmaWork->base[offset / 4];
    return true;
}

static bool sdmaWorkWrite(void *context, uint32_t offset, uint32_t value)
{
    SdmaWorkWindow *sdmaWork = static_cast<SdmaWorkWindow *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::sdmaWorkWriteAllowed(offset, value, sdmaWork->stage)) {
        return false;
    }
    sdmaWork->base[offset / 4] = value;
    return true;
}

// Maps the copy work area writable and uncached, runs use, and releases it.
template <typename Use> static cezanne::Status withSdmaWork(UInt32 stage, Use use)
{
    if (stage < cezanne::kSdmaCopyStage) {
        return cezanne::kRegisterNotAllowed;
    }
    const UInt32 length = stage >= cezanne::kGartStage ? cezanne::kGartSdmaWorkSize : cezanne::kSdmaWorkSize;
    IODeviceMemory *sdmaWorkMemory = IODeviceMemory::withRange(cezanne::kSdmaWorkPhysical, length);
    if (sdmaWorkMemory == nullptr) {
        return cezanne::kApertureUnavailable;
    }
    IOMemoryMap *sdmaWorkMap = sdmaWorkMemory->map(kIOMapInhibitCache);
    cezanne::Status status = cezanne::kApertureUnavailable;
    if (sdmaWorkMap != nullptr && sdmaWorkMap->getLength() >= length) {
        SdmaWorkWindow window = {reinterpret_cast<volatile UInt32 *>(sdmaWorkMap->getVirtualAddress()), stage, length};
        cezanne::MemoryReader reader = {sdmaWorkRead, &window};
        cezanne::MemoryWriter writer = {sdmaWorkWrite, &window};
        status = use(reader, writer);
    }
    if (sdmaWorkMap != nullptr) {
        sdmaWorkMap->release();
    }
    sdmaWorkMemory->release();
    return status;
}

// The stage 17 GART work area: page table, dummy page, IH ring and its
// write-back; written only with gartWorkWord's words.
struct GartWorkWindow {
    volatile UInt32 *base;
    UInt32 stage;
};

static bool gartWorkRead(void *context, uint32_t offset, uint32_t *value)
{
    const GartWorkWindow *gartWork = static_cast<const GartWorkWindow *>(context);
    if ((offset & 3) != 0 || offset + 4ull > cezanne::kGartWorkSize) {
        return false;
    }
    *value = gartWork->base[offset / 4];
    return true;
}

static bool gartWorkWrite(void *context, uint32_t offset, uint32_t value)
{
    GartWorkWindow *gartWork = static_cast<GartWorkWindow *>(context);
    // The core already checks this; the adapter refuses independently.
    if (!cezanne::gartWorkWriteAllowed(offset, value, gartWork->stage)) {
        return false;
    }
    gartWork->base[offset / 4] = value;
    return true;
}

// Maps the GART work area writable and uncached, runs use, and releases it.
template <typename Use> static cezanne::Status withGartWork(UInt32 stage, Use use)
{
    if (stage < cezanne::kGartStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IODeviceMemory *gartWorkMemory = IODeviceMemory::withRange(cezanne::kGartWorkPhysical, cezanne::kGartWorkSize);
    if (gartWorkMemory == nullptr) {
        return cezanne::kApertureUnavailable;
    }
    IOMemoryMap *gartWorkMap = gartWorkMemory->map(kIOMapInhibitCache);
    cezanne::Status status = cezanne::kApertureUnavailable;
    if (gartWorkMap != nullptr && gartWorkMap->getLength() >= cezanne::kGartWorkSize) {
        GartWorkWindow window = {reinterpret_cast<volatile UInt32 *>(gartWorkMap->getVirtualAddress()), stage};
        cezanne::MemoryReader reader = {gartWorkRead, &window};
        cezanne::MemoryWriter writer = {gartWorkWrite, &window};
        status = use(reader, writer);
    }
    if (gartWorkMap != nullptr) {
        gartWorkMap->release();
    }
    gartWorkMemory->release();
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
        status = withCarveoutMemory(cezanne::kMetricsPhysical, cezanne::kMetricsCheckSize, [writer, snapshot](const cezanne::MemoryReader &memory) {
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
    return withCarveoutMemory(cezanne::kMetricsPhysical, cezanne::kPageSize, [metrics, snapshot](const cezanne::MemoryReader &page) {
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

struct PspArgument {
    const cezanne::Range *ranges;
    uint32_t rangeCount;
    cezanne::PspMailbox *mailbox;
    uint32_t *response;
    bool *written;
    uint32_t *snapshot;
    uint32_t *changedInPage, *changedOutside;
    uint32_t *sdmaSnapshot; // stage 13 check
};

static cezanne::Status pspCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                         const cezanne::RegisterWriter *writer, void *argument)
{
    PspArgument *psp = static_cast<PspArgument *>(argument);
    cezanne::MetricsTarget target;
    cezanne::Status status =
        cezanne::checkPspRing(registers, length, stage, psp->ranges, psp->rangeCount, psp->mailbox, &target);
    if (status == cezanne::kOK) {
        uint32_t *snapshot = psp->snapshot;
        status = withCarveoutMemory(cezanne::kPspRingPhysical, cezanne::kPspRingCheckSize,
                                    [writer, snapshot](const cezanne::MemoryReader &memory) {
            return cezanne::checkRegionStable(memory, cezanne::kPspRingCheckSize, *writer,
                                              cezanne::kMetricsStablePauses, snapshot);
        });
    }
    if (status == cezanne::kOK && stage >= cezanne::kPspTmrStage) {
        cezanne::MetricsTarget tmr;
        status = cezanne::checkPspTmrTarget(registers, length, stage, psp->ranges, psp->rangeCount, &tmr);
        if (status == cezanne::kOK) {
            status = withCarveoutMemory(cezanne::kPspTmrPhysical, cezanne::kPspTmrSize,
                                        [writer](const cezanne::MemoryReader &memory) {
                return cezanne::checkRegionChecksum(memory, cezanne::kPspTmrSize, *writer,
                                                    cezanne::kMetricsStablePauses);
            });
        }
    }
    if (status == cezanne::kOK && stage >= cezanne::kPspSdmaStage) {
        cezanne::MetricsTarget firmware;
        status = cezanne::checkSdmaImage(kCezanneSdmaImage, kCezanneSdmaImageLength);
        if (status == cezanne::kOK) {
            status = cezanne::checkSdmaFirmwareTarget(registers, length, stage, psp->ranges, psp->rangeCount, &firmware);
        }
        if (status == cezanne::kOK) {
            uint32_t *snapshot = psp->sdmaSnapshot;
            status = withCarveoutMemory(cezanne::kSdmaFwPhysical, cezanne::kSdmaFwCheckSize,
                                        [writer, snapshot](const cezanne::MemoryReader &memory) {
                return cezanne::checkRegionStable(memory, cezanne::kSdmaFwCheckSize, *writer,
                                                  cezanne::kMetricsStablePauses, snapshot);
            });
        }
    }
    return status;
}

static cezanne::Status pspCreateOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    PspArgument *psp = static_cast<PspArgument *>(argument);
    return cezanne::createPspRing(registers, length, *writer, stage, psp->response, psp->written);
}

static cezanne::Status pspObserveOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *, void *argument)
{
    PspArgument *psp = static_cast<PspArgument *>(argument);
    cezanne::Status status = cezanne::readPspMailbox(registers, length, stage, psp->mailbox);
    if (status == cezanne::kOK) {
        status = withCarveoutMemory(cezanne::kPspRingPhysical, cezanne::kPspRingCheckSize,
                                    [psp](const cezanne::MemoryReader &memory) {
            return cezanne::compareRegion(memory, cezanne::kPspRingCheckSize, cezanne::kPspRingSize, psp->snapshot,
                                          psp->changedInPage, psp->changedOutside);
        });
    }
    return status;
}

static cezanne::Status pspDestroyOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *writer, void *argument)
{
    PspArgument *psp = static_cast<PspArgument *>(argument);
    // Only reached with a ring created by this driver (pspState_).
    cezanne::Status status = cezanne::destroyPspRing(registers, length, *writer, stage, true, psp->response);
    cezanne::Status mailbox = cezanne::readPspMailbox(registers, length, stage, psp->mailbox);
    return status != cezanne::kOK ? status : mailbox;
}

cezanne::Status CezanneGPU::pspRingCheck(const void *owner, cezanne::PspMailbox *mailbox)
{
    *mailbox = cezanne::PspMailbox();
    if (!diagnosticsReady_ || stage_ < cezanne::kPspRingStage) {
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
    // A ring this driver created stays tracked until it is destroyed.
    if (pspState_ != kPspIdle && pspState_ != kPspChecked) {
        status = cezanne::kPspOutOfOrder;
    } else {
        PspArgument psp = {ranges, count, mailbox, nullptr, nullptr, pspSnapshot_, nullptr, nullptr, sdmaSnapshot_};
        status = accessDevice(0, pspCheckOperation, &psp);
        pspState_ = status == cezanne::kOK ? kPspChecked : kPspIdle;
        pspOwner_ = owner;
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::pspRingCreate(const void *owner, uint32_t *response, bool *written)
{
    *response = 0;
    *written = false;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if (pspState_ == kPspChecked && pspOwner_ == owner) {
        PspArgument psp = {nullptr, 0, nullptr, response, written, nullptr, nullptr, nullptr, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, pspCreateOperation, &psp);
        // Once the command is written a ring may exist, even after an error.
        pspState_ = *written ? kPspCreated : kPspIdle;
        pspCreateOk_ = status == cezanne::kOK;
        IOLog(LOG_PREFIX "PSP ring create: %s, response 0x%08x, written %d\n", cezanne::statusName(status),
              *response, *written ? 1 : 0);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::pspRingObserve(const void *owner, cezanne::PspMailbox *mailbox, uint32_t *changedInPage,
                                           uint32_t *changedOutside)
{
    *mailbox = cezanne::PspMailbox();
    *changedInPage = *changedOutside = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if (pspState_ == kPspCreated && pspOwner_ == owner) {
        PspArgument psp = {nullptr, 0, mailbox, nullptr, nullptr, pspSnapshot_, changedInPage, changedOutside, nullptr};
        status = accessDevice(0, pspObserveOperation, &psp);
        // The ring still exists whatever the comparison found.
        pspState_ = kPspObserved;
        IOLog(LOG_PREFIX "PSP ring observe: %s, changed words %u in page, %u outside\n", cezanne::statusName(status),
              *changedInPage, *changedOutside);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::pspDestroyLocked(uint32_t *response, cezanne::PspMailbox *mailbox)
{
    // Caller holds lock_ and pspState_ shows a created ring.
    PspArgument psp = {nullptr, 0, mailbox, response, nullptr, nullptr, nullptr, nullptr, nullptr};
    cezanne::Status status = accessDevice(cezanne::kSmuPageOffset, pspDestroyOperation, &psp);
    pspState_ = kPspIdle;
    pspOwner_ = nullptr;
    pspCreateOk_ = false;
    IOLog(LOG_PREFIX "PSP ring destroy: %s, response 0x%08x\n", cezanne::statusName(status), *response);
    return status;
}

cezanne::Status CezanneGPU::pspRingDestroy(const void *owner, uint32_t *response, cezanne::PspMailbox *mailbox)
{
    *response = 0;
    *mailbox = cezanne::PspMailbox();
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    // After a create, with or without the observe step.
    if ((pspState_ == kPspCreated || pspState_ == kPspObserved) && pspOwner_ == owner) {
        status = pspDestroyLocked(response, mailbox);
    }
    IOLockUnlock(lock_);
    return status;
}

struct TmrArgument {
    uint32_t *fence;
    cezanne::PspResponse *response;
    uint32_t *writePointer;
    const uint32_t *snapshot;
    uint32_t *unexpected, *firstOffset;
    bool fenced;    // teardown: whether to send DESTROY_TMR first
    uint32_t frame; // teardown: its frame (the number already submitted)
    uint32_t *ringResponse;
    cezanne::PspMailbox *mailbox;
};

static cezanne::Status tmrSubmitOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    TmrArgument *tmr = static_cast<TmrArgument *>(argument);
    cezanne::Status status = withPspWork(stage, [&](const cezanne::MemoryReader &work,
                                                    const cezanne::MemoryWriter &memory) {
        cezanne::Status result = cezanne::writePspCommand(work, memory, stage, cezanne::kGfxCmdSetupTmr, 0);
        if (result == cezanne::kOK) {
            result = cezanne::submitPspFrame(registers, length, *writer, work, stage, 0, tmr->fence);
        }
        if (result == cezanne::kOK) {
            result = cezanne::readPspResponse(work, tmr->response);
        }
        return result;
    });
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegMp0C2PMsg67, tmr->writePointer);
    return status;
}

static cezanne::Status tmrObserveOperation(UInt32, const cezanne::RegisterReader &, UInt64,
                                           const cezanne::RegisterWriter *, void *argument)
{
    TmrArgument *tmr = static_cast<TmrArgument *>(argument);
    return withCarveoutMemory(cezanne::kPspRingPhysical, cezanne::kPspRingCheckSize,
                              [tmr](const cezanne::MemoryReader &region) {
        return cezanne::verifyPspWorkArea(region, tmr->snapshot, 1, cezanne::kGfxCmdSetupTmr, 1, tmr->unexpected,
                                          tmr->firstOffset);
    });
}

static cezanne::Status tmrTeardownOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                            const cezanne::RegisterWriter *writer, void *argument)
{
    TmrArgument *tmr = static_cast<TmrArgument *>(argument);
    cezanne::Status status = cezanne::kOK;
    // DESTROY_TMR only after the last frame fenced: never a frame while
    // another is pending.
    if (tmr->fenced) {
        status = withPspWork(stage, [&](const cezanne::MemoryReader &work, const cezanne::MemoryWriter &memory) {
            cezanne::Status result =
                cezanne::writePspCommand(work, memory, stage, cezanne::kGfxCmdDestroyTmr, tmr->frame);
            if (result == cezanne::kOK) {
                result = cezanne::submitPspFrame(registers, length, *writer, work, stage, tmr->frame, tmr->fence);
            }
            if (result == cezanne::kOK) {
                result = cezanne::readPspResponse(work, tmr->response);
            }
            return result;
        });
    }
    // The ring is destroyed whatever happened to DESTROY_TMR.
    cezanne::Status ring = cezanne::destroyPspRing(registers, length, *writer, stage, true, tmr->ringResponse);
    cezanne::readPspMailbox(registers, length, stage, tmr->mailbox);
    return status != cezanne::kOK ? status : ring;
}

cezanne::Status CezanneGPU::pspTmrSubmit(const void *owner, uint32_t *fence, cezanne::PspResponse *response,
                                         uint32_t *writePointer)
{
    *fence = *writePointer = 0;
    *response = cezanne::PspResponse();
    if (stage_ < cezanne::kPspTmrStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if (pspState_ == kPspCreated && pspCreateOk_ && pspOwner_ == owner) {
        TmrArgument tmr = {fence, response, writePointer, nullptr, nullptr, nullptr, false, 0, nullptr, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, tmrSubmitOperation, &tmr);
        // Once the write pointer moved, the frame is the PSP's.
        if (*writePointer == cezanne::kPspFrameDwords) {
            pspState_ = kPspTmrSubmitted;
            pspFrames_ = 1;
            pspLastFenced_ = *fence == 1;
            pspTmrOk_ = status == cezanne::kOK;
        }
        IOLog(LOG_PREFIX "PSP SETUP_TMR: %s, fence %u, status 0x%08x, C2PMSG_67 %u\n", cezanne::statusName(status),
              *fence, response->status, *writePointer);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::pspTmrObserve(const void *owner, uint32_t *unexpected, uint32_t *firstOffset)
{
    *unexpected = *firstOffset = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if (pspState_ == kPspTmrSubmitted && pspOwner_ == owner) {
        TmrArgument tmr = {nullptr, nullptr, nullptr, pspSnapshot_, unexpected, firstOffset, false, 0, nullptr, nullptr};
        status = accessDevice(0, tmrObserveOperation, &tmr);
        pspState_ = kPspTmrObserved;
        IOLog(LOG_PREFIX "PSP SETUP_TMR observe: %s, %u unexpected words, first at 0x%x\n",
              cezanne::statusName(status), *unexpected, *firstOffset);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::pspTeardownLocked(uint32_t *fence, uint32_t *tmrStatus, uint32_t *ringResponse,
                                              cezanne::PspMailbox *mailbox)
{
    // Caller holds lock_ and pspState_ shows a submitted SETUP_TMR.
    cezanne::PspResponse response;
    TmrArgument tmr = {fence,          &response,  nullptr,      nullptr, nullptr, nullptr,
                       pspLastFenced_, pspFrames_, ringResponse, mailbox};
    cezanne::Status status = accessDevice(cezanne::kSmuPageOffset, tmrTeardownOperation, &tmr);
    *tmrStatus = response.status;
    pspState_ = kPspIdle;
    pspOwner_ = nullptr;
    pspTmrOk_ = false;
    pspFrames_ = 0;
    pspLastFenced_ = false;
    pspCreateOk_ = false;
    IOLog(LOG_PREFIX "PSP teardown: %s, DESTROY_TMR fence %u status 0x%08x, ring response 0x%08x\n",
          cezanne::statusName(status), *fence, *tmrStatus, *ringResponse);
    return status;
}

cezanne::Status CezanneGPU::pspTmrTeardown(const void *owner, uint32_t *fence, uint32_t *tmrStatus,
                                           uint32_t *ringResponse, cezanne::PspMailbox *mailbox)
{
    *fence = *tmrStatus = *ringResponse = 0;
    *mailbox = cezanne::PspMailbox();
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if ((pspState_ == kPspTmrSubmitted || pspState_ == kPspTmrObserved || pspState_ == kPspSdmaLoaded ||
         pspState_ == kPspSdmaObserved) &&
        pspOwner_ == owner && copyState_ == kCopyIdle) {
        status = pspTeardownLocked(fence, tmrStatus, ringResponse, mailbox);
    }
    IOLockUnlock(lock_);
    return status;
}

struct SdmaArgument {
    uint32_t *fence;
    cezanne::PspResponse *response;
    uint32_t *writePointer;
    const uint32_t *workSnapshot, *firmwareSnapshot;
    uint32_t *workUnexpected, *workFirst, *firmwareUnexpected, *firmwareFirst, *checksum, *f32Cntl;
};

static cezanne::Status sdmaLoadOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                         const cezanne::RegisterWriter *writer, void *argument)
{
    SdmaArgument *sdma = static_cast<SdmaArgument *>(argument);
    cezanne::Status status = withSdmaFirmware(stage, [&](const cezanne::MemoryReader &buffer,
                                                         const cezanne::MemoryWriter &memory) {
        return cezanne::writeSdmaFirmware(kCezanneSdmaImage, kCezanneSdmaImageLength, buffer, memory, stage);
    });
    if (status == cezanne::kOK) {
        status = withPspWork(stage, [&](const cezanne::MemoryReader &work, const cezanne::MemoryWriter &memory) {
            cezanne::Status result = cezanne::writePspCommand(work, memory, stage, cezanne::kGfxCmdLoadIpFw, 1);
            if (result == cezanne::kOK) {
                result = cezanne::submitPspFrame(registers, length, *writer, work, stage, 1, sdma->fence);
            }
            if (result == cezanne::kOK) {
                result = cezanne::readPspResponse(work, sdma->response);
            }
            return result;
        });
    }
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegMp0C2PMsg67, sdma->writePointer);
    return status;
}

static cezanne::Status sdmaObserveOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                            const cezanne::RegisterWriter *, void *argument)
{
    SdmaArgument *sdma = static_cast<SdmaArgument *>(argument);
    cezanne::Status work = withCarveoutMemory(cezanne::kPspRingPhysical, cezanne::kPspRingCheckSize,
                                              [sdma](const cezanne::MemoryReader &region) {
        return cezanne::verifyPspWorkArea(region, sdma->workSnapshot, 2, cezanne::kGfxCmdLoadIpFw, 2,
                                          sdma->workUnexpected, sdma->workFirst);
    });
    cezanne::Status firmware = withCarveoutMemory(cezanne::kSdmaFwPhysical, cezanne::kSdmaFwCheckSize,
                                                  [sdma](const cezanne::MemoryReader &region) {
        return cezanne::verifySdmaFirmwareRegion(region, sdma->firmwareSnapshot, kCezanneSdmaImage,
                                                 sdma->firmwareUnexpected, sdma->firmwareFirst);
    });
    cezanne::Status checksum =
        cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0UcodeChecksum, sdma->checksum);
    cezanne::Status f32 =
        cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0F32Cntl, sdma->f32Cntl);
    if (f32 == cezanne::kOK && (*sdma->f32Cntl & cezanne::kSdmaF32Halt) == 0) {
        f32 = cezanne::kSdmaNotHalted;
    }
    const cezanne::Status results[] = {work, firmware, checksum, f32};
    for (cezanne::Status result : results) {
        if (result != cezanne::kOK) {
            return result;
        }
    }
    return cezanne::kOK;
}

cezanne::Status CezanneGPU::sdmaLoad(const void *owner, uint32_t *fence, cezanne::PspResponse *response,
                                     uint32_t *writePointer)
{
    *fence = *writePointer = 0;
    *response = cezanne::PspResponse();
    if (stage_ < cezanne::kPspSdmaStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    // Only after a SETUP_TMR that fenced with status 0, on this connection.
    if ((pspState_ == kPspTmrSubmitted || pspState_ == kPspTmrObserved) && pspTmrOk_ && pspOwner_ == owner) {
        SdmaArgument sdma = {fence,   response, writePointer, nullptr, nullptr, nullptr,
                             nullptr, nullptr,  nullptr,      nullptr, nullptr};
        status = accessDevice(cezanne::kSmuPageOffset, sdmaLoadOperation, &sdma);
        if (*writePointer == 2 * cezanne::kPspFrameDwords) {
            pspState_ = kPspSdmaLoaded;
            pspFrames_ = 2;
            pspLastFenced_ = *fence == 2;
        }
        IOLog(LOG_PREFIX "PSP LOAD_IP_FW SDMA0: %s, fence %u, status 0x%08x, fw_addr 0x%08x%08x, C2PMSG_67 %u\n",
              cezanne::statusName(status), *fence, response->status, response->fwAddrHi, response->fwAddrLo,
              *writePointer);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::sdmaObserve(const void *owner, uint32_t *workUnexpected, uint32_t *workFirst,
                                        uint32_t *firmwareUnexpected, uint32_t *firmwareFirst, uint32_t *checksum,
                                        uint32_t *f32Cntl)
{
    *workUnexpected = *workFirst = *firmwareUnexpected = *firmwareFirst = *checksum = *f32Cntl = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kPspOutOfOrder;
    if (pspState_ == kPspSdmaLoaded && pspOwner_ == owner) {
        SdmaArgument sdma = {nullptr,        nullptr,   nullptr,            pspSnapshot_,  sdmaSnapshot_, workUnexpected,
                             workFirst,      firmwareUnexpected, firmwareFirst, checksum,   f32Cntl};
        status = accessDevice(0, sdmaObserveOperation, &sdma);
        pspState_ = kPspSdmaObserved;
        IOLog(LOG_PREFIX "SDMA0 observe: %s, work %u unexpected, firmware %u unexpected, checksum 0x%08x, "
                         "F32_CNTL 0x%08x\n",
              cezanne::statusName(status), *workUnexpected, *firmwareUnexpected, *checksum, *f32Cntl);
    }
    IOLockUnlock(lock_);
    return status;
}

struct InventoryArgument {
    cezanne::SdmaInventory *inventory;
    uint32_t *up, *down;
};

static cezanne::Status sdmaInventoryOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                              const cezanne::RegisterWriter *writer, void *argument)
{
    InventoryArgument *inventory = static_cast<InventoryArgument *>(argument);
    return cezanne::runSdmaInventory(registers, length, *writer, stage, inventory->inventory, inventory->up,
                                     inventory->down);
}

cezanne::Status CezanneGPU::sdmaInventory(const void *owner, cezanne::SdmaInventory *inventory, uint32_t *upResponse,
                                          uint32_t *downResponse)
{
    *inventory = cezanne::SdmaInventory();
    *upResponse = *downResponse = 0;
    if (stage_ < cezanne::kSdmaInventoryStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kSdmaOutOfOrder;
    // Only with the firmware loaded on this connection: LOAD_IP_FW fenced.
    if ((pspState_ == kPspSdmaLoaded || pspState_ == kPspSdmaObserved) && pspFrames_ == 2 && pspLastFenced_ &&
        pspOwner_ == owner) {
        InventoryArgument argument = {inventory, upResponse, downResponse};
        status = accessDevice(cezanne::kSmuPageOffset, sdmaInventoryOperation, &argument);
        IOLog(LOG_PREFIX "SDMA inventory: %s, PowerUpSdma 0x%x, PowerDownSdma 0x%x\n", cezanne::statusName(status),
              *upResponse, *downResponse);
    }
    IOLockUnlock(lock_);
    return status;
}

struct CopyArgument {
    const cezanne::Range *ranges;
    uint32_t rangeCount;
    uint32_t *snapshot;
    uint32_t *a, *b, *c, *d, *e; // per-step outputs
    uint32_t frame, progress;
    bool fenced;
};

static cezanne::Status copyCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    CopyArgument *copy = static_cast<CopyArgument *>(argument);
    cezanne::Status status = cezanne::checkSdmaBoot19(registers, length, stage, copy->a, copy->b);
    cezanne::MetricsTarget target;
    if (status == cezanne::kOK) {
        status = cezanne::checkSdmaWorkTarget(registers, length, stage, copy->ranges, copy->rangeCount, &target);
    }
    if (status == cezanne::kOK) {
        uint32_t *snapshot = copy->snapshot;
        status = withCarveoutMemory(cezanne::kSdmaWorkPhysical, cezanne::kSdmaWorkCheckSize,
                                    [writer, snapshot](const cezanne::MemoryReader &memory) {
            return cezanne::checkRegionStable(memory, cezanne::kSdmaWorkCheckSize, *writer,
                                              cezanne::kMetricsStablePauses, snapshot);
        });
    }
    return status;
}

static cezanne::Status copyStartOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    CopyArgument *copy = static_cast<CopyArgument *>(argument);
    cezanne::Status status = withSdmaWork(stage, [&](const cezanne::MemoryReader &work,
                                                     const cezanne::MemoryWriter &memory) {
        return cezanne::writeSdmaWork(work, memory, stage);
    });
    if (status == cezanne::kOK) {
        status = cezanne::startSdma(registers, length, *writer, stage, copy->a, copy->b);
    }
    return status;
}

static cezanne::Status copySubmitOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *writer, void *argument)
{
    CopyArgument *copy = static_cast<CopyArgument *>(argument);
    cezanne::Status status = withCarveoutMemory(cezanne::kSdmaWorkPhysical, cezanne::kSdmaWorkCheckSize,
                                                [&](const cezanne::MemoryReader &work) {
        return cezanne::submitSdma(registers, length, *writer, work, stage, copy->frame, copy->a);
    });
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0GfxRbRptr, copy->b);
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0GfxRbWptr, copy->c);
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0F32Cntl, copy->d);
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0StatusReg, copy->e);
    return status;
}

static cezanne::Status copyVerifyOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *, void *argument)
{
    CopyArgument *copy = static_cast<CopyArgument *>(argument);
    cezanne::Status status = withCarveoutMemory(cezanne::kSdmaWorkPhysical, cezanne::kSdmaWorkCheckSize,
                                                [&](const cezanne::MemoryReader &region) {
        return cezanne::verifySdmaCopy(registers, length, region, copy->snapshot, stage, copy->a, copy->b, copy->c);
    });
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0StatusReg, copy->d);
    return status;
}

static cezanne::Status copyStopOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                         const cezanne::RegisterWriter *writer, void *argument)
{
    CopyArgument *copy = static_cast<CopyArgument *>(argument);
    cezanne::Status status = cezanne::stopSdma(registers, length, *writer, stage, copy->progress, copy->b);
    cezanne::readDiagnosticRegister(registers, length, stage, cezanne::kRegSdma0F32Cntl, copy->a);
    return status;
}

cezanne::Status CezanneGPU::sdmaCopyCheck(const void *owner, uint32_t *index, uint32_t *value)
{
    *index = *value = 0;
    if (!diagnosticsReady_ || stage_ < cezanne::kSdmaCopyStage) {
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
    status = cezanne::kSdmaOutOfOrder;
    // With the firmware loaded on this connection (LOAD_IP_FW fenced) and no copy under way.
    if ((pspState_ == kPspSdmaLoaded || pspState_ == kPspSdmaObserved) && pspFrames_ == 2 && pspLastFenced_ &&
        pspOwner_ == owner && copyState_ == kCopyIdle) {
        CopyArgument copy = {ranges, count, sdmaWorkSnapshot_, index, value, nullptr, nullptr, nullptr, 0, 0, false};
        status = accessDevice(0, copyCheckOperation, &copy);
        copyState_ = status == cezanne::kOK ? kCopyChecked : kCopyIdle;
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::sdmaStart(const void *owner, uint32_t *progress, uint32_t *upResponse)
{
    *progress = *upResponse = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kSdmaOutOfOrder;
    if (copyState_ == kCopyChecked && pspOwner_ == owner) {
        CopyArgument copy = {nullptr, 0, nullptr, progress, upResponse, nullptr, nullptr, nullptr, 0, 0, false};
        status = accessDevice(cezanne::kSdmaPageSet, copyStartOperation, &copy);
        sdmaProgress_ = *progress;
        copyState_ = status == cezanne::kOK ? kCopyStarted : kCopyChecked;
        IOLog(LOG_PREFIX "SDMA start: %s, progress %u, PowerUpSdma 0x%x\n", cezanne::statusName(status), *progress,
              *upResponse);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::sdmaSubmit(const void *owner, uint32_t frame, uint32_t *observed, uint32_t *rptr,
                                       uint32_t *wptr, uint32_t *f32Cntl, uint32_t *status)
{
    *observed = *rptr = *wptr = *f32Cntl = *status = 0;
    IOLockLock(lock_);
    cezanne::Status result = cezanne::kSdmaOutOfOrder;
    bool next = (frame == 0 && copyState_ == kCopyStarted) || (frame == 1 && copyState_ == kCopyTested) ||
                (frame == 2 && gartState_ == kGartEnabled && gartProgress_ == 3) ||
                (frame == 3 && intrState_ == kIntrEnabled && intrProgress_ == 2);
    if (next && pspOwner_ == owner) {
        CopyArgument copy = {nullptr, 0, nullptr, observed, rptr, wptr, f32Cntl, status, frame, 0, false};
        if (frame == 3) {
            msiAtSubmit_ = intrCount(this);
            intrSubmitTime_ = mach_absolute_time();
        }
        result = accessDevice(cezanne::kSdmaPageSet, copySubmitOperation, &copy);
        if (result == cezanne::kOK && frame == 3) {
            intrState_ = kIntrSubmitted;
        } else if (result == cezanne::kOK && frame == 2) {
            gartState_ = kGartSubmitted;
        } else if (result == cezanne::kOK) {
            copyState_ = frame == 0 ? kCopyTested : kCopySubmitted;
        }
        IOLog(LOG_PREFIX "SDMA frame %u: %s, observed 0x%08x, RPTR %u, WPTR %u, F32_CNTL 0x%08x, STATUS 0x%08x\n", frame,
              cezanne::statusName(result), *observed, *rptr, *wptr, *f32Cntl, *status);
    }
    IOLockUnlock(lock_);
    return result;
}

cezanne::Status CezanneGPU::sdmaVerify(const void *owner, uint32_t *rptr, uint32_t *unexpected,
                                       uint32_t *firstOffset, uint32_t *status)
{
    *rptr = *unexpected = *firstOffset = *status = 0;
    IOLockLock(lock_);
    cezanne::Status result = cezanne::kSdmaOutOfOrder;
    if (copyState_ == kCopySubmitted && pspOwner_ == owner) {
        CopyArgument copy = {nullptr, 0, sdmaWorkSnapshot_, rptr, unexpected, firstOffset, status, nullptr, 0, 0, false};
        result = accessDevice(0, copyVerifyOperation, &copy);
        copyVerified_ = result == cezanne::kOK;
        IOLog(LOG_PREFIX "SDMA verify: %s, RPTR %u, %u unexpected words, first at 0x%x\n",
              cezanne::statusName(result), *rptr, *unexpected, *firstOffset);
    }
    IOLockUnlock(lock_);
    return result;
}

cezanne::Status CezanneGPU::sdmaStopLocked(uint32_t *f32Cntl, uint32_t *downResponse, uint32_t *fence,
                                           uint32_t *ringResponse)
{
    // Caller holds lock_: the stage 17 restore if any GART or IH register
    // may have changed, halt and power down by progress, then the stage 13
    // teardown.
    cezanne::Status restore = cezanne::kOK;
    if (gartState_ >= kGartEnabled && gartState_ != kGartRestored) {
        uint32_t index = 0, value = 0, ihWptr = 0;
        restore = gartRestoreLocked(&index, &value, &ihWptr);
    }
    gartState_ = kGartIdle;
    gartVerified_ = false;
    intrState_ = kIntrIdle;
    intrVerified_ = false;
    copyVerified_ = false;
    CopyArgument copy = {nullptr, 0, nullptr, f32Cntl, downResponse, nullptr, nullptr, nullptr, 0, sdmaProgress_, false};
    cezanne::Status status = accessDevice(cezanne::kSdmaPageSet, copyStopOperation, &copy);
    uint32_t tmrStatus = 0;
    cezanne::PspMailbox mailbox;
    cezanne::Status teardown = pspTeardownLocked(fence, &tmrStatus, ringResponse, &mailbox);
    copyState_ = kCopyIdle;
    sdmaProgress_ = 0;
    IOLog(LOG_PREFIX "SDMA stop: %s, F32_CNTL 0x%08x, PowerDownSdma 0x%x; teardown %s\n", cezanne::statusName(status),
          *f32Cntl, *downResponse, cezanne::statusName(teardown));
    return restore != cezanne::kOK ? restore : status != cezanne::kOK ? status : teardown;
}

cezanne::Status CezanneGPU::sdmaStop(const void *owner, uint32_t *f32Cntl, uint32_t *downResponse, uint32_t *fence,
                                     uint32_t *ringResponse)
{
    *f32Cntl = *downResponse = *fence = *ringResponse = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kSdmaOutOfOrder;
    if (copyState_ != kCopyIdle && pspOwner_ == owner) {
        status = sdmaStopLocked(f32Cntl, downResponse, fence, ringResponse);
    }
    IOLockUnlock(lock_);
    return status;
}

struct GartArgument {
    const cezanne::Range *ranges;
    uint32_t rangeCount;
    uint32_t *snapshot, *display;
    const uint32_t *sdmaSnapshot;
    uint32_t *a, *b, *c; // per-step outputs
    uint32_t progress;
    cezanne::GartReport *report;
};

static cezanne::Status gartCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    GartArgument *gart = static_cast<GartArgument *>(argument);
    cezanne::Status status = cezanne::checkGartBoot22(registers, length, stage, gart->a, gart->b);
    cezanne::MetricsTarget target;
    if (status == cezanne::kOK) {
        status = cezanne::checkGartWorkTarget(registers, length, stage, gart->ranges, gart->rangeCount, &target);
    }
    if (status == cezanne::kOK) {
        uint32_t *snapshot = gart->snapshot;
        status = withCarveoutMemory(cezanne::kGartWorkPhysical, cezanne::kGartWorkCheckSize,
                                    [writer, snapshot](const cezanne::MemoryReader &memory) {
            return cezanne::checkRegionStable(memory, cezanne::kGartWorkCheckSize, *writer,
                                              cezanne::kMetricsStablePauses, snapshot);
        });
    }
    if (status == cezanne::kOK) {
        status = cezanne::readDisplayInventory(registers, length, stage, gart->display);
    }
    return status;
}

static cezanne::Status gartEnableOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *writer, void *argument)
{
    GartArgument *gart = static_cast<GartArgument *>(argument);
    cezanne::Status status = withGartWork(stage, [&](const cezanne::MemoryReader &work,
                                                     const cezanne::MemoryWriter &memory) {
        return cezanne::writeGartWork(work, memory, stage);
    });
    if (status == cezanne::kOK) {
        status = withSdmaWork(stage, [&](const cezanne::MemoryReader &work, const cezanne::MemoryWriter &memory) {
            return cezanne::writeSdmaFrame2(work, memory, stage);
        });
    }
    if (status == cezanne::kOK) {
        status = cezanne::enableGart(registers, length, *writer, stage, gart->a, gart->b);
    }
    return status;
}

static cezanne::Status gartVerifyOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *writer, void *argument)
{
    GartArgument *gart = static_cast<GartArgument *>(argument);
    return withCarveoutMemory(cezanne::kSdmaWorkPhysical, cezanne::kSdmaWorkCheckSize,
                              [&](const cezanne::MemoryReader &sdmaRegion) {
        return withCarveoutMemory(cezanne::kGartWorkPhysical, cezanne::kGartWorkCheckSize,
                                  [&](const cezanne::MemoryReader &gartRegion) {
            return cezanne::verifyGart(registers, length, sdmaRegion, gart->sdmaSnapshot, gartRegion, gart->snapshot,
                                       gart->display, *writer, stage, gart->report);
        });
    });
}

static cezanne::Status gartRestoreOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                            const cezanne::RegisterWriter *writer, void *argument)
{
    GartArgument *gart = static_cast<GartArgument *>(argument);
    uint32_t ack = 0;
    cezanne::Status status = cezanne::restoreGart(registers, length, *writer, stage, gart->progress, &ack);
    cezanne::Status restored = cezanne::checkGartRestored(registers, length, stage, gart->a, gart->b, gart->c);
    return status != cezanne::kOK ? status : restored;
}

cezanne::Status CezanneGPU::gartCheck(const void *owner, uint32_t *index, uint32_t *value)
{
    *index = *value = 0;
    if (!diagnosticsReady_ || stage_ < cezanne::kGartStage) {
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
    status = cezanne::kGartOutOfOrder;
    // After a verified stage 15 copy on this connection, once.
    if (copyState_ == kCopySubmitted && copyVerified_ && pspOwner_ == owner && gartState_ == kGartIdle) {
        GartArgument gart = {ranges, count, gartSnapshot_, gartDisplay_, nullptr, index, value, nullptr, 0, nullptr};
        status = accessDevice(0, gartCheckOperation, &gart);
        gartState_ = status == cezanne::kOK ? kGartChecked : kGartIdle;
        IOLog(LOG_PREFIX "GART check: %s, index %u, value 0x%08x\n", cezanne::statusName(status), *index, *value);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::gartEnable(const void *owner, uint32_t *progress, uint32_t *ack)
{
    *progress = *ack = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kGartOutOfOrder;
    if (gartState_ == kGartChecked && pspOwner_ == owner) {
        GartArgument gart = {nullptr, 0, nullptr, nullptr, nullptr, progress, ack, nullptr, 0, nullptr};
        status = accessDevice(cezanne::kGartPageSet, gartEnableOperation, &gart);
        gartProgress_ = *progress;
        // Anything written from here on is undone by the restore.
        gartState_ = status == cezanne::kOK ? kGartEnabled : kGartFailed;
        IOLog(LOG_PREFIX "GART enable: %s, progress %u, ENG17_ACK 0x%08x\n", cezanne::statusName(status), *progress,
              *ack);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::gartVerify(const void *owner, cezanne::GartReport *report)
{
    *report = cezanne::GartReport();
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kGartOutOfOrder;
    if (gartState_ == kGartSubmitted && pspOwner_ == owner) {
        GartArgument gart = {nullptr, 0, gartSnapshot_, gartDisplay_, sdmaWorkSnapshot_, nullptr, nullptr, nullptr, 0,
                             report};
        status = accessDevice(0, gartVerifyOperation, &gart);
        gartState_ = kGartVerified;
        gartVerified_ = status == cezanne::kOK;
        IOLog(LOG_PREFIX "GART verify: %s, RPTR %u, fence %u, fault 0x%08x, IH write-back 0x%08x, %u entries, "
                         "%u SDMA traps; %u + %u unexpected words, %u display changes\n",
              cezanne::statusName(status), report->rptr, report->fence2, report->faultStatus, report->ihWriteback,
              report->ihEntries, report->sdmaTraps, report->sdmaUnexpected, report->gartUnexpected,
              report->displayChanged);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::gartRestoreLocked(uint32_t *index, uint32_t *value, uint32_t *ihWptr)
{
    // Caller holds lock_. The stage 18 restore first if interrupts may be on
    // or the handler is registered.
    cezanne::Status intr = cezanne::kOK;
    if (intrProgress_ != 0 || intrSource_ != nullptr) {
        uint32_t msi[4] = {};
        intr = intrRestoreLocked(index, value, msi);
        *index = *value = 0;
    }
    GartArgument gart = {nullptr, 0, nullptr, nullptr, nullptr, index, value, ihWptr, gartProgress_, nullptr};
    cezanne::Status status = accessDevice(cezanne::kGartPageSet, gartRestoreOperation, &gart);
    IOLog(LOG_PREFIX "GART restore (progress %u): %s, index %u, value 0x%08x, IH_RB_WPTR 0x%08x\n", gartProgress_,
          cezanne::statusName(status), *index, *value, *ihWptr);
    setProperty("CezanneGPU GART restore", cezanne::statusName(status));
    gartProgress_ = 0;
    gartState_ = kGartRestored;
    return intr != cezanne::kOK ? intr : status;
}

cezanne::Status CezanneGPU::gartRestore(const void *owner, uint32_t *index, uint32_t *value, uint32_t *ihWptr)
{
    *index = *value = *ihWptr = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kGartOutOfOrder;
    if (gartState_ >= kGartEnabled && gartState_ != kGartRestored && pspOwner_ == owner) {
        status = gartRestoreLocked(index, value, ihWptr);
    }
    IOLockUnlock(lock_);
    return status;
}

struct IntrArgument {
    uint32_t *a, *b, *c, *d; // per-step outputs
    uint32_t *snapshot, *display;
    const uint32_t *sdmaSnapshot;
    cezanne::InterruptCounter counter;
    uint32_t msiBefore;
    cezanne::IntrReport *report;
};

static cezanne::Status intrCheckOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                          const cezanne::RegisterWriter *, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    return cezanne::checkIntrBoot22(registers, length, stage, intr->a, intr->b);
}

static cezanne::Status intrArmOperation(UInt32 stage, const cezanne::RegisterReader &, UInt64,
                                        const cezanne::RegisterWriter *writer, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    cezanne::Status status = withSdmaWork(stage, [&](const cezanne::MemoryReader &work,
                                                     const cezanne::MemoryWriter &memory) {
        return cezanne::writeSdmaFrame3(work, memory, stage);
    });
    if (status == cezanne::kOK) {
        status = withGartWork(stage, [&](const cezanne::MemoryReader &work, const cezanne::MemoryWriter &memory) {
            return cezanne::armIntr(work, memory, *writer, stage, intr->a);
        });
    }
    return status;
}

static cezanne::Status intrStartOperation(UInt32 stage, const cezanne::RegisterReader &, UInt64,
                                          const cezanne::RegisterWriter *writer, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    return cezanne::startIntr(*writer, stage, intr->a);
}

static cezanne::Status intrVerifyOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                           const cezanne::RegisterWriter *writer, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    return withCarveoutMemory(cezanne::kSdmaWorkPhysical, cezanne::kSdmaWorkCheckSize,
                              [&](const cezanne::MemoryReader &sdmaRegion) {
        return withCarveoutMemory(cezanne::kGartWorkPhysical, cezanne::kGartWorkCheckSize,
                                  [&](const cezanne::MemoryReader &gartRegion) {
            return cezanne::verifyIntr(registers, length, sdmaRegion, intr->sdmaSnapshot, gartRegion, intr->snapshot,
                                       intr->display, *writer, intr->counter, intr->msiBefore, stage, intr->report);
        });
    });
}

static cezanne::Status intrAckOperation(UInt32 stage, const cezanne::RegisterReader &, UInt64,
                                        const cezanne::RegisterWriter *writer, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    return withCarveoutMemory(cezanne::kGartWorkPhysical, cezanne::kGartWorkCheckSize,
                              [&](const cezanne::MemoryReader &gartRegion) {
        return cezanne::ackIntr(gartRegion, *writer, intr->counter, stage, intr->a, intr->b, intr->c, intr->d);
    });
}

static cezanne::Status intrQuiesceOperation(UInt32 stage, const cezanne::RegisterReader &, UInt64,
                                            const cezanne::RegisterWriter *writer, void *)
{
    return cezanne::quiesceIntr(*writer, stage);
}

static cezanne::Status intrRestoreOperation(UInt32 stage, const cezanne::RegisterReader &registers, UInt64 length,
                                            const cezanne::RegisterWriter *writer, void *argument)
{
    IntrArgument *intr = static_cast<IntrArgument *>(argument);
    return cezanne::restoreIntr(registers, length, *writer, stage, intr->a, intr->b);
}

bool CezanneGPU::intrFilter(OSObject *owner, IOFilterInterruptEventSource *)
{
    // Primary interrupt context: count and time the MSI only. No lock, no
    // log, no register access; returning false schedules nothing.
    CezanneGPU *self = static_cast<CezanneGPU *>(owner);
    UInt64 now = mach_absolute_time();
    SInt32 n = OSIncrementAtomic(&self->msiCount_);
    if (n >= 0 && n < static_cast<SInt32>(cezanne::kIntrTimes)) {
        self->msiTimes_[n] = now;
    }
    return false;
}

void CezanneGPU::intrAction(OSObject *, IOInterruptEventSource *, int)
{
    // Never scheduled: intrFilter always returns false.
}

uint32_t CezanneGPU::sinceEnable(UInt64 time) const
{
    // Microseconds from the ENABLE_INTR write; 0 for a time before it.
    if (time <= intrEnableTime_) {
        return 0;
    }
    UInt64 nanoseconds = 0;
    absolutetime_to_nanoseconds(time - intrEnableTime_, &nanoseconds);
    return static_cast<uint32_t>(nanoseconds / 1000);
}

uint32_t CezanneGPU::intrCount(void *context)
{
    CezanneGPU *self = static_cast<CezanneGPU *>(context);
    return static_cast<uint32_t>(OSAddAtomic(0, &self->msiCount_));
}

void CezanneGPU::readMsiLocked(uint32_t msi[4])
{
    // Caller holds lock_. Configuration-space reads only.
    msi[0] = msi[1] = msi[2] = msi[3] = 0;
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, getProvider());
    if (pci == nullptr || !pci->open(this)) {
        return;
    }
    cezanne::ConfigReader config = {configRead, pci};
    cezanne::MsiCapability capability;
    if (cezanne::readMsiCapability(config, &capability) == cezanne::kOK) {
        msi[0] = capability.control;
        msi[1] = capability.addressLo;
        msi[2] = capability.addressHi;
        msi[3] = capability.data;
    }
    pci->close(this);
}

cezanne::Status CezanneGPU::addIntrSourceLocked()
{
    // Caller holds lock_. The only interrupt registration: the MSI vector
    // found by intrCheck, on the driver's own work loop.
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, getProvider());
    if (pci == nullptr || msiIndex_ < 0 || intrSource_ != nullptr) {
        return cezanne::kIntrSourceFailed;
    }
    intrLoop_ = IOWorkLoop::workLoop();
    if (intrLoop_ != nullptr) {
        intrSource_ = IOFilterInterruptEventSource::filterInterruptEventSource(this, intrAction, intrFilter, pci,
                                                                               msiIndex_);
    }
    if (intrSource_ == nullptr || intrLoop_->addEventSource(intrSource_) != kIOReturnSuccess) {
        removeIntrSourceLocked();
        return cezanne::kIntrSourceFailed;
    }
    // Nothing can interrupt before enable().
    msiCount_ = 0;
    for (uint32_t i = 0; i < cezanne::kIntrTimes; i++) {
        msiTimes_[i] = 0;
    }
    intrSource_->enable();
    return cezanne::kOK;
}

void CezanneGPU::removeIntrSourceLocked()
{
    // Caller holds lock_ (or the driver is stopping).
    if (intrSource_ != nullptr) {
        intrSource_->disable();
        if (intrLoop_ != nullptr) {
            intrLoop_->removeEventSource(intrSource_);
        }
        intrSource_->release();
        intrSource_ = nullptr;
    }
    if (intrLoop_ != nullptr) {
        intrLoop_->release();
        intrLoop_ = nullptr;
    }
}

cezanne::Status CezanneGPU::intrCheck(const void *owner, uint32_t *index, uint32_t *value, uint32_t *msiIndex,
                                      uint32_t msi[4])
{
    *index = *value = *msiIndex = 0;
    msi[0] = msi[1] = msi[2] = msi[3] = 0;
    if (!diagnosticsReady_ || stage_ < cezanne::kIntrStage) {
        return cezanne::kRegisterNotAllowed;
    }
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kIntrOutOfOrder;
    // After a passing stage 17 verify on this connection, once.
    if (gartState_ == kGartVerified && gartVerified_ && gartProgress_ == 3 && pspOwner_ == owner &&
        intrState_ == kIntrIdle && intrSource_ == nullptr) {
        msiIndex_ = -1;
        IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, getProvider());
        for (int i = 0; pci != nullptr && i < 8; i++) {
            int type = 0;
            if (pci->getInterruptType(i, &type) != kIOReturnSuccess) {
                break;
            }
            if ((type & kIOInterruptTypePCIMessaged) != 0) {
                msiIndex_ = i;
                break;
            }
        }
        readMsiLocked(msi);
        status = cezanne::kIntrNoMsi;
        if (msiIndex_ >= 0) {
            *msiIndex = static_cast<uint32_t>(msiIndex_);
            IntrArgument intr = {index, value, nullptr, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, 0, nullptr};
            status = accessDevice(0, intrCheckOperation, &intr);
        }
        intrState_ = status == cezanne::kOK ? kIntrChecked : kIntrIdle;
        IOLog(LOG_PREFIX "interrupt check: %s, MSI index %d, control 0x%04x\n", cezanne::statusName(status), msiIndex_,
              msi[0]);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::intrEnable(const void *owner, uint32_t *progress, uint32_t msi[4])
{
    *progress = 0;
    msi[0] = msi[1] = msi[2] = msi[3] = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kIntrOutOfOrder;
    if (intrState_ == kIntrChecked && pspOwner_ == owner) {
        // Arm the IH, then the handler, and only then ENABLE_INTR.
        IntrArgument intr = {progress, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, 0, nullptr};
        status = accessDevice(cezanne::kIntrPageSet, intrArmOperation, &intr);
        intrProgress_ = *progress;
        if (status == cezanne::kOK) {
            status = addIntrSourceLocked();
        }
        if (status == cezanne::kOK) {
            intrEnableTime_ = mach_absolute_time();
            status = accessDevice(cezanne::kIntrPageSet, intrStartOperation, &intr);
            intrProgress_ = *progress;
        }
        readMsiLocked(msi);
        // Anything written or registered from here on is undone by the restore.
        intrState_ = status == cezanne::kOK ? kIntrEnabled : kIntrFailed;
        IOLog(LOG_PREFIX "interrupt enable: %s, progress %u, MSI control 0x%04x data 0x%04x\n",
              cezanne::statusName(status), *progress, msi[0], msi[3]);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::intrVerify(const void *owner, cezanne::IntrReport *report)
{
    *report = cezanne::IntrReport();
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kIntrOutOfOrder;
    if (intrState_ == kIntrSubmitted && pspOwner_ == owner) {
        IntrArgument intr = {nullptr, nullptr, nullptr, nullptr, gartSnapshot_, gartDisplay_, sdmaWorkSnapshot_,
                             {intrCount, this}, msiAtSubmit_, report};
        status = accessDevice(0, intrVerifyOperation, &intr);
        intrState_ = kIntrVerified;
        intrVerified_ = status == cezanne::kOK;
        report->submitMicroseconds = sinceEnable(intrSubmitTime_);
        for (uint32_t i = 0; i < cezanne::kIntrTimes && i < report->msiCount; i++) {
            report->msiMicroseconds[i] = sinceEnable(msiTimes_[i]);
        }
        if (msiAtSubmit_ < report->msiCount && msiAtSubmit_ < cezanne::kIntrTimes) {
            uint32_t first = report->msiMicroseconds[msiAtSubmit_];
            report->latencyMicroseconds = first > report->submitMicroseconds ? first - report->submitMicroseconds : 0;
        }
        IOLog(LOG_PREFIX "interrupt verify: %s, %u MSI (%u before the submit), first after it in %u us, fence %u, "
                         "IH write-back 0x%08x, %u entries, %u SDMA traps; %u + %u unexpected words, %u display changes\n",
              cezanne::statusName(status), report->msiCount, report->msiBefore, report->latencyMicroseconds,
              report->fence3,
              report->ihWriteback, report->ihEntries, report->sdmaTraps, report->sdmaUnexpected,
              report->gartUnexpected, report->displayChanged);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::intrAck(const void *owner, uint32_t *rptr, uint32_t *countBefore, uint32_t *countAfter,
                                    uint32_t *writeback)
{
    *rptr = *countBefore = *countAfter = *writeback = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kIntrOutOfOrder;
    // Once, after a passing verify.
    if (intrState_ == kIntrVerified && intrVerified_ && pspOwner_ == owner) {
        IntrArgument intr = {rptr, countBefore, countAfter, writeback, nullptr, nullptr, nullptr, {intrCount, this}, 0,
                             nullptr};
        status = accessDevice(cezanne::kIntrPageSet, intrAckOperation, &intr);
        intrVerified_ = false;
        IOLog(LOG_PREFIX "interrupt acknowledge: %s, IH_RB_RPTR <- 0x%x, MSI %u then %u, write-back 0x%08x\n",
              cezanne::statusName(status), *rptr, *countBefore, *countAfter, *writeback);
    }
    IOLockUnlock(lock_);
    return status;
}

cezanne::Status CezanneGPU::intrRestoreLocked(uint32_t *index, uint32_t *value, uint32_t msi[4])
{
    // Caller holds lock_: the IH quiet, the handler removed, INTERRUPT_CNTL2
    // back, in that order (vega10_ih_irq_disable before free_irq).
    IntrArgument intr = {index, value, nullptr, nullptr, nullptr, nullptr, nullptr, {nullptr, nullptr}, 0, nullptr};
    cezanne::Status quiesce = cezanne::kOK, restore = cezanne::kOK;
    if (intrProgress_ != 0) {
        quiesce = accessDevice(cezanne::kIntrPageSet, intrQuiesceOperation, &intr);
    }
    removeIntrSourceLocked();
    if (intrProgress_ != 0) {
        restore = accessDevice(cezanne::kIntrPageSet, intrRestoreOperation, &intr);
    }
    readMsiLocked(msi);
    cezanne::Status status = quiesce != cezanne::kOK ? quiesce : restore;
    IOLog(LOG_PREFIX "interrupt restore (progress %u): %s, index %u, value 0x%08x, %d MSI in all\n", intrProgress_,
          cezanne::statusName(status), *index, *value, static_cast<int>(OSAddAtomic(0, &msiCount_)));
    setProperty("CezanneGPU interrupt restore", cezanne::statusName(status));
    intrProgress_ = 0;
    intrState_ = kIntrRestored;
    intrVerified_ = false;
    return status;
}

cezanne::Status CezanneGPU::intrRestore(const void *owner, uint32_t *index, uint32_t *value, uint32_t msi[4])
{
    *index = *value = 0;
    msi[0] = msi[1] = msi[2] = msi[3] = 0;
    IOLockLock(lock_);
    cezanne::Status status = cezanne::kIntrOutOfOrder;
    if ((intrProgress_ != 0 || intrSource_ != nullptr) && pspOwner_ == owner) {
        status = intrRestoreLocked(index, value, msi);
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
    if (pspOwner_ == owner && copyState_ != kCopyIdle) {
        uint32_t f32 = 0, down = 0, fence = 0, ring = 0;
        cezanne::Status status = sdmaStopLocked(&f32, &down, &fence, &ring);
        IOLog(LOG_PREFIX "SDMA copy abandoned; stop and teardown: %s\n", cezanne::statusName(status));
        setProperty("CezanneGPU SDMA copy abandoned stop", cezanne::statusName(status));
    }
    if (pspOwner_ == owner && (pspState_ == kPspTmrSubmitted || pspState_ == kPspTmrObserved ||
                               pspState_ == kPspSdmaLoaded || pspState_ == kPspSdmaObserved)) {
        uint32_t fence = 0, tmrStatus = 0, ringResponse = 0;
        cezanne::PspMailbox mailbox;
        cezanne::Status status = pspTeardownLocked(&fence, &tmrStatus, &ringResponse, &mailbox);
        IOLog(LOG_PREFIX "PSP TMR abandoned after the submit; teardown: %s\n", cezanne::statusName(status));
        setProperty("CezanneGPU PSP TMR abandoned teardown", cezanne::statusName(status));
    }
    if (pspOwner_ == owner) {
        if (pspState_ == kPspCreated || pspState_ == kPspObserved) {
            uint32_t response = 0;
            cezanne::PspMailbox mailbox;
            cezanne::Status status = pspDestroyLocked(&response, &mailbox);
            IOLog(LOG_PREFIX "PSP ring abandoned after a create attempt; destroy: %s\n", cezanne::statusName(status));
            setProperty("CezanneGPU PSP ring abandoned destroy", cezanne::statusName(status));
        }
        pspState_ = kPspIdle;
        pspOwner_ = nullptr;
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
    static IOReturn pspRingCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspRingCreate(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspRingObserve(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspRingDestroy(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspTmrSubmit(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspTmrObserve(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn pspTmrTeardown(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaLoad(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaObserve(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaInventory(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaCopyCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaStart(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaSubmit(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaVerify(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn sdmaStop(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn gartCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn gartEnable(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn gartVerify(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn gartRestore(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn intrCheck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn intrEnable(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn intrVerify(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn intrAck(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
    static IOReturn intrRestore(OSObject *target, void *reference, IOExternalMethodArguments *arguments);
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

static void putMailbox(IOExternalMethodArguments *arguments, uint32_t first, const cezanne::PspMailbox &mailbox)
{
    const uint64_t values[] = {mailbox.command, mailbox.writePointer, mailbox.ringLow, mailbox.ringHigh,
                               mailbox.ringSize};
    for (uint32_t i = 0; i < 5; i++) {
        arguments->scalarOutput[first + i] = values[i];
    }
}

IOReturn CezanneGPUUserClient::pspRingCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::PspMailbox mailbox;
    arguments->scalarOutput[0] = self->gpu_->pspRingCheck(self, &mailbox);
    arguments->scalarOutput[1] = mailbox.signOfLife;
    putMailbox(arguments, 2, mailbox);
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspRingCreate(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t response = 0;
    bool written = false;
    arguments->scalarOutput[0] = self->gpu_->pspRingCreate(self, &response, &written);
    arguments->scalarOutput[1] = response;
    arguments->scalarOutput[2] = written ? 1 : 0;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspRingObserve(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::PspMailbox mailbox;
    uint32_t inPage = 0, outside = 0;
    arguments->scalarOutput[0] = self->gpu_->pspRingObserve(self, &mailbox, &inPage, &outside);
    putMailbox(arguments, 1, mailbox);
    arguments->scalarOutput[6] = inPage;
    arguments->scalarOutput[7] = outside;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspRingDestroy(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::PspMailbox mailbox;
    uint32_t response = 0;
    arguments->scalarOutput[0] = self->gpu_->pspRingDestroy(self, &response, &mailbox);
    arguments->scalarOutput[1] = response;
    putMailbox(arguments, 2, mailbox);
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspTmrSubmit(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t fence = 0, writePointer = 0;
    cezanne::PspResponse response;
    arguments->scalarOutput[0] = self->gpu_->pspTmrSubmit(self, &fence, &response, &writePointer);
    const uint64_t values[] = {fence, response.status, response.fwAddrLo, response.fwAddrHi, response.tmrSize,
                               writePointer};
    for (uint32_t i = 0; i < 6; i++) {
        arguments->scalarOutput[i + 1] = values[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspTmrObserve(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t unexpected = 0, first = 0;
    arguments->scalarOutput[0] = self->gpu_->pspTmrObserve(self, &unexpected, &first);
    arguments->scalarOutput[1] = unexpected;
    arguments->scalarOutput[2] = first;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::pspTmrTeardown(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t fence = 0, tmrStatus = 0, ringResponse = 0;
    cezanne::PspMailbox mailbox;
    arguments->scalarOutput[0] = self->gpu_->pspTmrTeardown(self, &fence, &tmrStatus, &ringResponse, &mailbox);
    arguments->scalarOutput[1] = fence;
    arguments->scalarOutput[2] = tmrStatus;
    arguments->scalarOutput[3] = ringResponse;
    arguments->scalarOutput[4] = mailbox.command;
    arguments->scalarOutput[5] = mailbox.writePointer;
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::sdmaLoad(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t fence = 0, writePointer = 0;
    cezanne::PspResponse response;
    arguments->scalarOutput[0] = self->gpu_->sdmaLoad(self, &fence, &response, &writePointer);
    const uint64_t values[] = {fence, response.status, response.fwAddrLo, response.fwAddrHi, writePointer};
    for (uint32_t i = 0; i < 5; i++) {
        arguments->scalarOutput[i + 1] = values[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::sdmaObserve(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t values[6] = {};
    arguments->scalarOutput[0] =
        self->gpu_->sdmaObserve(self, &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]);
    for (uint32_t i = 0; i < 6; i++) {
        arguments->scalarOutput[i + 1] = values[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::sdmaInventory(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::SdmaInventory inventory;
    uint32_t up = 0, down = 0;
    arguments->scalarOutput[0] = self->gpu_->sdmaInventory(self, &inventory, &up, &down);
    arguments->scalarOutput[1] = up;
    arguments->scalarOutput[2] = down;
    // The dispatch table fixes the structure size at sizeof(SdmaInventory).
    UInt8 *out = static_cast<UInt8 *>(arguments->structureOutput);
    const UInt8 *in = reinterpret_cast<const UInt8 *>(&inventory);
    for (uint32_t i = 0; i < sizeof(inventory); i++) {
        out[i] = in[i];
    }
    return kIOReturnSuccess;
}

static IOReturn putScalars(IOExternalMethodArguments *arguments, cezanne::Status status, const uint32_t *values,
                           uint32_t count)
{
    arguments->scalarOutput[0] = status;
    for (uint32_t i = 0; i < count; i++) {
        arguments->scalarOutput[i + 1] = values[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::sdmaCopyCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[2] = {};
    return putScalars(arguments, self->gpu_->sdmaCopyCheck(self, &v[0], &v[1]), v, 2);
}

IOReturn CezanneGPUUserClient::sdmaStart(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[2] = {};
    return putScalars(arguments, self->gpu_->sdmaStart(self, &v[0], &v[1]), v, 2);
}

IOReturn CezanneGPUUserClient::sdmaSubmit(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint64_t frame = arguments->scalarInput[0];
    if (frame > 3) {
        return kIOReturnBadArgument;
    }
    uint32_t v[5] = {};
    return putScalars(arguments,
                      self->gpu_->sdmaSubmit(self, static_cast<uint32_t>(frame), &v[0], &v[1], &v[2], &v[3], &v[4]),
                      v, 5);
}

IOReturn CezanneGPUUserClient::sdmaVerify(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[4] = {};
    return putScalars(arguments, self->gpu_->sdmaVerify(self, &v[0], &v[1], &v[2], &v[3]), v, 4);
}

IOReturn CezanneGPUUserClient::sdmaStop(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[4] = {};
    return putScalars(arguments, self->gpu_->sdmaStop(self, &v[0], &v[1], &v[2], &v[3]), v, 4);
}

IOReturn CezanneGPUUserClient::gartCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[2] = {};
    return putScalars(arguments, self->gpu_->gartCheck(self, &v[0], &v[1]), v, 2);
}

IOReturn CezanneGPUUserClient::gartEnable(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[2] = {};
    return putScalars(arguments, self->gpu_->gartEnable(self, &v[0], &v[1]), v, 2);
}

IOReturn CezanneGPUUserClient::gartVerify(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::GartReport report;
    arguments->scalarOutput[0] = self->gpu_->gartVerify(self, &report);
    // The dispatch table fixes the structure size at sizeof(GartReport).
    UInt8 *out = static_cast<UInt8 *>(arguments->structureOutput);
    const UInt8 *in = reinterpret_cast<const UInt8 *>(&report);
    for (uint32_t i = 0; i < sizeof(report); i++) {
        out[i] = in[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::gartRestore(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[3] = {};
    return putScalars(arguments, self->gpu_->gartRestore(self, &v[0], &v[1], &v[2]), v, 3);
}

IOReturn CezanneGPUUserClient::intrCheck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[7] = {};
    return putScalars(arguments, self->gpu_->intrCheck(self, &v[0], &v[1], &v[2], &v[3]), v, 7);
}

IOReturn CezanneGPUUserClient::intrEnable(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[5] = {};
    return putScalars(arguments, self->gpu_->intrEnable(self, &v[0], &v[1]), v, 5);
}

IOReturn CezanneGPUUserClient::intrVerify(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    cezanne::IntrReport report;
    arguments->scalarOutput[0] = self->gpu_->intrVerify(self, &report);
    // The dispatch table fixes the structure size at sizeof(IntrReport).
    UInt8 *out = static_cast<UInt8 *>(arguments->structureOutput);
    const UInt8 *in = reinterpret_cast<const UInt8 *>(&report);
    for (uint32_t i = 0; i < sizeof(report); i++) {
        out[i] = in[i];
    }
    return kIOReturnSuccess;
}

IOReturn CezanneGPUUserClient::intrAck(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[4] = {};
    return putScalars(arguments, self->gpu_->intrAck(self, &v[0], &v[1], &v[2], &v[3]), v, 4);
}

IOReturn CezanneGPUUserClient::intrRestore(OSObject *target, void *, IOExternalMethodArguments *arguments)
{
    CezanneGPUUserClient *self = static_cast<CezanneGPUUserClient *>(target);
    uint32_t v[6] = {};
    return putScalars(arguments, self->gpu_->intrRestore(self, &v[0], &v[1], &v[2]), v, 6);
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
        {pspRingCheck, 0, 0, 7, 0},   // kDiagnosticPspRingCheck
        {pspRingCreate, 0, 0, 3, 0},  // kDiagnosticPspRingCreate
        {pspRingObserve, 0, 0, 8, 0}, // kDiagnosticPspRingObserve
        {pspRingDestroy, 0, 0, 7, 0}, // kDiagnosticPspRingDestroy
        {pspTmrSubmit, 0, 0, 7, 0},   // kDiagnosticPspTmrSubmit
        {pspTmrObserve, 0, 0, 3, 0},  // kDiagnosticPspTmrObserve
        {pspTmrTeardown, 0, 0, 6, 0}, // kDiagnosticPspTmrTeardown
        {sdmaLoad, 0, 0, 6, 0},       // kDiagnosticSdmaLoad
        {sdmaObserve, 0, 0, 7, 0},    // kDiagnosticSdmaObserve
        {sdmaInventory, 0, 0, 3, sizeof(cezanne::SdmaInventory)}, // kDiagnosticSdmaInventory
        {sdmaCopyCheck, 0, 0, 3, 0}, // kDiagnosticSdmaCopyCheck
        {sdmaStart, 0, 0, 3, 0},     // kDiagnosticSdmaStart
        {sdmaSubmit, 1, 0, 6, 0},    // kDiagnosticSdmaSubmit
        {sdmaVerify, 0, 0, 5, 0},    // kDiagnosticSdmaVerify
        {sdmaStop, 0, 0, 5, 0},      // kDiagnosticSdmaStop
        {gartCheck, 0, 0, 3, 0},     // kDiagnosticGartCheck
        {gartEnable, 0, 0, 3, 0},    // kDiagnosticGartEnable
        {gartVerify, 0, 0, 1, sizeof(cezanne::GartReport)}, // kDiagnosticGartVerify
        {gartRestore, 0, 0, 4, 0},   // kDiagnosticGartRestore
        {intrCheck, 0, 0, 8, 0},     // kDiagnosticIntrCheck
        {intrEnable, 0, 0, 6, 0},    // kDiagnosticIntrEnable
        {intrVerify, 0, 0, 1, sizeof(cezanne::IntrReport)}, // kDiagnosticIntrVerify
        {intrAck, 0, 0, 5, 0},       // kDiagnosticIntrAck
        {intrRestore, 0, 0, 7, 0},   // kDiagnosticIntrRestore
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
    // Every connection's close already restored; the handler must not
    // outlive the driver in any case.
    if (lock_ != nullptr) {
        IOLockLock(lock_);
        removeIntrSourceLocked();
        IOLockUnlock(lock_);
    }
    IOService::stop(provider);
}
