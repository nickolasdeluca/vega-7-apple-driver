// Host unit tests for driver/core against fake configuration space and
// registers. Built and run by tests/test_core.py; touches no hardware.
#include "cezanne_core.h"

#include <cstdio>
#include <cstring>

using namespace cezanne;

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            failures++;                                                          \
        }                                                                        \
    } while (0)

struct FakeConfig {
    uint8_t bytes[256];
    int failAt; // offset whose read fails, or -1
    int reads;

    FakeConfig() : failAt(-1), reads(0)
    {
        std::memset(bytes, 0, sizeof(bytes));
        put(0x00, 2, kVendorAMD);
        put(0x02, 2, kDeviceCezanne);
        put(0x04, 2, 0x0007);                   // I/O, memory, bus master
        put(0x06, 2, 0x0010);                   // capability list
        put(0x08, 4, 0x03000000u | kRevisionTarget);
        put(0x24, 4, 0xfca00000u);              // 32-bit memory BAR5
        put(0x34, 1, 0x48);
        put(0x48, 2, 0x5009);                   // vendor-specific, next 0x50
        put(0x50, 2, 0x6401);                   // power management, next 0x64
        put(0x54, 2, 0x0008);                   // PMCSR: D0, no soft reset
        put(0x64, 2, 0x0010);                   // PCI Express, end
    }
    void put(int offset, int width, uint32_t value)
    {
        for (int i = 0; i < width; i++) bytes[offset + i] = static_cast<uint8_t>(value >> (8 * i));
    }
    static bool read(void *context, uint8_t offset, uint8_t width, uint32_t *value)
    {
        FakeConfig *self = static_cast<FakeConfig *>(context);
        self->reads++;
        if (offset == self->failAt || offset + width > 256) return false;
        *value = 0;
        for (int i = 0; i < width; i++) *value |= static_cast<uint32_t>(self->bytes[offset + i]) << (8 * i);
        return true;
    }
    ConfigReader reader() { return ConfigReader{read, this}; }
};

struct FakeRegisters {
    uint32_t c2pmsg33 = 0x80000000u;
    uint32_t memsize = 2048;
    bool fail = false;
    uint32_t order[8];
    int reads = 0;

    static bool read32(void *context, uint32_t offset, uint32_t *value)
    {
        FakeRegisters *self = static_cast<FakeRegisters *>(context);
        if (self->reads < 8) self->order[self->reads] = offset;
        self->reads++;
        if (self->fail) return false;
        *value = offset == kRegC2PMsg33 ? self->c2pmsg33 : offset == kRegConfigMemsize ? self->memsize : 0xDEADBEEF;
        return true;
    }
    RegisterReader reader() { return RegisterReader{read32, this}; }
};

static Status stateStatus(FakeConfig &config, PciState *state)
{
    Status status = readPciState(config.reader(), state);
    return status == kOK ? checkPciState(*state) : status;
}

static void testValidDevice()
{
    FakeConfig config;
    PciState state;
    CHECK(stateStatus(config, &state) == kOK);
    CHECK(state.vendor == 0x1002 && state.device == 0x1638 && state.revision == 0xc9);
    CHECK(state.classCode == 0x030000);
    CHECK(state.powerCapability == 0x50 && state.powerControl == 0x0008);
    CHECK(state.bar5 == 0xfca00000u);
    CHECK(checkAperture(state, 0xfca00000u, 0x80000) == kOK);
}

static void testPreMapRefusals()
{
    PciState state;
    {
        FakeConfig c;
        c.put(0x08, 1, 0xc8);
        CHECK(stateStatus(c, &state) == kIdentityMismatch);
    }
    {
        FakeConfig c;
        c.put(0x02, 2, 0x1636);
        CHECK(stateStatus(c, &state) == kIdentityMismatch);
    }
    {
        FakeConfig c;
        c.put(0x54, 2, 0x0003);
        CHECK(stateStatus(c, &state) == kNotInD0);
    }
    {
        FakeConfig c;
        c.put(0x04, 2, 0x0005);
        CHECK(stateStatus(c, &state) == kMemoryDecodeDisabled);
    }
    {
        FakeConfig c;
        c.put(0x24, 4, 0xfca00004u); // 64-bit memory BAR
        CHECK(stateStatus(c, &state) == kBarNotMemory32);
    }
    {
        FakeConfig c;
        c.put(0x24, 4, 0x0000e001u); // I/O BAR
        CHECK(stateStatus(c, &state) == kBarNotMemory32);
    }
}

