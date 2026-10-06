// cezanne-diag: reads CezanneGPU's allowlisted registers through the
// diagnostic interface (stage 4 on). The driver re-checks the device and maps
// the register BAR read-only for every read. The only writes it can request
// are the stage 6 scratch test (--scratch-test), the stage 7 SMU version
// queries (--smu-query), the stage 8 DisallowGfxOff (--gfxoff-disallow) and
// the stage 9 metrics-table transfer (--smu-metrics) and the stage 11 PSP
// ring create and destroy (--psp-ring) and the stage 12 TMR setup and
// teardown through the ring (--psp-tmr) and the stage 13 SDMA0 firmware load
// (--psp-sdma), and the stage 14 SDMA power-up register inventory
// (--sdma-inventory), and the stage 15 first SDMA copy (--sdma-copy), and
// the stage 17 GART and interrupt ring (--gart-ih).
// --psp-state (stage 10) and --inventory16 (stage 16) only read.
//
// Usage: sudo cezanne-diag [--repeat N] [--interval MS] [--scratch-test] [--smu-query] [--gfxoff-disallow]
//                          [--smu-metrics] [--psp-ring] [--psp-tmr] [--psp-sdma] [--sdma-inventory]
//                          [--sdma-copy] [--gart-ih] [--psp-state] [--inventory16]
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

// Every register the driver allows at stage 16, in its list order; earlier
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
    // Stage 10.
    {"MP0_SMN_C2PMSG_36", kRegMp0C2PMsg36},
    {"MP0_SMN_C2PMSG_64", kRegMp0C2PMsg64},
    {"MP0_SMN_C2PMSG_67", kRegMp0C2PMsg67},
    {"MP0_SMN_C2PMSG_69", kRegMp0C2PMsg69},
    {"MP0_SMN_C2PMSG_70", kRegMp0C2PMsg70},
    {"MP0_SMN_C2PMSG_71", kRegMp0C2PMsg71},
    {"MC_VM_FB_OFFSET_MMHUB", kRegMmhubFbOffset},
    {"MC_VM_SYS_APR_DEFAULT_LSB_MMHUB", kRegMmhubDefaultAddrLsb},
    {"MC_VM_SYS_APR_DEFAULT_MSB_MMHUB", kRegMmhubDefaultAddrMsb},
    {"MC_VM_AGP_TOP_MMHUB", kRegMmhubAgpTop},
    {"MC_VM_AGP_BOT_MMHUB", kRegMmhubAgpBot},
    {"MC_VM_AGP_BASE_MMHUB", kRegMmhubAgpBase},
    {"MC_VM_SYS_APR_LOW_MMHUB", kRegMmhubApertureLow},
    {"MC_VM_SYS_APR_HIGH_MMHUB", kRegMmhubApertureHigh},
    {"MC_VM_FB_LOCATION_BASE_GC", kRegGcFbLocationBase},
    {"MC_VM_FB_LOCATION_TOP_GC", kRegGcFbLocationTop},
    {"MC_VM_AGP_TOP_GC", kRegGcAgpTop},
    {"MC_VM_AGP_BOT_GC", kRegGcAgpBot},
    {"MC_VM_AGP_BASE_GC", kRegGcAgpBase},
    {"MC_VM_SYS_APR_LOW_GC", kRegGcApertureLow},
    {"MC_VM_SYS_APR_HIGH_GC", kRegGcApertureHigh},
    // Stage 13.
    {"SDMA0_UCODE_CHECKSUM", kRegSdma0UcodeChecksum},
    // Stage 14.
    {"SDMA0_CNTL", kRegSdma0Cntl},
    {"SDMA0_CHICKEN_BITS", kRegSdma0ChickenBits},
    {"SDMA0_GB_ADDR_CONFIG", kRegSdma0GbAddrConfig},
    {"SDMA0_GB_ADDR_CONFIG_READ", kRegSdma0GbAddrConfigRead},
    {"SDMA0_SEM_WAIT_FAIL_TIMER_CNTL", kRegSdma0SemWaitFailTimerCntl},
    {"SDMA0_UTCL1_WATERMK", kRegSdma0Utcl1Watermk},
    {"SDMA0_UTCL1_TIMEOUT", kRegSdma0Utcl1Timeout},
    {"SDMA0_UTCL1_PAGE", kRegSdma0Utcl1Page},
    {"SDMA0_GFX_RB_BASE", kRegSdma0GfxRbBase},
    {"SDMA0_GFX_RB_BASE_HI", kRegSdma0GfxRbBaseHi},
    {"SDMA0_GFX_RB_RPTR", kRegSdma0GfxRbRptr},
    {"SDMA0_GFX_RB_RPTR_HI", kRegSdma0GfxRbRptrHi},
    {"SDMA0_GFX_RB_WPTR", kRegSdma0GfxRbWptr},
    {"SDMA0_GFX_RB_WPTR_HI", kRegSdma0GfxRbWptrHi},
    {"SDMA0_GFX_RB_WPTR_POLL_CNTL", kRegSdma0GfxRbWptrPollCntl},
    {"SDMA0_GFX_RB_RPTR_ADDR_HI", kRegSdma0GfxRbRptrAddrHi},
    {"SDMA0_GFX_RB_RPTR_ADDR_LO", kRegSdma0GfxRbRptrAddrLo},
    {"SDMA0_GFX_IB_CNTL", kRegSdma0GfxIbCntl},
    {"SDMA0_GFX_DOORBELL", kRegSdma0GfxDoorbell},
    {"SDMA0_GFX_DOORBELL_OFFSET", kRegSdma0GfxDoorbellOffset},
    {"SDMA0_GFX_RB_WPTR_POLL_ADDR_HI", kRegSdma0GfxRbWptrPollAddrHi},
    {"SDMA0_GFX_RB_WPTR_POLL_ADDR_LO", kRegSdma0GfxRbWptrPollAddrLo},
    {"SDMA0_GFX_MINOR_PTR_UPDATE", kRegSdma0GfxMinorPtrUpdate},
    {"SDMA0_RLC0_RB_WPTR_POLL_CNTL", kRegSdma0Rlc0RbWptrPollCntl},
    {"SDMA0_RLC1_RB_WPTR_POLL_CNTL", kRegSdma0Rlc1RbWptrPollCntl},
    // Stage 16.
    {"OTG0_OTG_CONTROL", kRegOtg0OtgControl},
    {"OTG0_OTG_H_TOTAL", kRegOtg0OtgHTotal},
    {"OTG0_OTG_V_TOTAL", kRegOtg0OtgVTotal},
    {"OTG0_OTG_H_BLANK_START_END", kRegOtg0OtgHBlankStartEnd},
    {"OTG0_OTG_V_BLANK_START_END", kRegOtg0OtgVBlankStartEnd},
    {"HUBP0_DCHUBP_CNTL", kRegHubp0DchubpCntl},
    {"HUBP0_DCSURF_SURFACE_CONFIG", kRegHubp0DcsurfSurfaceConfig},
    {"HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION", kRegHubp0DcsurfPriViewportDimension},
    {"HUBPREQ0_DCSURF_SURFACE_PITCH", kRegHubpreq0DcsurfSurfacePitch},
    {"HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS", kRegHubpreq0DcsurfPrimarySurfaceAddress},
    {"HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH", kRegHubpreq0DcsurfPrimarySurfaceAddressHigh},
    {"OTG1_OTG_CONTROL", kRegOtg1OtgControl},
    {"OTG1_OTG_H_TOTAL", kRegOtg1OtgHTotal},
    {"OTG1_OTG_V_TOTAL", kRegOtg1OtgVTotal},
    {"OTG1_OTG_H_BLANK_START_END", kRegOtg1OtgHBlankStartEnd},
    {"OTG1_OTG_V_BLANK_START_END", kRegOtg1OtgVBlankStartEnd},
    {"HUBP1_DCHUBP_CNTL", kRegHubp1DchubpCntl},
    {"HUBP1_DCSURF_SURFACE_CONFIG", kRegHubp1DcsurfSurfaceConfig},
    {"HUBP1_DCSURF_PRI_VIEWPORT_DIMENSION", kRegHubp1DcsurfPriViewportDimension},
    {"HUBPREQ1_DCSURF_SURFACE_PITCH", kRegHubpreq1DcsurfSurfacePitch},
    {"HUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS", kRegHubpreq1DcsurfPrimarySurfaceAddress},
    {"HUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH", kRegHubpreq1DcsurfPrimarySurfaceAddressHigh},
    {"OTG2_OTG_CONTROL", kRegOtg2OtgControl},
    {"OTG2_OTG_H_TOTAL", kRegOtg2OtgHTotal},
    {"OTG2_OTG_V_TOTAL", kRegOtg2OtgVTotal},
    {"OTG2_OTG_H_BLANK_START_END", kRegOtg2OtgHBlankStartEnd},
    {"OTG2_OTG_V_BLANK_START_END", kRegOtg2OtgVBlankStartEnd},
    {"HUBP2_DCHUBP_CNTL", kRegHubp2DchubpCntl},
    {"HUBP2_DCSURF_SURFACE_CONFIG", kRegHubp2DcsurfSurfaceConfig},
    {"HUBP2_DCSURF_PRI_VIEWPORT_DIMENSION", kRegHubp2DcsurfPriViewportDimension},
    {"HUBPREQ2_DCSURF_SURFACE_PITCH", kRegHubpreq2DcsurfSurfacePitch},
    {"HUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS", kRegHubpreq2DcsurfPrimarySurfaceAddress},
    {"HUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH", kRegHubpreq2DcsurfPrimarySurfaceAddressHigh},
    {"OTG3_OTG_CONTROL", kRegOtg3OtgControl},
    {"OTG3_OTG_H_TOTAL", kRegOtg3OtgHTotal},
    {"OTG3_OTG_V_TOTAL", kRegOtg3OtgVTotal},
    {"OTG3_OTG_H_BLANK_START_END", kRegOtg3OtgHBlankStartEnd},
    {"OTG3_OTG_V_BLANK_START_END", kRegOtg3OtgVBlankStartEnd},
    {"HUBP3_DCHUBP_CNTL", kRegHubp3DchubpCntl},
    {"HUBP3_DCSURF_SURFACE_CONFIG", kRegHubp3DcsurfSurfaceConfig},
    {"HUBP3_DCSURF_PRI_VIEWPORT_DIMENSION", kRegHubp3DcsurfPriViewportDimension},
    {"HUBPREQ3_DCSURF_SURFACE_PITCH", kRegHubpreq3DcsurfSurfacePitch},
    {"HUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS", kRegHubpreq3DcsurfPrimarySurfaceAddress},
    {"HUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH", kRegHubpreq3DcsurfPrimarySurfaceAddressHigh},
    {"DCN_VM_FB_LOCATION_BASE", kRegDcnVmFbLocationBase},
    {"DCN_VM_FB_LOCATION_TOP", kRegDcnVmFbLocationTop},
    {"DCN_VM_FB_OFFSET", kRegDcnVmFbOffset},
    {"DCN_VM_AGP_BASE", kRegDcnVmAgpBase},
    {"DCN_VM_AGP_BOT", kRegDcnVmAgpBot},
    {"DCN_VM_AGP_TOP", kRegDcnVmAgpTop},
    {"DIG0_DIG_BE_CNTL", kRegDig0DigBeCntl},
    {"DIG1_DIG_BE_CNTL", kRegDig1DigBeCntl},
    {"DIG2_DIG_BE_CNTL", kRegDig2DigBeCntl},
    {"DIG3_DIG_BE_CNTL", kRegDig3DigBeCntl},
    {"DIG4_DIG_BE_CNTL", kRegDig4DigBeCntl},
    {"VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32", kRegVmContext0PageTableBaseAddrLo32},
    {"VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32", kRegVmContext0PageTableBaseAddrHi32},
    {"VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32", kRegVmContext0PageTableStartAddrLo32},
    {"VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32", kRegVmContext0PageTableStartAddrHi32},
    {"VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32", kRegVmContext0PageTableEndAddrLo32},
    {"VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32", kRegVmContext0PageTableEndAddrHi32},
    {"VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32", kRegVmL2ProtectionFaultDefaultAddrLo32},
    {"VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32", kRegVmL2ProtectionFaultDefaultAddrHi32},
    {"VM_L2_PROTECTION_FAULT_CNTL", kRegVmL2ProtectionFaultCntl},
    {"VM_L2_PROTECTION_FAULT_CNTL2", kRegVmL2ProtectionFaultCntl2},
    {"VM_L2_PROTECTION_FAULT_STATUS", kRegVmL2ProtectionFaultStatus},
    {"VM_L2_CNTL2", kRegVmL2Cntl2},
    {"VM_L2_CNTL3", kRegVmL2Cntl3},
    {"VM_L2_CNTL4", kRegVmL2Cntl4},
    {"VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32", kRegVmL2Context1IdentityApertureLowAddrLo32},
    {"VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32", kRegVmL2Context1IdentityApertureLowAddrHi32},
    {"VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32", kRegVmL2Context1IdentityApertureHighAddrLo32},
    {"VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32", kRegVmL2Context1IdentityApertureHighAddrHi32},
    {"VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32", kRegVmL2ContextIdentityPhysicalOffsetLo32},
    {"VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32", kRegVmL2ContextIdentityPhysicalOffsetHi32},
    {"VM_INVALIDATE_ENG17_ACK", kRegVmInvalidateEng17Ack},
    {"VM_INVALIDATE_ENG0_ADDR_RANGE_LO32", kRegVmInvalidateEng0AddrRangeLo32},
    {"VM_INVALIDATE_ENG0_ADDR_RANGE_HI32", kRegVmInvalidateEng0AddrRangeHi32},
    {"IH_RB_BASE", kRegIhRbBase},
    {"IH_RB_BASE_HI", kRegIhRbBaseHi},
    {"IH_RB_WPTR", kRegIhRbWptr},
    {"IH_RB_RPTR", kRegIhRbRptr},
    {"IH_RB_WPTR_ADDR_LO", kRegIhRbWptrAddrLo},
    {"IH_RB_WPTR_ADDR_HI", kRegIhRbWptrAddrHi},
    {"IH_DOORBELL_RPTR", kRegIhDoorbellRptr},
    {"IH_CHICKEN", kRegIhChicken},
    {"IH_RB_CNTL_RING1", kRegIhRbCntlRing1},
    {"IH_RB_CNTL_RING2", kRegIhRbCntlRing2},
    {"INTERRUPT_CNTL", kRegInterruptCntl},
    {"INTERRUPT_CNTL2", kRegInterruptCntl2},
    {"BIF_IH_DOORBELL_RANGE", kRegBifIhDoorbellRange},
    {"VM_INVALIDATE_ENG17_ADDR_RANGE_LO32", kRegVmInvalidateEng17AddrRangeLo32},
    {"VM_INVALIDATE_ENG17_ADDR_RANGE_HI32", kRegVmInvalidateEng17AddrRangeHi32},
};
static_assert(sizeof(kRegisters) / sizeof(kRegisters[0]) == kStage16RegisterCount + kStage17RegisterCount,
              "one name per register");

