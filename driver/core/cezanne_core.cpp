#include "cezanne_core.h"

namespace cezanne {

// PCI Local Bus / PCI Power Management specification constants.
static const uint8_t kCfgVendor = 0x00, kCfgDevice = 0x02, kCfgCommand = 0x04, kCfgStatus = 0x06,
                     kCfgRevision = 0x08, kCfgCapabilities = 0x34;
static const uint16_t kCommandMemorySpace = 0x0002;
static const uint16_t kStatusCapabilityList = 0x0010;
static const uint8_t kCapabilityPower = 0x01;
static const uint16_t kPowerStateMask = 0x0003; // PMCSR bits 1:0, 0 = D0
// 48 entries of at least 4 bytes fill the 192 bytes after the header.
static const int kMaxCapabilities = 48;

const char *statusName(Status status)
{
    switch (status) {
    case kOK: return "ok";
    case kConfigReadFailed: return "config-read-failed";
    case kIdentityMismatch: return "identity-mismatch";
    case kNoCapabilityList: return "no-capability-list";
    case kCapabilityListMalformed: return "capability-list-malformed";
    case kNoPowerCapability: return "no-power-capability";
    case kNotInD0: return "not-in-d0";
    case kMemoryDecodeDisabled: return "memory-decode-disabled";
    case kBarNotMemory32: return "bar5-not-32-bit-memory";
    case kApertureUnavailable: return "bar5-mapping-unavailable";
    case kBarMismatch: return "bar5-mapping-mismatch";
    case kApertureTooSmall: return "aperture-too-small";
    case kRegisterNotAllowed: return "register-not-allowed";
    case kRegisterReadFailed: return "register-read-failed";
    case kDeviceNotResponding: return "device-not-responding";
    case kProviderOpenFailed: return "provider-open-failed";
    case kCarveoutInvalid: return "carveout-invalid";
    case kDeviceRangesMalformed: return "device-ranges-malformed";
    case kCarveoutOverlapsDevice: return "carveout-overlaps-device";
    case kTableUnavailable: return "table-mapping-unavailable";
    case kDiscoverySignature: return "discovery-signature";
    case kDiscoveryChecksum: return "discovery-checksum";
    case kDiscoveryMalformed: return "discovery-malformed";
    case kDiscoveryUnsupported: return "discovery-unsupported";
    case kDiscoveryBaseMismatch: return "discovery-base-mismatch";
    case kGcInfoUnavailable: return "gc-info-unavailable";
    case kGfxIndexNotSe0Sh0: return "gfx-index-not-se0-sh0";
    case kGfxNotOn: return "gfx-not-on";
    case kCpNotHalted: return "cp-not-halted";
    case kRlcEnabled: return "rlc-enabled";
    case kGfxBusy: return "gfx-busy";
    case kScratchUnstable: return "scratch-unstable";
    case kRegisterWriteFailed: return "register-write-failed";
    case kScratchReadbackMismatch: return "scratch-readback-mismatch";
    case kScratchRestoreMismatch: return "scratch-restore-mismatch";
    case kScratchOutOfOrder: return "scratch-out-of-order";
    case kSmuBusy: return "smu-busy";
    case kSmuTimeout: return "smu-timeout";
    case kSmuResponseNotOk: return "smu-response-not-ok";
    case kSmuOutOfOrder: return "smu-out-of-order";
    case kGfxOffTimeout: return "gfxoff-timeout";
    case kMetricsAddressMismatch: return "metrics-address-mismatch";
    case kMetricsTargetInvalid: return "metrics-target-invalid";
    case kTableRegionInUse: return "table-region-in-use";
    case kTableNotWritten: return "table-not-written";
    case kTableOverflow: return "table-overflow";
    case kMetricsOutOfOrder: return "metrics-out-of-order";
    case kPspNotRunning: return "psp-not-running";
    case kPspNotReady: return "psp-not-ready";
    case kPspRingExists: return "psp-ring-exists";
    case kPspTimeout: return "psp-timeout";
    case kPspResponseNotOk: return "psp-response-not-ok";
    case kPspRegionChanged: return "psp-region-changed";
    case kPspOutOfOrder: return "psp-out-of-order";
    case kPspReadbackMismatch: return "psp-readback-mismatch";
    case kPspFenceTimeout: return "psp-fence-timeout";
    case kPspCommandFailed: return "psp-command-failed";
    case kSdmaImageInvalid: return "sdma-image-invalid";
    case kSdmaNotHalted: return "sdma-not-halted";
    case kSdmaOutOfOrder: return "sdma-out-of-order";
    case kSdmaUnexpectedState: return "sdma-unexpected-state";
    case kSdmaTimeout: return "sdma-timeout";
    case kSdmaVerifyFailed: return "sdma-verify-failed";
    case kGartUnexpectedState: return "gart-unexpected-state";
    case kGartSemaphoreTimeout: return "gart-semaphore-timeout";
    case kGartAckTimeout: return "gart-ack-timeout";
    case kGartFault: return "gart-fault";
    case kIhNoTrap: return "ih-no-trap";
    case kGartVerifyFailed: return "gart-verify-failed";
    case kGartNotRestored: return "gart-not-restored";
    case kGartOutOfOrder: return "gart-out-of-order";
    case kIntrNoMsi: return "intr-no-msi";
    case kIntrUnexpectedState: return "intr-unexpected-state";
    case kIntrSourceFailed: return "intr-source-failed";
    case kIntrNotDelivered: return "intr-not-delivered";
    case kIntrVerifyFailed: return "intr-verify-failed";
    case kIntrRefired: return "intr-refired";
    case kIntrNotRestored: return "intr-not-restored";
    case kIntrOutOfOrder: return "intr-out-of-order";
    case kDisplayUnexpectedState: return "display-unexpected-state";
    case kDisplayFlipTimeout: return "display-flip-timeout";
    case kDisplayVerifyFailed: return "display-verify-failed";
    case kDisplayNotRestored: return "display-not-restored";
    case kDisplayOutOfOrder: return "display-out-of-order";
    case kFlipUnexpectedState: return "flip-unexpected-state";
    case kFlipFillMismatch: return "flip-fill-mismatch";
    case kFlipIntrNotDelivered: return "flip-intr-not-delivered";
    case kFlipIntrVerifyFailed: return "flip-intr-verify-failed";
    case kFlipVerifyFailed: return "flip-verify-failed";
    case kFlipNotRestored: return "flip-not-restored";
    case kFlipOutOfOrder: return "flip-out-of-order";
    }
    return "unknown";
}

static bool read(const ConfigReader &config, uint8_t offset, uint8_t width, uint32_t *value)
{
    *value = 0;
    return config.read(config.context, offset, width, value);
}

Status readPciState(const ConfigReader &config, PciState *state)
{
    *state = PciState();
    uint32_t value = 0;
    if (!read(config, kCfgVendor, 2, &value)) return kConfigReadFailed;
    state->vendor = static_cast<uint16_t>(value);
    if (!read(config, kCfgDevice, 2, &value)) return kConfigReadFailed;
    state->device = static_cast<uint16_t>(value);
    if (!read(config, kCfgRevision, 4, &value)) return kConfigReadFailed;
    state->revision = static_cast<uint8_t>(value);
    state->classCode = value >> 8;
    if (!read(config, kCfgCommand, 2, &value)) return kConfigReadFailed;
    state->command = static_cast<uint16_t>(value);
    if (!read(config, kCfgStatus, 2, &value)) return kConfigReadFailed;
    state->status = static_cast<uint16_t>(value);
    if (!read(config, kRegisterBar, 4, &value)) return kConfigReadFailed;
    state->bar5 = value;

    if ((state->status & kStatusCapabilityList) == 0) return kNoCapabilityList;
    if (!read(config, kCfgCapabilities, 1, &value)) return kConfigReadFailed;
    uint8_t offset = static_cast<uint8_t>(value & 0xFC);
    for (int i = 0; offset != 0; i++) {
        if (i == kMaxCapabilities || offset < 0x40) return kCapabilityListMalformed;
        if (!read(config, offset, 2, &value)) return kConfigReadFailed;
        if ((value & 0xFF) == kCapabilityPower) {
            state->powerCapability = offset;
            if (!read(config, static_cast<uint8_t>(offset + 4), 2, &value)) return kConfigReadFailed;
            state->powerControl = static_cast<uint16_t>(value);
            return kOK;
        }
        offset = static_cast<uint8_t>((value >> 8) & 0xFC);
    }
    return kNoPowerCapability;
}

Status checkPciState(const PciState &state)
{
    if (state.vendor != kVendorAMD || state.device != kDeviceCezanne || state.revision != kRevisionTarget)
        return kIdentityMismatch;
    if (state.powerCapability == 0) return kNoPowerCapability;
    if ((state.powerControl & kPowerStateMask) != 0) return kNotInD0;
    if ((state.command & kCommandMemorySpace) == 0) return kMemoryDecodeDisabled;
    // Bit 0 clear: memory space; bits 2:1 zero: 32-bit decoder.
    if ((state.bar5 & 0x7) != 0) return kBarNotMemory32;
    return kOK;
}

static uint64_t requiredAperture()
{
    uint64_t end = 0;
    for (uint32_t i = 0; i < kStage1RegisterCount; i++) {
        if (kStage1Registers[i] + 4ull > end) end = kStage1Registers[i] + 4ull;
    }
    return end;
}

Status checkAperture(const PciState &state, uint64_t physical, uint64_t length)
{
    if ((state.bar5 & ~0xFull) != physical) return kBarMismatch;
    if (length < requiredAperture()) return kApertureTooSmall;
    return kOK;
}

bool registerAllowed(uint32_t offset, uint32_t stage)
{
    // Each stage's list extends the previous one (checked by the tests), so a
    // prefix of the stage 16 list is the list for any stage.
    uint32_t count = stage >= 16  ? kStage16RegisterCount
                     : stage >= 14 ? kStage14RegisterCount
                     : stage >= 13 ? kStage13RegisterCount
                     : stage >= 10 ? kStage10RegisterCount
                     : stage >= 6 ? kStage6RegisterCount
                     : stage == 5 ? kStage5RegisterCount
                     : stage >= 3 ? kStage3RegisterCount
                     : stage == 2 ? kStage2RegisterCount
                     : stage == 1 ? kStage1RegisterCount
                                  : 0;
    for (uint32_t i = 0; i < count; i++) {
        if (kStage16Registers[i] == offset) return true;
    }
    for (uint32_t i = 0; stage >= kGartStage && i < kStage17RegisterCount; i++) {
        if (kStage17Registers[i] == offset) return true;
    }
    for (uint32_t i = 0; stage >= kDisplayStage && i < kStage19RegisterCount; i++) {
        if (kStage19Registers[i] == offset) return true;
    }
    for (uint32_t i = 0; stage >= kFlipStage && i < kStage20RegisterCount; i++) {
        if (kStage20Registers[i] == offset) return true;
    }
    return false;
}

static Status readRegister(const RegisterReader &registers, uint64_t length, uint32_t stage, uint32_t offset,
                           uint32_t *value)
{
    *value = 0;
    if (!registerAllowed(offset, stage) || (offset & 3) != 0 || offset + 4ull > length) return kRegisterNotAllowed;
    return registers.read32(registers.context, offset, value) ? kOK : kRegisterReadFailed;
}

Status readAllowedRegister(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           uint32_t offset, uint32_t *value)
{
    return readRegister(registers, apertureLength, stage, offset, value);
}

bool gfxGated(uint32_t offset)
{
    for (uint32_t i = 0; i < kGfxGatedRegisterCount; i++) {
        if (kGfxGatedRegisters[i] == offset) return true;
    }
    for (uint32_t i = 0; i < kStage10GfxGatedRegisterCount; i++) {
        if (kStage10GfxGatedRegisters[i] == offset) return true;
    }
    return false;
}

Status readDiagnosticRegister(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                              uint32_t offset, uint32_t *value)
{
    *value = 0;
    if (!registerAllowed(offset, stage)) return kRegisterNotAllowed;
    if (gfxGated(offset)) {
        uint32_t misc = 0;
        Status status = readRegister(registers, apertureLength, stage, kRegSmuioGfxMiscCntl, &misc);
        if (status != kOK) return status;
        if (((misc & kGfxOffStatusMask) >> kGfxOffStatusShift) != kGfxOffStatusOn) return kGfxNotOn;
    }
    return readRegister(registers, apertureLength, stage, offset, value);
}

Status readBootState(const RegisterReader &registers, uint64_t apertureLength, BootState *state)
{
    *state = BootState();
    Status status = readRegister(registers, apertureLength, 1, kRegC2PMsg33, &state->c2pmsg33);
    if (status != kOK) return status;
    status = readRegister(registers, apertureLength, 1, kRegConfigMemsize, &state->configMemsize);
    if (status != kOK) return status;
    // A device that has stopped decoding returns all ones for every read.
    if (state->c2pmsg33 == 0xFFFFFFFFu && state->configMemsize == 0xFFFFFFFFu) return kDeviceNotResponding;
    state->ifwiReady = (state->c2pmsg33 & kC2PMsg33IfwiReady) != 0;
    state->vramBytes = static_cast<uint64_t>(state->configMemsize) << 20;
    return kOK;
}

Status readCarveout(const RegisterReader &registers, uint64_t apertureLength, const BootState &boot,
                    Carveout *carveout)
{
    *carveout = Carveout();
    Status status = readRegister(registers, apertureLength, 2, kRegMcVmFbOffset, &carveout->fbOffset);
    if (status != kOK) return status;
    if (carveout->fbOffset == 0xFFFFFFFFu) return kDeviceNotResponding;
    // Reserved bits set, no offset, or a carveout too small to hold the table.
    if ((carveout->fbOffset & ~kFbOffsetMask) != 0 || carveout->fbOffset == 0 ||
        boot.vramBytes < kDiscoveryTmrOffset || boot.configMemsize > 0x100000u)
        return kCarveoutInvalid;
    carveout->base = static_cast<uint64_t>(carveout->fbOffset) << kFbOffsetShift;
    carveout->size = boot.vramBytes;
    if (carveout->base + carveout->size > kPhysicalLimit) return kCarveoutInvalid;
    carveout->table = carveout->base + carveout->size - kDiscoveryTmrOffset;
    return kOK;
}

static uint32_t le32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

static uint16_t le16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

Status parseAssignedAddresses(const uint8_t *data, uint32_t length, Range *ranges, uint32_t capacity,
                              uint32_t *count)
{
    *count = 0;
    if (data == nullptr || length == 0 || length % 20 != 0) return kDeviceRangesMalformed;
    for (uint32_t offset = 0; offset < length; offset += 20) {
        uint32_t space = (le32(data + offset) >> 24) & 0x3; // 1 I/O, 2 32-bit, 3 64-bit memory
        uint64_t base = (static_cast<uint64_t>(le32(data + offset + 4)) << 32) | le32(data + offset + 8);
        uint64_t size = (static_cast<uint64_t>(le32(data + offset + 12)) << 32) | le32(data + offset + 16);
        if (space == 0) return kDeviceRangesMalformed;
        if (space == 1) continue;
        if (size == 0 || base + size < base || *count == capacity) return kDeviceRangesMalformed;
        ranges[(*count)++] = Range{base, size};
    }
    return *count == 0 ? kDeviceRangesMalformed : kOK;
}

Status checkCarveout(const Carveout &carveout, const Range *ranges, uint32_t count)
{
    if (carveout.size == 0) return kCarveoutInvalid;
    for (uint32_t i = 0; i < count; i++) {
        if (ranges[i].base < carveout.base + carveout.size && carveout.base < ranges[i].base + ranges[i].length)
            return kCarveoutOverlapsDevice;
    }
    return kOK;
}

// Discovery binary layout (discovery.h, v6.12; all fields little-endian).
static const uint32_t kBinarySignature = 0x28211407u, kTableSignature = 0x53445049u;
static const uint32_t kBinaryHeaderSize = 12 + 6 * 8;       // binary_header
static const uint32_t kBinaryChecksumStart = 10;            // after binary_checksum
static const uint32_t kIpHeaderSize = 4 + 2 + 2 + 4 + 2 + 16 * 4 + 2; // ip_discovery_header
static const uint32_t kIpEntrySize = 8;                     // struct ip without base_address[]
static const uint32_t kMaxDies = 16, kMaxIps = 256;
static const uint32_t kGcTableId = 0x4347; // GC_TABLE_ID

static uint16_t byteSum(const uint8_t *data, uint32_t size)
{
    uint16_t sum = 0;
    for (uint32_t i = 0; i < size; i++) sum = static_cast<uint16_t>(sum + data[i]);
    return sum;
}

Status parseDiscovery(const uint8_t *binary, uint32_t length, Discovery *d)
{
    *d = Discovery();
    if (binary == nullptr || length < kBinaryHeaderSize) return kDiscoveryMalformed;
    if (le32(binary) != kBinarySignature) return kDiscoverySignature;
    d->versionMajor = le16(binary + 4);
    d->versionMinor = le16(binary + 6);
    d->binarySize = le16(binary + 10);
    uint32_t size = d->binarySize;
    if (size < kBinaryHeaderSize || size > length) return kDiscoveryMalformed;
    if (byteSum(binary + kBinaryChecksumStart, size - kBinaryChecksumStart) != le16(binary + 8))
        return kDiscoveryChecksum;
    d->binaryValid = true;

    // table_list[IP_DISCOVERY] starts at byte 12: offset, checksum, size.
    uint32_t table = le16(binary + 12);
    if (table < kBinaryHeaderSize || table + kIpHeaderSize > size) return kDiscoveryMalformed;
    const uint8_t *ihdr = binary + table;
    if (le32(ihdr) != kTableSignature) return kDiscoverySignature;
    d->tableVersion = le16(ihdr + 4);
    uint32_t tableSize = le16(ihdr + 6);
    if (tableSize < kIpHeaderSize || table + tableSize > size) return kDiscoveryMalformed;
    if (byteSum(ihdr, tableSize) != le16(binary + 14)) return kDiscoveryChecksum;
    // Version 4 may store 64-bit base addresses (flag bit 0 at byte 78).
    if (d->tableVersion >= 4 && (ihdr[78] & 1) != 0) return kDiscoveryUnsupported;
    d->numDies = le16(ihdr + 12);
    if (d->numDies == 0 || d->numDies > kMaxDies) return kDiscoveryMalformed;

    // Like Linux, die and IP entries are bounded by the binary, not the table.
    uint32_t end = size;
    for (uint32_t die = 0; die < d->numDies; die++) {
        uint32_t dieOffset = le16(ihdr + 14 + 4 * die + 2);
        if (dieOffset < kBinaryHeaderSize || dieOffset + 4 > end) return kDiscoveryMalformed;
        if (le16(binary + dieOffset) != die) return kDiscoveryMalformed;
        uint32_t ips = le16(binary + dieOffset + 2);
        uint32_t ip = dieOffset + 4;
        for (uint32_t j = 0; j < ips; j++) {
            if (d->numIps == kMaxIps || ip + kIpEntrySize > end) return kDiscoveryMalformed;
            const uint8_t *entry = binary + ip;
            uint16_t hwId = le16(entry);
            uint8_t instance = entry[2], bases = entry[3];
            if (ip + kIpEntrySize + 4u * bases > end) return kDiscoveryMalformed;
            const uint8_t *base = entry + kIpEntrySize;
            if (hwId == kHwIdGc && instance == 0 && !d->gcFound && bases >= 2) {
                d->gcFound = true;
                d->gcMajor = entry[4];
                d->gcMinor = entry[5];
                d->gcRevision = entry[6];
                d->gcBase0 = le32(base);
                d->gcBase1 = le32(base + 4);
            } else if (hwId == kHwIdMp0 && instance == 0 && !d->mp0Found && bases >= 1) {
                d->mp0Found = true;
                d->mp0Base0 = le32(base);
            }
            d->numIps++;
            ip += kIpEntrySize + 4u * bases;
        }
    }
    // table_list[GC] (byte 20): gpu_info_header then gc_info_v2_x fields.
    uint32_t gc = le16(binary + 20);
    if (gc >= kBinaryHeaderSize && gc + 12 + 16 <= size && le32(binary + gc) == kGcTableId) {
        d->gcInfoMajor = le16(binary + gc + 4);
        d->gcInfoMinor = le16(binary + gc + 6);
        if (d->gcInfoMajor == 2) {
            d->gcInfoFound = true;
            d->gcNumSe = le32(binary + gc + 12);
            d->gcCuPerSh = le32(binary + gc + 16);
            d->gcShPerSe = le32(binary + gc + 20);
            d->gcRbPerSe = le32(binary + gc + 24);
        }
    }
    if (!d->gcFound || !d->mp0Found) return kDiscoveryMalformed;
    if (d->gcBase0 != kExpectedGcBase0 || d->gcBase1 != kExpectedGcBase1 || d->mp0Base0 != kExpectedMp0Base0)
        return kDiscoveryBaseMismatch;
    return kOK;
}

static uint32_t bitmask(uint32_t bits)
{
    return bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
}

static uint32_t popcount(uint32_t value)
{
    uint32_t count = 0;
    for (; value != 0; value &= value - 1) count++;
    return count;
}

Status readGfxConfig(const RegisterReader &registers, uint64_t apertureLength, const Discovery &discovery,
                     GfxConfig *config)
{
    *config = GfxConfig();
    struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegGrbmStatus, &config->grbmStatus},
        {kRegGrbmGfxIndex, &config->grbmGfxIndex},
        {kRegCcShaderArrayConfig, &config->ccShaderArrayConfig},
        {kRegUserShaderArrayConfig, &config->userShaderArrayConfig},
        {kRegCcRbBackendDisable, &config->ccRbBackendDisable},
        {kRegUserRbBackendDisable, &config->userRbBackendDisable},
        {kRegGbAddrConfig, &config->gbAddrConfig},
    };
    bool allOnes = true;
    for (auto &read : reads) {
        Status status = readRegister(registers, apertureLength, 3, read.offset, read.value);
        if (status != kOK) return status;
        allOnes = allOnes && *read.value == 0xFFFFFFFFu;
    }
    if (allOnes) return kDeviceNotResponding;
    config->guiActive = (config->grbmStatus & 0x80000000u) != 0;

    if (!discovery.gcInfoFound || discovery.gcNumSe != 1 || discovery.gcShPerSe != 1 || discovery.gcCuPerSh == 0 ||
        discovery.gcCuPerSh > 16 || discovery.gcRbPerSe == 0 || discovery.gcRbPerSe > 8)
        return kGcInfoUnavailable;
    // SE_INDEX (23:16) and SH_INDEX (15:8) select where reads come from; the
    // BROADCAST bits name writes only.
    if ((config->grbmGfxIndex & 0x00FFFF00u) != 0) return kGfxIndexNotSe0Sh0;

    uint32_t inactiveCus = ((config->ccShaderArrayConfig | config->userShaderArrayConfig) & 0xFFFF0000u) >> 16;
    config->cuActiveMask = ~inactiveCus & bitmask(discovery.gcCuPerSh);
    uint32_t disabledRbs = ((config->ccRbBackendDisable | config->userRbBackendDisable) & 0x00FF0000u) >> 16;
    config->rbActiveMask = ~disabledRbs & bitmask(discovery.gcRbPerSe / discovery.gcShPerSe);
    config->cuActiveCount = popcount(config->cuActiveMask);
    config->rbActiveCount = popcount(config->rbActiveMask);
    return kOK;
}

