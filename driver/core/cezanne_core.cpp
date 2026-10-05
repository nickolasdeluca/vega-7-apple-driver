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

bool registerAllowed(uint32_t offset)
{
    for (uint32_t i = 0; i < kStage1RegisterCount; i++) {
        if (kStage1Registers[i] == offset) return true;
    }
    return false;
}

static Status readRegister(const RegisterReader &registers, uint64_t length, uint32_t offset, uint32_t *value)
{
    *value = 0;
    if (!registerAllowed(offset) || (offset & 3) != 0 || offset + 4ull > length) return kRegisterNotAllowed;
    return registers.read32(registers.context, offset, value) ? kOK : kRegisterReadFailed;
}

Status readBootState(const RegisterReader &registers, uint64_t apertureLength, BootState *state)
{
    *state = BootState();
    Status status = readRegister(registers, apertureLength, kRegC2PMsg33, &state->c2pmsg33);
    if (status != kOK) return status;
    status = readRegister(registers, apertureLength, kRegConfigMemsize, &state->configMemsize);
    if (status != kOK) return status;
    // A device that has stopped decoding returns all ones for every read.
    if (state->c2pmsg33 == 0xFFFFFFFFu && state->configMemsize == 0xFFFFFFFFu) return kDeviceNotResponding;
    state->ifwiReady = (state->c2pmsg33 & kC2PMsg33IfwiReady) != 0;
    state->vramBytes = static_cast<uint64_t>(state->configMemsize) << 20;
    return kOK;
}

} // namespace cezanne