const char *registerName(uint32_t offset)
{
    for (const Named &reg : kRegisters) {
        if (reg.offset == offset) return reg.name;
    }
    return "?";
}

void usage(FILE *out)
{
    std::fprintf(out, "usage: sudo cezanne-diag [--repeat N] [--interval MS] [--scratch-test] [--smu-query]\n"
                      "                         [--gfxoff-disallow] [--smu-metrics] [--psp-ring] [--psp-tmr]\n"
                      "                         [--psp-sdma] [--sdma-inventory] [--sdma-copy] [--gart-ih]\n"
                      "                         [--psp-state] [--inventory16]\n"
                      "Reads every CezanneGPU diagnostic register N times (default 1), MS apart (default 1000).\n"
                      "--scratch-test first runs the stage 6 write test: writes 0xCAFEDEAD to SCRATCH_REG0,\n"
                      "then restores its original value.\n"
                      "--smu-query first asks the SMU for its driver-interface and firmware versions.\n"
                      "--gfxoff-disallow first sends DisallowGfxOff and waits for GFX to report on.\n"
                      "--smu-metrics first has the SMU write its metrics table to the checked carveout page\n"
                      "and prints it.\n"
                      "--psp-ring first creates the PSP kernel-mode ring at the checked carveout page, compares\n"
                      "the page region with its snapshot, and destroys the ring.\n"
                      "--psp-tmr first creates the ring, submits SETUP_TMR through it, checks the memory around\n"
                      "it, then sends DESTROY_TMR and destroys the ring.\n"
                      "--psp-sdma does the same with the SDMA0 firmware load (LOAD_IP_FW) between SETUP_TMR\n"
                      "and DESTROY_TMR; SDMA0 stays halted.\n"
                      "--sdma-inventory does --psp-sdma, then reads 31 SDMA registers before PowerUpSdma, after\n"
                      "it and after PowerDownSdma, before the teardown.\n"
                      "--sdma-copy does --psp-sdma, then starts SDMA0, runs the ring test and one 4 KiB copy with a\n"
                      "fence, verifies it, and halts, powers down and tears down.\n"
                      "--gart-ih does --sdma-copy up to its verify, then enables the GART and IH ring 0, copies\n"
                      "through the GART with a fence and a TRAP, verifies, restores every register, and stops.\n"
                      "--psp-state first decodes the PSP ring mailbox and the memory-hub apertures (reads only).\n"
                      "--inventory16 first reads the display, memory-hub VM and interrupt registers (reads only).\n");
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

// The stage 8 DisallowGfxOff: check the mailbox is idle, send the message and
// confirm GFX reports on.
bool gfxOffDisallow(io_connect_t connection)
{
    uint64_t check[4] = {};
    step("gfxoff 1/2 check: MP1 mailbox idle (C2PMSG_90 non-zero)");
    if (!call(connection, kDiagnosticSmuCheck, check, 4)) return false;
    std::printf("%s\n  C2PMSG_66 0x%08llx  C2PMSG_82 0x%08llx  C2PMSG_90 0x%08llx\n",
                statusName(static_cast<Status>(check[0])), static_cast<unsigned long long>(check[1]),
                static_cast<unsigned long long>(check[2]), static_cast<unsigned long long>(check[3]));
    if (check[0] != kOK) return false;
    uint64_t out[3] = {};
    step("gfxoff 2/2 send: DisallowGfxOff (0x8), then wait for PWR_GFXOFF_STATUS 2");
    if (!call(connection, kDiagnosticGfxOffDisallow, out, 3)) return false;
    std::printf("%s, response 0x%02llx, SMUIO_GFX_MISC_CNTL 0x%08llx (PWR_GFXOFF_STATUS %llu)\n",
                statusName(static_cast<Status>(out[0])), static_cast<unsigned long long>(out[1]),
                static_cast<unsigned long long>(out[2]),
                static_cast<unsigned long long>((out[2] & kGfxOffStatusMask) >> kGfxOffStatusShift));
    return out[0] == kOK;
}

// smu12_driver_if.h CLOCK_IDs_e order.
const char *const kClockNames[18] = {"SMNCLK", "SOCCLK", "MP0CLK", "MP1CLK", "MP2CLK", "VCLK",
                                     "LCLK",   "DCLK",   "ACLK",   "ISPCLK", "SHUBCLK", "DISPCLK",
                                     "DPPCLK", "DPREFCLK", "DCFCLK", "FCLK", "UMCCLK", "GFXCLK"};

double centi(uint16_t value)
{
    return value / 100.0;
}

void printMetrics(const SmuMetrics &m)
{
    const uint16_t *w = m.words;
    std::printf("  clocks (MHz):");
    for (uint32_t i = 0; i < 18; i++) std::printf("%s %s %u", i % 6 == 0 ? "\n   " : "", kClockNames[i], w[kMetricsClockFrequency + i]);
    std::printf("\n  average MHz: GFX %u  SOC %u  VCLK %u  FCLK %u\n", w[kMetricsAverageGfxclk],
                w[kMetricsAverageSocclk], w[kMetricsAverageVclk], w[kMetricsAverageFclk]);
    std::printf("  activity: GFX %.2f %%  UVD %.2f %%\n", centi(w[kMetricsAverageGfxActivity]),
                centi(w[kMetricsAverageUvdActivity]));
    std::printf("  VDD %u mV %u mA %u mW   SOC %u mV %u mA %u mW\n", w[kMetricsVoltage], w[kMetricsCurrent],
                w[kMetricsPower], w[kMetricsVoltage + 1], w[kMetricsCurrent + 1], w[kMetricsPower + 1]);
    std::printf("  socket power %u W  APU power %u W  fan PWM %u (milli)\n", w[kMetricsCurrentSocketPower],
                w[kMetricsApuPower], w[kMetricsFanPwm]);
    std::printf("  cores (MHz / mW / C):");
    for (uint32_t i = 0; i < 8; i++)
        std::printf("%s %u/%u/%.2f", i % 4 == 0 ? "\n   " : "", w[kMetricsCoreFrequency + i], w[kMetricsCorePower + i],
                    centi(w[kMetricsCoreTemperature + i]));
    std::printf("\n  L3: %u MHz %.2f C, %u MHz %.2f C\n", w[kMetricsL3Frequency], centi(w[kMetricsL3Temperature]),
                w[kMetricsL3Frequency + 1], centi(w[kMetricsL3Temperature + 1]));
    std::printf("  GFX %.2f C  SOC %.2f C  throttler 0x%04x\n", centi(w[kMetricsGfxTemperature]),
                centi(w[kMetricsSocTemperature]), w[kMetricsThrottlerStatus]);
    std::printf("  STAPM limit %u W (original %u W)  TDC VDD %u SOC %u mA  EDC VDD %u SOC %u mA\n",
                w[kMetricsStapmCurrentLimit], w[kMetricsStapmOriginalLimit], w[kMetricsVddTdc], w[kMetricsSocTdc],
                w[kMetricsVddEdc], w[kMetricsSocEdc]);
}

// The stage 9 metrics table: check the page, have the SMU write it, read it.
bool smuMetrics(io_connect_t connection)
{
    uint64_t check[5] = {};
    step("metrics 1/3 check: mailbox idle, FB registers, carveout bounds, 64 KiB unchanged over ~1 s");
    if (!call(connection, kDiagnosticMetricsCheck, check, 5)) return false;
    std::printf("%s\n  MC_VM_FB_LOCATION_BASE 0x%llx  MC_VM_FB_OFFSET 0x%llx  GPU 0x%llx  physical 0x%llx\n",
                statusName(static_cast<Status>(check[0])), static_cast<unsigned long long>(check[1]),
                static_cast<unsigned long long>(check[2]), static_cast<unsigned long long>(check[3]),
                static_cast<unsigned long long>(check[4]));
    if (check[0] != kOK) return false;

    uint64_t transfer[4] = {};
    step("metrics 2/3 transfer: SetDriverDramAddrHigh, SetDriverDramAddrLow, TransferTableSmu2Dram");
    if (!call(connection, kDiagnosticMetricsTransfer, transfer, 4)) return false;
    std::printf("%s, responses 0x%02llx 0x%02llx 0x%02llx\n", statusName(static_cast<Status>(transfer[0])),
                static_cast<unsigned long long>(transfer[1]), static_cast<unsigned long long>(transfer[2]),
                static_cast<unsigned long long>(transfer[3]));
    if (transfer[0] != kOK) return false;

    step("metrics 3/3 read: the page through a read-only mapping; only the 148 table bytes may differ from before");
    uint64_t status = 0;
    uint32_t statusCount = 1;
    SmuMetrics metrics = {};
    size_t size = sizeof(metrics);
    kern_return_t result = IOConnectCallMethod(connection, kDiagnosticMetricsRead, nullptr, 0, nullptr, 0, &status,
                                               &statusCount, &metrics, &size);
    if (result != KERN_SUCCESS || statusCount != 1 || size != sizeof(metrics)) {
        std::printf("call failed 0x%08x\n", result);
        return false;
    }
    std::printf("%s\n", statusName(static_cast<Status>(status)));
    if (status != kOK) return false;
    printMetrics(metrics);
    return true;
}

} // namespace

