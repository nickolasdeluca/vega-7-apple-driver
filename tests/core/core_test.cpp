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

// A fake read-only memory region (the metrics check region).
struct FakeMemory {
    uint32_t words[kMetricsCheckSize / 4];
    bool fail = false;
    FakeMemory() { std::memset(words, 0, sizeof(words)); }
    static bool read32(void *context, uint32_t offset, uint32_t *value)
    {
        FakeMemory *self = static_cast<FakeMemory *>(context);
        if (self->fail || offset % 4 != 0 || offset >= kMetricsCheckSize) return false;
        *value = self->words[offset / 4];
        return true;
    }
    MemoryReader reader() { return MemoryReader{read32, this}; }
};

struct FakeRegisters {
    uint32_t c2pmsg33 = 0x80000000u;
    uint32_t memsize = 2048;
    uint32_t fbOffset = 0x580; // 0x580000000 >> 24
    uint32_t gfxIndex = 0xE0000000u;    // broadcast writes, SE 0 / SH 0 / instance 0
    uint32_t ccShader = 0x00800000u;    // CU 7 fused off
    uint32_t userShader = 0;
    uint32_t ccRb = 0, userRb = 0;
    uint32_t gfxMisc = 0x4; // PWR_GFXOFF_STATUS 2: GFX on
    uint32_t cpMe = 0x15000000u, cpMec = 0x50000000u, rlc = 0, grbm = 0x00003028u; // boot 7 values
    uint32_t scratch = 0;
    bool scratchWritable = true;
    // A fake SMU mailbox (boot 7 idle state) and its behaviour.
    uint32_t smuMsg = 0, smuArg = 0, smuResp = 1;
    uint32_t smuReply = 1;       // response it gives
    int smuDelay = 0;            // pauses before it answers; -1 never
    int smuPending = -1;         // pauses left for the current message
    int gfxOnDelay = 0;          // pauses after DisableGfxOff until GFX reads on; -1 never
    uint32_t mmhubFbBase = 0xf400;
    FakeMemory *memory = nullptr; // where TransferTableSmu2Dram writes, if set
    bool tableOverflows = false;  // write past the 148 bytes too
    uint32_t smuAddrHigh = 0, smuAddrLow = 0;
    int gfxOffPending = -1;
    // A fake PSP mailbox (boot 14 state): secure OS up, ready, no ring.
    uint32_t psp81 = 0x0016d568u, psp64 = 0x80000000u, psp67 = 0, psp69 = 0, psp70 = 0, psp71 = 0;
    uint32_t pspReply = 0x80000000u; // C2PMSG_64 once it answers
    int pspDelay = 0;                // pauses before it answers; -1 never
    int pspPending = -1;
    int pauses = 0;
    bool fail = false;
    uint32_t order[32];
    int reads = 0;