bool writeAllowed(uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage >= kScratchStage && offset == kRegScratchReg0) return true;
    if (stage < kSmuStage) return false;
    if (stage >= kSdmaCopyStage && sdmaWriteListed(offset, value)) return true;
    if (stage >= kGartStage && gartWriteListed(offset, value)) return true;
    if (stage >= kIntrStage && intrWriteListed(offset, value)) return true;
    if (stage >= kDisplayStage && displayWriteListed(offset, value)) return true;
    if (stage >= kFlipStage && flipWriteListed(offset, value)) return true;
    if (offset == kRegMp1C2PMsg90) return value == 0;
    if (offset == kRegMp1C2PMsg82)
        return value == 0 || (stage >= kMetricsStage && (value == uint32_t(kMetricsGpuAddress >> 32) ||
                                                         value == uint32_t(kMetricsGpuAddress) ||
                                                         value == kTableSmuMetrics));
    if (offset == kRegMp1C2PMsg66)
        return value == kSmuMsgGetSmuVersion || value == kSmuMsgGetDriverIfVersion ||
               (stage >= kGfxOffStage && value == kSmuMsgDisableGfxOff) ||
               (stage >= kMetricsStage && (value == kSmuMsgSetDriverDramAddrHigh ||
                                           value == kSmuMsgSetDriverDramAddrLow ||
                                           value == kSmuMsgTransferTableSmu2Dram)) ||
               (stage >= kSdmaInventoryStage && (value == kSmuMsgPowerUpSdma || value == kSmuMsgPowerDownSdma));
    if (stage < kPspRingStage) return false;
    if (offset == kRegMp0C2PMsg64) return value == kPspCmdInitGpcomRing || value == kPspCmdDestroyRings;
    if (offset == kRegMp0C2PMsg69) return value == uint32_t(kPspRingGpuAddress);
    if (offset == kRegMp0C2PMsg70) return value == uint32_t(kPspRingGpuAddress >> 32);
    if (offset == kRegMp0C2PMsg71) return value == kPspRingSize;
    if (stage < kPspTmrStage) return false;
    if (offset == kRegMp0C2PMsg67)
        return value == kPspFrameDwords || value == 2 * kPspFrameDwords ||
               (stage >= kPspSdmaStage && value == 3 * kPspFrameDwords);
    return false;
}

bool smuArgumentAllowed(uint32_t message, uint32_t argument, uint32_t stage)
{
    switch (message) {
    case kSmuMsgGetSmuVersion:
    case kSmuMsgGetDriverIfVersion: return stage >= kSmuStage && argument == 0;
    case kSmuMsgDisableGfxOff: return stage >= kGfxOffStage && argument == 0;
    case kSmuMsgSetDriverDramAddrHigh: return stage >= kMetricsStage && argument == uint32_t(kMetricsGpuAddress >> 32);
    case kSmuMsgSetDriverDramAddrLow: return stage >= kMetricsStage && argument == uint32_t(kMetricsGpuAddress);
    case kSmuMsgTransferTableSmu2Dram: return stage >= kMetricsStage && argument == kTableSmuMetrics;
    case kSmuMsgPowerUpSdma:
    case kSmuMsgPowerDownSdma: return stage >= kSdmaInventoryStage && argument == 0;
    default: return false;
    }
}