// Reads one register through the driver, printing its name first. Returns
// false (and prints the status) unless the read returned ok.
bool readNamed(io_connect_t connection, const char *name, uint32_t offset, uint32_t *value)
{
    std::printf("  %-32s ", name);
    std::fflush(stdout);
    uint64_t input = offset;
    uint64_t output[2] = {0, 0};
    uint32_t outputCount = 2;
    kern_return_t result =
        IOConnectCallScalarMethod(connection, kDiagnosticReadRegister, &input, 1, output, &outputCount);
    if (result != KERN_SUCCESS || outputCount != 2) {
        std::printf("call failed 0x%08x\n", result);
        return false;
    }
    if (output[0] != kOK) {
        std::printf("%s\n", statusName(static_cast<Status>(output[0])));
        return false;
    }
    *value = static_cast<uint32_t>(output[1]);
    std::printf("0x%08x\n", *value);
    return true;
}

// One hub's apertures as GPU address ranges (mmhub_v1_0/gfxhub_v1_0
// init_system_aperture_regs: FB and AGP in 16 MiB units, system aperture in
// 256 KiB units).
struct Hub {
    const char *name;
    uint32_t fbBase, fbTop, fbOffset, agpBase, agpBot, agpTop, low, high;
};

bool showHub(io_connect_t connection, const Hub &hub)
{
    std::printf("%s hub:\n", hub.name);
    uint32_t v[8] = {};
    const uint32_t offsets[8] = {hub.fbBase, hub.fbTop, hub.fbOffset, hub.agpBase,
                                 hub.agpBot, hub.agpTop, hub.low,    hub.high};
    const char *names[8] = {"MC_VM_FB_LOCATION_BASE", "MC_VM_FB_LOCATION_TOP", "MC_VM_FB_OFFSET",
                            "MC_VM_AGP_BASE",         "MC_VM_AGP_BOT",         "MC_VM_AGP_TOP",
                            "MC_VM_SYSTEM_APERTURE_LOW", "MC_VM_SYSTEM_APERTURE_HIGH"};
    for (uint32_t i = 0; i < 8; i++) {
        if (!readNamed(connection, names[i], offsets[i], &v[i])) return false;
    }
    std::printf("  FB          0x%010llx-0x%010llx -> physical 0x%010llx\n",
                static_cast<unsigned long long>(uint64_t(v[0] & 0xFFFFFF) << 24),
                static_cast<unsigned long long>((uint64_t(v[1] & 0xFFFFFF) << 24) | 0xFFFFFF),
                static_cast<unsigned long long>(uint64_t(v[2] & 0xFFFFFF) << 24));
    std::printf("  AGP         0x%010llx-0x%010llx base 0x%010llx\n",
                static_cast<unsigned long long>(uint64_t(v[4] & 0xFFFFFF) << 24),
                static_cast<unsigned long long>((uint64_t(v[5] & 0xFFFFFF) << 24) | 0xFFFFFF),
                static_cast<unsigned long long>(uint64_t(v[3] & 0xFFFFFF) << 24));
    std::printf("  system      0x%010llx-0x%010llx\n",
                static_cast<unsigned long long>(uint64_t(v[6] & 0x3FFFFFFF) << 18),
                static_cast<unsigned long long>((uint64_t(v[7] & 0x3FFFFFFF) << 18) | 0x3FFFF));
    return true;
}