    static bool read32(void *context, uint32_t offset, uint32_t *value)
    {
        FakeRegisters *self = static_cast<FakeRegisters *>(context);
        if (self->reads < 32) self->order[self->reads] = offset;
        self->reads++;
        if (self->fail) return false;
        *value = offset == kRegC2PMsg33        ? self->c2pmsg33
                 : offset == kRegConfigMemsize ? self->memsize
                 : offset == kRegMcVmFbOffset  ? self->fbOffset
                 : offset == kRegGrbmStatus    ? self->grbm
                 : offset == kRegCpMeCntl      ? self->cpMe
                 : offset == kRegCpMecCntl     ? self->cpMec
                 : offset == kRegRlcCntl       ? self->rlc
                 : offset == kRegScratchReg0   ? self->scratch
                 : offset == kRegMp1C2PMsg66   ? self->smuMsg
                 : offset == kRegMp1C2PMsg82   ? self->smuArg
                 : offset == kRegMp1C2PMsg90   ? self->smuResp
                 : offset == kRegMmhubFbLocationBase ? self->mmhubFbBase
                 : offset == kRegGrbmGfxIndex  ? self->gfxIndex
                 : offset == kRegCcShaderArrayConfig   ? self->ccShader
                 : offset == kRegUserShaderArrayConfig ? self->userShader
                 : offset == kRegCcRbBackendDisable    ? self->ccRb
                 : offset == kRegUserRbBackendDisable  ? self->userRb
                 : offset == kRegGbAddrConfig          ? 0x24000042u
                 : offset == kRegSmuioGfxMiscCntl      ? self->gfxMisc
                 : offset == kRegMp0C2PMsg81           ? self->psp81
                 : offset == kRegMp0C2PMsg64           ? self->psp64
                 : offset == kRegMp0C2PMsg67           ? self->psp67
                 : offset == kRegMp0C2PMsg69           ? self->psp69
                 : offset == kRegMp0C2PMsg70           ? self->psp70
                 : offset == kRegMp0C2PMsg71           ? self->psp71
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
    for (uint32_t s = kOK; s <= kMetricsOutOfOrder; s++) CHECK(std::strcmp(statusName(static_cast<Status>(s)), "unknown") != 0);
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
    CHECK(registerAllowed(kRegGrbmGfxIndex, 4) && !registerAllowed(kRegGrbmGfxIndex + 4, 4));
    {
        FakeRegisters r;
        uint32_t value = 1;
        CHECK(readAllowedRegister(r.reader(), 0x80000, 4, kRegGbAddrConfig, &value) == kOK && value == 0x24000042u);
        CHECK(readAllowedRegister(r.reader(), 0x80000, 4, kRegGbAddrConfig + 2, &value) == kRegisterNotAllowed);
        CHECK(readAllowedRegister(r.reader(), 0x80000, 2, kRegGbAddrConfig, &value) == kRegisterNotAllowed);
        CHECK(readAllowedRegister(r.reader(), 0x80000, 4, 0x8014, &value) == kRegisterNotAllowed && value == 0);
        CHECK(r.reads == 1);
    }
    for (uint32_t i = 0; i < kStage2RegisterCount; i++) CHECK(kStage3Registers[i] == kStage2Registers[i]);
    for (uint32_t i = 0; i < kStage1RegisterCount; i++) CHECK(kStage2Registers[i] == kStage1Registers[i]);
    CHECK(kRegGrbmGfxIndex == 0x30800 && kRegCcShaderArrayConfig == 0x89bc && kRegGbAddrConfig == 0x98f8);
}

static void testStage5()
{
    // Byte offsets: measured base + dword from the v6.12 headers, times 4.
    CHECK(kRegSmuioGfxMiscCntl == 0x5a320 && kRegMp1C2PMsg90 == 0x58a68 && kRegMp0C2PMsg81 == 0x58244);
    CHECK(kRegRlcCgcgCglsCtrl == 0x3b124 && kRegCpMeCntl == 0x86d8 && kRegSdma0F32Cntl == 0x4a28);
    CHECK(kRegMmhubFbLocationBase == 0x6a0b0 && kRegIhRbCntl == 0x4480 && kRegAthubMiscCntl == 0x30a8);
    for (uint32_t i = 0; i < kStage3RegisterCount; i++) CHECK(kStage5Registers[i] == kStage3Registers[i]);
    for (uint32_t i = 0; i < kStage5RegisterCount; i++) {
        CHECK(kStage5Registers[i] % 4 == 0 && kStage5Registers[i] + 4 <= 0x80000); // inside BAR5
        for (uint32_t j = i + 1; j < kStage5RegisterCount; j++) CHECK(kStage5Registers[i] != kStage5Registers[j]);
    }
    for (uint32_t i = 0; i < kGfxGatedRegisterCount; i++) CHECK(registerAllowed(kGfxGatedRegisters[i], 5));
    CHECK(!registerAllowed(kRegMp1C2PMsg90, 4) && registerAllowed(kRegMp1C2PMsg90, 5));
    CHECK(gfxGated(kRegCpMeCntl) && !gfxGated(kRegSdma0F32Cntl) && !gfxGated(kRegGrbmStatus));

    uint32_t value = 0;
    {
        FakeRegisters r;
        r.gfxMisc = 0x4;
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 5, kRegCpMeCntl, &value) == kOK);
        CHECK(r.reads == 2 && r.order[0] == kRegSmuioGfxMiscCntl && r.order[1] == kRegCpMeCntl);
    }
    const uint32_t notOn[] = {0x0, 0x2, 0x6};
    for (uint32_t misc : notOn) {
        FakeRegisters r;
        r.gfxMisc = misc;
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 5, kRegCpMeCntl, &value) == kGfxNotOn);
        CHECK(r.reads == 1 && value == 0);
    }
    {
        FakeRegisters r;
        r.gfxMisc = 0;
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 5, kRegSdma0F32Cntl, &value) == kOK);
        CHECK(r.reads == 1); // not gated: SMUIO not consulted
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 4, kRegSdma0F32Cntl, &value) == kRegisterNotAllowed);
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 4, kRegGbAddrConfig, &value) == kOK);
    }
}

static void testStage10()
{
    CHECK(kRegMp0C2PMsg36 == 0x58190 && kRegMp0C2PMsg64 == 0x58200 && kRegMp0C2PMsg67 == 0x5820c);
    CHECK(kRegMp0C2PMsg69 == 0x58214 && kRegMp0C2PMsg70 == 0x58218 && kRegMp0C2PMsg71 == 0x5821c);
    CHECK(kRegMmhubFbOffset == 0x6a05c && kRegMmhubDefaultAddrLsb == 0x6a060 && kRegMmhubAgpTop == 0x6a0b8);
    CHECK(kRegMmhubApertureLow == 0x6a0c4 && kRegMmhubApertureHigh == 0x6a0c8);
    CHECK(kRegGcFbLocationBase == 0xa600 && kRegGcAgpTop == 0xa608 && kRegGcApertureHigh == 0xa618);
    CHECK(kStage10RegisterCount == kStage6RegisterCount + 21);
    for (uint32_t i = 0; i < kStage6RegisterCount; i++) CHECK(kStage10Registers[i] == kStage6Registers[i]);
    for (uint32_t i = 0; i < kStage10RegisterCount; i++) {
        CHECK(kStage10Registers[i] % 4 == 0 && kStage10Registers[i] + 4 <= 0x80000); // inside BAR5
        for (uint32_t j = i + 1; j < kStage10RegisterCount; j++) CHECK(kStage10Registers[i] != kStage10Registers[j]);
    }
    for (uint32_t i = kStage6RegisterCount; i < kStage10RegisterCount; i++) {
        CHECK(registerAllowed(kStage10Registers[i], 10) && !registerAllowed(kStage10Registers[i], 9));
        CHECK(!writeAllowed(kStage10Registers[i], 0, 10));
    }
    CHECK(!registerAllowed(kRegMp0C2PMsg64, 9));
    for (uint32_t i = 0; i < kStage10GfxGatedRegisterCount; i++) CHECK(gfxGated(kStage10GfxGatedRegisters[i]));
    CHECK(gfxGated(kRegGcApertureHigh) && !gfxGated(kRegMp0C2PMsg64) && !gfxGated(kRegMmhubApertureHigh));

    uint32_t value = 0;
    {
        FakeRegisters r;
        r.gfxMisc = 0;
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 10, kRegGcApertureLow, &value) == kGfxNotOn);
        CHECK(r.reads == 1 && value == 0);
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 10, kRegMp0C2PMsg64, &value) == kOK);
        CHECK(r.reads == 2); // not gated: SMUIO not consulted
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 9, kRegMp0C2PMsg64, &value) == kRegisterNotAllowed);
    }
    {
        FakeRegisters r;
        r.gfxMisc = 0x4;
        CHECK(readDiagnosticRegister(r.reader(), 0x80000, 10, kRegGcApertureLow, &value) == kOK);
        CHECK(r.reads == 2 && r.order[0] == kRegSmuioGfxMiscCntl && r.order[1] == kRegGcApertureLow);
    }
}