// The only write site in the core.
static Status writeRegister(const RegisterWriter &writer, uint32_t stage, uint32_t offset, uint32_t value)
{
    if (!writeAllowed(offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

Status checkScratch(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    uint32_t stage, ScratchCheck *check)
{
    *check = ScratchCheck();
    if (!writeAllowed(kRegScratchReg0, kScratchPattern, stage)) return kRegisterNotAllowed;
    Status status = readRegister(registers, apertureLength, stage, kRegSmuioGfxMiscCntl, &check->gfxMisc);
    if (status != kOK) return status;
    if (((check->gfxMisc & kGfxOffStatusMask) >> kGfxOffStatusShift) != kGfxOffStatusOn) return kGfxNotOn;
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegCpMeCntl, &check->cpMeCntl},
        {kRegCpMecCntl, &check->cpMecCntl},
        {kRegRlcCntl, &check->rlcCntl},
        {kRegGrbmStatus, &check->grbmStatus},
        {kRegScratchReg0, &check->original},
    };
    for (const auto &read : reads) {
        status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    if ((check->cpMeCntl & kCpMeHalts) != kCpMeHalts || (check->cpMecCntl & kCpMecHalts) != kCpMecHalts)
        return kCpNotHalted;
    if (check->rlcCntl != 0) return kRlcEnabled;
    if ((check->grbmStatus & kGrbmGuiActive) != 0) return kGfxBusy;
    writer.pause(writer.context);
    status = readRegister(registers, apertureLength, stage, kRegScratchReg0, &check->original2);
    if (status != kOK) return status;
    return check->original2 == check->original ? kOK : kScratchUnstable;
}

Status writeScratchPattern(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                           uint32_t stage, uint32_t original, uint32_t *readback)
{
    *readback = 0;
    ScratchCheck check;
    Status status = checkScratch(registers, apertureLength, writer, stage, &check);
    if (status != kOK) return status;
    if (check.original != original) return kScratchUnstable;
    status = writeRegister(writer, stage, kRegScratchReg0, kScratchPattern);
    if (status != kOK) return status;
    status = readRegister(registers, apertureLength, stage, kRegScratchReg0, readback);
    if (status != kOK) return status;
    return *readback == kScratchPattern ? kOK : kScratchReadbackMismatch;
}

Status restoreScratch(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t original, uint32_t *readback)
{
    *readback = 0;
    Status status = writeRegister(writer, stage, kRegScratchReg0, original);
    if (status != kOK) return status;
    status = readRegister(registers, apertureLength, stage, kRegScratchReg0, readback);
    if (status != kOK) return status;
    return *readback == original ? kOK : kScratchRestoreMismatch;
}

Status checkSmu(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, SmuMailbox *mailbox)
{
    *mailbox = SmuMailbox();
    if (stage < kSmuStage) return kRegisterNotAllowed;
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegMp1C2PMsg66, &mailbox->message},
        {kRegMp1C2PMsg82, &mailbox->argument},
        {kRegMp1C2PMsg90, &mailbox->response},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    return mailbox->response == 0 ? kSmuBusy : kOK;
}

// smu_cmn_send_smc_msg_with_param. The answer is read only if requested and
// only after an OK response.
static Status sendSmuMessage(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                             uint32_t stage, uint32_t message, uint32_t argument, uint32_t *response,
                             uint32_t *answer)
{
    if (!smuArgumentAllowed(message, argument, stage)) return kRegisterNotAllowed;
    SmuMailbox mailbox;
    Status status = checkSmu(registers, apertureLength, stage, &mailbox);
    if (status != kOK) return status;
    // __smu_cmn_send_msg: response, argument, then message.
    status = writeRegister(writer, stage, kRegMp1C2PMsg90, 0);
    if (status == kOK) status = writeRegister(writer, stage, kRegMp1C2PMsg82, argument);
    if (status == kOK) status = writeRegister(writer, stage, kRegMp1C2PMsg66, message);
    if (status != kOK) return status;
    // __smu_cmn_poll_stat.
    for (uint32_t i = 0; i <= kSmuPollPauses; i++) {
        status = readRegister(registers, apertureLength, stage, kRegMp1C2PMsg90, response);
        if (status != kOK) return status;
        if (*response != 0) break;
        if (i == kSmuPollPauses) return kSmuTimeout;
        writer.pause(writer.context);
    }
    if (*response != kSmuResponseOk) return kSmuResponseNotOk;
    if (answer == nullptr) return kOK;
    // smu_cmn_read_arg.
    return readRegister(registers, apertureLength, stage, kRegMp1C2PMsg82, answer);
}

Status sendSmuQuery(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    uint32_t stage, uint32_t message, uint32_t *response, uint32_t *answer)
{
    *response = 0;
    *answer = 0;
    if (stage < kSmuStage || (message != kSmuMsgGetSmuVersion && message != kSmuMsgGetDriverIfVersion))
        return kRegisterNotAllowed;
    return sendSmuMessage(registers, apertureLength, writer, stage, message, 0, response, answer);
}

Status disallowGfxOff(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t *response, uint32_t *gfxMisc)
{
    *response = 0;
    *gfxMisc = 0;
    if (stage < kGfxOffStage) return kRegisterNotAllowed;
    Status status =
        sendSmuMessage(registers, apertureLength, writer, stage, kSmuMsgDisableGfxOff, 0, response, nullptr);
    if (status != kOK) return status;
    // smu_v12_0_gfx_off_control: wait for GFX to be on.
    for (uint32_t i = 0; i <= kGfxOffConfirmPauses; i++) {
        status = readRegister(registers, apertureLength, stage, kRegSmuioGfxMiscCntl, gfxMisc);
        if (status != kOK) return status;
        if (((*gfxMisc & kGfxOffStatusMask) >> kGfxOffStatusShift) == kGfxOffStatusOn) return kOK;
        if (i == kGfxOffConfirmPauses) return kGfxOffTimeout;
        writer.pause(writer.context);
    }
    return kGfxOffTimeout;
}

static bool overlaps(uint64_t base, uint64_t length, uint64_t otherBase, uint64_t otherLength)
{
    return base < otherBase + otherLength && otherBase < base + length;
}

// The stage 9 checks for one fixed carveout page and its check region.
static Status checkCarveoutPage(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                                uint64_t carveoutOffset, uint32_t checkSize, const Range *ranges,
                                uint32_t rangeCount, MetricsTarget *target)
{
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegMmhubFbLocationBase, &target->fbLocationBase},
        {kRegMcVmFbOffset, &target->fbOffset},
        {kRegConfigMemsize, &target->configMemsize},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    if (target->fbLocationBase != kExpectedFbLocationBase || target->fbOffset != kExpectedFbOffset)
        return kMetricsAddressMismatch;
    target->gpuAddress = (uint64_t(target->fbLocationBase) << 24) + carveoutOffset;
    target->physical = (uint64_t(target->fbOffset) << 24) + carveoutOffset;
    // Inside the carveout, clear of its reserved low and high regions.
    uint64_t carveoutSize = uint64_t(target->configMemsize) << 20;
    if (target->configMemsize == 0xFFFFFFFFu || carveoutSize < kCarveoutLowReserve + kCarveoutHighReserve ||
        carveoutOffset < kCarveoutLowReserve || carveoutOffset + checkSize > carveoutSize - kCarveoutHighReserve)
        return kMetricsTargetInvalid;
    if (rangeCount == 0) return kMetricsTargetInvalid;
    for (uint32_t i = 0; i < rangeCount; i++) {
        if (overlaps(target->physical, checkSize, ranges[i].base, ranges[i].length)) return kMetricsTargetInvalid;
    }
    return kOK;
}

Status checkMetricsTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                          const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kMetricsStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kMetricsCarveoutOffset, kMetricsCheckSize,
                                      ranges, rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kMetricsGpuAddress || target->physical != kMetricsPhysical) return kMetricsAddressMismatch;
    return kOK;
}

Status checkRegionStable(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer,
                         uint32_t pauses, uint32_t *snapshot)
{
    for (uint32_t offset = 0; offset < length; offset += 4) {
        if (!memory.read32(memory.context, offset, &snapshot[offset / 4])) return kRegisterReadFailed;
    }
    for (uint32_t i = 0; i < pauses; i++) writer.pause(writer.context);
    for (uint32_t offset = 0; offset < length; offset += 4) {
        uint32_t value = 0;
        if (!memory.read32(memory.context, offset, &value)) return kRegisterReadFailed;
        if (value != snapshot[offset / 4]) return kTableRegionInUse;
    }
    return kOK;
}

Status requestMetrics(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t responses[3])
{
    responses[0] = responses[1] = responses[2] = 0;
    if (stage < kMetricsStage) return kRegisterNotAllowed;
    const struct {
        uint32_t message, argument;
    } messages[] = {
        {kSmuMsgSetDriverDramAddrHigh, uint32_t(kMetricsGpuAddress >> 32)},
        {kSmuMsgSetDriverDramAddrLow, uint32_t(kMetricsGpuAddress)},
        {kSmuMsgTransferTableSmu2Dram, kTableSmuMetrics},
    };
    for (uint32_t i = 0; i < 3; i++) {
        Status status = sendSmuMessage(registers, apertureLength, writer, stage, messages[i].message,
                                       messages[i].argument, &responses[i], nullptr);
        if (status != kOK) return status;
    }
    return kOK;
}

Status verifyMetricsPage(const MemoryReader &page, const uint32_t *snapshot, SmuMetrics *metrics)
{
    *metrics = SmuMetrics();
    // Everything after the table must be as it was before the transfer.
    for (uint32_t offset = kMetricsSize; offset < kPageSize; offset += 4) {
        uint32_t value = 0;
        if (!page.read32(page.context, offset, &value)) return kRegisterReadFailed;
        if (value != snapshot[offset / 4]) return kTableOverflow;
    }
    bool written = false;
    for (uint32_t offset = 0; offset < kMetricsSize; offset += 4) {
        uint32_t value = 0;
        if (!page.read32(page.context, offset, &value)) return kRegisterReadFailed;
        written = written || value != snapshot[offset / 4];
        metrics->words[offset / 2] = static_cast<uint16_t>(value);
        metrics->words[offset / 2 + 1] = static_cast<uint16_t>(value >> 16);
    }
    return written ? kOK : kTableNotWritten;
}

Status readPspMailbox(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                      PspMailbox *mailbox)
{
    *mailbox = PspMailbox();
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegMp0C2PMsg81, &mailbox->signOfLife}, {kRegMp0C2PMsg64, &mailbox->command},
        {kRegMp0C2PMsg67, &mailbox->writePointer}, {kRegMp0C2PMsg69, &mailbox->ringLow},
        {kRegMp0C2PMsg70, &mailbox->ringHigh}, {kRegMp0C2PMsg71, &mailbox->ringSize},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    return kOK;
}

bool pspCommandAllowed(uint32_t command, uint32_t low, uint32_t high, uint32_t size, uint32_t stage)
{
    if (stage < kPspRingStage) return false;
    switch (command) {
    case kPspCmdInitGpcomRing:
        return low == uint32_t(kPspRingGpuAddress) && high == uint32_t(kPspRingGpuAddress >> 32) &&
               size == kPspRingSize;
    case kPspCmdDestroyRings: return low == 0 && high == 0 && size == 0;
    default: return false;
    }
}

// The state stage 10 found after a cold boot: secure OS running, ready, no ring.
static Status checkPspIdle(const PspMailbox &mailbox)
{
    if (mailbox.signOfLife == 0) return kPspNotRunning;
    if ((mailbox.command & kPspResponseMask) != kPspResponseFlag) return kPspNotReady;
    if ((mailbox.writePointer | mailbox.ringLow | mailbox.ringHigh | mailbox.ringSize) != 0) return kPspRingExists;
    return kOK;
}

Status checkPspRing(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, const Range *ranges,
                    uint32_t rangeCount, PspMailbox *mailbox, MetricsTarget *target)
{
    *target = MetricsTarget();
    *mailbox = PspMailbox();
    if (stage < kPspRingStage) return kRegisterNotAllowed;
    Status status = readPspMailbox(registers, apertureLength, stage, mailbox);
    if (status == kOK) status = checkPspIdle(*mailbox);
    if (status != kOK) return status;
    status = checkCarveoutPage(registers, apertureLength, stage, kPspRingCarveoutOffset, kPspRingCheckSize, ranges,
                               rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kPspRingGpuAddress || target->physical != kPspRingPhysical) return kMetricsAddressMismatch;
    return kOK;
}

// psp_v12_0: arguments, then the command in C2PMSG_64, a 20 ms settle, and a
// poll for the response flag. written is set once the command is written.
// The PSP must have answered its previous command (flag set; kPspNotReady
// otherwise), whatever that answer's status: a destroy must still be sent
// after a rejected create. Linux checks nothing before writing.
static Status sendPspCommand(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                             uint32_t stage, uint32_t command, uint32_t low, uint32_t high, uint32_t size,
                             uint32_t *response, bool *written)
{
    *response = 0;
    *written = false;
    if (!pspCommandAllowed(command, low, high, size, stage)) return kRegisterNotAllowed;
    uint32_t ready = 0;
    Status status = readRegister(registers, apertureLength, stage, kRegMp0C2PMsg64, &ready);
    if (status != kOK) return status;
    if ((ready & kPspResponseFlag) == 0) return kPspNotReady;
    if (command == kPspCmdInitGpcomRing) {
        status = writeRegister(writer, stage, kRegMp0C2PMsg69, low);
        if (status == kOK) status = writeRegister(writer, stage, kRegMp0C2PMsg70, high);
    }
    if (status == kOK && command == kPspCmdInitGpcomRing) status = writeRegister(writer, stage, kRegMp0C2PMsg71, size);
    if (status == kOK) status = writeRegister(writer, stage, kRegMp0C2PMsg64, command);
    if (status != kOK) return status;
    *written = true;
    for (uint32_t i = 0; i < kPspSettlePauses; i++) writer.pause(writer.context);
    for (uint32_t i = 0; i <= kPspPollPauses; i++) {
        status = readRegister(registers, apertureLength, stage, kRegMp0C2PMsg64, response);
        if (status != kOK) return status;
        if ((*response & kPspResponseFlag) != 0) break;
        if (i == kPspPollPauses) return kPspTimeout;
        writer.pause(writer.context);
    }
    return (*response & kPspResponseMask) == kPspResponseFlag ? kOK : kPspResponseNotOk;
}

Status createPspRing(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                     uint32_t stage, uint32_t *response, bool *written)
{
    *response = 0;
    *written = false;
    if (stage < kPspRingStage) return kRegisterNotAllowed;
    PspMailbox mailbox;
    Status status = readPspMailbox(registers, apertureLength, stage, &mailbox);
    if (status == kOK) status = checkPspIdle(mailbox);
    if (status != kOK) return status;
    return sendPspCommand(registers, apertureLength, writer, stage, kPspCmdInitGpcomRing,
                          uint32_t(kPspRingGpuAddress), uint32_t(kPspRingGpuAddress >> 32), kPspRingSize, response,
                          written);
}