// The stage 10 summary: the PSP ring mailbox and both hubs' apertures.
// Reads only.
bool pspState(io_connect_t connection)
{
    std::printf("PSP ring mailbox:\n");
    uint32_t c2p64 = 0, c2p67 = 0, c2p69 = 0, c2p70 = 0, c2p71 = 0;
    if (!readNamed(connection, "MP0_SMN_C2PMSG_64 (command)", kRegMp0C2PMsg64, &c2p64) ||
        !readNamed(connection, "MP0_SMN_C2PMSG_67 (write ptr)", kRegMp0C2PMsg67, &c2p67) ||
        !readNamed(connection, "MP0_SMN_C2PMSG_69 (addr low)", kRegMp0C2PMsg69, &c2p69) ||
        !readNamed(connection, "MP0_SMN_C2PMSG_70 (addr high)", kRegMp0C2PMsg70, &c2p70) ||
        !readNamed(connection, "MP0_SMN_C2PMSG_71 (size)", kRegMp0C2PMsg71, &c2p71))
        return false;
    std::printf("  response flag %s, status 0x%04x, command field 0x%02x -> %s\n",
                (c2p64 & kPspResponseFlag) ? "set" : "clear", c2p64 & kPspStatusMask, (c2p64 >> 16) & 0xFF,
                (c2p64 & (kPspResponseFlag | kPspStatusMask)) == kPspResponseFlag ? "ready" : "not ready");
    std::printf("  ring: %s (address 0x%08x%08x, size 0x%x, write pointer 0x%x)\n",
                (c2p69 | c2p70 | c2p71) == 0 ? "none set" : "address or size set", c2p70, c2p69, c2p71, c2p67);

    uint32_t lsb = 0, msb = 0;
    std::printf("MMHUB default page:\n");
    if (!readNamed(connection, "MC_VM_SYS_APR_DEFAULT_LSB", kRegMmhubDefaultAddrLsb, &lsb) ||
        !readNamed(connection, "MC_VM_SYS_APR_DEFAULT_MSB", kRegMmhubDefaultAddrMsb, &msb))
        return false;
    std::printf("  physical 0x%010llx\n",
                static_cast<unsigned long long>((uint64_t(lsb) << 12) | (uint64_t(msb & 0xF) << 44)));

    const Hub mmhub = {"MMHUB",          kRegMmhubFbLocationBase, kRegMmhubFbLocationTop, kRegMmhubFbOffset,
                       kRegMmhubAgpBase, kRegMmhubAgpBot,         kRegMmhubAgpTop,        kRegMmhubApertureLow,
                       kRegMmhubApertureHigh};
    const Hub gc = {"GC (GFX-gated)", kRegGcFbLocationBase, kRegGcFbLocationTop, kRegMcVmFbOffset, kRegGcAgpBase,
                    kRegGcAgpBot,     kRegGcAgpTop,         kRegGcApertureLow,  kRegGcApertureHigh};
    bool mmhubOk = showHub(connection, mmhub);
    bool gcOk = showHub(connection, gc);
    return mmhubOk && gcOk;
}

void printMailbox(const uint64_t *values)
{
    std::printf("  C2PMSG_64 0x%08llx  C2PMSG_67 0x%08llx  C2PMSG_69 0x%08llx  C2PMSG_70 0x%08llx  C2PMSG_71 0x%08llx\n",
                static_cast<unsigned long long>(values[0]), static_cast<unsigned long long>(values[1]),
                static_cast<unsigned long long>(values[2]), static_cast<unsigned long long>(values[3]),
                static_cast<unsigned long long>(values[4]));
}

// The stage 11 PSP ring: check, create, observe, destroy. The destroy runs
// after any create command the driver wrote, whatever its response, and even
// if observing fails; the driver also destroys the ring if this exits in
// between.
bool pspRing(io_connect_t connection)
{
    uint64_t check[7] = {};
    step("psp 1/4 check: secure OS running, C2PMSG_64 ready, no ring; 64 KiB at the ring page stable for ~1 s");
    if (!call(connection, kDiagnosticPspRingCheck, check, 7)) return false;
    std::printf("%s\n  C2PMSG_81 0x%08llx\n", statusName(static_cast<Status>(check[0])),
                static_cast<unsigned long long>(check[1]));
    printMailbox(check + 2);
    if (check[0] != kOK) return false;

    uint64_t created[3] = {};
    std::printf("psp 2/4 create: INIT_GPCOM_RING at GPU 0x%010llx (physical 0x%010llx), size 0x%x ... ",
                static_cast<unsigned long long>(kPspRingGpuAddress),
                static_cast<unsigned long long>(kPspRingPhysical), kPspRingSize);
    std::fflush(stdout);
    if (!call(connection, kDiagnosticPspRingCreate, created, 3)) return false;
    std::printf("%s, response 0x%08llx%s\n", statusName(static_cast<Status>(created[0])),
                static_cast<unsigned long long>(created[1]), created[2] ? "" : " (command not written)");
    if (!created[2]) return false;

    uint64_t observed[8] = {};
    bool observedCall = false;
    if (created[0] == kOK) {
        step("psp 3/4 observe: mailbox; 64 KiB at the ring compared with the snapshot");
        observedCall = call(connection, kDiagnosticPspRingObserve, observed, 8);
    } else {
        std::printf("psp 3/4 observe: skipped, the create was not ok\n");
    }
    if (observedCall) {
        std::printf("%s\n", statusName(static_cast<Status>(observed[0])));
        printMailbox(observed + 1);
        std::printf("  changed words: %llu in the ring page, %llu in the 60 KiB after it\n",
                    static_cast<unsigned long long>(observed[6]), static_cast<unsigned long long>(observed[7]));
    }

    uint64_t destroyed[7] = {};
    step("psp 4/4 destroy: DESTROY_RINGS");
    if (!call(connection, kDiagnosticPspRingDestroy, destroyed, 7)) return false;
    std::printf("%s, response 0x%08llx\n", statusName(static_cast<Status>(destroyed[0])),
                static_cast<unsigned long long>(destroyed[1]));
    printMailbox(destroyed + 2);
    return created[0] == kOK && observedCall && observed[0] == kOK && destroyed[0] == kOK;
}

