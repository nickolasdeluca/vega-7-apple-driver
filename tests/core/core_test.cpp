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
    uint32_t fbOffset = 0x580; // 0x580000000 >> 24
    uint32_t gfxIndex = 0xE0000000u;    // broadcast writes, SE 0 / SH 0 / instance 0
    uint32_t ccShader = 0x00800000u;    // CU 7 fused off
    uint32_t userShader = 0;
    uint32_t ccRb = 0, userRb = 0;
    bool fail = false;
    uint32_t order[16];
    int reads = 0;

    static bool read32(void *context, uint32_t offset, uint32_t *value)
    {
        FakeRegisters *self = static_cast<FakeRegisters *>(context);
        if (self->reads < 16) self->order[self->reads] = offset;
        self->reads++;
        if (self->fail) return false;
        *value = offset == kRegC2PMsg33        ? self->c2pmsg33
                 : offset == kRegConfigMemsize ? self->memsize
                 : offset == kRegMcVmFbOffset  ? self->fbOffset
                 : offset == kRegGrbmStatus    ? 0x00003028u
                 : offset == kRegGrbmGfxIndex  ? self->gfxIndex
                 : offset == kRegCcShaderArrayConfig   ? self->ccShader
                 : offset == kRegUserShaderArrayConfig ? self->userShader
                 : offset == kRegCcRbBackendDisable    ? self->ccRb
                 : offset == kRegUserRbBackendDisable  ? self->userRb
                 : offset == kRegGbAddrConfig          ? 0x24000042u
                                                       : 0xDEADBEEF;
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
    CHECK(registerAllowed(kRegC2PMsg33, 1) && registerAllowed(kRegConfigMemsize, 1));
    CHECK(!registerAllowed(kRegMcVmFbOffset, 1) && registerAllowed(kRegMcVmFbOffset, 2));
    CHECK(!registerAllowed(kRegC2PMsg33, 0));
    CHECK(!registerAllowed(0, 2) && !registerAllowed(4, 2) && !registerAllowed(kRegConfigMemsize + 4, 2));
    CHECK(kRegC2PMsg33 == 0x58184 && kRegConfigMemsize == 0x378c && kRegMcVmFbOffset == 0xa5ac);
    CHECK(std::strcmp(statusName(kNotInD0), "not-in-d0") == 0);
    for (uint32_t s = kOK; s <= kGfxIndexNotSe0Sh0; s++) CHECK(std::strcmp(statusName(static_cast<Status>(s)), "unknown") != 0);
    CHECK(std::strcmp(statusName(static_cast<Status>(999)), "unknown") == 0);
}

static BootState bootState()
{
    FakeRegisters r;
    BootState boot;
    readBootState(r.reader(), 0x80000, &boot);
    return boot;
}

static void testCarveout()
{
    BootState boot = bootState();
    {
        FakeRegisters r;
        Carveout c;
        CHECK(readCarveout(r.reader(), 0x80000, boot, &c) == kOK);
        CHECK(r.reads == 1 && r.order[0] == kRegMcVmFbOffset);
        CHECK(c.base == 0x580000000ull && c.size == 2048ull << 20);
        CHECK(c.table == 0x580000000ull + (2048ull << 20) - 0x10000);
    }
    {
        FakeRegisters r;
        Carveout c;
        CHECK(readCarveout(r.reader(), 0x4000, boot, &c) == kRegisterNotAllowed); // aperture too short
        CHECK(r.reads == 0);
    }
    const uint32_t bad[] = {0, 0x01000058u, 0xFFFFFFFFu};
    const Status expected[] = {kCarveoutInvalid, kCarveoutInvalid, kDeviceNotResponding};
    for (int i = 0; i < 3; i++) {
        FakeRegisters r;
        r.fbOffset = bad[i];
        Carveout c;
        CHECK(readCarveout(r.reader(), 0x80000, boot, &c) == expected[i]);
    }
    {
        FakeRegisters r;
        BootState tiny = boot;
        tiny.vramBytes = 0x8000;
        Carveout c;
        CHECK(readCarveout(r.reader(), 0x80000, tiny, &c) == kCarveoutInvalid);
    }
    {
        FakeRegisters r;
        r.fbOffset = 0x00FFFFFFu; // ends past 2^48
        Carveout c;
        CHECK(readCarveout(r.reader(), 0x80000, boot, &c) == kCarveoutInvalid);
    }
}