Status compareRegion(const MemoryReader &memory, uint32_t length, uint32_t pageSize, const uint32_t *snapshot,
                     uint32_t *changedInPage, uint32_t *changedOutside)
{
    *changedInPage = *changedOutside = 0;
    for (uint32_t offset = 0; offset < length; offset += 4) {
        uint32_t value = 0;
        if (!memory.read32(memory.context, offset, &value)) return kRegisterReadFailed;
        if (value != snapshot[offset / 4]) (offset < pageSize ? *changedInPage : *changedOutside) += 1;
    }
    return *changedInPage == 0 && *changedOutside == 0 ? kOK : kPspRegionChanged;
}

Status destroyPspRing(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, bool created, uint32_t *response)
{
    *response = 0;
    if (stage < kPspRingStage) return kRegisterNotAllowed;
    if (!created) return kPspOutOfOrder;
    bool written = false;
    return sendPspCommand(registers, apertureLength, writer, stage, kPspCmdDestroyRings, 0, 0, 0, response, &written);
}

uint32_t pspCommandWord(uint32_t command, uint32_t word)
{
    if (word == kPspCmdIdOffset / 4) return command;
    if (command == kGfxCmdLoadIpFw) {
        // psp_prep_load_ip_fw_cmd_buf: psp_gfx_cmd_load_ip_fw at +28.
        switch (word - kPspCmdFieldsOffset / 4) {
        case 0: return uint32_t(kSdmaFwGpuAddress);
        case 1: return uint32_t(kSdmaFwGpuAddress >> 32);
        case 2: return kSdmaUcodeSize;
        case 3: return kGfxFwTypeSdma0;
        default: return 0;
        }
    }
    if (command != kGfxCmdSetupTmr) return 0;
    // psp_prep_tmr_cmd_buf: psp_gfx_cmd_setup_tmr at +28.
    switch (word - kPspCmdFieldsOffset / 4) {
    case 0: return uint32_t(kPspTmrGpuAddress);
    case 1: return uint32_t(kPspTmrGpuAddress >> 32);
    case 2: return kPspTmrSize;
    case 3: return kPspTmrFlagVirtPhysAddr;
    case 4: return uint32_t(kPspTmrPhysical);
    case 5: return uint32_t(kPspTmrPhysical >> 32);
    default: return 0;
    }
}

uint32_t pspFrameWord(uint32_t frame, uint32_t word)
{
    // psp_ring_cmd_submit: cmd_buf_addr, cmd_buf_size 0, fence_addr, fence_value.
    switch (word) {
    case 0: return uint32_t(kPspCmdGpuAddress);
    case 1: return uint32_t(kPspCmdGpuAddress >> 32);
    case 3: return uint32_t(kPspFenceGpuAddress);
    case 4: return uint32_t(kPspFenceGpuAddress >> 32);
    case 5: return frame + 1;
    default: return 0;
    }
}

bool pspWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage < kPspTmrStage || (offset & 3) != 0 || offset >= kPspWorkSize) return false;
    if (offset >= kPspFencePage) return value == 0;
    if (offset >= kPspCmdPage) {
        uint32_t word = (offset - kPspCmdPage) / 4;
        return value == 0 || value == pspCommandWord(kGfxCmdSetupTmr, word) ||
               value == pspCommandWord(kGfxCmdDestroyTmr, word) ||
               (stage >= kPspSdmaStage && value == pspCommandWord(kGfxCmdLoadIpFw, word));
    }
    uint32_t frame = offset / kPspFrameSize;
    uint32_t frames = stage >= kPspSdmaStage ? 3 : 2;
    return frame < frames && (value == 0 || value == pspFrameWord(frame, (offset % kPspFrameSize) / 4));
}

static Status writeWork(const MemoryWriter &writer, uint32_t stage, uint32_t offset, uint32_t value)
{
    if (!pspWorkWriteAllowed(offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

Status checkPspTmrTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                         const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kPspTmrStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kPspTmrCarveoutOffset, kPspTmrSize, ranges,
                                      rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kPspTmrGpuAddress || target->physical != kPspTmrPhysical) return kMetricsAddressMismatch;
    return kOK;
}

static Status regionSum(const MemoryReader &memory, uint32_t length, uint64_t *sum)
{
    *sum = 0;
    for (uint32_t offset = 0; offset < length; offset += 4) {
        uint32_t value = 0;
        if (!memory.read32(memory.context, offset, &value)) return kRegisterReadFailed;
        // Position-dependent, so moved words also change the sum.
        *sum = (*sum ^ value) * 0x100000001B3ull + offset;
    }
    return kOK;
}

Status checkRegionChecksum(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer,
                           uint32_t pauses)
{
    uint64_t first = 0, second = 0;
    Status status = regionSum(memory, length, &first);
    if (status != kOK) return status;
    for (uint32_t i = 0; i < pauses; i++) writer.pause(writer.context);
    status = regionSum(memory, length, &second);
    if (status != kOK) return status;
    return first == second ? kOK : kTableRegionInUse;
}

static bool commandFrameAllowed(uint32_t command, uint32_t frame, uint32_t stage)
{
    if (stage < kPspTmrStage) return false;
    switch (command) {
    case kGfxCmdSetupTmr: return frame == 0;
    case kGfxCmdDestroyTmr: return frame == 1 || (stage >= kPspSdmaStage && frame == 2);
    case kGfxCmdLoadIpFw: return stage >= kPspSdmaStage && frame == 1;
    default: return false;
    }
}

Status writePspCommand(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage, uint32_t command,
                       uint32_t frame)
{
    if (!commandFrameAllowed(command, frame, stage)) return kRegisterNotAllowed;
    // Each pass writes, then reads back: the command page, the fence page
    // (SETUP_TMR only), and the frame.
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t word = 0; word < kPspRingSize / 4; word++) {
            uint32_t offset = kPspCmdPage + word * 4, expected = pspCommandWord(command, word);
            if (pass == 0) {
                Status status = writeWork(writer, stage, offset, expected);
                if (status != kOK) return status;
            } else {
                uint32_t value = 0;
                if (!work.read32(work.context, offset, &value)) return kRegisterReadFailed;
                if (value != expected) return kPspReadbackMismatch;
            }
        }
        for (uint32_t word = 0; command == kGfxCmdSetupTmr && word < kPspRingSize / 4; word++) {
            uint32_t offset = kPspFencePage + word * 4;
            if (pass == 0) {
                Status status = writeWork(writer, stage, offset, 0);
                if (status != kOK) return status;
            } else {
                uint32_t value = 0;
                if (!work.read32(work.context, offset, &value)) return kRegisterReadFailed;
                if (value != 0) return kPspReadbackMismatch;
            }
        }
        for (uint32_t word = 0; word < kPspFrameDwords; word++) {
            uint32_t offset = frame * kPspFrameSize + word * 4, expected = pspFrameWord(frame, word);
            if (pass == 0) {
                Status status = writeWork(writer, stage, offset, expected);
                if (status != kOK) return status;
            } else {
                uint32_t value = 0;
                if (!work.read32(work.context, offset, &value)) return kRegisterReadFailed;
                if (value != expected) return kPspReadbackMismatch;
            }
        }
    }
    return kOK;
}

Status submitPspFrame(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      const MemoryReader &work, uint32_t stage, uint32_t frame, uint32_t *fence)
{
    *fence = 0;
    if (stage < kPspTmrStage || frame > (stage >= kPspSdmaStage ? 2u : 1u)) return kRegisterNotAllowed;
    uint32_t pointer = 0;
    Status status = readRegister(registers, apertureLength, stage, kRegMp0C2PMsg67, &pointer);
    if (status != kOK) return status;
    if (pointer != frame * kPspFrameDwords) return kPspOutOfOrder;
    status = writeRegister(writer, stage, kRegMp0C2PMsg67, (pointer + kPspFrameDwords) % kPspRingDwords);
    if (status != kOK) return status;
    for (uint32_t i = 0; i <= kPspFencePollPauses; i++) {
        if (!work.read32(work.context, kPspFencePage, fence)) return kRegisterReadFailed;
        if (*fence == frame + 1) return kOK;
        if (i == kPspFencePollPauses) return kPspFenceTimeout;
        writer.pause(writer.context);
    }
    return kPspFenceTimeout;
}

Status readPspResponse(const MemoryReader &work, PspResponse *response)
{
    *response = PspResponse();
    uint32_t *fields[] = {&response->status, nullptr, &response->fwAddrLo, &response->fwAddrHi, &response->tmrSize};
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t value = 0;
        if (!work.read32(work.context, kPspCmdPage + kPspRespOffset + i * 4, &value)) return kRegisterReadFailed;
        if (fields[i] != nullptr) *fields[i] = value;
    }
    return response->status == 0 ? kOK : kPspCommandFailed;
}

Status verifyPspWorkArea(const MemoryReader &region, const uint32_t *snapshot, uint32_t frames, uint32_t command,
                         uint32_t fence, uint32_t *unexpected, uint32_t *firstOffset)
{
    *unexpected = 0;
    *firstOffset = 0;
    for (uint32_t offset = 0; offset < kPspRingCheckSize; offset += 4) {
        uint32_t value = 0;
        if (!region.read32(region.context, offset, &value)) return kRegisterReadFailed;
        bool ok;
        if (offset < frames * kPspFrameSize) {
            ok = value == pspFrameWord(offset / kPspFrameSize, (offset % kPspFrameSize) / 4);
        } else if (offset >= kPspCmdPage && offset < kPspFencePage) {
            uint32_t at = offset - kPspCmdPage;
            ok = (at >= kPspRespOffset && at < kPspRespOffset + kPspRespSize) || value == pspCommandWord(command, at / 4);
        } else if (offset >= kPspFencePage && offset < kPspWorkSize) {
            ok = value == (offset == kPspFencePage ? fence : 0);
        } else {
            ok = value == snapshot[offset / 4];
        }
        if (!ok && (*unexpected)++ == 0) *firstOffset = offset;
    }
    return *unexpected == 0 ? kOK : kPspRegionChanged;
}

Status checkSdmaImage(const uint8_t *image, uint32_t length)
{
    // common_firmware_header: size_bytes, header_size_bytes, header version
    // 1.0 and IP 4.1 (four uint16), ucode_version, ucode_size_bytes,
    // ucode_array_offset_bytes.
    if (image == nullptr || length != kSdmaImageSize) return kSdmaImageInvalid;
    if (le32(image) != kSdmaImageSize || le32(image + 8) != 0x00000001u || le32(image + 12) != 0x00010004u ||
        le32(image + 16) != kSdmaUcodeVersion || le32(image + 20) != kSdmaUcodeSize ||
        le32(image + 24) != kSdmaUcodeOffset)
        return kSdmaImageInvalid;
    return kSdmaUcodeOffset + kSdmaUcodeSize <= length ? kOK : kSdmaImageInvalid;
}

uint32_t sdmaFirmwareWord(const uint8_t *image, uint32_t offset)
{
    return offset + 4 <= kSdmaUcodeSize ? le32(image + kSdmaUcodeOffset + offset) : 0;
}

bool sdmaFirmwareWriteAllowed(const uint8_t *image, uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage < kPspSdmaStage || (offset & 3) != 0 || offset >= kSdmaFwBufferSize) return false;
    return value == sdmaFirmwareWord(image, offset);
}

static Status writeFirmwareWord(const uint8_t *image, const MemoryWriter &writer, uint32_t stage, uint32_t offset)
{
    uint32_t value = sdmaFirmwareWord(image, offset);
    if (!sdmaFirmwareWriteAllowed(image, offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

Status checkSdmaFirmwareTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                               const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kPspSdmaStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kSdmaFwCarveoutOffset, kSdmaFwCheckSize,
                                      ranges, rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kSdmaFwGpuAddress || target->physical != kSdmaFwPhysical) return kMetricsAddressMismatch;
    return kOK;
}

Status writeSdmaFirmware(const uint8_t *image, uint32_t length, const MemoryReader &buffer,
                         const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kPspSdmaStage) return kRegisterNotAllowed;
    Status status = checkSdmaImage(image, length);
    if (status != kOK) return status;
    for (uint32_t offset = 0; offset < kSdmaFwBufferSize; offset += 4) {
        status = writeFirmwareWord(image, writer, stage, offset);
        if (status != kOK) return status;
    }
    for (uint32_t offset = 0; offset < kSdmaFwBufferSize; offset += 4) {
        uint32_t value = 0;
        if (!buffer.read32(buffer.context, offset, &value)) return kRegisterReadFailed;
        if (value != sdmaFirmwareWord(image, offset)) return kPspReadbackMismatch;
    }
    return kOK;
}

Status verifySdmaFirmwareRegion(const MemoryReader &region, const uint32_t *snapshot, const uint8_t *image,
                                uint32_t *unexpected, uint32_t *firstOffset)
{
    *unexpected = 0;
    *firstOffset = 0;
    for (uint32_t offset = 0; offset < kSdmaFwCheckSize; offset += 4) {
        uint32_t value = 0;
        if (!region.read32(region.context, offset, &value)) return kRegisterReadFailed;
        uint32_t expected = offset < kSdmaFwBufferSize ? sdmaFirmwareWord(image, offset) : snapshot[offset / 4];
        if (value != expected && (*unexpected)++ == 0) *firstOffset = offset;
    }
    return *unexpected == 0 ? kOK : kPspRegionChanged;
}

Status readSdmaInventory(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *values)
{
    for (uint32_t i = 0; i < kSdmaInventoryCount; i++) values[i] = 0;
    if (stage < kSdmaInventoryStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < kSdmaInventoryCount; i++) {
        Status status = readRegister(registers, apertureLength, stage, kSdmaInventory[i], &values[i]);
        if (status != kOK) return status;
    }
    return kOK;
}

