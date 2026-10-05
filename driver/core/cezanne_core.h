// Cezanne hardware core: identity, PCI state and boot-state register checks.
//
// Freestanding C++ shared by the kext and the host unit tests: it uses no
// IOKit, libc or allocation. The adapter supplies read callbacks. There is
// deliberately no write path: every stage the core implements so far only
// reads, and adding a write is a reviewed stage change (docs/test-boot.md).
//
// Register offsets are byte offsets into the MMIO register BAR (BAR5). Linux
// v6.12 selects BAR5 for CHIP_BONAIRE and later in amdgpu_device_init and,
// for discovery-based APUs such as CHIP_RENOIR, reads exactly these two
// registers before any register write in amdgpu_discovery_read_binary_from_mem
// (amdgpu_discovery.c, dword indices 0x16061 and 0xde3).

#ifndef CEZANNE_CORE_H
#define CEZANNE_CORE_H

#include <stdint.h>

namespace cezanne {

const uint16_t kVendorAMD = 0x1002;
const uint16_t kDeviceCezanne = 0x1638;
const uint8_t kRevisionTarget = 0xc9;

// Highest stage this build implements. The test EFI's cezanne-stage boot
// argument selects a stage up to this value.
const uint32_t kMaxStage = 1;

const uint8_t kRegisterBar = 0x24; // BAR5 configuration offset

// MP0_SMN_C2PMSG_33: bit 31 is set once IFWI initialisation has completed.
const uint32_t kRegC2PMsg33 = 0x16061 * 4;
const uint32_t kC2PMsg33IfwiReady = 0x80000000u;
// RCC_CONFIG_MEMSIZE: the firmware-reserved "VRAM" size in MiB.
const uint32_t kRegConfigMemsize = 0xde3 * 4;

// The only registers stage 1 may read; the adapter refuses any other offset.
const uint32_t kStage1Registers[] = {kRegC2PMsg33, kRegConfigMemsize};
const uint32_t kStage1RegisterCount = sizeof(kStage1Registers) / sizeof(kStage1Registers[0]);

enum Status : uint32_t {
    kOK = 0,
    kConfigReadFailed,
    kIdentityMismatch,
    kNoCapabilityList,
    kCapabilityListMalformed,
    kNoPowerCapability,
    kNotInD0,
    kMemoryDecodeDisabled,
    kBarNotMemory32,
    kApertureUnavailable,
    kBarMismatch,
    kApertureTooSmall,
    kRegisterNotAllowed,
    kRegisterReadFailed,
    kDeviceNotResponding,
    // The adapter could not open its provider, so no device access was tried.
    kProviderOpenFailed,
};

const char *statusName(Status status);

// Reads 1, 2 or 4 bytes of PCI configuration space at an offset below 0x100.
struct ConfigReader {
    bool (*read)(void *context, uint8_t offset, uint8_t width, uint32_t *value);
    void *context;
};

// Reads one aligned 32-bit register at a byte offset into the register BAR.
struct RegisterReader {
    bool (*read32)(void *context, uint32_t offset, uint32_t *value);
    void *context;
};

struct PciState {
    uint16_t vendor;
    uint16_t device;
    uint8_t revision;
    uint32_t classCode; // 24-bit base class, subclass, programming interface
    uint16_t command;
    uint16_t status;
    uint32_t bar5;
    uint8_t powerCapability; // configuration offset, 0 when absent
    uint16_t powerControl;   // PMCSR
};

// Reads the standard header and walks the capability list (bounded).
Status readPciState(const ConfigReader &config, PciState *state);

// Checks that must pass before the register BAR is mapped: identity and
// revision, memory decoding already enabled, power state D0, and a 32-bit
// memory BAR5. Never changes the device to make a check pass.
Status checkPciState(const PciState &state);

// Checks the adapter's mapping against BAR5 and the stage 1 registers.
Status checkAperture(const PciState &state, uint64_t physical, uint64_t length);

bool registerAllowed(uint32_t offset);

struct BootState {
    uint32_t c2pmsg33;
    uint32_t configMemsize;
    bool ifwiReady;
    uint64_t vramBytes; // configMemsize << 20, as Linux computes it
};

// Reads the stage 1 registers in a fixed order through the reader.
Status readBootState(const RegisterReader &registers, uint64_t apertureLength, BootState *state);

} // namespace cezanne

#endif