// Records writes into a FakeRegisters; pause can simulate another writer.
struct FakeWriter {
    FakeRegisters *registers;
    bool fail = false;
    uint32_t pauseWrites = 0; // non-zero: value something else writes during the pause
    uint32_t offsets[32], values[32];
    int writes = 0;

    explicit FakeWriter(FakeRegisters *r) : registers(r) {}
    static bool write32(void *context, uint32_t offset, uint32_t value)
    {
        FakeWriter *self = static_cast<FakeWriter *>(context);
        if (self->writes < 32) {
            self->offsets[self->writes] = offset;
            self->values[self->writes] = value;
        }
        self->writes++;
        if (self->fail) return false;
        FakeRegisters *r = self->registers;
        if (offset == kRegScratchReg0 && r->scratchWritable) r->scratch = value;
        if (offset == kRegMp1C2PMsg90) r->smuResp = value;
        if (offset == kRegMp1C2PMsg82) r->smuArg = value;
        if (offset == kRegMp1C2PMsg66) {
            r->smuMsg = value;
            r->smuPending = r->smuDelay;
            if (r->smuPending == 0) answer(r);
        }
        if (offset == kRegMp0C2PMsg69) r->psp69 = value;
        if (offset == kRegMp0C2PMsg70) r->psp70 = value;
        if (offset == kRegMp0C2PMsg71) r->psp71 = value;
        if (offset == kRegMp0C2PMsg64) {
            r->psp64 = value; // response flag clear until the PSP answers
            r->pspPending = r->pspDelay;
            if (r->pspPending == 0) r->psp64 = r->pspReply;
        }
        return true;
    }
    static void answer(FakeRegisters *r)
    {
        r->smuResp = r->smuReply;
        if (r->smuReply == kSmuResponseOk &&
            (r->smuMsg == kSmuMsgGetDriverIfVersion || r->smuMsg == kSmuMsgGetSmuVersion))
            r->smuArg = r->smuMsg == kSmuMsgGetDriverIfVersion ? 14 : 0x00403500u;
        if (r->smuReply == kSmuResponseOk && r->smuMsg == kSmuMsgDisableGfxOff) r->gfxOffPending = r->gfxOnDelay;
        if (r->smuReply == kSmuResponseOk && r->smuMsg == kSmuMsgSetDriverDramAddrHigh) r->smuAddrHigh = r->smuArg;
        if (r->smuReply == kSmuResponseOk && r->smuMsg == kSmuMsgSetDriverDramAddrLow) r->smuAddrLow = r->smuArg;
        // The SMU writes the table only to the address it was given.
        if (r->smuReply == kSmuResponseOk && r->smuMsg == kSmuMsgTransferTableSmu2Dram && r->memory != nullptr &&
            ((uint64_t(r->smuAddrHigh) << 32) | r->smuAddrLow) == kMetricsGpuAddress) {
            for (uint32_t i = 0; i < kMetricsSize / 4; i++) r->memory->words[i] = 0x00010000u * (2 * i + 1) + 2 * i;
            if (r->tableOverflows) r->memory->words[kMetricsSize / 4] = 1;
        }
        r->smuPending = -1;
    }
    static void pause(void *context)
    {
        FakeWriter *self = static_cast<FakeWriter *>(context);
        if (self->pauseWrites != 0) self->registers->scratch = self->pauseWrites;
        FakeRegisters *r = self->registers;
        if (r->smuPending > 0 && --r->smuPending == 0) answer(r);
        if (r->gfxOffPending > 0 && --r->gfxOffPending == 0) r->gfxMisc = 0x4;
        if (r->pspPending > 0 && --r->pspPending == 0) r->psp64 = r->pspReply;
        r->pauses++;
    }
    RegisterWriter writer() { return RegisterWriter{write32, pause, this}; }
};

static Status checkWith(uint32_t FakeRegisters::*field, uint32_t value)
{
    FakeRegisters r;
    r.*field = value;
    FakeWriter w(&r);
    ScratchCheck c;
    return checkScratch(r.reader(), 0x80000, w.writer(), 6, &c);
}