Status runSdmaInventory(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                        uint32_t stage, SdmaInventory *inventory, uint32_t *upResponse, uint32_t *downResponse)
{
    *inventory = SdmaInventory();
    *upResponse = *downResponse = 0;
    if (stage < kSdmaInventoryStage) return kRegisterNotAllowed;
    Status status = readSdmaInventory(registers, apertureLength, stage, inventory->loaded);
    if (status != kOK) return status;
    status = sendSmuMessage(registers, apertureLength, writer, stage, kSmuMsgPowerUpSdma, 0, upResponse, nullptr);
    if (status != kOK) return status;
    Status powered = readSdmaInventory(registers, apertureLength, stage, inventory->powered);
    // Power SDMA back down whatever the reading found.
    status = sendSmuMessage(registers, apertureLength, writer, stage, kSmuMsgPowerDownSdma, 0, downResponse, nullptr);
    if (status != kOK) return status;
    if (powered != kOK) return powered;
    return readSdmaInventory(registers, apertureLength, stage, inventory->gated);
}

bool sdmaWriteListed(uint32_t offset, uint32_t value)
{
    for (const SdmaWrite &write : kSdmaGolden)
        if (write.offset == offset && write.value == value) return true;
    for (const SdmaWrite &write : kSdmaStart)
        if (write.offset == offset && write.value == value) return true;
    for (const SdmaWrite &write : kSdmaStop)
        if (write.offset == offset && write.value == value) return true;
    return offset == kRegSdma0GfxRbWptr && (value == kSdmaFrameDwords * 4 || value == 2 * kSdmaFrameDwords * 4);
}

uint32_t sdmaRingWord(uint32_t dword)
{
    const uint64_t wb = kSdmaWorkGpuAddress + kSdmaWbPage;
    const uint64_t source = kSdmaWorkGpuAddress + kSdmaSrcPage, destination = kSdmaWorkGpuAddress + kSdmaDstPage;
    // Frame 0: sdma_v4_0_ring_test_ring.
    const uint32_t test[] = {kSdmaOpWrite, uint32_t(wb + kSdmaWbTest), uint32_t((wb + kSdmaWbTest) >> 32), 0,
                             kSdmaTestValue};
    // Frame 1: sdma_v4_0_emit_copy_buffer and sdma_v4_0_ring_emit_fence (no TRAP).
    const uint32_t copy[] = {kSdmaOpCopy,
                             kSdmaCopyBytes - 1,
                             0,
                             uint32_t(source),
                             uint32_t(source >> 32),
                             uint32_t(destination),
                             uint32_t(destination >> 32),
                             kSdmaOpFence,
                             uint32_t(wb + kSdmaWbFence),
                             uint32_t((wb + kSdmaWbFence) >> 32),
                             1};
    if (dword < 5) return test[dword];
    if (dword >= kSdmaFrameDwords && dword < kSdmaFrameDwords + 11) return copy[dword - kSdmaFrameDwords];
    return kSdmaOpNop;
}

uint32_t sdmaWorkWord(uint32_t offset)
{
    if (offset < kSdmaWbPage) return sdmaRingWord(offset / 4);
    if (offset >= kSdmaSrcPage && offset < kSdmaDstPage) return kSdmaSourceBase + (offset - kSdmaSrcPage) / 4;
    return 0;
}

bool sdmaWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage < kSdmaCopyStage || (offset & 3) != 0) return false;
    if (offset < kSdmaWorkSize && value == sdmaWorkWord(offset)) return true;
    if (stage < kGartStage) return false;
    // Stage 17: frame 2 in the ring, and the zeroed second destination.
    if (offset >= 2 * kSdmaFrameDwords * 4 && offset < 3 * kSdmaFrameDwords * 4) return value == gartRingWord(offset / 4);
    // Stage 18: frame 3.
    if (offset >= 3 * kSdmaFrameDwords * 4 && offset < kSdmaRingDwords * 4)
        return stage >= kIntrStage && value == intrRingWord(offset / 4);
    // Stage 20: frame 4, over the consumed frame 0.
    if (stage >= kFlipStage && offset < kSdmaFrameDwords * 4 && value == fillRingWord(offset / 4)) return true;
    return offset >= kSdmaDst2Page && offset < kGartSdmaWorkSize && value == 0;
}

static Status writeSdmaWorkWord(const MemoryWriter &writer, uint32_t stage, uint32_t offset, uint32_t value)
{
    if (!sdmaWorkWriteAllowed(offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

Status checkSdmaWorkTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kSdmaWorkCarveoutOffset, kSdmaWorkCheckSize,
                                      ranges, rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kSdmaWorkGpuAddress || target->physical != kSdmaWorkPhysical)
        return kMetricsAddressMismatch;
    return kOK;
}

Status checkSdmaBoot19(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value)
{
    *index = *value = 0;
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < kSdmaInventoryCount; i++) {
        Status status = readRegister(registers, apertureLength, stage, kSdmaInventory[i], value);
        if (status != kOK) return status;
        if (i != kSdmaStatusIndex && *value != kSdmaBoot19[i]) {
            *index = i;
            return kSdmaUnexpectedState;
        }
    }
    *value = 0;
    return kOK;
}

Status writeSdmaWork(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    for (uint32_t offset = 0; offset < kSdmaWorkSize; offset += 4) {
        Status status = writeSdmaWorkWord(writer, stage, offset, sdmaWorkWord(offset));
        if (status != kOK) return status;
    }
    for (uint32_t offset = 0; offset < kSdmaWorkSize; offset += 4) {
        uint32_t value = 0;
        if (!work.read32(work.context, offset, &value)) return kRegisterReadFailed;
        if (value != sdmaWorkWord(offset)) return kPspReadbackMismatch;
    }
    return kOK;
}

Status startSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                 uint32_t stage, uint32_t *progress, uint32_t *upResponse)
{
    *progress = *upResponse = 0;
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    Status status = sendSmuMessage(registers, apertureLength, writer, stage, kSmuMsgPowerUpSdma, 0, upResponse, nullptr);
    if (status != kOK) return status;
    *progress = 1;
    for (const SdmaWrite &write : kSdmaGolden) {
        *progress = 2;
        status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    for (const SdmaWrite &write : kSdmaStart) {
        status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    return kOK;
}

Status submitSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                  const MemoryReader &work, uint32_t stage, uint32_t frame, uint32_t *observed)
{
    *observed = 0;
    if (stage < kSdmaCopyStage || frame > 4 || (frame == 2 && stage < kGartStage) || (frame == 3 && stage < kIntrStage) ||
        (frame == 4 && stage < kFlipStage))
        return kRegisterNotAllowed;
    uint32_t pointer = 0;
    Status status = readRegister(registers, apertureLength, stage, kRegSdma0GfxRbWptr, &pointer);
    if (status != kOK) return status;
    if (pointer != frame * kSdmaFrameDwords * 4) return kSdmaOutOfOrder;
    // sdma_v4_0_ring_set_wptr: low dword, then the high dword, which
    // commits it (boot 20: the low write alone left GFX_RB_WPTR at 0).
    status = writeRegister(writer, stage, kRegSdma0GfxRbWptr, (frame + 1) * kSdmaFrameDwords * 4);
    if (status == kOK) status = writeRegister(writer, stage, kRegSdma0GfxRbWptrHi, 0);
    if (status != kOK) return status;
    const uint32_t at = kSdmaWbPage + (frame == 0   ? kSdmaWbTest
                                       : frame == 1 ? kSdmaWbFence
                                       : frame == 2 ? kSdmaWbFence2
                                       : frame == 3 ? kSdmaWbFence3
                                                    : kSdmaWbFence4);
    const uint32_t expected = frame == 0 ? kSdmaTestValue : frame;
    for (uint32_t i = 0; i <= kSdmaPollPauses; i++) {
        if (!work.read32(work.context, at, observed)) return kRegisterReadFailed;
        if (*observed == expected) return kOK;
        if (i == kSdmaPollPauses) return kSdmaTimeout;
        writer.pause(writer.context);
    }
    return kSdmaTimeout;
}

Status verifySdmaCopy(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &region,
                      const uint32_t *snapshot, uint32_t stage, uint32_t *rptr, uint32_t *unexpected,
                      uint32_t *firstOffset)
{
    *rptr = *unexpected = *firstOffset = 0;
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    Status status = readRegister(registers, apertureLength, stage, kRegSdma0GfxRbRptr, rptr);
    if (status != kOK) return status;
    for (uint32_t offset = 0; offset < kSdmaWorkCheckSize; offset += 4) {
        uint32_t value = 0;
        if (!region.read32(region.context, offset, &value)) return kRegisterReadFailed;
        bool ok;
        if (offset < kSdmaWbPage || (offset >= kSdmaSrcPage && offset < kSdmaDstPage)) {
            ok = value == sdmaWorkWord(offset);
        } else if (offset >= kSdmaWbPage && offset < kSdmaSrcPage) {
            uint32_t at = offset - kSdmaWbPage;
            ok = at == kSdmaWbRptr || at == kSdmaWbRptr + 4 ||
                 value == (at == kSdmaWbTest ? kSdmaTestValue : at == kSdmaWbFence ? 1u : 0u);
        } else if (offset >= kSdmaDstPage && offset < kSdmaWorkSize) {
            ok = value == sdmaWorkWord(offset - kSdmaDstPage + kSdmaSrcPage);
        } else {
            ok = value == snapshot[offset / 4];
        }
        if (!ok && (*unexpected)++ == 0) *firstOffset = offset;
    }
    return *rptr == 2 * kSdmaFrameDwords * 4 && *unexpected == 0 ? kOK : kSdmaVerifyFailed;
}

Status stopSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                uint32_t stage, uint32_t progress, uint32_t *downResponse)
{
    *downResponse = 0;
    if (stage < kSdmaCopyStage) return kRegisterNotAllowed;
    Status first = kOK;
    if (progress >= 2) {
        for (const SdmaWrite &write : kSdmaStop) {
            Status status = writeRegister(writer, stage, write.offset, write.value);
            if (first == kOK) first = status;
        }
    }
    if (progress >= 1) {
        Status status = sendSmuMessage(registers, apertureLength, writer, stage, kSmuMsgPowerDownSdma, 0,
                                       downResponse, nullptr);
        if (first == kOK) first = status;
    }
    return first;
}

bool semaphoreReadAllowed(uint32_t offset, uint32_t stage)
{
    return stage >= kGartStage && offset == kRegVmInvalidateEng17Sem;
}

bool gartWriteListed(uint32_t offset, uint32_t value)
{
    for (const GartWrite &write : kGartEnable)
        if (write.offset == offset && (write.value == value || write.boot22 == value)) return true;
    for (const GartWrite &write : kIhEnable)
        if (write.offset == offset && (write.value == value || write.boot22 == value)) return true;
    return (offset == kRegIhRbCntl && value == kIhRbCntlOn) ||
           (offset == kRegVmInvalidateEng17Req && value == kGartInvalidateReq) ||
           (offset == kRegVmInvalidateEng17Sem && value == 0) || (offset == kRegSdma0Cntl && value == kSdmaCntlTrap) ||
           (offset == kRegSdma0GfxRbWptr && value == 3 * kSdmaFrameDwords * 4);
}

uint32_t gartWorkWord(uint32_t offset)
{
    if (offset == kGartTablePage) return uint32_t(kGartPte0);
    if (offset == kGartTablePage + 4) return uint32_t(kGartPte0 >> 32);
    return 0;
}

bool gartWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage < kGartStage || (offset & 3) != 0 || offset >= kGartWorkSize) return false;
    return value == gartWorkWord(offset);
}

static Status writeGartWorkWord(const MemoryWriter &writer, uint32_t stage, uint32_t offset)
{
    uint32_t value = gartWorkWord(offset);
    if (!gartWorkWriteAllowed(offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

uint32_t gartRingWord(uint32_t dword)
{
    const uint64_t fence = kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbFence2;
    const uint64_t destination = kSdmaWorkGpuAddress + kSdmaDst2Page;
    // Frame 2: sdma_v4_0_emit_copy_buffer from the GART address, then
    // sdma_v4_0_ring_emit_fence with its TRAP (interrupt context 0).
    const uint32_t frame[] = {kSdmaOpCopy,
                              kSdmaCopyBytes - 1,
                              0,
                              uint32_t(kGartSourceAddress),
                              uint32_t(kGartSourceAddress >> 32),
                              uint32_t(destination),
                              uint32_t(destination >> 32),
                              kSdmaOpFence,
                              uint32_t(fence),
                              uint32_t(fence >> 32),
                              2,
                              kSdmaOpTrap,
                              0};
    const uint32_t first = 2 * kSdmaFrameDwords;
    if (dword >= first && dword < first + kSdmaFrame2Dwords) return frame[dword - first];
    return sdmaRingWord(dword);
}

Status checkGartWorkTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kGartStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kGartWorkCarveoutOffset, kGartWorkCheckSize,
                                      ranges, rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kGartWorkGpuAddress || target->physical != kGartWorkPhysical)
        return kMetricsAddressMismatch;
    return kOK;
}

// The precondition list's register and boot 22 value: kGartEnable,
// kIhEnable, SDMA0_CNTL, then display pipe 0.
static void gartCheckEntry(uint32_t i, uint32_t *offset, uint32_t *expected)
{
    if (i < kGartEnableCount) {
        *offset = kGartEnable[i].offset;
        *expected = kGartEnable[i].boot22;
    } else if (i < kGartEnableCount + kIhEnableCount) {
        *offset = kIhEnable[i - kGartEnableCount].offset;
        *expected = kIhEnable[i - kGartEnableCount].boot22;
    } else if (i == kGartEnableCount + kIhEnableCount) {
        *offset = kRegSdma0Cntl;
        *expected = kSdmaCntlBoot;
    } else {
        *offset = kDisplayInventory[i - kGartEnableCount - kIhEnableCount - 1];
        *expected = kDisplayPipe0Boot22[i - kGartEnableCount - kIhEnableCount - 1];
    }
}

// DCHUBP_CNTL's live status bits (kHubpLiveStatus); display comparisons ignore them.
static uint32_t displayMask(uint32_t index)
{
    return index < kDisplayPipes * kDisplayPipeRegisters && index % kDisplayPipeRegisters == kDchubpCntlIndex
               ? ~kHubpLiveStatus
               : 0xFFFFFFFFu;
}

Status checkGartBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value)
{
    *index = *value = 0;
    if (stage < kGartStage) return kRegisterNotAllowed;
    const uint32_t display = kGartEnableCount + kIhEnableCount + 1;
    for (uint32_t i = 0; i < kGartCheckCount; i++) {
        uint32_t offset = 0, expected = 0;
        gartCheckEntry(i, &offset, &expected);
        *index = i;
        Status status = readRegister(registers, apertureLength, stage, offset, value);
        if (status != kOK) return status;
        uint32_t mask = i >= display ? displayMask(i - display) : 0xFFFFFFFFu;
        if ((*value & mask) != (expected & mask)) return kGartUnexpectedState;
    }
    *index = *value = 0;
    return kOK;
}