// Destroys a ring created without a TMR submit (the stage 11 selector).
bool destroyRingOnly(io_connect_t connection)
{
    uint64_t destroyed[7] = {};
    step("psp destroy: DESTROY_RINGS");
    if (!call(connection, kDiagnosticPspRingDestroy, destroyed, 7)) return false;
    std::printf("%s, response 0x%08llx\n", statusName(static_cast<Status>(destroyed[0])),
                static_cast<unsigned long long>(destroyed[1]));
    return destroyed[0] == kOK;
}

// The stage 12 TMR: check, create, SETUP_TMR, observe, then DESTROY_TMR and
// the ring destroy. Every created ring gets a destroy; the driver also tears
// down if this exits in between.
bool pspTmr(io_connect_t connection)
{
    uint64_t check[7] = {};
    step("tmr 1/5 check: PSP ready, no ring; 64 KiB at the ring stable; 4 MiB TMR region placed and stable");
    if (!call(connection, kDiagnosticPspRingCheck, check, 7)) return false;
    std::printf("%s\n  C2PMSG_81 0x%08llx\n", statusName(static_cast<Status>(check[0])),
                static_cast<unsigned long long>(check[1]));
    printMailbox(check + 2);
    if (check[0] != kOK) return false;

    uint64_t created[3] = {};
    std::printf("tmr 2/5 create: INIT_GPCOM_RING at GPU 0x%010llx ... ",
                static_cast<unsigned long long>(kPspRingGpuAddress));
    std::fflush(stdout);
    if (!call(connection, kDiagnosticPspRingCreate, created, 3)) return false;
    std::printf("%s, response 0x%08llx\n", statusName(static_cast<Status>(created[0])),
                static_cast<unsigned long long>(created[1]));
    if (!created[2]) return false;
    if (created[0] != kOK) {
        destroyRingOnly(connection);
        return false;
    }

    uint64_t submitted[7] = {};
    std::printf("tmr 3/5 submit: SETUP_TMR (TMR GPU 0x%010llx, physical 0x%010llx, 0x%x bytes) as frame 0,\n"
                "  command 0x%010llx, fence 0x%010llx; C2PMSG_67 <- %u; wait for fence 1 ... ",
                static_cast<unsigned long long>(kPspTmrGpuAddress), static_cast<unsigned long long>(kPspTmrPhysical),
                kPspTmrSize, static_cast<unsigned long long>(kPspCmdGpuAddress),
                static_cast<unsigned long long>(kPspFenceGpuAddress), kPspFrameDwords);
    std::fflush(stdout);
    if (!call(connection, kDiagnosticPspTmrSubmit, submitted, 7)) return false;
    std::printf("%s\n  fence %llu, response status 0x%08llx, fw_addr 0x%08llx%08llx, tmr_size 0x%llx, C2PMSG_67 %llu\n",
                statusName(static_cast<Status>(submitted[0])), static_cast<unsigned long long>(submitted[1]),
                static_cast<unsigned long long>(submitted[2]), static_cast<unsigned long long>(submitted[4]),
                static_cast<unsigned long long>(submitted[3]), static_cast<unsigned long long>(submitted[5]),
                static_cast<unsigned long long>(submitted[6]));
    if (submitted[6] != kPspFrameDwords) {
        // The frame never reached the PSP: only the ring needs destroying.
        destroyRingOnly(connection);
        return false;
    }

    uint64_t observed[3] = {};
    step("tmr 4/5 observe: 64 KiB at the ring against the snapshot and the driver's words");
    bool observedCall = call(connection, kDiagnosticPspTmrObserve, observed, 3);
    if (observedCall) {
        std::printf("%s, %llu unexpected words", statusName(static_cast<Status>(observed[0])),
                    static_cast<unsigned long long>(observed[1]));
        if (observed[1] != 0)
            std::printf(", first at ring + 0x%llx", static_cast<unsigned long long>(observed[2]));
        std::printf("\n");
    }

    uint64_t teardown[6] = {};
    step(submitted[1] == 1 ? "tmr 5/5 teardown: DESTROY_TMR as frame 1 (C2PMSG_67 <- 32, fence 2), then DESTROY_RINGS"
                           : "tmr 5/5 teardown: no fence, so DESTROY_RINGS only");
    if (!call(connection, kDiagnosticPspTmrTeardown, teardown, 6)) return false;
    std::printf("%s\n  DESTROY_TMR fence %llu, status 0x%08llx; ring response 0x%08llx; C2PMSG_64 0x%08llx, "
                "C2PMSG_67 %llu\n",
                statusName(static_cast<Status>(teardown[0])), static_cast<unsigned long long>(teardown[1]),
                static_cast<unsigned long long>(teardown[2]), static_cast<unsigned long long>(teardown[3]),
                static_cast<unsigned long long>(teardown[4]), static_cast<unsigned long long>(teardown[5]));
    return submitted[0] == kOK && observedCall && observed[0] == kOK && teardown[0] == kOK;
}

// Runs the stage 12 teardown selector and prints it.
bool tmrTeardown(io_connect_t connection, const char *text)
{
    uint64_t teardown[6] = {};
    step(text);
    if (!call(connection, kDiagnosticPspTmrTeardown, teardown, 6)) return false;
    std::printf("%s\n  DESTROY_TMR fence %llu, status 0x%08llx; ring response 0x%08llx; C2PMSG_64 0x%08llx, "
                "C2PMSG_67 %llu\n",
                statusName(static_cast<Status>(teardown[0])), static_cast<unsigned long long>(teardown[1]),
                static_cast<unsigned long long>(teardown[2]), static_cast<unsigned long long>(teardown[3]),
                static_cast<unsigned long long>(teardown[4]), static_cast<unsigned long long>(teardown[5]));
    return teardown[0] == kOK;
}

// The stage 13 SDMA0 load: check, create, SETUP_TMR, firmware copy and
// LOAD_IP_FW, observe, teardown. Every created ring gets a destroy; the
// driver also tears down if this exits in between.
// The stage 14 inventory: three readings of the 31 SDMA registers around
// PowerUpSdma and PowerDownSdma.
bool sdmaInventory(io_connect_t connection)
{
    step("inventory: read 31 SDMA registers, PowerUpSdma (0xE), read, PowerDownSdma (0xD), read");
    uint64_t scalars[3] = {};
    uint32_t scalarCount = 3;
    SdmaInventory inventory = {};
    size_t size = sizeof(inventory);
    kern_return_t result = IOConnectCallMethod(connection, kDiagnosticSdmaInventory, nullptr, 0, nullptr, 0, scalars,
                                               &scalarCount, &inventory, &size);
    if (result != KERN_SUCCESS || scalarCount != 3 || size != sizeof(inventory)) {
        std::printf("call failed 0x%08x\n", result);
        return false;
    }
    std::printf("%s, PowerUpSdma response 0x%02llx, PowerDownSdma response 0x%02llx\n",
                statusName(static_cast<Status>(scalars[0])), static_cast<unsigned long long>(scalars[1]),
                static_cast<unsigned long long>(scalars[2]));
    std::printf("  %-32s %-8s %-10s %-10s %-10s\n", "register", "offset", "loaded", "powered", "gated");
    for (uint32_t i = 0; i < kSdmaInventoryCount; i++) {
        std::printf("  %-32s 0x%05x 0x%08x 0x%08x 0x%08x%s\n", registerName(kSdmaInventory[i]), kSdmaInventory[i],
                    inventory.loaded[i], inventory.powered[i], inventory.gated[i],
                    inventory.loaded[i] != inventory.powered[i] || inventory.powered[i] != inventory.gated[i]
                        ? "  *"
                        : "");
    }
    return scalars[0] == kOK;
}