static void testScratch()
{
    CHECK(checkWith(&FakeRegisters::cpMe, 0x14000000u) == kCpNotHalted);
    CHECK(checkWith(&FakeRegisters::rlc, 0x1) == kRlcEnabled);
    CHECK(kRegScratchReg0 == 0x30100 && kScratchPageOffset == 0x30000 && kScratchPattern == 0xCAFEDEADu);
    CHECK(writeAllowed(kRegScratchReg0, kScratchPattern, 6) && !writeAllowed(kRegScratchReg0, kScratchPattern, 5));
    for (uint32_t i = 0; i < kStage6RegisterCount; i++) {
        if (kStage6Registers[i] != kRegScratchReg0) CHECK(!writeAllowed(kStage6Registers[i], 0, 6));
    }
    CHECK(!writeAllowed(kRegScratchReg0 + 4, 0, 6) && !writeAllowed(kRegGrbmGfxIndex, 0, 6));
    for (uint32_t i = 0; i < kStage5RegisterCount; i++) CHECK(kStage6Registers[i] == kStage5Registers[i]);
    CHECK(registerAllowed(kRegScratchReg0, 6) && !registerAllowed(kRegScratchReg0, 5));

    {
        FakeRegisters r;
        r.scratch = 0x12345678u;
        FakeWriter w(&r);
        ScratchCheck c;
        CHECK(checkScratch(r.reader(), 0x80000, w.writer(), 6, &c) == kOK);
        CHECK(c.original == 0x12345678u && c.original2 == 0x12345678u && w.writes == 0);
        CHECK(r.order[0] == kRegSmuioGfxMiscCntl);
        uint32_t readback = 0;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, c.original, &readback) == kOK);
        CHECK(readback == kScratchPattern && w.writes == 1 && w.offsets[0] == kRegScratchReg0 &&
              w.values[0] == kScratchPattern);
        CHECK(restoreScratch(r.reader(), 0x80000, w.writer(), 6, c.original, &readback) == kOK);
        CHECK(readback == 0x12345678u && r.scratch == 0x12345678u && w.writes == 2 && w.values[1] == 0x12345678u);
    }
    // Every failed precondition stops before any write.
    struct Case {
        uint32_t FakeRegisters::*field;
        uint32_t value;
        Status expected;
    } cases[] = {
        {&FakeRegisters::gfxMisc, 0x0, kGfxNotOn},
        {&FakeRegisters::cpMe, 0x14000000u, kCpNotHalted},
        {&FakeRegisters::cpMe, 0x11000000u, kCpNotHalted},
        {&FakeRegisters::cpMec, 0x40000000u, kCpNotHalted},
        {&FakeRegisters::rlc, 0x1, kRlcEnabled},
        {&FakeRegisters::grbm, 0x80003028u, kGfxBusy},
    };
    for (const Case &k : cases) {
        FakeRegisters r;
        r.*(k.field) = k.value;
        FakeWriter w(&r);
        ScratchCheck c;
        CHECK(checkScratch(r.reader(), 0x80000, w.writer(), 6, &c) == k.expected);
        uint32_t readback = 1;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == k.expected);
        CHECK(w.writes == 0 && readback == 0);
    }
    {
        FakeRegisters r;
        FakeWriter w(&r);
        w.pauseWrites = 0x55; // something else writes it between the two reads
        ScratchCheck c;
        CHECK(checkScratch(r.reader(), 0x80000, w.writer(), 6, &c) == kScratchUnstable);
        uint32_t readback = 0;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kScratchUnstable);
        CHECK(w.writes == 0);
    }
    {
        FakeRegisters r;
        r.scratch = 7;
        FakeWriter w(&r);
        uint32_t readback = 0;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kScratchUnstable);
        CHECK(w.writes == 0); // original changed since the check
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 5, 7, &readback) == kRegisterNotAllowed);
        CHECK(restoreScratch(r.reader(), 0x80000, w.writer(), 5, 7, &readback) == kRegisterNotAllowed);
        CHECK(w.writes == 0);
    }
    {
        FakeRegisters r;
        r.scratchWritable = false; // writes are ignored
        FakeWriter w(&r);
        uint32_t readback = 0;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kScratchReadbackMismatch);
        CHECK(readback == 0 && w.writes == 1);
        r.scratch = 9; // restore cannot take effect either
        CHECK(restoreScratch(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kScratchRestoreMismatch);
    }
    {
        FakeRegisters r;
        FakeWriter w(&r);
        w.fail = true;
        uint32_t readback = 0;
        CHECK(writeScratchPattern(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kRegisterWriteFailed);
        CHECK(restoreScratch(r.reader(), 0x80000, w.writer(), 6, 0, &readback) == kRegisterWriteFailed);
    }
}