static void testCapabilityWalk()
{
    PciState state;
    {
        FakeConfig c;
        c.put(0x06, 2, 0x0000);
        CHECK(stateStatus(c, &state) == kNoCapabilityList);
    }
    {
        FakeConfig c;
        c.put(0x48, 2, 0x4809); // points to itself
        CHECK(stateStatus(c, &state) == kCapabilityListMalformed);
        CHECK(c.reads < 70);
    }
    {
        FakeConfig c;
        c.put(0x34, 1, 0x20); // inside the standard header
        CHECK(stateStatus(c, &state) == kCapabilityListMalformed);
    }
    {
        FakeConfig c;
        c.put(0x48, 2, 0x6409); // skips the power capability
        CHECK(stateStatus(c, &state) == kNoPowerCapability);
    }
    {
        FakeConfig c;
        c.failAt = 0x54;
        CHECK(stateStatus(c, &state) == kConfigReadFailed);
    }
    {
        FakeConfig c;
        c.failAt = 0x00;
        CHECK(stateStatus(c, &state) == kConfigReadFailed);
    }
}

static void testAperture()
{
    FakeConfig config;
    PciState state;
    CHECK(stateStatus(config, &state) == kOK);
    CHECK(checkAperture(state, 0xfcb00000u, 0x80000) == kBarMismatch);
    CHECK(checkAperture(state, 0xfca00000u, kRegC2PMsg33 + 3) == kApertureTooSmall);
    CHECK(checkAperture(state, 0xfca00000u, kRegC2PMsg33 + 4) == kOK);
}

static void testBootState()
{
    {
        FakeRegisters r;
        BootState boot;
        CHECK(readBootState(r.reader(), 0x80000, &boot) == kOK);
        CHECK(boot.ifwiReady && boot.configMemsize == 2048 && boot.vramBytes == 2048ull << 20);
        CHECK(r.reads == 2 && r.order[0] == kRegC2PMsg33 && r.order[1] == kRegConfigMemsize);
    }
    {
        FakeRegisters r;
        r.c2pmsg33 = 0x00000001;
        BootState boot;
        CHECK(readBootState(r.reader(), 0x80000, &boot) == kOK);
        CHECK(!boot.ifwiReady);
    }
    {
        FakeRegisters r;
        r.c2pmsg33 = r.memsize = 0xFFFFFFFFu;
        BootState boot;
        CHECK(readBootState(r.reader(), 0x80000, &boot) == kDeviceNotResponding);
        CHECK(!boot.ifwiReady && boot.vramBytes == 0);
    }
    {
        FakeRegisters r;
        r.fail = true;
        BootState boot;
        CHECK(readBootState(r.reader(), 0x80000, &boot) == kRegisterReadFailed);
        CHECK(r.reads == 1);
    }
    {
        FakeRegisters r;
        BootState boot;
        CHECK(readBootState(r.reader(), 0x10000, &boot) == kRegisterNotAllowed);
        CHECK(r.reads == 0); // refused before reaching the reader
    }
}

static void testAllowlist()
{
    CHECK(registerAllowed(kRegC2PMsg33) && registerAllowed(kRegConfigMemsize));
    CHECK(!registerAllowed(0) && !registerAllowed(4) && !registerAllowed(kRegConfigMemsize + 4));
    CHECK(kRegC2PMsg33 == 0x58184 && kRegConfigMemsize == 0x378c);
    CHECK(std::strcmp(statusName(kNotInD0), "not-in-d0") == 0);
    CHECK(std::strcmp(statusName(static_cast<Status>(999)), "unknown") == 0);
}

int main()
{
    testValidDevice();
    testPreMapRefusals();
    testCapabilityWalk();
    testAperture();
    testBootState();
    testAllowlist();
    if (failures == 0) std::printf("core tests passed\n");
    return failures == 0 ? 0 : 1;
}
