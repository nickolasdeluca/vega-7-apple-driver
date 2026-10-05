// cezanne-diag: reads CezanneGPU's allowlisted registers through the
// diagnostic interface (stage 4 on). The driver re-checks the device and maps
// the register BAR read-only for every read. The only writes it can request
// are the stage 6 scratch test (--scratch-test) and the stage 7 SMU version
// queries (--smu-query).
//
// Usage: sudo cezanne-diag [--repeat N] [--interval MS] [--scratch-test] [--smu-query]
#include <IOKit/IOKitLib.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "cezanne_core.h"

using namespace cezanne;

namespace {

struct Named {
    const char *name;
    uint32_t offset;
};

// Every register the driver allows at stage 6, in its list order; earlier
// stages allow a prefix.
const Named kRegisters[] = {
    {"MP0_SMN_C2PMSG_33", kRegC2PMsg33},
    {"RCC_CONFIG_MEMSIZE", kRegConfigMemsize},
    {"MC_VM_FB_OFFSET", kRegMcVmFbOffset},
    {"GRBM_STATUS", kRegGrbmStatus},
    {"GRBM_GFX_INDEX", kRegGrbmGfxIndex},
    {"CC_GC_SHADER_ARRAY_CONFIG", kRegCcShaderArrayConfig},
    {"GC_USER_SHADER_ARRAY_CONFIG", kRegUserShaderArrayConfig},
    {"CC_RB_BACKEND_DISABLE", kRegCcRbBackendDisable},
    {"GC_USER_RB_BACKEND_DISABLE", kRegUserRbBackendDisable},
    {"GB_ADDR_CONFIG", kRegGbAddrConfig},
    // Stage 5.
    {"SMUIO_GFX_MISC_CNTL", kRegSmuioGfxMiscCntl},
    {"MP1_SMN_C2PMSG_66", kRegMp1C2PMsg66},
    {"MP1_SMN_C2PMSG_82", kRegMp1C2PMsg82},
    {"MP1_SMN_C2PMSG_90", kRegMp1C2PMsg90},
    {"MP0_SMN_C2PMSG_35", kRegMp0C2PMsg35},
    {"MP0_SMN_C2PMSG_81", kRegMp0C2PMsg81},
    {"RLC_CGTT_MGCG_OVERRIDE", kRegRlcCgttMgcgOverride},
    {"RLC_CGCG_CGLS_CTRL", kRegRlcCgcgCglsCtrl},
    {"RLC_CGCG_CGLS_CTRL_3D", kRegRlcCgcgCglsCtrl3d},
    {"RLC_MEM_SLP_CNTL", kRegRlcMemSlpCntl},
    {"CP_MEM_SLP_CNTL", kRegCpMemSlpCntl},
    {"RLC_PG_CNTL", kRegRlcPgCntl},
    {"GRBM_STATUS2", kRegGrbmStatus2},
    {"GRBM_STATUS_SE0", kRegGrbmStatusSe0},
    {"CP_BUSY_STAT", kRegCpBusyStat},
    {"CP_CPF_STATUS", kRegCpCpfStatus},
    {"CP_ME_CNTL", kRegCpMeCntl},
    {"CP_MEC_CNTL", kRegCpMecCntl},
    {"RLC_CNTL", kRegRlcCntl},
    {"RLC_STAT", kRegRlcStat},
    {"CP_PFP_INSTR_PNTR", kRegCpPfpInstrPntr},
    {"CP_ME_INSTR_PNTR", kRegCpMeInstrPntr},
    {"CP_MEC1_INSTR_PNTR", kRegCpMec1InstrPntr},
    {"SDMA0_CLK_CTRL", kRegSdma0ClkCtrl},
    {"SDMA0_POWER_CNTL", kRegSdma0PowerCntl},
    {"SDMA0_F32_CNTL", kRegSdma0F32Cntl},
    {"SDMA0_STATUS_REG", kRegSdma0StatusReg},
    {"SDMA0_GFX_RB_CNTL", kRegSdma0GfxRbCntl},
    {"HDP_MEM_POWER_LS", kRegHdpMemPowerLs},
    {"ATHUB_MISC_CNTL", kRegAthubMiscCntl},
    {"ATC_L2_MISC_CG", kRegAtcL2MiscCg},
    {"DAGB0_CNTL_MISC2", kRegDagb0CntlMisc2},
    {"MC_VM_FB_LOCATION_BASE_MMHUB", kRegMmhubFbLocationBase},
    {"MC_VM_FB_LOCATION_TOP_MMHUB", kRegMmhubFbLocationTop},
    {"VM_L2_CNTL_MMHUB", kRegMmhubVmL2Cntl},
    {"VM_CONTEXT0_CNTL_MMHUB", kRegMmhubVmContext0Cntl},
    {"MC_VM_MX_L1_TLB_CNTL_MMHUB", kRegMmhubMxL1TlbCntl},
    {"IH_RB_CNTL", kRegIhRbCntl},
    // Stage 6.
    {"SCRATCH_REG0", kRegScratchReg0},
};
static_assert(sizeof(kRegisters) / sizeof(kRegisters[0]) == kStage6RegisterCount, "one name per register");

void usage(FILE *out)
{
    std::fprintf(out, "usage: sudo cezanne-diag [--repeat N] [--interval MS] [--scratch-test] [--smu-query]\n"
                      "Reads every CezanneGPU diagnostic register N times (default 1), MS apart (default 1000).\n"
                      "--scratch-test first runs the stage 6 write test: writes 0xCAFEDEAD to SCRATCH_REG0,\n"
                      "then restores its original value.\n"
                      "--smu-query first asks the SMU for its driver-interface and firmware versions.\n");
}

bool parseCount(const char *text, unsigned long max, unsigned long *value)
{
    char *end = nullptr;
    errno = 0;
    unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > max) return false;
    *value = parsed;
    return true;
}