static void testSmu()
{
    CHECK(kRegMp1C2PMsg66 == 0x58a08 && kSmuPageOffset == 0x58000);
    CHECK((kRegMp1C2PMsg82 & ~0xFFFu) == kSmuPageOffset && (kRegMp1C2PMsg90 & ~0xFFFu) == kSmuPageOffset);
    // The value-level allowlist, at stage 7 and not before.
    CHECK(writeAllowed(kRegMp1C2PMsg90, 0, 7) && writeAllowed(kRegMp1C2PMsg82, 0, 7));
    CHECK(writeAllowed(kRegMp1C2PMsg66, 2, 7) && writeAllowed(kRegMp1C2PMsg66, 3, 7));
    CHECK(!writeAllowed(kRegMp1C2PMsg66, 2, 6) && !writeAllowed(kRegMp1C2PMsg90, 0, 6));
    CHECK(!writeAllowed(kRegMp1C2PMsg90, 1, 7) && !writeAllowed(kRegMp1C2PMsg82, 1, 7));
    for (uint32_t message = 0; message < 0x40; message++) {
        if (message != 2 && message != 3) CHECK(!writeAllowed(kRegMp1C2PMsg66, message, 7));
        if (message != 2 && message != 3 && message != 8) CHECK(!writeAllowed(kRegMp1C2PMsg66, message, 8));
    }
    CHECK(writeAllowed(kRegMp1C2PMsg66, kSmuMsgDisableGfxOff, 8) && !writeAllowed(kRegMp1C2PMsg66, 0x7, 8));
    CHECK(!writeAllowed(kRegMp0C2PMsg81, 0, 7) && !writeAllowed(kRegC2PMsg33, 0, 7) && !writeAllowed(kRegMp0C2PMsg35, 0, 7));
    for (uint32_t offset = kSmuPageOffset; offset < kSmuPageOffset + kPageSize; offset += 4) {
        if (offset != kRegMp1C2PMsg66 && offset != kRegMp1C2PMsg82 && offset != kRegMp1C2PMsg90)
            CHECK(!writeAllowed(offset, 0, 7));
    }

    uint32_t response = 0, answer = 0;
    {
        FakeRegisters r;
        FakeWriter w(&r);
        SmuMailbox m;
        CHECK(checkSmu(r.reader(), 0x80000, 7, &m) == kOK && m.response == 1 && w.writes == 0);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetDriverIfVersion, &response, &answer) == kOK);
        CHECK(response == kSmuResponseOk && answer == 14);
        // __smu_cmn_send_msg order: response, argument, message.
        CHECK(w.writes == 3 && w.offsets[0] == kRegMp1C2PMsg90 && w.values[0] == 0 && w.offsets[1] == kRegMp1C2PMsg82 &&
              w.values[1] == 0 && w.offsets[2] == kRegMp1C2PMsg66 && w.values[2] == kSmuMsgGetDriverIfVersion);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) == kOK);
        CHECK(answer == 0x00403500u && w.writes == 6 && r.smuMsg == kSmuMsgGetSmuVersion);
    }
    {
        FakeRegisters r;
        r.smuDelay = 5; // answers after a few pauses
        FakeWriter w(&r);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) == kOK);
        CHECK(answer == 0x00403500u);
    }
    {
        FakeRegisters r;
        r.smuResp = 0; // a message is in flight
        FakeWriter w(&r);
        SmuMailbox m;
        CHECK(checkSmu(r.reader(), 0x80000, 7, &m) == kSmuBusy);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) == kSmuBusy);
        CHECK(w.writes == 0);
    }
    {
        FakeRegisters r;
        r.smuDelay = -1; // never answers
        FakeWriter w(&r);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) == kSmuTimeout);
        CHECK(response == 0 && answer == 0 && w.writes == 3);
    }
    const uint32_t errors[] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB};
    for (uint32_t error : errors) {
        FakeRegisters r;
        r.smuReply = error;
        r.smuArg = 0x77;
        FakeWriter w(&r);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) ==
              kSmuResponseNotOk);
        CHECK(response == error && answer == 0); // no answer read after a failure
    }
    {
        FakeRegisters r;
        FakeWriter w(&r);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 6, kSmuMsgGetSmuVersion, &response, &answer) ==
              kRegisterNotAllowed);
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, 0x4, &response, &answer) == kRegisterNotAllowed);
        SmuMailbox m;
        CHECK(checkSmu(r.reader(), 0x80000, 6, &m) == kRegisterNotAllowed);
        CHECK(w.writes == 0);
        w.fail = true;
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 7, kSmuMsgGetSmuVersion, &response, &answer) ==
              kRegisterWriteFailed);
        CHECK(w.writes == 1); // stops at the first failed write
    }
}

static void testGfxOff()
{
    uint32_t response = 0, misc = 0;
    {
        FakeRegisters r;
        FakeWriter w(&r);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 8, &response, &misc) == kOK);
        CHECK(response == kSmuResponseOk && ((misc & kGfxOffStatusMask) >> kGfxOffStatusShift) == kGfxOffStatusOn);
        CHECK(w.writes == 3 && w.offsets[2] == kRegMp1C2PMsg66 && w.values[2] == kSmuMsgDisableGfxOff);
        CHECK(r.smuArg == 0); // no answer is produced or read
    }
    {
        FakeRegisters r;
        r.gfxMisc = 0x0; // in GFXOFF; turns on 3 pauses after the message
        r.gfxOnDelay = 3;
        FakeWriter w(&r);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 8, &response, &misc) == kOK && misc == 0x4);
    }
    {
        FakeRegisters r;
        r.gfxMisc = 0x0;
        r.gfxOnDelay = -1; // never reports GFX on
        FakeWriter w(&r);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 8, &response, &misc) == kGfxOffTimeout);
        CHECK(response == kSmuResponseOk);
    }
    {
        FakeRegisters r;
        r.smuReply = 0xFD;
        FakeWriter w(&r);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 8, &response, &misc) == kSmuResponseNotOk);
        CHECK(response == 0xFD && misc == 0); // no confirmation poll after a failure
    }
    {
        FakeRegisters r;
        r.smuResp = 0;
        FakeWriter w(&r);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 8, &response, &misc) == kSmuBusy && w.writes == 0);
        CHECK(disallowGfxOff(r.reader(), 0x80000, w.writer(), 7, &response, &misc) == kRegisterNotAllowed);
        uint32_t answer = 0;
        CHECK(sendSmuQuery(r.reader(), 0x80000, w.writer(), 8, kSmuMsgDisableGfxOff, &response, &answer) ==
              kRegisterNotAllowed);
        CHECK(w.writes == 0);
    }
}