static void putCells(uint8_t *out, uint32_t hi, uint64_t base, uint64_t size)
{
    const uint32_t cells[] = {hi, static_cast<uint32_t>(base >> 32), static_cast<uint32_t>(base),
                              static_cast<uint32_t>(size >> 32), static_cast<uint32_t>(size)};
    for (int c = 0; c < 5; c++)
        for (int b = 0; b < 4; b++) out[4 * c + b] = static_cast<uint8_t>(cells[c] >> (8 * b));
}

static void testDeviceRanges()
{
    // The host's BARs at stage 1: BAR0 64-bit 256 MiB, BAR2 64-bit 2 MiB, BAR4 I/O, BAR5 32-bit 512 KiB.
    uint8_t data[80];
    putCells(data, 0x83000010u, 0x640000000ull, 0x10000000ull);
    putCells(data + 20, 0x83000018u, 0x650000000ull, 0x200000ull);
    putCells(data + 40, 0x81000020u, 0xe000ull, 0x100ull);
    putCells(data + 60, 0x82000024u, 0xfca00000ull, 0x80000ull);
    Range ranges[8];
    uint32_t count = 0;
    CHECK(parseAssignedAddresses(data, sizeof(data), ranges, 8, &count) == kOK);
    CHECK(count == 3 && ranges[0].base == 0x640000000ull && ranges[2].length == 0x80000ull);
    CHECK(parseAssignedAddresses(data, 79, ranges, 8, &count) == kDeviceRangesMalformed);
    CHECK(parseAssignedAddresses(data, sizeof(data), ranges, 2, &count) == kDeviceRangesMalformed);
    CHECK(parseAssignedAddresses(data + 40, 20, ranges, 8, &count) == kDeviceRangesMalformed); // I/O only
    CHECK(parseAssignedAddresses(data, sizeof(data), ranges, 8, &count) == kOK);

    Carveout c = {0x580, 0x580000000ull, 2048ull << 20, 0};
    CHECK(checkCarveout(c, ranges, count) == kOK);
    c.base = 0x5f0000000ull; // would run into BAR0
    CHECK(checkCarveout(c, ranges, count) == kCarveoutOverlapsDevice);
    c.base = 0xfc000000ull; // contains BAR5
    CHECK(checkCarveout(c, ranges, count) == kCarveoutOverlapsDevice);
}

// A minimal valid discovery binary: one die with SDMA0, GC and MP0.
static const uint32_t kTable = 0x40, kDie = kTable + 80, kFirstIp = kDie + 4;

struct FakeDiscovery {
    uint8_t bytes[kDiscoveryTmrSize];
    uint32_t size;