Status checkGartRestored(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                         uint32_t *value, uint32_t *ihWptr)
{
    *index = *value = *ihWptr = 0;
    if (stage < kGartStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < kGartEnableCount + kIhEnableCount + 1; i++) {
        uint32_t offset = 0, expected = 0;
        gartCheckEntry(i, &offset, &expected);
        *index = i;
        Status status = readRegister(registers, apertureLength, stage, offset, value);
        if (status != kOK) return status;
        if (offset == kRegIhRbWptr) {
            *ihWptr = *value; // the IH's own pointer: reported, not required
            continue;
        }
        if (*value != expected) return kGartNotRestored;
    }
    *index = *value = 0;
    return kOK;
}

Status readDisplayInventory(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                            uint32_t *values)
{
    for (uint32_t i = 0; i < kDisplayInventoryCount; i++) {
        Status status = readRegister(registers, apertureLength, stage, kDisplayInventory[i], &values[i]);
        if (status != kOK) return status;
    }
    return kOK;
}

Status writeGartWork(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kGartStage) return kRegisterNotAllowed;
    for (uint32_t offset = 0; offset < kGartWorkSize; offset += 4) {
        Status status = writeGartWorkWord(writer, stage, offset);
        if (status != kOK) return status;
    }
    for (uint32_t offset = 0; offset < kGartWorkSize; offset += 4) {
        uint32_t value = 0;
        if (!work.read32(work.context, offset, &value)) return kRegisterReadFailed;
        if (value != gartWorkWord(offset)) return kPspReadbackMismatch;
    }
    return kOK;
}

// Frame 2's dwords, then the second destination page.
static bool frame2Word(uint32_t i, uint32_t *offset, uint32_t *value)
{
    const uint32_t ringWords = kSdmaFrameDwords, pageWords = kPageSize / 4;
    if (i < ringWords) {
        *offset = (2 * kSdmaFrameDwords + i) * 4;
        *value = gartRingWord(2 * kSdmaFrameDwords + i);
        return true;
    }
    if (i < ringWords + pageWords) {
        *offset = kSdmaDst2Page + (i - ringWords) * 4;
        *value = 0;
        return true;
    }
    return false;
}

Status writeSdmaFrame2(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kGartStage) return kRegisterNotAllowed;
    uint32_t offset = 0, value = 0;
    for (uint32_t i = 0; frame2Word(i, &offset, &value); i++) {
        Status status = writeSdmaWorkWord(writer, stage, offset, value);
        if (status != kOK) return status;
    }
    for (uint32_t i = 0; frame2Word(i, &offset, &value); i++) {
        uint32_t read = 0;
        if (!work.read32(work.context, offset, &read)) return kRegisterReadFailed;
        if (read != value) return kPspReadbackMismatch;
    }
    return kOK;
}

Status flushGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                 uint32_t stage, uint32_t *ack)
{
    *ack = 0;
    if (!semaphoreReadAllowed(kRegVmInvalidateEng17Sem, stage)) return kRegisterNotAllowed;
    // A read that returns 1 acquires the semaphore (boot 22).
    for (uint32_t i = 0;; i++) {
        uint32_t semaphore = 0;
        if (!registers.read32(registers.context, kRegVmInvalidateEng17Sem, &semaphore)) return kRegisterReadFailed;
        if ((semaphore & 1) != 0) break;
        if (i == kGartPollPauses) return kGartSemaphoreTimeout;
        writer.pause(writer.context);
    }
    Status status = writeRegister(writer, stage, kRegVmInvalidateEng17Req, kGartInvalidateReq);
    for (uint32_t i = 0; status == kOK; i++) {
        status = readRegister(registers, apertureLength, stage, kRegVmInvalidateEng17Ack, ack);
        if (status != kOK || (*ack & 1) != 0) break;
        if (i == kGartPollPauses) status = kGartAckTimeout;
        else writer.pause(writer.context);
    }
    // Released whatever happened after the acquire.
    Status release = writeRegister(writer, stage, kRegVmInvalidateEng17Sem, 0);
    return status != kOK ? status : release;
}

Status enableGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                  uint32_t stage, uint32_t *progress, uint32_t *ack)
{
    *progress = *ack = 0;
    if (stage < kGartStage) return kRegisterNotAllowed;
    for (const GartWrite &write : kGartEnable) {
        *progress = 1;
        Status status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    Status status = flushGart(registers, apertureLength, writer, stage, ack);
    if (status != kOK) return status;
    for (const GartWrite &write : kIhEnable) {
        *progress = 2;
        status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    status = writeRegister(writer, stage, kRegIhRbCntl, kIhRbCntlOn);
    if (status != kOK) return status;
    *progress = 3;
    return writeRegister(writer, stage, kRegSdma0Cntl, kSdmaCntlTrap);
}

// Reads the IH write-back and the ring entries it covers into the report
// (a GartReport or an IntrReport).
template <typename Report> static Status scanIh(const MemoryReader &gart, Report *report, uint32_t *end, bool *wrapped)
{
    if (!gart.read32(gart.context, kIhWbPage, &report->ihWriteback)) return kRegisterReadFailed;
    *end = report->ihWriteback & kIhWptrOffsetMask;
    *wrapped = (report->ihWriteback & kIhWptrOverflow) != 0 || *end >= kIhRingBytes;
    report->ihEntries = *wrapped ? kIhRingBytes / kIhEntryBytes : *end / kIhEntryBytes;
    report->sdmaTraps = report->otherEntries = 0;
    for (uint32_t entry = 0; entry < report->ihEntries; entry++) {
        for (uint32_t word = 0; word < kIhEntryBytes / 4; word++) {
            uint32_t value = 0;
            if (!gart.read32(gart.context, kIhRingPage + entry * kIhEntryBytes + word * 4, &value))
                return kRegisterReadFailed;
            if (entry < kIhReportEntries) report->ring[entry * kIhEntryBytes / 4 + word] = value;
            if (word != 0) continue;
            // dw0: client_id 7:0, src_id 15:8 (vega10_ih_decode_iv).
            if ((value & 0xFF) == kIhClientSdma0 && ((value >> 8) & 0xFF) == kIhSrcSdmaTrap) report->sdmaTraps++;
            else report->otherEntries++;
        }
    }
    return kOK;
}

// The SDMA region after frame 2 (stage 17) or frame 3 (stage 18): the ring,
// the write-back page (read pointer not pinned; the test value and the
// fences), the source and both destinations holding it, the snapshot beyond.
static Status scanSdmaRegion(const MemoryReader &sdmaRegion, const uint32_t *sdmaSnapshot, bool frame3,
                             uint32_t *fence2, uint32_t *fence3, uint32_t *unexpected, uint32_t *first)
{
    for (uint32_t offset = 0; offset < kSdmaWorkCheckSize; offset += 4) {
        uint32_t value = 0;
        if (!sdmaRegion.read32(sdmaRegion.context, offset, &value)) return kRegisterReadFailed;
        bool ok;
        if (offset < kSdmaWbPage) {
            ok = value == (frame3 ? intrRingWord(offset / 4) : gartRingWord(offset / 4));
        } else if (offset < kSdmaSrcPage) {
            uint32_t at = offset - kSdmaWbPage;
            if (at == kSdmaWbFence2) *fence2 = value;
            if (at == kSdmaWbFence3 && fence3 != nullptr) *fence3 = value;
            ok = at == kSdmaWbRptr || at == kSdmaWbRptr + 4 ||
                 value == (at == kSdmaWbTest     ? kSdmaTestValue
                           : at == kSdmaWbFence  ? 1u
                           : at == kSdmaWbFence2 ? 2u
                           : at == kSdmaWbFence3 && frame3 ? 3u
                                                 : 0u);
        } else if (offset < kGartSdmaWorkSize) {
            // The source, and both destinations holding it.
            ok = value == sdmaWorkWord(kSdmaSrcPage + (offset & (kPageSize - 1)));
        } else {
            ok = value == sdmaSnapshot[offset / 4];
        }
        if (!ok && (*unexpected)++ == 0) *first = offset;
    }
    return kOK;
}

// The GART region: table and dummy page, ring entries up to the write
// pointer and zero after it, the write-back dword, the snapshot beyond.
static Status scanGartRegion(const MemoryReader &gartRegion, const uint32_t *gartSnapshot, uint32_t end, bool wrapped,
                             uint32_t *unexpected, uint32_t *first)
{
    for (uint32_t offset = 0; offset < kGartWorkCheckSize; offset += 4) {
        uint32_t value = 0;
        if (!gartRegion.read32(gartRegion.context, offset, &value)) return kRegisterReadFailed;
        bool ok;
        if (offset < kIhRingPage) {
            ok = value == gartWorkWord(offset);
        } else if (offset < kIhWbPage) {
            ok = wrapped || offset - kIhRingPage < end || value == 0;
        } else if (offset < kGartWorkSize) {
            ok = offset == kIhWbPage || value == 0;
        } else {
            ok = value == gartSnapshot[offset / 4];
        }
        if (!ok && (*unexpected)++ == 0) *first = offset;
    }
    return kOK;
}

// The display inventory against the check's reading (live HUBP bits ignored).
static Status compareDisplay(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                             const uint32_t *display, uint32_t *changed, uint32_t *first)
{
    for (uint32_t i = 0; i < kDisplayInventoryCount; i++) {
        uint32_t value = 0;
        Status status = readRegister(registers, apertureLength, stage, kDisplayInventory[i], &value);
        if (status != kOK) return status;
        if ((value & displayMask(i)) != (display[i] & displayMask(i)) && (*changed)++ == 0) *first = i;
    }
    return kOK;
}

Status verifyGart(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &sdmaRegion,
                  const uint32_t *sdmaSnapshot, const MemoryReader &gartRegion, const uint32_t *gartSnapshot,
                  const uint32_t *display, const RegisterWriter &writer, uint32_t stage, GartReport *report)
{
    *report = GartReport();
    if (stage < kGartStage) return kRegisterNotAllowed;
    // The trap's entry may follow the fence.
    uint32_t end = 0;
    bool wrapped = false;
    for (uint32_t i = 0;; i++) {
        Status status = scanIh(gartRegion, report, &end, &wrapped);
        if (status != kOK) return status;
        if (report->sdmaTraps != 0 || i == kSdmaPollPauses) break;
        writer.pause(writer.context);
    }
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegSdma0GfxRbRptr, &report->rptr},
        {kRegVmL2ProtectionFaultStatus, &report->faultStatus},
        {kRegIhRbWptr, &report->ihWptr},
        {kRegIhRbRptr, &report->ihRptr},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    Status status = scanSdmaRegion(sdmaRegion, sdmaSnapshot, false, &report->fence2, nullptr, &report->sdmaUnexpected,
                                   &report->sdmaFirst);
    if (status == kOK)
        status = scanGartRegion(gartRegion, gartSnapshot, end, wrapped, &report->gartUnexpected, &report->gartFirst);
    if (status == kOK)
        status = compareDisplay(registers, apertureLength, stage, display, &report->displayChanged, &report->displayFirst);
    if (status != kOK) return status;
    if (report->faultStatus != 0) return kGartFault;
    if (report->sdmaTraps == 0) return kIhNoTrap;
    return report->rptr == 3 * kSdmaFrameDwords * 4 && report->fence2 == 2 && report->sdmaUnexpected == 0 &&
                   report->gartUnexpected == 0 && report->displayChanged == 0
               ? kOK
               : kGartVerifyFailed;
}

Status restoreGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t progress, uint32_t *ack)
{
    *ack = 0;
    if (stage < kGartStage) return kRegisterNotAllowed;
    Status first = kOK;
    auto note = [&first](Status status) {
        if (first == kOK) first = status;
    };
    if (progress >= 2) {
        note(writeRegister(writer, stage, kRegIhRbCntl, kIhRbCntlOff));
        for (const GartWrite &write : kIhEnable) note(writeRegister(writer, stage, write.offset, write.boot22));
    }
    if (progress >= 3) note(writeRegister(writer, stage, kRegSdma0Cntl, kSdmaCntlBoot));
    if (progress >= 1) {
        const GartWrite &context = kGartEnable[kGartContext0Index];
        note(writeRegister(writer, stage, context.offset, context.boot22));
        for (uint32_t i = 0; i < kGartEnableCount; i++) {
            if (i != kGartContext0Index) note(writeRegister(writer, stage, kGartEnable[i].offset, kGartEnable[i].boot22));
        }
        note(flushGart(registers, apertureLength, writer, stage, ack));
    }
    return first;
}