// Runs the stage 15 stop selector (halt, PowerDownSdma, DESTROY_TMR, ring
// destroy) and prints it.
bool copyStop(io_connect_t connection)
{
    uint64_t stop[5] = {};
    step("copy 6/6 stop: halt SDMA0, PowerDownSdma, DESTROY_TMR, DESTROY_RINGS");
    if (!call(connection, kDiagnosticSdmaStop, stop, 5)) return false;
    std::printf("%s\n  SDMA0_F32_CNTL 0x%08llx, PowerDownSdma 0x%02llx, DESTROY_TMR fence %llu, ring response 0x%08llx\n",
                statusName(static_cast<Status>(stop[0])), static_cast<unsigned long long>(stop[1]),
                static_cast<unsigned long long>(stop[2]), static_cast<unsigned long long>(stop[3]),
                static_cast<unsigned long long>(stop[4]));
    return stop[0] == kOK && (stop[1] & kSdmaF32Halt) != 0;
}

// Prints the precondition list's entry at index (kGartEnable, kIhEnable,
// SDMA0_CNTL, display pipe 0) with its boot 22 value.
void printGartCheckEntry(uint64_t index, uint64_t value)
{
    uint32_t offset = 0, expected = 0;
    if (index < kGartEnableCount) {
        offset = kGartEnable[index].offset;
        expected = kGartEnable[index].boot22;
    } else if (index < kGartEnableCount + kIhEnableCount) {
        offset = kIhEnable[index - kGartEnableCount].offset;
        expected = kIhEnable[index - kGartEnableCount].boot22;
    } else if (index == kGartEnableCount + kIhEnableCount) {
        offset = kRegSdma0Cntl;
        expected = kSdmaCntlBoot;
    } else if (index < kGartCheckCount) {
        offset = kDisplayInventory[index - kGartEnableCount - kIhEnableCount - 1];
        expected = kDisplayPipe0Boot22[index - kGartEnableCount - kIhEnableCount - 1];
    }
    std::printf(" (%s reads 0x%08llx, boot 22 0x%08x)", registerName(offset), static_cast<unsigned long long>(value),
                expected);
}

// The stage 17 steps after a verified copy: check, enable, frame 2, verify,
// restore. The restore runs whenever the enable was sent; the stage 15 stop
// that follows would also run it.
bool gartIh(io_connect_t connection)
{
    uint64_t check[3] = {};
    step("gart 1/5 check: GART and IH registers and display pipe 0 at boot 22; area 0xf440800000 placed and stable");
    if (!call(connection, kDiagnosticGartCheck, check, 3)) return false;
    std::printf("%s", statusName(static_cast<Status>(check[0])));
    if (check[0] == kGartUnexpectedState) printGartCheckEntry(check[1], check[2]);
    std::printf("\n");
    if (check[0] != kOK) return false;

    uint64_t enable[3] = {};
    step("gart 2/5 enable: page table and frame 2, 21 GART writes, engine 17 flush, IH ring 0, SDMA0_CNTL <- 0x3");
    bool ok = call(connection, kDiagnosticGartEnable, enable, 3);
    if (ok) {
        std::printf("%s, progress %llu, VM_INVALIDATE_ENG17_ACK 0x%08llx\n", statusName(static_cast<Status>(enable[0])),
                    static_cast<unsigned long long>(enable[1]), static_cast<unsigned long long>(enable[2]));
        ok = enable[0] == kOK;
    }
    if (ok) {
        uint64_t frame = 2, out[6] = {};
        step("gart 3/5 frame 2: COPY_LINEAR 4 KiB from GART 0x0, FENCE 2, TRAP; GFX_RB_WPTR <- 3072, _HI <- 0");
        ok = call(connection, kDiagnosticSdmaSubmit, out, 6, &frame, 1);
        if (ok) {
            std::printf("%s, fence %llu, GFX_RB_RPTR %llu, GFX_RB_WPTR %llu\n"
                        "  SDMA0_F32_CNTL 0x%08llx, SDMA0_STATUS_REG 0x%08llx\n",
                        statusName(static_cast<Status>(out[0])), static_cast<unsigned long long>(out[1]),
                        static_cast<unsigned long long>(out[2]), static_cast<unsigned long long>(out[3]),
                        static_cast<unsigned long long>(out[4]), static_cast<unsigned long long>(out[5]));
            ok = out[0] == kOK;
        }
    }
    if (ok) {
        step("gart 4/5 verify: second destination, fence 2, no VM fault, SDMA0 trap in the IH ring, both regions, display");
        uint64_t scalar = 0;
        uint32_t scalarCount = 1;
        GartReport report = {};
        size_t size = sizeof(report);
        kern_return_t result = IOConnectCallMethod(connection, kDiagnosticGartVerify, nullptr, 0, nullptr, 0, &scalar,
                                                   &scalarCount, &report, &size);
        ok = result == KERN_SUCCESS && scalarCount == 1 && size == sizeof(report);
        if (!ok) {
            std::printf("call failed 0x%08x\n", result);
        } else {
            std::printf("%s\n  GFX_RB_RPTR %u, fence 2 %u, VM_L2_PROTECTION_FAULT_STATUS 0x%08x\n"
                        "  IH write-back 0x%08x, IH_RB_WPTR 0x%08x, IH_RB_RPTR 0x%08x: %u entries, %u SDMA0 traps, %u other\n"
                        "  SDMA region %u unexpected (first +0x%x), GART region %u unexpected (first +0x%x)\n"
                        "  display: %u changed",
                        statusName(static_cast<Status>(scalar)), report.rptr, report.fence2, report.faultStatus,
                        report.ihWriteback, report.ihWptr, report.ihRptr, report.ihEntries, report.sdmaTraps,
                        report.otherEntries, report.sdmaUnexpected, report.sdmaFirst, report.gartUnexpected,
                        report.gartFirst, report.displayChanged);
            if (report.displayChanged != 0) std::printf(" (first %s)", registerName(kDisplayInventory[report.displayFirst]));
            std::printf("\n");
            for (uint32_t entry = 0; entry < report.ihEntries && entry < kIhReportEntries; entry++) {
                const uint32_t *dw = report.ring + entry * kIhEntryBytes / 4;
                std::printf("  IH %3u: client %3u source %3u ring %u vmid %u | %08x %08x %08x %08x %08x %08x %08x %08x\n",
                            entry, dw[0] & 0xFF, (dw[0] >> 8) & 0xFF, (dw[0] >> 16) & 0xFF, (dw[0] >> 24) & 0xF, dw[0],
                            dw[1], dw[2], dw[3], dw[4], dw[5], dw[6], dw[7]);
            }
            ok = scalar == kOK;
        }
    }
    uint64_t restore[4] = {};
    step("gart 5/5 restore: IH ring off, IH and SDMA0_CNTL to boot 22, VM_CONTEXT0_CNTL then GART to boot 22, flush");
    bool restored = call(connection, kDiagnosticGartRestore, restore, 4);
    if (restored) {
        std::printf("%s", statusName(static_cast<Status>(restore[0])));
        if (restore[0] == kGartNotRestored) printGartCheckEntry(restore[1], restore[2]);
        std::printf("\n  IH_RB_WPTR 0x%08llx (boot 22 0x%08x)\n", static_cast<unsigned long long>(restore[3]),
                    kIhEnable[5].boot22);
        restored = restore[0] == kOK;
    }
    return ok && restored;
}