// Each step is printed and flushed before it is sent, so a hang leaves the
// step on screen.
void step(const char *text)
{
    std::printf("%s ... ", text);
    std::fflush(stdout);
}

bool call(io_connect_t connection, uint32_t selector, uint64_t *output, uint32_t count, const uint64_t *input = nullptr,
          uint32_t inputCount = 0)
{
    uint32_t outputCount = count;
    kern_return_t result = IOConnectCallScalarMethod(connection, selector, input, inputCount, output, &outputCount);
    if (result != KERN_SUCCESS || outputCount != count) {
        std::printf("call failed 0x%08x\n", result);
        return false;
    }
    return true;
}

// The stage 6 scratch test: check, write, restore. Returns true if every
// step returned ok. The driver restores the register if this exits early.
bool scratchTest(io_connect_t connection)
{
    uint64_t check[7] = {};
    step("scratch 1/3 check: GFX on, CP halted, RLC off, GUI idle; read SCRATCH_REG0 twice");
    if (!call(connection, kDiagnosticScratchCheck, check, 7)) return false;
    std::printf("%s\n", statusName(static_cast<Status>(check[0])));
    std::printf("  SMUIO_GFX_MISC_CNTL 0x%08llx  CP_ME_CNTL 0x%08llx  CP_MEC_CNTL 0x%08llx\n"
                "  RLC_CNTL 0x%08llx  GRBM_STATUS 0x%08llx  SCRATCH_REG0 original 0x%08llx\n",
                static_cast<unsigned long long>(check[1]), static_cast<unsigned long long>(check[2]),
                static_cast<unsigned long long>(check[3]), static_cast<unsigned long long>(check[4]),
                static_cast<unsigned long long>(check[5]), static_cast<unsigned long long>(check[6]));
    if (check[0] != kOK) return false;

    uint64_t written[2] = {};
    std::printf("scratch 2/3 write: SCRATCH_REG0 <- 0x%08x ... ", kScratchPattern);
    std::fflush(stdout);
    if (!call(connection, kDiagnosticScratchWrite, written, 2)) return false;
    std::printf("%s, read back 0x%08llx\n", statusName(static_cast<Status>(written[0])),
                static_cast<unsigned long long>(written[1]));

    uint64_t restored[2] = {};
    std::printf("scratch 3/3 restore: SCRATCH_REG0 <- 0x%08llx ... ", static_cast<unsigned long long>(check[6]));
    std::fflush(stdout);
    if (!call(connection, kDiagnosticScratchRestore, restored, 2)) return false;
    std::printf("%s, read back 0x%08llx\n", statusName(static_cast<Status>(restored[0])),
                static_cast<unsigned long long>(restored[1]));
    return written[0] == kOK && restored[0] == kOK;
}

