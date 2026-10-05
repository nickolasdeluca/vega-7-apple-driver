// cezanne-diag: reads CezanneGPU's allowlisted registers through the stage 4
// diagnostic interface. The driver re-checks the device and maps the register
// BAR read-only for every read; this tool cannot request a write.
//
// Usage: sudo cezanne-diag [--repeat N] [--interval MS]
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

// Every register the driver allows at stage 5, in its list order; a stage 4
// driver allows the first kStage3RegisterCount.
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
};
static_assert(sizeof(kRegisters) / sizeof(kRegisters[0]) == kStage5RegisterCount, "one name per register");

void usage(FILE *out)
{
    std::fprintf(out, "usage: sudo cezanne-diag [--repeat N] [--interval MS]\n"
                      "Reads every CezanneGPU diagnostic register N times (default 1), MS apart (default 1000).\n");
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

} // namespace

int main(int argc, char **argv)
{
    unsigned long repeat = 1, interval = 1000;
    for (int i = 1; i < argc; i++) {
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

    const uint32_t count = info[1] >= 5 ? kStage5RegisterCount : kStage3RegisterCount;
    int failures = 0;
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
