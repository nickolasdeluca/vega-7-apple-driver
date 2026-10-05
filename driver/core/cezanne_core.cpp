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
    // prefix of the stage 6 list is the list for any stage.
    uint32_t count = stage >= 6   ? kStage6RegisterCount
                     : stage == 5 ? kStage5RegisterCount
                     : stage >= 3 ? kStage3RegisterCount
                     : stage == 2 ? kStage2RegisterCount
                     : stage == 1 ? kStage1RegisterCount
                                  : 0;
    for (uint32_t i = 0; i < count; i++) {
        if (kStage6Registers[i] == offset) return true;
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
                                           value == kSmuMsgTransferTableSmu2Dram));
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

Status checkMetricsTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                          const Range *ranges, uint32_t rangeCount, MetricsTarget *target)
{
    *target = MetricsTarget();
    if (stage < kMetricsStage) return kRegisterNotAllowed;
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
    target->gpuAddress = (uint64_t(target->fbLocationBase) << 24) + kMetricsCarveoutOffset;
    target->physical = (uint64_t(target->fbOffset) << 24) + kMetricsCarveoutOffset;
    if (target->gpuAddress != kMetricsGpuAddress || target->physical != kMetricsPhysical) return kMetricsAddressMismatch;
    // Inside the carveout, clear of its reserved low and high regions.
    uint64_t carveoutSize = uint64_t(target->configMemsize) << 20;
    if (target->configMemsize == 0xFFFFFFFFu || carveoutSize < kCarveoutLowReserve + kCarveoutHighReserve ||
        kMetricsCarveoutOffset < kCarveoutLowReserve ||
        kMetricsCarveoutOffset + kMetricsCheckSize > carveoutSize - kCarveoutHighReserve)
        return kMetricsTargetInvalid;
    if (rangeCount == 0) return kMetricsTargetInvalid;
    for (uint32_t i = 0; i < rangeCount; i++) {
        if (overlaps(target->physical, kMetricsCheckSize, ranges[i].base, ranges[i].length))
            return kMetricsTargetInvalid;
    }
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

} // namespace cezanne