static void testMetrics()
{
    CHECK(kMetricsGpuAddress == 0xF440000000ull && kMetricsPhysical == 0x600000000ull);
    CHECK(sizeof(SmuMetrics) == kMetricsSize && kMetricsWordCount * 2 == kMetricsSize);
    // Message and argument pairs, from stage 9 only.
    CHECK(smuArgumentAllowed(kSmuMsgSetDriverDramAddrHigh, 0xF4, 9) && smuArgumentAllowed(kSmuMsgSetDriverDramAddrLow, 0x40000000u, 9));
    CHECK(smuArgumentAllowed(kSmuMsgTransferTableSmu2Dram, 7, 9) && !smuArgumentAllowed(kSmuMsgTransferTableSmu2Dram, 7, 8));
    CHECK(!smuArgumentAllowed(kSmuMsgSetDriverDramAddrLow, 0xF4, 9) && !smuArgumentAllowed(kSmuMsgSetDriverDramAddrHigh, 0x40000000u, 9));
    CHECK(!smuArgumentAllowed(kSmuMsgTransferTableSmu2Dram, 4, 9) && !smuArgumentAllowed(0x1D, 7, 9));
    CHECK(!smuArgumentAllowed(kSmuMsgGetSmuVersion, 7, 9) && smuArgumentAllowed(kSmuMsgGetSmuVersion, 0, 9));
    for (uint32_t table = 0; table < 8; table++) {
        if (table != kTableSmuMetrics) CHECK(!smuArgumentAllowed(kSmuMsgTransferTableSmu2Dram, table, 9));
    }
    CHECK(writeAllowed(kRegMp1C2PMsg66, 0x1C, 9) && !writeAllowed(kRegMp1C2PMsg66, 0x1C, 8) &&
          !writeAllowed(kRegMp1C2PMsg66, 0x1D, 9));
    CHECK(writeAllowed(kRegMp1C2PMsg82, 0x40000000u, 9) && !writeAllowed(kRegMp1C2PMsg82, 0x40000000u, 8) &&
          !writeAllowed(kRegMp1C2PMsg82, 0x40001000u, 9));

    // The host's BARs, as at stage 1.
    uint8_t data[80];
    putCells(data, 0x83000010u, 0x640000000ull, 0x10000000ull);
    putCells(data + 20, 0x83000018u, 0x650000000ull, 0x200000ull);
    putCells(data + 40, 0x81000020u, 0xe000ull, 0x100ull);
    putCells(data + 60, 0x82000024u, 0xfca00000ull, 0x80000ull);
    Range ranges[8];
    uint32_t count = 0;
    CHECK(parseAssignedAddresses(data, sizeof(data), ranges, 8, &count) == kOK);
    MetricsTarget t;
    {
        FakeRegisters r;
        r.fbOffset = 0x5c0;
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, ranges, count, &t) == kOK);
        CHECK(t.gpuAddress == 0xF440000000ull && t.physical == 0x600000000ull && t.configMemsize == 2048);
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 8, ranges, count, &t) == kRegisterNotAllowed);
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, ranges, 0, &t) == kMetricsTargetInvalid);
        r.mmhubFbBase = 0xf401;
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, ranges, count, &t) == kMetricsAddressMismatch);
    }
    {
        FakeRegisters r; // FB offset 0x580: not the measured value
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, ranges, count, &t) == kMetricsAddressMismatch);
    }
    {
        FakeRegisters r;
        r.fbOffset = 0x5c0;
        r.memsize = 1024; // the page would fall in the high reserve
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, ranges, count, &t) == kMetricsTargetInvalid);
        r.memsize = 2048;
        Range clash[1] = {{0x600008000ull, 0x1000}}; // a device range inside the check region
        CHECK(checkMetricsTarget(r.reader(), 0x80000, 9, clash, 1, &t) == kMetricsTargetInvalid);
    }

    // The stability check: stale non-zero data passes, a change fails.
    static uint32_t snapshot[kMetricsCheckSize / 4];
    {
        FakeRegisters r;
        FakeWriter w(&r);
        FakeMemory m;
        for (uint32_t i = 0; i < kMetricsCheckSize / 4; i++) m.words[i] = 0x9E3779B9u * i; // stale data
        CHECK(checkRegionStable(m.reader(), kMetricsCheckSize, w.writer(), 3, snapshot) == kOK);
        CHECK(snapshot[1] == 0x9E3779B9u && snapshot[kMetricsCheckSize / 4 - 1] == m.words[kMetricsCheckSize / 4 - 1]);
        m.fail = true;
        CHECK(checkRegionStable(m.reader(), kMetricsCheckSize, w.writer(), 3, snapshot) == kRegisterReadFailed);
    }
    {
        // Something writes the last word of the region during the wait.
        struct ChangingMemory {
            FakeMemory memory;
            int pauses = 0;
            static void pause(void *context)
            {
                ChangingMemory *self = static_cast<ChangingMemory *>(context);
                if (++self->pauses == 2) self->memory.words[kMetricsCheckSize / 4 - 1] ^= 1;
            }
        } changing;
        RegisterWriter writer = {nullptr, ChangingMemory::pause, &changing};
        CHECK(checkRegionStable(changing.memory.reader(), kMetricsCheckSize, writer, 3, snapshot) == kTableRegionInUse);
        CHECK(changing.pauses == 3);
    }

    // The three messages, the SMU's write and the verification.
    {
        FakeRegisters r;
        FakeMemory m;
        for (uint32_t i = 0; i < kMetricsCheckSize / 4; i++) m.words[i] = 0x5A5A0000u + i; // stale data
        r.memory = &m;
        FakeWriter w(&r);
        CHECK(checkRegionStable(m.reader(), kMetricsCheckSize, w.writer(), 1, snapshot) == kOK);
        uint32_t responses[3];
        CHECK(requestMetrics(r.reader(), 0x80000, w.writer(), 9, responses) == kOK);
        CHECK(responses[0] == 1 && responses[1] == 1 && responses[2] == 1 && w.writes == 9);
        CHECK(w.values[1] == 0xF4 && w.values[2] == kSmuMsgSetDriverDramAddrHigh);
        CHECK(w.values[4] == 0x40000000u && w.values[5] == kSmuMsgSetDriverDramAddrLow);
        CHECK(w.values[7] == kTableSmuMetrics && w.values[8] == kSmuMsgTransferTableSmu2Dram);
        SmuMetrics metrics;
        CHECK(verifyMetricsPage(m.reader(), snapshot, &metrics) == kOK);
        CHECK(metrics.words[0] == 0 && metrics.words[1] == 1 && metrics.words[kMetricsGfxTemperature] == 60 &&
              metrics.words[kMetricsWordCount - 1] == 73);
        CHECK(requestMetrics(r.reader(), 0x80000, w.writer(), 8, responses) == kRegisterNotAllowed);
    }
    {
        FakeRegisters r;
        FakeMemory m;
        r.memory = &m;
        r.tableOverflows = true;
        FakeWriter w(&r);
        CHECK(checkRegionStable(m.reader(), kMetricsCheckSize, w.writer(), 1, snapshot) == kOK);
        uint32_t responses[3];
        CHECK(requestMetrics(r.reader(), 0x80000, w.writer(), 9, responses) == kOK);
        SmuMetrics metrics;
        CHECK(verifyMetricsPage(m.reader(), snapshot, &metrics) == kTableOverflow);
    }
    {
        FakeRegisters r;
        FakeMemory m; // the SMU writes nothing over stale data
        for (uint32_t i = 0; i < kMetricsCheckSize / 4; i++) m.words[i] = 0x12340000u + i;
        FakeWriter w(&r);
        CHECK(checkRegionStable(m.reader(), kMetricsCheckSize, w.writer(), 1, snapshot) == kOK);
        uint32_t responses[3];
        CHECK(requestMetrics(r.reader(), 0x80000, w.writer(), 9, responses) == kOK);
        SmuMetrics metrics;
        CHECK(verifyMetricsPage(m.reader(), snapshot, &metrics) == kTableNotWritten);
    }
    {
        FakeRegisters r;
        r.smuReply = 0xFE;
        FakeWriter w(&r);
        uint32_t responses[3];
        CHECK(requestMetrics(r.reader(), 0x80000, w.writer(), 9, responses) == kSmuResponseNotOk);
        CHECK(responses[0] == 0xFE && responses[1] == 0 && w.writes == 3); // stops after the first message
    }
}