    FakeDiscovery() : size(0)
    {
        std::memset(bytes, 0, sizeof(bytes));
        put32(0, 0x28211407u);
        put16(4, 2);
        put16(12, static_cast<uint16_t>(kTable));
        put32(kTable, 0x53445049u);
        put16(kTable + 4, 1);
        put16(kTable + 12, 1); // one die
        put16(kTable + 14 + 2, static_cast<uint16_t>(kDie));
        put16(kDie, 0);
        put16(kDie + 2, 3); // three IPs
        uint32_t ip = kFirstIp;
        ip = addIp(ip, 42, 4, 1, 1, 0x1260);        // SDMA0, one base
        ip = addIp(ip, 11, 9, 3, 2, 0x2000, 0xA000); // GC 9.3.0
        ip = addIp(ip, 255, 12, 0, 1, 0x16000);      // MP0
        put16(kTable + 6, static_cast<uint16_t>(ip - kTable));
        // GC info v2.0 after the IP table: 1 SE, 8 CU/SH, 1 SH/SE, 2 RB/SE.
        put16(20, static_cast<uint16_t>(ip));
        put32(ip, 0x4347);
        put16(ip + 4, 2);
        put32(ip + 8, 80);
        put32(ip + 12, 1);
        put32(ip + 16, 8);
        put32(ip + 20, 1);
        put32(ip + 24, 2);
        size = ip + 80 + 16; // GC table and trailing bytes outside the IP table
        seal();
    }
    void put16(uint32_t o, uint16_t v)
    {
        bytes[o] = static_cast<uint8_t>(v);
        bytes[o + 1] = static_cast<uint8_t>(v >> 8);
    }
    void put32(uint32_t o, uint32_t v)
    {
        put16(o, static_cast<uint16_t>(v));
        put16(o + 2, static_cast<uint16_t>(v >> 16));
    }
    uint32_t addIp(uint32_t o, uint16_t hw, uint8_t major, uint8_t minor, uint8_t n, uint32_t b0, uint32_t b1 = 0)
    {
        put16(o, hw);
        bytes[o + 3] = n;
        bytes[o + 4] = major;
        bytes[o + 5] = minor;
        put32(o + 8, b0);
        if (n > 1) put32(o + 12, b1);
        return o + 8 + 4u * n;
    }
    static uint16_t sum(const uint8_t *p, uint32_t n)
    {
        uint16_t s = 0;
        for (uint32_t i = 0; i < n; i++) s = static_cast<uint16_t>(s + p[i]);
        return s;
    }
    void sealBinary() { put16(8, sum(bytes + 10, size - 10)); }
    void seal()
    {
        put16(10, static_cast<uint16_t>(size));
        put16(14, sum(bytes + kTable, static_cast<uint32_t>(bytes[kTable + 6] | (bytes[kTable + 7] << 8))));
        sealBinary();
    }
};

static Status parse(const FakeDiscovery &f, Discovery *d, uint32_t length = kDiscoveryTmrSize)
{
    return parseDiscovery(f.bytes, length, d);
}

static void testDiscovery()
{
    Discovery d;
    {
        FakeDiscovery f;
        CHECK(parse(f, &d) == kOK);
        CHECK(d.versionMajor == 2 && d.tableVersion == 1 && d.numDies == 1 && d.numIps == 3);
        CHECK(d.gcFound && d.gcMajor == 9 && d.gcMinor == 3 && d.gcBase0 == 0x2000 && d.gcBase1 == 0xA000);
        CHECK(d.mp0Found && d.mp0Base0 == 0x16000);
        CHECK(d.gcInfoFound && d.gcInfoMajor == 2 && d.gcNumSe == 1 && d.gcCuPerSh == 8 && d.gcShPerSe == 1 &&
              d.gcRbPerSe == 2);
    }
    {
        FakeDiscovery f;
        f.bytes[0] ^= 1;
        CHECK(parse(f, &d) == kDiscoverySignature);
    }
    {
        FakeDiscovery f;
        f.bytes[kTable] ^= 1;
        f.sealBinary();
        CHECK(parse(f, &d) == kDiscoverySignature);
    }
    {
        FakeDiscovery f;
        f.bytes[f.size - 1] ^= 1; // outside the IP table: only the binary checksum covers it
        CHECK(parse(f, &d) == kDiscoveryChecksum);
    }
    {
        FakeDiscovery f;
        f.bytes[kFirstIp + 8] ^= 1; // SDMA base inside the IP table
        f.sealBinary();             // binary checksum fixed, table checksum not
        CHECK(parse(f, &d) == kDiscoveryChecksum);
    }
    {
        FakeDiscovery f;
        CHECK(parse(f, &d, f.size - 1) == kDiscoveryMalformed); // size beyond the buffer
    }
    {
        FakeDiscovery f;
        f.put16(kDie + 2, 200); // more IPs than the binary holds
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryMalformed);
    }
    {
        FakeDiscovery f;
        f.put16(kDie, 1); // die id differs from its index
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryMalformed);
    }
    {
        FakeDiscovery f;
        f.put32(kFirstIp + 12 + 8, 0x1260); // GC base 0 differs from renoir_ip_offset.h
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryBaseMismatch);
    }
    {
        FakeDiscovery f;
        f.put32(kFirstIp + 12 + 16 + 8, 0x16100); // MP0 base 0 differs
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryBaseMismatch);
    }
    {
        FakeDiscovery f;
        f.put16(kFirstIp + 12 + 16, 254); // no MP0 entry
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryMalformed);
    }
    {
        FakeDiscovery f;
        f.put16(kTable + 4, 4);
        f.bytes[kTable + 78] = 1; // 64-bit base addresses
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryUnsupported);
    }
    {
        FakeDiscovery f;
        f.put16(kTable + 12, 0); // no dies
        f.seal();
        CHECK(parse(f, &d) == kDiscoveryMalformed);
    }
}