Status readMsiCapability(const ConfigReader &config, MsiCapability *msi)
{
    *msi = MsiCapability();
    uint32_t value = 0;
    if (!read(config, kCfgStatus, 2, &value)) return kConfigReadFailed;
    if ((value & kStatusCapabilityList) == 0) return kNoCapabilityList;
    if (!read(config, kCfgCapabilities, 1, &value)) return kConfigReadFailed;
    uint8_t offset = static_cast<uint8_t>(value & 0xFC);
    for (int i = 0; offset != 0; i++) {
        if (i == kMaxCapabilities || offset < 0x40) return kCapabilityListMalformed;
        if (!read(config, offset, 2, &value)) return kConfigReadFailed;
        if ((value & 0xFF) == kCapabilityMsi) break;
        offset = static_cast<uint8_t>((value >> 8) & 0xFC);
    }
    if (offset == 0 || offset > 0xF0) return kIntrNoMsi;
    msi->offset = offset;
    if (!read(config, static_cast<uint8_t>(offset + 2), 2, &value)) return kConfigReadFailed;
    msi->control = static_cast<uint16_t>(value);
    if (!read(config, static_cast<uint8_t>(offset + 4), 4, &msi->addressLo)) return kConfigReadFailed;
    // Message Control bit 7: 64-bit address, so the data follows the high dword.
    uint8_t data = static_cast<uint8_t>(offset + 8);
    if ((msi->control & 0x80) != 0) {
        if (!read(config, data, 4, &msi->addressHi)) return kConfigReadFailed;
        data = static_cast<uint8_t>(offset + 12);
    }
    if (!read(config, data, 2, &value)) return kConfigReadFailed;
    msi->data = static_cast<uint16_t>(value);
    return kOK;
}

bool intrWriteListed(uint32_t offset, uint32_t value)
{
    return (offset == kRegInterruptCntl2 && (value == kInterruptCntl2Dummy || value == 0)) ||
           (offset == kRegIhRbCntl && (value == kIhRbCntlRearm || value == kIhRbCntlIntr)) ||
           (offset == kRegIhRbRptr && value % kIhEntryBytes == 0 && value < kIhRingBytes) ||
           (offset == kRegSdma0GfxRbWptr && value == kSdmaRingDwords * 4);
}

uint32_t intrRingWord(uint32_t dword)
{
    const uint64_t fence = kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbFence3;
    // Frame 3: sdma_v4_0_ring_emit_fence with its TRAP (interrupt context 0).
    const uint32_t frame[] = {kSdmaOpFence, uint32_t(fence), uint32_t(fence >> 32), 3, kSdmaOpTrap, 0};
    const uint32_t first = 3 * kSdmaFrameDwords;
    if (dword >= first && dword < first + kSdmaFrame3Dwords) return frame[dword - first];
    return gartRingWord(dword);
}

Status checkIntrBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value)
{
    *index = *value = 0;
    if (stage < kIntrStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < kIntrCheckCount; i++) {
        *index = i;
        Status status = readRegister(registers, apertureLength, stage, kIntrCheck[i], value);
        if (status != kOK) return status;
        if (*value != 0) return kIntrUnexpectedState;
    }
    *index = *value = 0;
    return kOK;
}

Status writeSdmaFrame3(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kIntrStage) return kRegisterNotAllowed;
    for (uint32_t dword = 3 * kSdmaFrameDwords; dword < kSdmaRingDwords; dword++) {
        Status status = writeSdmaWorkWord(writer, stage, dword * 4, intrRingWord(dword));
        if (status != kOK) return status;
    }
    for (uint32_t dword = 3 * kSdmaFrameDwords; dword < kSdmaRingDwords; dword++) {
        uint32_t value = 0;
        if (!work.read32(work.context, dword * 4, &value)) return kRegisterReadFailed;
        if (value != intrRingWord(dword)) return kPspReadbackMismatch;
    }
    return kOK;
}

Status armIntr(const MemoryReader &gartWork, const MemoryWriter &gart, const RegisterWriter &writer,
               uint32_t stage, uint32_t *progress)
{
    *progress = 0;
    if (stage < kIntrStage) return kRegisterNotAllowed;
    // vega10_ih_toggle_interrupts(false): the ring off, its pointers reset.
    *progress = 1;
    const SdmaWrite off[] = {{kRegIhRbCntl, kIhRbCntlOff}, {kRegIhRbRptr, 0}, {kRegIhRbWptr, 0}};
    for (const SdmaWrite &write : off) {
        Status status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    // The stage 17 entries go; the ring and its write-back start empty.
    for (uint32_t offset = kIhRingPage; offset < kGartWorkSize; offset += 4) {
        Status status = writeGartWorkWord(gart, stage, offset);
        if (status != kOK) return status;
    }
    for (uint32_t offset = kIhRingPage; offset < kGartWorkSize; offset += 4) {
        uint32_t value = 0;
        if (!gartWork.read32(gartWork.context, offset, &value)) return kRegisterReadFailed;
        if (value != 0) return kPspReadbackMismatch;
    }
    // nbio_v7_0_ih_control, then vega10_ih_enable_ring with RPTR_REARM.
    const SdmaWrite arm[] = {{kRegInterruptCntl2, kInterruptCntl2Dummy},
                             {kRegIhRbCntl, kIhRbCntlRearm},
                             {kRegIhRbWptr, 0},
                             {kRegIhRbRptr, 0}};
    for (const SdmaWrite &write : arm) {
        Status status = writeRegister(writer, stage, write.offset, write.value);
        if (status != kOK) return status;
    }
    return kOK;
}

Status startIntr(const RegisterWriter &writer, uint32_t stage, uint32_t *progress)
{
    if (stage < kIntrStage) return kRegisterNotAllowed;
    *progress = 2;
    return writeRegister(writer, stage, kRegIhRbCntl, kIhRbCntlIntr);
}

Status verifyIntr(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &sdmaRegion,
                  const uint32_t *sdmaSnapshot, const MemoryReader &gartRegion, const uint32_t *gartSnapshot,
                  const uint32_t *display, const RegisterWriter &writer, const InterruptCounter &counter,
                  uint32_t msiBefore, uint32_t stage, IntrReport *report)
{
    *report = IntrReport();
    if (stage < kIntrStage) return kRegisterNotAllowed;
    report->msiBefore = msiBefore;
    // The fence, the IH entry and the MSI may arrive in any order.
    uint32_t end = 0;
    bool wrapped = false;
    for (uint32_t i = 0;; i++) {
        Status status = scanIh(gartRegion, report, &end, &wrapped);
        if (status != kOK) return status;
        if (!sdmaRegion.read32(sdmaRegion.context, kSdmaWbPage + kSdmaWbFence3, &report->fence3))
            return kRegisterReadFailed;
        report->msiCount = counter.count(counter.context);
        if ((report->sdmaTraps != 0 && report->fence3 == 3 && report->msiCount > msiBefore) || i == kSdmaPollPauses)
            break;
        writer.pause(writer.context);
    }
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegSdma0GfxRbRptr, &report->rptr},
        {kRegVmL2ProtectionFaultStatus, &report->faultStatus},
        {kRegIhRbCntl, &report->ihRbCntl},
        {kRegIhRbWptr, &report->ihWptr},
        {kRegIhRbRptr, &report->ihRptr},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    uint32_t fence2 = 0;
    Status status = scanSdmaRegion(sdmaRegion, sdmaSnapshot, true, &fence2, &report->fence3, &report->sdmaUnexpected,
                                   &report->sdmaFirst);
    if (status == kOK)
        status = scanGartRegion(gartRegion, gartSnapshot, end, wrapped, &report->gartUnexpected, &report->gartFirst);
    if (status == kOK)
        status = compareDisplay(registers, apertureLength, stage, display, &report->displayChanged, &report->displayFirst);
    if (status != kOK) return status;
    report->msiCount = counter.count(counter.context);
    if (report->faultStatus != 0) return kGartFault;
    if (report->sdmaTraps == 0) return kIhNoTrap;
    if (report->msiCount <= msiBefore) return kIntrNotDelivered;
    return report->fence3 == 3 && report->msiCount - msiBefore == 1 && report->sdmaUnexpected == 0 &&
                   report->gartUnexpected == 0 && report->displayChanged == 0
               ? kOK
               : kIntrVerifyFailed;
}

Status ackIntr(const MemoryReader &gartRegion, const RegisterWriter &writer, const InterruptCounter &counter,
               uint32_t stage, uint32_t *rptr, uint32_t *countBefore, uint32_t *countAfter, uint32_t *writeback)
{
    *rptr = *countBefore = *countAfter = *writeback = 0;
    if (stage < kIntrStage) return kRegisterNotAllowed;
    uint32_t before = 0;
    if (!gartRegion.read32(gartRegion.context, kIhWbPage, &before)) return kRegisterReadFailed;
    *countBefore = counter.count(counter.context);
    *rptr = before & kIhWptrOffsetMask;
    Status status = writeRegister(writer, stage, kRegIhRbRptr, *rptr);
    if (status != kOK) return status;
    for (uint32_t i = 0; i < kIntrSettlePauses; i++) writer.pause(writer.context);
    if (!gartRegion.read32(gartRegion.context, kIhWbPage, writeback)) return kRegisterReadFailed;
    *countAfter = counter.count(counter.context);
    return *countAfter != *countBefore || *writeback != before ? kIntrRefired : kOK;
}

Status quiesceIntr(const RegisterWriter &writer, uint32_t stage)
{
    if (stage < kIntrStage) return kRegisterNotAllowed;
    Status first = kOK;
    const SdmaWrite off[] = {{kRegIhRbCntl, kIhRbCntlRearm}, {kRegIhRbRptr, 0}, {kRegIhRbWptr, 0}};
    for (const SdmaWrite &write : off) {
        Status status = writeRegister(writer, stage, write.offset, write.value);
        if (first == kOK) first = status;
    }
    // vega10_ih_irq_disable: "Wait and acknowledge irq".
    writer.pause(writer.context);
    return first;
}

Status restoreIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *index, uint32_t *value)
{
    *index = *value = 0;
    if (stage < kIntrStage) return kRegisterNotAllowed;
    Status status = writeRegister(writer, stage, kRegInterruptCntl2, 0);
    for (uint32_t i = 0; i < kIntrCheckCount; i++) {
        *index = i;
        Status read = readRegister(registers, apertureLength, stage, kIntrCheck[i], value);
        if (read != kOK) return status != kOK ? status : read;
        if (*value != 0) return status != kOK ? status : kIntrNotRestored;
    }
    *index = *value = 0;
    return status;
}

bool displayWriteListed(uint32_t offset, uint32_t value)
{
    return (offset == kRegHubpreq0DcsurfPrimarySurfaceAddressHigh && value == uint32_t(kGopSurfaceAddress >> 32)) ||
           (offset == kRegHubpreq0DcsurfPrimarySurfaceAddress &&
            (value == uint32_t(kPatternGpuAddress) || value == uint32_t(kGopSurfaceAddress)));
}

uint32_t patternWord(uint32_t offset)
{
    const uint32_t pixel = offset / 4;
    if (pixel >= kPatternWidth * kPatternHeight) return 0;
    const uint32_t x = pixel % kPatternWidth, y = pixel / kPatternWidth;
    if (x % kPatternLineSpacing < 2 || x >= kPatternWidth - 2) return kPatternLine;
    return kPatternBands[y / kPatternBandLines];
}

bool patternWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage)
{
    if (stage < kDisplayStage || (offset & 3) != 0 || offset >= kPatternSize) return false;
    // Stage 20 also zeroes the region before SDMA fills it.
    return value == patternWord(offset) || (stage >= kFlipStage && value == 0);
}

static Status writePatternWord(const MemoryWriter &writer, uint32_t stage, uint32_t offset, uint32_t value)
{
    if (!patternWriteAllowed(offset, value, stage)) return kRegisterNotAllowed;
    return writer.write32(writer.context, offset, value) ? kOK : kRegisterWriteFailed;
}

Status checkPatternTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                          const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kDisplayStage) return kRegisterNotAllowed;
    Status status = checkCarveoutPage(registers, apertureLength, stage, kPatternCarveoutOffset, kPatternSize, ranges,
                                      rangeCount, target);
    if (status != kOK) return status;
    if (target->gpuAddress != kPatternGpuAddress || target->physical != kPatternPhysical)
        return kMetricsAddressMismatch;
    return kOK;
}

Status regionChecksum(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer, uint32_t pauses,
                      uint64_t *sum)
{
    uint64_t second = 0;
    Status status = regionSum(memory, length, sum);
    if (status != kOK) return status;
    for (uint32_t i = 0; i < pauses; i++) writer.pause(writer.context);
    status = regionSum(memory, length, &second);
    if (status != kOK) return status;
    return *sum == second ? kOK : kTableRegionInUse;
}

Status checkDisplayBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                          uint32_t *value, uint32_t *frameCount)
{
    *index = *value = *frameCount = 0;
    if (stage < kDisplayStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < kDisplayCheckCount; i++) {
        *index = i;
        uint32_t offset = i < 11 ? kDisplayInventory[i] : kDisplayExpect[i - 11].offset;
        Status status = readRegister(registers, apertureLength, stage, offset, value);
        if (status != kOK) return status;
        bool ok = i < 11 ? (*value & displayMask(i)) == (kDisplayPipe0Boot22[i] & displayMask(i))
                         : (*value & kDisplayExpect[i - 11].mask) == kDisplayExpect[i - 11].value;
        if (!ok) return kDisplayUnexpectedState;
    }
    *index = *value = 0;
    return readRegister(registers, apertureLength, stage, kRegOtg0OtgStatusFrameCount, frameCount);
}

Status writePattern(const MemoryReader &pattern, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kDisplayStage) return kRegisterNotAllowed;
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        Status status = writePatternWord(writer, stage, offset, patternWord(offset));
        if (status != kOK) return status;
    }
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        uint32_t value = 0;
        if (!pattern.read32(pattern.context, offset, &value)) return kRegisterReadFailed;
        if (value != patternWord(offset)) return kPspReadbackMismatch;
    }
    return kOK;
}