// The stage 15 copy, after the stage 13 load: check, start, ring test,
// copy, verify, stop; with gart, the stage 17 steps between verify and stop.
// Returns whether every step passed; the caller's teardown is skipped once
// the check passed (stop does it).
bool sdmaCopy(io_connect_t connection, bool *stopped, bool gart)
{
    *stopped = false;
    uint64_t check[3] = {};
    step("copy 1/6 check: SDMA0 holds its boot 19 values; work area 0xf440300000 placed and stable");
    if (!call(connection, kDiagnosticSdmaCopyCheck, check, 3)) return false;
    std::printf("%s", statusName(static_cast<Status>(check[0])));
    if (check[0] == kSdmaUnexpectedState)
        std::printf(" (%s reads 0x%08llx, boot 19 0x%08x)", registerName(kSdmaInventory[check[1]]),
                    static_cast<unsigned long long>(check[2]), kSdmaBoot19[check[1]]);
    std::printf("\n");
    if (check[0] != kOK) return false;

    *stopped = true; // from here the stop selector undoes everything
    uint64_t start[3] = {};
    step("copy 2/6 start: write the work area, PowerUpSdma, 10 golden settings, 24 start writes");
    bool ok = call(connection, kDiagnosticSdmaStart, start, 3);
    if (ok) {
        std::printf("%s, progress %llu, PowerUpSdma 0x%02llx\n", statusName(static_cast<Status>(start[0])),
                    static_cast<unsigned long long>(start[1]), static_cast<unsigned long long>(start[2]));
        ok = start[0] == kOK;
    }
    const char *frames[] = {"copy 3/6 ring test: WRITE_LINEAR 0xDEADBEEF, GFX_RB_WPTR <- 1024, _HI <- 0",
                            "copy 4/6 copy: COPY_LINEAR 4 KiB and FENCE 1, GFX_RB_WPTR <- 2048, _HI <- 0"};
    for (uint64_t frame = 0; ok && frame < 2; frame++) {
        uint64_t out[6] = {};
        step(frames[frame]);
        ok = call(connection, kDiagnosticSdmaSubmit, out, 6, &frame, 1);
        if (ok) {
            std::printf("%s, observed 0x%08llx, GFX_RB_RPTR %llu, GFX_RB_WPTR %llu\n"
                        "  SDMA0_F32_CNTL 0x%08llx, SDMA0_STATUS_REG 0x%08llx\n",
                        statusName(static_cast<Status>(out[0])), static_cast<unsigned long long>(out[1]),
                        static_cast<unsigned long long>(out[2]), static_cast<unsigned long long>(out[3]),
                        static_cast<unsigned long long>(out[4]), static_cast<unsigned long long>(out[5]));
            ok = out[0] == kOK;
        }
    }
    if (ok) {
        uint64_t verify[5] = {};
        step("copy 5/6 verify: destination equals source, fence 1, 64 KiB region");
        ok = call(connection, kDiagnosticSdmaVerify, verify, 5);
        if (ok) {
            std::printf("%s\n  GFX_RB_RPTR %llu, %llu unexpected words (first +0x%llx), SDMA0_STATUS_REG 0x%08llx\n",
                        statusName(static_cast<Status>(verify[0])), static_cast<unsigned long long>(verify[1]),
                        static_cast<unsigned long long>(verify[2]), static_cast<unsigned long long>(verify[3]),
                        static_cast<unsigned long long>(verify[4]));
            ok = verify[0] == kOK;
        }
    }
    if (ok && gart) ok = gartIh(connection);
    bool halted = copyStop(connection);
    return ok && halted;
}

// mode: 0 load only, 1 with the stage 14 inventory, 2 with the stage 15 copy,
// 3 with the copy and the stage 17 GART and interrupt ring.
bool pspSdma(io_connect_t connection, int mode)
{
    uint64_t check[7] = {};
    step("sdma 1/6 check: PSP, ring, TMR and firmware-buffer regions; embedded SDMA0 image header");
    if (!call(connection, kDiagnosticPspRingCheck, check, 7)) return false;
    std::printf("%s\n", statusName(static_cast<Status>(check[0])));
    printMailbox(check + 2);
    if (check[0] != kOK) return false;
    uint64_t input = kRegSdma0UcodeChecksum, before[2] = {}, halt[2] = {};
    uint32_t count = 2;
    IOConnectCallScalarMethod(connection, kDiagnosticReadRegister, &input, 1, before, &count);
    input = kRegSdma0F32Cntl;
    count = 2;
    IOConnectCallScalarMethod(connection, kDiagnosticReadRegister, &input, 1, halt, &count);
    std::printf("  before: SDMA0_UCODE_CHECKSUM 0x%08llx (%s), SDMA0_F32_CNTL 0x%08llx (%s)\n",
                static_cast<unsigned long long>(before[1]), statusName(static_cast<Status>(before[0])),
                static_cast<unsigned long long>(halt[1]), statusName(static_cast<Status>(halt[0])));

    uint64_t created[3] = {};
    step("sdma 2/6 create: INIT_GPCOM_RING");
    if (!call(connection, kDiagnosticPspRingCreate, created, 3)) return false;
    std::printf("%s, response 0x%08llx\n", statusName(static_cast<Status>(created[0])),
                static_cast<unsigned long long>(created[1]));
    if (!created[2]) return false;
    if (created[0] != kOK) {
        destroyRingOnly(connection);
        return false;
    }

    uint64_t submitted[7] = {};
    step("sdma 3/6 SETUP_TMR as frame 0, wait for fence 1");
    if (!call(connection, kDiagnosticPspTmrSubmit, submitted, 7)) return false;
    std::printf("%s, fence %llu, status 0x%08llx, C2PMSG_67 %llu\n", statusName(static_cast<Status>(submitted[0])),
                static_cast<unsigned long long>(submitted[1]), static_cast<unsigned long long>(submitted[2]),
                static_cast<unsigned long long>(submitted[6]));
    if (submitted[6] != kPspFrameDwords) {
        destroyRingOnly(connection);
        return false;
    }
    if (submitted[0] != kOK) {
        tmrTeardown(connection, "sdma teardown: DESTROY_TMR if fenced, then DESTROY_RINGS");
        return false;
    }

    uint64_t loaded[6] = {};
    std::printf("sdma 4/6 load: copy the SDMA0 image (%u bytes) to GPU 0x%010llx, then LOAD_IP_FW (type %u) as\n"
                "  frame 1, wait for fence 2 ... ",
                kSdmaUcodeSize, static_cast<unsigned long long>(kSdmaFwGpuAddress), kGfxFwTypeSdma0);
    std::fflush(stdout);
    bool loadedCall = call(connection, kDiagnosticSdmaLoad, loaded, 6);
    if (loadedCall) {
        uint64_t fwAddress = (loaded[4] << 32) | loaded[3];
        bool inTmr = fwAddress >= kPspTmrGpuAddress && fwAddress < kPspTmrGpuAddress + kPspTmrSize;
        std::printf("%s\n  fence %llu, status 0x%08llx, fw_addr 0x%010llx (%s), C2PMSG_67 %llu\n",
                    statusName(static_cast<Status>(loaded[0])), static_cast<unsigned long long>(loaded[1]),
                    static_cast<unsigned long long>(loaded[2]), static_cast<unsigned long long>(fwAddress),
                    inTmr ? "inside the TMR" : "not a TMR address", static_cast<unsigned long long>(loaded[5]));
    }

    uint64_t observed[7] = {};
    bool observedCall = false;
    if (loadedCall && loaded[5] == 2 * kPspFrameDwords) {
        step("sdma 5/6 observe: both regions, SDMA0_UCODE_CHECKSUM, SDMA0_F32_CNTL still halted");
        observedCall = call(connection, kDiagnosticSdmaObserve, observed, 7);
        if (observedCall) {
            std::printf("%s\n  work area: %llu unexpected (first +0x%llx); firmware region: %llu unexpected "
                        "(first +0x%llx)\n  after: SDMA0_UCODE_CHECKSUM 0x%08llx, SDMA0_F32_CNTL 0x%08llx\n",
                        statusName(static_cast<Status>(observed[0])), static_cast<unsigned long long>(observed[1]),
                        static_cast<unsigned long long>(observed[2]), static_cast<unsigned long long>(observed[3]),
                        static_cast<unsigned long long>(observed[4]), static_cast<unsigned long long>(observed[5]),
                        static_cast<unsigned long long>(observed[6]));
        }
    } else {
        std::printf("sdma 5/6 observe: skipped, LOAD_IP_FW was not submitted\n");
    }

    bool inventoried = true;
    if (mode >= 2) {
        if (!(loadedCall && loaded[1] == 2)) {
            std::printf("copy: skipped, LOAD_IP_FW did not fence\n");
        } else {
            bool stopped = false;
            bool copied = sdmaCopy(connection, &stopped, mode == 3);
            if (stopped) return copied && loaded[0] == kOK;
        }
        inventoried = false;
    }
    if (mode == 1) {
        // Only with the firmware loaded: LOAD_IP_FW fenced.
        if (loadedCall && loaded[1] == 2) {
            inventoried = sdmaInventory(connection);
        } else {
            std::printf("inventory: skipped, LOAD_IP_FW did not fence\n");
            inventoried = false;
        }
    }
    bool torn = tmrTeardown(connection, "sdma 6/6 teardown: DESTROY_TMR as the next frame if the last fenced, "
                                        "then DESTROY_RINGS");
    return loadedCall && loaded[0] == kOK && observedCall && observed[0] == kOK && inventoried && torn;
}