static void testPspRing()
{
    CHECK(kPspRingGpuAddress == 0xF440100000ull && kPspRingPhysical == 0x600100000ull);
    CHECK(uint32_t(kPspRingGpuAddress) == 0x40100000u && uint32_t(kPspRingGpuAddress >> 32) == 0xF4u);
    CHECK(kPspCmdGbrIhSet == 0x80000u && kPspCmdInitGpcomRing == (2u << 16) && kPspCmdDestroyRings == 0x30000u);
    CHECK(kPspIhVmcConfig == (0x1244bu | (1u << 18) | (1u << 20)) && kPspIhUmcConfig == (0x1216bu | (1u << 20)));
    // The ring's check region is clear of the stage 9 metrics region.
    CHECK(kPspRingPhysical >= kMetricsPhysical + kMetricsCheckSize);

    // Commands and their only arguments.
    CHECK(pspCommandAllowed(kPspCmdGbrIhSet, kPspIhClientVmc, kPspIhVmcConfig, 0, 11));
    CHECK(pspCommandAllowed(kPspCmdGbrIhSet, kPspIhClientUmc, kPspIhUmcConfig, 0, 11));
    CHECK(pspCommandAllowed(kPspCmdInitGpcomRing, 0x40100000u, 0xF4u, kPspRingSize, 11));
    CHECK(pspCommandAllowed(kPspCmdDestroyRings, 0, 0, 0, 11));
    CHECK(!pspCommandAllowed(kPspCmdGbrIhSet, kPspIhClientVmc, kPspIhVmcConfig, 0, 10));
    CHECK(!pspCommandAllowed(kPspCmdGbrIhSet, kPspIhClientVmc, kPspIhUmcConfig, 0, 11));
    CHECK(!pspCommandAllowed(kPspCmdGbrIhSet, 0x40100000u, 0xF4u, 0, 11));
    CHECK(!pspCommandAllowed(kPspCmdInitGpcomRing, kPspIhClientVmc, kPspIhVmcConfig, kPspRingSize, 11));
    CHECK(!pspCommandAllowed(kPspCmdInitGpcomRing, 0x40100000u, 0xF4u, 0x2000, 11));
    CHECK(!pspCommandAllowed(kPspCmdInitGpcomRing, 0x40000000u, 0xF4u, kPspRingSize, 11));
    CHECK(!pspCommandAllowed(kPspCmdDestroyRings, 3, 0, 0, 11));
    CHECK(!pspCommandAllowed(0x00010000u, 0, 0, 0, 11) && !pspCommandAllowed(0x00070000u, 0, 0, 0, 11));
    // Register values, from stage 11 only.
    CHECK(writeAllowed(kRegMp0C2PMsg64, kPspCmdInitGpcomRing, 11) && !writeAllowed(kRegMp0C2PMsg64, kPspCmdInitGpcomRing, 10));
    CHECK(!writeAllowed(kRegMp0C2PMsg64, 0x00070000u, 11) && !writeAllowed(kRegMp0C2PMsg64, 0x00010000u, 11));
    CHECK(writeAllowed(kRegMp0C2PMsg69, 0x40100000u, 11) && !writeAllowed(kRegMp0C2PMsg69, 0x40000000u, 11));
    CHECK(writeAllowed(kRegMp0C2PMsg70, 0xF4u, 11) && !writeAllowed(kRegMp0C2PMsg70, 0xF5u, 11));
    CHECK(writeAllowed(kRegMp0C2PMsg71, 0x1000u, 11) && !writeAllowed(kRegMp0C2PMsg71, 0x2000u, 11));
    CHECK(!writeAllowed(kRegMp0C2PMsg67, 0, 11) && !writeAllowed(kRegMp0C2PMsg81, 0, 11));

    uint8_t data[80];
    putCells(data, 0x83000010u, 0x640000000ull, 0x10000000ull);
    putCells(data + 20, 0x83000018u, 0x650000000ull, 0x200000ull);
    putCells(data + 40, 0x81000020u, 0xe000ull, 0x100ull);
    putCells(data + 60, 0x82000024u, 0xfca00000ull, 0x80000ull);
    Range ranges[8];
    uint32_t count = 0;
    CHECK(parseAssignedAddresses(data, sizeof(data), ranges, 8, &count) == kOK);
    PspMailbox m;
    MetricsTarget t;
    {
        FakeRegisters r;
        r.fbOffset = 0x5c0;
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kOK);
        CHECK(t.gpuAddress == 0xF440100000ull && t.physical == 0x600100000ull && m.signOfLife == 0x0016d568u);
        CHECK(checkPspRing(r.reader(), 0x80000, 10, ranges, count, &m, &t) == kRegisterNotAllowed);
        Range clash[1] = {{0x600108000ull, 0x1000}};
        CHECK(checkPspRing(r.reader(), 0x80000, 11, clash, 1, &m, &t) == kMetricsTargetInvalid);
        r.psp81 = 0;
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kPspNotRunning);
        r.psp81 = 1;
        r.psp64 = 0x80000001u;
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kPspNotReady);
        r.psp64 = 0x80000000u;
        r.psp71 = 0x1000;
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kPspRingExists);
        r.psp71 = 0;
        r.psp67 = 1;
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kPspRingExists);
    }
    {
        FakeRegisters r; // FB offset 0x580
        CHECK(checkPspRing(r.reader(), 0x80000, 11, ranges, count, &m, &t) == kMetricsAddressMismatch);
    }

    uint32_t responses[3];
    {
        FakeRegisters r;
        r.pspDelay = 25; // answers 5 pauses into the poll
        FakeWriter w(&r);
        CHECK(createPspRing(r.reader(), 0x80000, w.writer(), 11, responses) == kOK);
        const uint32_t offsets[] = {kRegMp0C2PMsg69, kRegMp0C2PMsg70, kRegMp0C2PMsg64, kRegMp0C2PMsg69,
                                    kRegMp0C2PMsg70, kRegMp0C2PMsg64, kRegMp0C2PMsg69, kRegMp0C2PMsg70,
                                    kRegMp0C2PMsg71, kRegMp0C2PMsg64};
        const uint32_t values[] = {3, 0x0015244bu, 0x00080000u, 4, 0x0011216bu, 0x00080000u,
                                   0x40100000u, 0xF4u, 0x1000u, 0x00020000u};
        CHECK(w.writes == 10);
        for (int i = 0; i < 10; i++) CHECK(w.offsets[i] == offsets[i] && w.values[i] == values[i]);
        CHECK(responses[0] == 0x80000000u && responses[1] == 0x80000000u && responses[2] == 0x80000000u);
        CHECK(r.pauses == 3 * 25);
        CHECK(createPspRing(r.reader(), 0x80000, w.writer(), 10, responses) == kRegisterNotAllowed);
    }
    {
        FakeRegisters r;
        r.pspReply = 0x80000005u; // answered, status 5
        FakeWriter w(&r);
        CHECK(createPspRing(r.reader(), 0x80000, w.writer(), 11, responses) == kPspResponseNotOk);
        CHECK(w.writes == 3 && responses[0] == 0x80000005u && responses[1] == 0);
    }
    {
        FakeRegisters r;
        r.pspDelay = -1; // never answers
        FakeWriter w(&r);
        CHECK(createPspRing(r.reader(), 0x80000, w.writer(), 11, responses) == kPspTimeout);
        CHECK(w.writes == 3 && r.pauses == int(kPspSettlePauses + kPspPollPauses));
    }
    {
        FakeRegisters r;
        r.psp69 = 0x1234; // a ring address already set
        FakeWriter w(&r);
        CHECK(createPspRing(r.reader(), 0x80000, w.writer(), 11, responses) == kPspRingExists);
        CHECK(w.writes == 0);
    }

    uint32_t response = 0;
    {
        FakeRegisters r;
        FakeWriter w(&r);
        CHECK(destroyPspRing(r.reader(), 0x80000, w.writer(), 11, false, &response) == kPspOutOfOrder);
        CHECK(w.writes == 0);
        CHECK(destroyPspRing(r.reader(), 0x80000, w.writer(), 11, true, &response) == kOK);
        CHECK(w.writes == 1 && w.offsets[0] == kRegMp0C2PMsg64 && w.values[0] == kPspCmdDestroyRings);
        CHECK(response == 0x80000000u && r.pauses == int(kPspSettlePauses));
        r.psp64 = 0x00020000u; // still busy with an earlier command
        int before = w.writes;
        CHECK(destroyPspRing(r.reader(), 0x80000, w.writer(), 11, true, &response) == kPspNotReady);
        CHECK(w.writes == before);
    }

    // The region compare.
    static uint32_t snapshot[kPspRingCheckSize / 4];
    {
        FakeMemory memory;
        for (uint32_t i = 0; i < kPspRingCheckSize / 4; i++) memory.words[i] = snapshot[i] = 0x9E3779B9u * i;
        uint32_t inPage = 9, outside = 9;
        CHECK(compareRegion(memory.reader(), kPspRingCheckSize, kPspRingSize, snapshot, &inPage, &outside) == kOK);
        CHECK(inPage == 0 && outside == 0);
        memory.words[3] ^= 1;
        memory.words[kPspRingSize / 4] ^= 1;
        memory.words[kPspRingCheckSize / 4 - 1] ^= 1;
        CHECK(compareRegion(memory.reader(), kPspRingCheckSize, kPspRingSize, snapshot, &inPage, &outside) ==
              kPspRegionChanged);
        CHECK(inPage == 1 && outside == 2);
    }
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
    testStage5();
    testStage10();
    testScratch();
    testSmu();
    testGfxOff();
    testMetrics();
    testPspRing();
    if (failures == 0) std::printf("core tests passed\n");
    return failures == 0 ? 0 : 1;
}