Status flipDisplay(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint64_t address, uint64_t *inuse, uint32_t *pauses)
{
    *inuse = 0;
    *pauses = 0;
    if (stage < kDisplayStage || (address != kPatternGpuAddress && address != kGopSurfaceAddress))
        return kRegisterNotAllowed;
    // program_surface_flip_and_addr's order: the high dword, then the low
    // one, which latches the flip at the next vertical sync.
    Status status = writeRegister(writer, stage, kRegHubpreq0DcsurfPrimarySurfaceAddressHigh, uint32_t(address >> 32));
    if (status == kOK) status = writeRegister(writer, stage, kRegHubpreq0DcsurfPrimarySurfaceAddress, uint32_t(address));
    if (status != kOK) return status;
    // hubp2_is_flip_pending.
    for (uint32_t i = 0;; i++) {
        uint32_t control = 0, low = 0, high = 0;
        status = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfFlipControl, &control);
        if (status == kOK) status = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceEarliestInuse, &low);
        if (status == kOK)
            status = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceEarliestInuseHigh, &high);
        if (status != kOK) return status;
        *inuse = (uint64_t(high) << 32) | low;
        if ((control & kFlipPending) == 0 && *inuse == address) return kOK;
        if (i == kDisplayFlipPauses) return kDisplayFlipTimeout;
        writer.pause(writer.context);
        (*pauses)++;
    }
}

// verifyDisplay and verifyFlip: the region must hold word's image.
static Status verifySurface(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                            const uint32_t *display, uint32_t flipFrames, uint32_t stage, uint32_t (*word)(uint32_t),
                            DisplayReport *report)
{
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegHubpreq0DcsurfSurfaceEarliestInuse, &report->inuseLo},
        {kRegHubpreq0DcsurfSurfaceEarliestInuseHigh, &report->inuseHi},
        {kRegHubpreq0DcsurfFlipControl, &report->flipControl},
        {kRegOtg0OtgStatusFrameCount, &report->frameCount},
    };
    for (const auto &read : reads) {
        Status status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    report->framesAdvanced = (report->frameCount - flipFrames) & kOtgFrameCountMask;
    for (uint32_t i = 0; i < kDisplayInventoryCount; i++) {
        uint32_t value = 0;
        Status status = readRegister(registers, apertureLength, stage, kDisplayInventory[i], &value);
        if (status != kOK) return status;
        uint32_t expected = i == kDisplayAddressIndex ? uint32_t(kPatternGpuAddress) : display[i];
        if ((value & displayMask(i)) != (expected & displayMask(i)) && report->displayChanged++ == 0)
            report->displayFirst = i;
    }
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        uint32_t value = 0;
        if (!pattern.read32(pattern.context, offset, &value)) return kRegisterReadFailed;
        if (value != word(offset) && report->patternUnexpected++ == 0) report->patternFirst = offset;
    }
    const uint64_t inuse = (uint64_t(report->inuseHi) << 32) | report->inuseLo;
    return inuse == kPatternGpuAddress && (report->flipControl & kFlipPending) == 0 && report->framesAdvanced != 0 &&
                   report->displayChanged == 0 && report->patternUnexpected == 0
               ? kOK
               : kDisplayVerifyFailed;
}

Status verifyDisplay(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                     const uint32_t *display, uint32_t flipFrames, uint32_t stage, DisplayReport *report)
{
    *report = DisplayReport();
    if (stage < kDisplayStage) return kRegisterNotAllowed;
    return verifySurface(registers, apertureLength, pattern, display, flipFrames, stage, patternWord, report);
}

Status checkDisplayRestored(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                            uint32_t *index, uint32_t *value)
{
    *index = *value = 0;
    if (stage < kDisplayStage) return kRegisterNotAllowed;
    for (uint32_t i = 0; i < 11; i++) {
        *index = i;
        Status status = readRegister(registers, apertureLength, stage, kDisplayInventory[i], value);
        if (status != kOK) return status;
        if ((*value & displayMask(i)) != (kDisplayPipe0Boot22[i] & displayMask(i))) return kDisplayNotRestored;
    }
    *index = *value = 0;
    return kOK;
}

bool flipWriteListed(uint32_t offset, uint32_t value)
{
    if (offset == kRegHubpreq0DcsurfSurfaceFlipInterrupt) {
        for (uint32_t write : kFlipIntWrites)
            if (value == write) return true;
        return false;
    }
    return offset == kRegSdma0GfxRbWptr && value == kFlipWptr;
}

uint32_t fillWord(uint32_t offset)
{
    return offset >= kFillTailOffset ? 0 : kPatternBands[7 - offset / kFillBandBytes];
}

uint32_t fillRingWord(uint32_t dword)
{
    if (dword >= kSdmaFrameDwords) return intrRingWord(dword);
    if (dword < kFillCount * 5) {
        // sdma_v4_0_emit_fill_buffer: the header has no fill size set.
        const uint32_t fill = dword / 5, offset = fill * kFillBandBytes;
        const uint64_t destination = kPatternGpuAddress + offset;
        const uint32_t bytes = fill < 8 ? kFillBandBytes : kPatternSize - kFillTailOffset;
        const uint32_t packet[] = {kSdmaOpConstFill, uint32_t(destination), uint32_t(destination >> 32),
                                   fillWord(offset), bytes - 1};
        return packet[dword % 5];
    }
    const uint64_t fence = kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbFence4;
    const uint32_t fenceWords[] = {kSdmaOpFence, uint32_t(fence), uint32_t(fence >> 32), 4};
    if (dword < kSdmaFrame4Dwords) return fenceWords[dword - kFillCount * 5];
    return kSdmaOpNop;
}

Status checkFlipIntr(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                     uint32_t *value, uint32_t *dest2, uint32_t *continue17)
{
    *index = *value = *dest2 = *continue17 = 0;
    if (stage < kFlipStage) return kRegisterNotAllowed;
    uint32_t flipInterrupt = 0;
    Status status = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, &flipInterrupt);
    if (status == kOK) status = readRegister(registers, apertureLength, stage, kRegDchubInterruptDest2, dest2);
    if (status == kOK) status = readRegister(registers, apertureLength, stage, kRegDispInterruptStatusContinue17, continue17);
    if (status != kOK) return status;
    if (flipInterrupt != 0) {
        *value = flipInterrupt;
        return kFlipUnexpectedState;
    }
    if ((*dest2 & kFlipIntDest) != 0) {
        *index = 1;
        *value = *dest2;
        return kFlipUnexpectedState;
    }
    return kOK;
}

Status clearPattern(const MemoryReader &pattern, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kFlipStage) return kRegisterNotAllowed;
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        Status status = writePatternWord(writer, stage, offset, 0);
        if (status != kOK) return status;
    }
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        uint32_t value = 0;
        if (!pattern.read32(pattern.context, offset, &value)) return kRegisterReadFailed;
        if (value != 0) return kPspReadbackMismatch;
    }
    return kOK;
}

Status writeSdmaFrame4(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage)
{
    if (stage < kFlipStage) return kRegisterNotAllowed;
    for (uint32_t dword = 0; dword < kSdmaFrameDwords; dword++) {
        Status status = writeSdmaWorkWord(writer, stage, dword * 4, fillRingWord(dword));
        if (status != kOK) return status;
    }
    for (uint32_t dword = 0; dword < kSdmaFrameDwords; dword++) {
        uint32_t value = 0;
        if (!work.read32(work.context, dword * 4, &value)) return kRegisterReadFailed;
        if (value != fillRingWord(dword)) return kPspReadbackMismatch;
    }
    return kOK;
}

Status checkFill(const MemoryReader &pattern, uint32_t stage, uint32_t *unexpected, uint32_t *first)
{
    *unexpected = *first = 0;
    if (stage < kFlipStage) return kRegisterNotAllowed;
    for (uint32_t offset = 0; offset < kPatternSize; offset += 4) {
        uint32_t value = 0;
        if (!pattern.read32(pattern.context, offset, &value)) return kRegisterReadFailed;
        if (value != fillWord(offset) && (*unexpected)++ == 0) *first = offset;
    }
    return *unexpected == 0 ? kOK : kFlipFillMismatch;
}

Status armFlipIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *readback)
{
    *readback = 0;
    if (stage < kFlipStage) return kRegisterNotAllowed;
    // dal_irq_service_set acknowledges, then enables.
    Status status = writeRegister(writer, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, kFlipIntClear);
    if (status == kOK) status = writeRegister(writer, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, kFlipIntEnable);
    if (status == kOK)
        status = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, readback);
    if (status != kOK) return status;
    return (*readback & kFlipIntEnable) != 0 ? kOK : kFlipUnexpectedState;
}

Status flipWithIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    const MemoryReader &gartRegion, const InterruptCounter &counter, uint32_t stage, uint64_t address,
                    uint32_t msiBefore, FlipReport *report)
{
    *report = FlipReport();
    if (stage < kFlipStage) return kRegisterNotAllowed;
    report->msiBefore = msiBefore;
    Status status = readRegister(registers, apertureLength, stage, kRegIhRbRptr, &report->ihStart);
    if (status != kOK) return status;
    uint64_t inuse = 0;
    Status flip = flipDisplay(registers, apertureLength, writer, stage, address, &inuse, &report->pauses);
    report->inuseLo = uint32_t(inuse);
    report->inuseHi = uint32_t(inuse >> 32);
    if (flip != kOK && flip != kDisplayFlipTimeout) return flip;
    // The MSI may come before or after the poll sees the flip.
    while (counter.count(counter.context) == msiBefore && report->msiPauses < kDisplayFlipPauses) {
        writer.pause(writer.context);
        report->msiPauses++;
    }
    report->msiAfter = counter.count(counter.context);
    const struct {
        uint32_t offset;
        uint32_t *value;
    } reads[] = {
        {kRegOtg0OtgStatusFrameCount, &report->frameCount},
        {kRegHubpreq0DcsurfSurfaceFlipInterrupt, &report->flipInterrupt},
        {kRegDispInterruptStatusContinue17, &report->continue17},
    };
    for (const auto &read : reads) {
        status = readRegister(registers, apertureLength, stage, read.offset, read.value);
        if (status != kOK) return status;
    }
    // The new IH entries: from the read pointer to the write-back.
    if (!gartRegion.read32(gartRegion.context, kIhWbPage, &report->ihWriteback)) return kRegisterReadFailed;
    const uint32_t end = report->ihWriteback & kIhWptrOffsetMask & (kIhRingBytes - 1);
    uint32_t at = report->ihStart & (kIhRingBytes - 1), entry = 0;
    for (; at != end && entry < kIhRingBytes / kIhEntryBytes; at = (at + kIhEntryBytes) & (kIhRingBytes - 1), entry++) {
        for (uint32_t word = 0; word < kIhEntryBytes / 4; word++) {
            uint32_t value = 0;
            if (!gartRegion.read32(gartRegion.context, kIhRingPage + at + word * 4, &value)) return kRegisterReadFailed;
            if (entry < kFlipReportEntries) report->entries[entry * kIhEntryBytes / 4 + word] = value;
            if (word != 0) continue;
            // dw0: client_id 7:0, src_id 15:8 (vega10_ih_decode_iv).
            if ((value & 0xFF) == kIhClientDce && ((value >> 8) & 0xFF) == kIhSrcHubp0Flip) report->flipEntries++;
            else report->otherEntries++;
        }
    }
    if (flip != kOK) return flip;
    if (report->msiAfter == msiBefore) return kFlipIntrNotDelivered;
    return report->msiAfter - msiBefore == 1 && report->flipEntries == 1 ? kOK : kFlipIntrVerifyFailed;
}

Status ackFlipIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *readback)
{
    *readback = 0;
    if (stage < kFlipStage) return kRegisterNotAllowed;
    Status status =
        writeRegister(writer, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, kFlipIntClear | kFlipIntEnable);
    if (status != kOK) return status;
    return readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, readback);
}

Status verifyFlip(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                  const uint32_t *display, uint32_t flipFrames, const InterruptCounter &counter, uint32_t msiAtAck,
                  uint32_t stage, DisplayReport *report, uint32_t *msiChange)
{
    *report = DisplayReport();
    *msiChange = 0;
    if (stage < kFlipStage) return kRegisterNotAllowed;
    Status status = verifySurface(registers, apertureLength, pattern, display, flipFrames, stage, fillWord, report);
    *msiChange = counter.count(counter.context) - msiAtAck;
    if (status == kDisplayVerifyFailed || (status == kOK && *msiChange != 0)) return kFlipVerifyFailed;
    return status;
}

Status restoreFlip(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   const MemoryReader &gartRegion, const InterruptCounter &counter, uint32_t stage, uint32_t msiBefore,
                   FlipReport *report, uint32_t *index, uint32_t *value)
{
    *index = *value = 0;
    if (stage < kFlipStage) {
        *report = FlipReport();
        return kRegisterNotAllowed;
    }
    Status flip = flipWithIntr(registers, apertureLength, writer, gartRegion, counter, stage, kGopSurfaceAddress,
                               msiBefore, report);
    // The flip back's MSI is recorded, not required.
    if (flip == kFlipIntrNotDelivered || flip == kFlipIntrVerifyFailed) flip = kOK;
    // dal_irq_service_set(false): acknowledge (a read-modify-write that keeps
    // the enable bit), then disable.
    Status status =
        writeRegister(writer, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, kFlipIntClear | kFlipIntEnable);
    if (status == kOK) status = writeRegister(writer, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, 0);
    Status restored = checkDisplayRestored(registers, apertureLength, stage, index, value);
    if (restored == kDisplayNotRestored) restored = kFlipNotRestored;
    if (restored == kOK) {
        *index = 11;
        restored = readRegister(registers, apertureLength, stage, kRegHubpreq0DcsurfSurfaceFlipInterrupt, value);
        if (restored == kOK && *value != 0) restored = kFlipNotRestored;
        if (restored == kOK) *index = 0;
    }
    return flip != kOK ? flip : status != kOK ? status : restored;
}

} // namespace cezanne