// The stage 7 SMU queries: check the mailbox is idle, then the two version
// queries Linux sends first. Stops at the first status other than ok.
bool smuQuery(io_connect_t connection)
{
    uint64_t check[4] = {};
    step("smu 1/3 check: MP1 mailbox idle (C2PMSG_90 non-zero)");
    if (!call(connection, kDiagnosticSmuCheck, check, 4)) return false;
    std::printf("%s\n  C2PMSG_66 0x%08llx  C2PMSG_82 0x%08llx  C2PMSG_90 0x%08llx\n",
                statusName(static_cast<Status>(check[0])), static_cast<unsigned long long>(check[1]),
                static_cast<unsigned long long>(check[2]), static_cast<unsigned long long>(check[3]));
    if (check[0] != kOK) return false;

    const struct {
        const char *text;
        uint64_t message;
    } queries[] = {
        {"smu 2/3 query: GetDriverIfVersion (0x3)", kSmuMsgGetDriverIfVersion},
        {"smu 3/3 query: GetSmuVersion (0x2)", kSmuMsgGetSmuVersion},
    };
    for (const auto &query : queries) {
        uint64_t out[3] = {};
        step(query.text);
        if (!call(connection, kDiagnosticSmuQuery, out, 3, &query.message, 1)) return false;
        std::printf("%s, response 0x%02llx, answer 0x%08llx\n", statusName(static_cast<Status>(out[0])),
                    static_cast<unsigned long long>(out[1]), static_cast<unsigned long long>(out[2]));
        if (out[0] != kOK) return false;
        if (query.message == kSmuMsgGetSmuVersion) {
            uint32_t v = static_cast<uint32_t>(out[2]);
            std::printf("  SMU firmware %u.%u.%u.%u (program.major.minor.debug)\n", v >> 24, (v >> 16) & 0xff,
                        (v >> 8) & 0xff, v & 0xff);
        }
    }
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    unsigned long repeat = 1, interval = 1000;
    bool scratch = false, smu = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--scratch-test") == 0) {
            scratch = true;
            continue;
        }
        if (std::strcmp(argv[i], "--smu-query") == 0) {
            smu = true;
            continue;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        }
        if (std::strcmp(argv[i], "--repeat") == 0 && i + 1 < argc && parseCount(argv[i + 1], 100000, &repeat)) {
            i++;
        } else if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc &&
                   parseCount(argv[i + 1], 3600000, &interval)) {
            i++;
        } else {
            usage(stderr);
            return 2;
        }
    }

    io_service_t service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("CezanneGPU"));
    if (service == IO_OBJECT_NULL) {
        std::fprintf(stderr, "cezanne-diag: no CezanneGPU service (boot the stage %u test EFI)\n",
                     kDiagnosticStage);
        return 1;
    }
    io_connect_t connection = IO_OBJECT_NULL;
    kern_return_t result = IOServiceOpen(service, mach_task_self(), 0, &connection);
    IOObjectRelease(service);
    if (result != KERN_SUCCESS) {
        std::fprintf(stderr, "cezanne-diag: cannot open CezanneGPU (0x%08x)%s\n", result,
                     result == kIOReturnNotPrivileged ? ": run with sudo"
                     : result == kIOReturnUnsupported ? ": diagnostics need stage 4 with stage 3 ok"
                                                      : "");
        return 1;
    }

    uint64_t info[2] = {0, 0};
    uint32_t infoCount = 2;
    result = IOConnectCallScalarMethod(connection, kDiagnosticGetInfo, nullptr, 0, info, &infoCount);
    if (result != KERN_SUCCESS || infoCount != 2 || info[0] != kDiagnosticVersion) {
        std::fprintf(stderr, "cezanne-diag: unexpected interface (0x%08x, version %llu)\n", result,
                     static_cast<unsigned long long>(info[0]));
        IOServiceClose(connection);
        return 1;
    }
    std::printf("CezanneGPU diagnostics v%llu, driver stage %llu\n", static_cast<unsigned long long>(info[0]),
                static_cast<unsigned long long>(info[1]));

    const uint32_t count = info[1] >= 6   ? kStage6RegisterCount
                           : info[1] == 5 ? kStage5RegisterCount
                                          : kStage3RegisterCount;
    int failures = 0;
    if (scratch) {
        if (info[1] < kScratchStage) {
            std::fprintf(stderr, "cezanne-diag: --scratch-test needs driver stage %u\n", kScratchStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!scratchTest(connection)) failures++;
    }
    if (smu) {
        if (info[1] < kSmuStage) {
            std::fprintf(stderr, "cezanne-diag: --smu-query needs driver stage %u\n", kSmuStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!smuQuery(connection)) failures++;
    }
    for (unsigned long pass = 0; pass < repeat; pass++) {
        if (pass > 0) usleep(static_cast<useconds_t>(interval * 1000));
        if (repeat > 1) std::printf("-- pass %lu\n", pass + 1);
        for (uint32_t i = 0; i < count; i++) {
            const Named &reg = kRegisters[i];
            // Name first and flushed: if a read hangs the machine, the last
            // line on screen identifies the register.
            std::printf("%-30s 0x%05x  ", reg.name, reg.offset);
            std::fflush(stdout);
            uint64_t input = reg.offset;
            uint64_t output[2] = {0, 0};
            uint32_t outputCount = 2;
            result = IOConnectCallScalarMethod(connection, kDiagnosticReadRegister, &input, 1, output, &outputCount);
            if (result != KERN_SUCCESS || outputCount != 2) {
                std::printf("call failed 0x%08x\n", result);
                failures++;
            } else if (output[0] != kOK) {
                std::printf("%s\n", statusName(static_cast<Status>(output[0])));
                failures++;
            } else {
                std::printf("0x%08llx\n", static_cast<unsigned long long>(output[1]));
            }
        }
    }
    IOServiceClose(connection);
    return failures == 0 ? 0 : 1;
}