// Reads a list through the driver into values, printing each name first.
// Returns the number of failed reads.
int readList(io_connect_t connection, const uint32_t *offsets, uint32_t count, uint32_t *values, bool print)
{
    int failed = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (print) {
            std::printf("  %-48s 0x%05x  ", registerName(offsets[i]), offsets[i]);
            std::fflush(stdout);
        }
        uint64_t input = offsets[i], output[2] = {0, 0};
        uint32_t outputCount = 2;
        kern_return_t result =
            IOConnectCallScalarMethod(connection, kDiagnosticReadRegister, &input, 1, output, &outputCount);
        values[i] = static_cast<uint32_t>(output[1]);
        bool ok = result == KERN_SUCCESS && outputCount == 2 && output[0] == kOK;
        if (!ok) failed++;
        if (print) {
            if (ok)
                std::printf("0x%08x\n", values[i]);
            else
                std::printf("%s\n", result == KERN_SUCCESS ? statusName(static_cast<Status>(output[0])) : "call failed");
        }
    }
    return failed;
}

// The stage 16 inventory: display, VM, interrupts, then the display again.
bool inventory16(io_connect_t connection)
{
    uint32_t display[kDisplayInventoryCount], vm[kVmInventoryCount], ih[kIhInventoryCount],
        again[kDisplayInventoryCount];
    std::printf("inventory16 A: display (DCN 2.1)\n");
    int failed = readList(connection, kDisplayInventory, kDisplayInventoryCount, display, true);
    std::printf("inventory16 B: memory hub VM (GART path)\n");
    failed += readList(connection, kVmInventory, kVmInventoryCount, vm, true);
    std::printf("inventory16 C: interrupts (IH, NBIO)\n");
    failed += readList(connection, kIhInventory, kIhInventoryCount, ih, true);
    std::printf("inventory16 A again: display\n");
    failed += readList(connection, kDisplayInventory, kDisplayInventoryCount, again, false);
    for (uint32_t i = 0; i < kDisplayInventoryCount; i++) {
        if (display[i] != again[i])
            std::printf("  changed: %s 0x%08x -> 0x%08x\n", registerName(kDisplayInventory[i]), display[i], again[i]);
    }
    // Decode each pipe: OTG enable and timing, HUBP surface.
    for (uint32_t pipe = 0; pipe < kDisplayPipes; pipe++) {
        const uint32_t *r = display + pipe * kDisplayPipeRegisters;
        uint32_t control = r[0], hTotal = r[1] & 0x7FFF, vTotal = r[2] & 0x7FFF;
        uint32_t hStart = r[3] & 0x7FFF, hEnd = (r[3] >> 16) & 0x7FFF, vStart = r[4] & 0x7FFF,
                 vEnd = (r[4] >> 16) & 0x7FFF;
        uint32_t blank = r[5] & 1, format = r[6] & 0x7F, width = r[7] & 0x3FFF, height = (r[7] >> 16) & 0x3FFF,
                 pitch = r[8] & 0x3FFF;
        uint64_t surface = (uint64_t(r[10]) << 32) | r[9];
        std::printf("pipe %u: OTG %s, total %ux%u, active %ux%u; HUBP %s, surface 0x%010llx%s, viewport %ux%u,"
                    " pitch %u, format %u\n",
                    pipe, (control & kOtgMasterEn) ? "enabled" : "off", hTotal + 1, vTotal + 1, hStart - hEnd,
                    vStart - vEnd, blank ? "blanked" : "unblanked", static_cast<unsigned long long>(surface),
                    surface == (uint64_t(kExpectedFbLocationBase) << 24) ? " (GOP framebuffer, carveout offset 0)" : "",
                    width, height, pitch + 1, format);
    }
    std::printf("%d failed reads\n", failed);
    return failed == 0;
}

int main(int argc, char **argv)
{
    unsigned long repeat = 1, interval = 1000;
    bool scratch = false, smu = false, gfxoff = false, metrics = false, ring = false, tmr = false, sdma = false,
         inventory = false, copy = false, psp = false, inventory16Flag = false, gartFlag = false;
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--scratch-test") == 0) {
            scratch = true;
            continue;
        }
        if (std::strcmp(argv[i], "--smu-query") == 0) {
            smu = true;
            continue;
        }
        if (std::strcmp(argv[i], "--gfxoff-disallow") == 0) {
            gfxoff = true;
            continue;
        }
        if (std::strcmp(argv[i], "--smu-metrics") == 0) {
            metrics = true;
            continue;
        }
        if (std::strcmp(argv[i], "--psp-ring") == 0) {
            ring = true;
            continue;
        }
        if (std::strcmp(argv[i], "--psp-tmr") == 0) {
            tmr = true;
            continue;
        }
        if (std::strcmp(argv[i], "--psp-sdma") == 0) {
            sdma = true;
            continue;
        }
        if (std::strcmp(argv[i], "--sdma-inventory") == 0) {
            inventory = true;
            continue;
        }
        if (std::strcmp(argv[i], "--sdma-copy") == 0) {
            copy = true;
            continue;
        }
        if (std::strcmp(argv[i], "--gart-ih") == 0) {
            gartFlag = true;
            continue;
        }
        if (std::strcmp(argv[i], "--inventory16") == 0) {
            inventory16Flag = true;
            continue;
        }
        if (std::strcmp(argv[i], "--psp-state") == 0) {
            psp = true;
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

    const uint32_t count = info[1] >= 17  ? kStage16RegisterCount + kStage17RegisterCount
                           : info[1] >= 16 ? kStage16RegisterCount
                           : info[1] >= 14 ? kStage14RegisterCount
                           : info[1] >= 13 ? kStage13RegisterCount
                           : info[1] >= 10 ? kStage10RegisterCount
                           : info[1] >= 6 ? kStage6RegisterCount
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
    if (gfxoff) {
        if (info[1] < kGfxOffStage) {
            std::fprintf(stderr, "cezanne-diag: --gfxoff-disallow needs driver stage %u\n", kGfxOffStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!gfxOffDisallow(connection)) failures++;
    }
    if (metrics) {
        if (info[1] < kMetricsStage) {
            std::fprintf(stderr, "cezanne-diag: --smu-metrics needs driver stage %u\n", kMetricsStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!smuMetrics(connection)) failures++;
    }
    if (ring) {
        if (info[1] < kPspRingStage) {
            std::fprintf(stderr, "cezanne-diag: --psp-ring needs driver stage %u\n", kPspRingStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspRing(connection)) failures++;
    }
    if (tmr) {
        if (info[1] < kPspTmrStage) {
            std::fprintf(stderr, "cezanne-diag: --psp-tmr needs driver stage %u\n", kPspTmrStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspTmr(connection)) failures++;
    }
    if (sdma) {
        if (info[1] < kPspSdmaStage) {
            std::fprintf(stderr, "cezanne-diag: --psp-sdma needs driver stage %u\n", kPspSdmaStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspSdma(connection, 0)) failures++;
    }
    if (inventory) {
        if (info[1] < kSdmaInventoryStage) {
            std::fprintf(stderr, "cezanne-diag: --sdma-inventory needs driver stage %u\n", kSdmaInventoryStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspSdma(connection, 1)) failures++;
    }
    if (copy) {
        if (info[1] < kSdmaCopyStage) {
            std::fprintf(stderr, "cezanne-diag: --sdma-copy needs driver stage %u\n", kSdmaCopyStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspSdma(connection, 2)) failures++;
    }
    if (gartFlag) {
        if (info[1] < kGartStage) {
            std::fprintf(stderr, "cezanne-diag: --gart-ih needs driver stage %u\n", kGartStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspSdma(connection, 3)) failures++;
    }
    if (inventory16Flag) {
        if (info[1] < kInventory16Stage) {
            std::fprintf(stderr, "cezanne-diag: --inventory16 needs driver stage %u\n", kInventory16Stage);
            IOServiceClose(connection);
            return 1;
        }
        if (!inventory16(connection)) failures++;
    }
    if (psp) {
        if (info[1] < kPspStateStage) {
            std::fprintf(stderr, "cezanne-diag: --psp-state needs driver stage %u\n", kPspStateStage);
            IOServiceClose(connection);
            return 1;
        }
        if (!pspState(connection)) failures++;
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