static Discovery discovery()
{
    FakeDiscovery f;
    Discovery d;
    parse(f, &d);
    return d;
}

static void testGfxConfig()
{
    const Discovery d = discovery();
    {
        FakeRegisters r;
        GfxConfig g;
        CHECK(readGfxConfig(r.reader(), 0x80000, d, &g) == kOK);
        CHECK(r.reads == 7 && r.order[0] == kRegGrbmStatus && r.order[1] == kRegGrbmGfxIndex);
        CHECK(g.cuActiveMask == 0x7F && g.cuActiveCount == 7);
        CHECK(g.rbActiveMask == 0x3 && g.rbActiveCount == 2);
        CHECK(!g.guiActive && g.gbAddrConfig == 0x24000042u);
    }
    {
        FakeRegisters r;
        r.userShader = 0x00010000u; // user also disables CU 0
        r.userRb = 0x00020000u;     // and RB 1
        GfxConfig g;
        CHECK(readGfxConfig(r.reader(), 0x80000, d, &g) == kOK);
        CHECK(g.cuActiveMask == 0x7E && g.cuActiveCount == 6 && g.rbActiveMask == 0x1);
    }
    {
        FakeRegisters r;
        r.gfxIndex = 0x00000100u; // SH 1 selected
        GfxConfig g;
        CHECK(readGfxConfig(r.reader(), 0x80000, d, &g) == kGfxIndexNotSe0Sh0);
        CHECK(g.cuActiveCount == 0 && g.ccShaderArrayConfig == 0x00800000u);
    }
    {
        FakeRegisters r;
        Discovery two = d;
        two.gcNumSe = 2;
        GfxConfig g;
        CHECK(readGfxConfig(r.reader(), 0x80000, two, &g) == kGcInfoUnavailable);
        Discovery none = d;
        none.gcInfoFound = false;
        CHECK(readGfxConfig(r.reader(), 0x80000, none, &g) == kGcInfoUnavailable);
    }
    {
        FakeRegisters r;
        GfxConfig g;
        CHECK(readGfxConfig(r.reader(), 0x30000, d, &g) == kRegisterNotAllowed); // GRBM_GFX_INDEX beyond
    }
    CHECK(!registerAllowed(kRegGrbmGfxIndex, 2) && registerAllowed(kRegGrbmGfxIndex, 3));
    for (uint32_t i = 0; i < kStage2RegisterCount; i++) CHECK(kStage3Registers[i] == kStage2Registers[i]);
    for (uint32_t i = 0; i < kStage1RegisterCount; i++) CHECK(kStage2Registers[i] == kStage1Registers[i]);
    CHECK(kRegGrbmGfxIndex == 0x30800 && kRegCcShaderArrayConfig == 0x89bc && kRegGbAddrConfig == 0x98f8);
}

int main()
{
    testValidDevice();
    testPreMapRefusals();
    testCapabilityWalk();
    testAperture();
    testBootState();
    testAllowlist();
    testCarveout();
    testDeviceRanges();
    testDiscovery();
    testGfxConfig();
    if (failures == 0) std::printf("core tests passed\n");
    return failures == 0 ? 0 : 1;
}
