// Cezanne hardware core: identity, PCI state, boot-state registers and the
// IP discovery table.
//
// Freestanding C++ shared by the kext and the host unit tests: it uses no
// IOKit, libc or allocation. The adapter supplies read callbacks, a register
// write callback (stage 6 on) and a work-area memory write callback (stage
// 12). The register write allowlist names exact registers and values:
// SCRATCH_REG0 (stage 6) and the three SMU mailbox writes of a version query
// (stage 7), plus the DisableGfxOff message (stage 8), plus the three
// metrics-table messages and their arguments (stage 9), plus the PSP ring
// create and destroy (stage 11) and its write pointer (stage 12). The memory
// write allowlist names the three work-area pages and their exact words
// (stage 12). Widening either is a reviewed stage change (docs/test-boot.md).
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
const uint32_t kMaxStage = 16;

const uint8_t kRegisterBar = 0x24; // BAR5 configuration offset

// MP0_SMN_C2PMSG_33: bit 31 is set once IFWI initialisation has completed.
const uint32_t kRegC2PMsg33 = 0x16061 * 4;
const uint32_t kC2PMsg33IfwiReady = 0x80000000u;
// RCC_CONFIG_MEMSIZE: the firmware-reserved "VRAM" size in MiB.
const uint32_t kRegConfigMemsize = 0xde3 * 4;

// The only registers stage 1 may read; the adapter refuses any other offset.
const uint32_t kStage1Registers[] = {kRegC2PMsg33, kRegConfigMemsize};
const uint32_t kStage1RegisterCount = sizeof(kStage1Registers) / sizeof(kStage1Registers[0]);

// Stage 2 adds GC MC_VM_FB_OFFSET: Renoir GC_BASE segment 0 (0x2000,
// renoir_ip_offset.h) plus dword 0x96b (gc_9_0_offset.h). Linux v6.12 reads it
// in gfxhub_v1_0_get_mc_fb_offset and never writes it: for APUs the firmware
// sets it to the system physical address of "VRAM" (the carveout) >> 24, and
// gmc_v9_0_mc_init accesses VRAM directly there on x86-64.
const uint32_t kRegMcVmFbOffset = (0x2000 + 0x96b) * 4;
const uint32_t kFbOffsetMask = 0x00FFFFFFu; // MC_VM_FB_OFFSET__FB_OFFSET_MASK
const uint32_t kFbOffsetShift = 24;
const uint32_t kStage2Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset};
const uint32_t kStage2RegisterCount = sizeof(kStage2Registers) / sizeof(kStage2Registers[0]);

// Stage 3 adds GC configuration registers Linux v6.12 gfx_v9_0.c reads
// (gc_9_0_offset.h dwords; segment 0 base 0x2000, segment 1 base 0xA000):
const uint32_t kRegGrbmStatus = (0x2000 + 0x0004) * 4;           // GUI_ACTIVE bit 31
const uint32_t kRegCcShaderArrayConfig = (0x2000 + 0x026f) * 4;   // INACTIVE_CUS 31:16
const uint32_t kRegUserShaderArrayConfig = (0x2000 + 0x0270) * 4; // INACTIVE_CUS 31:16
const uint32_t kRegCcRbBackendDisable = (0x2000 + 0x063d) * 4;    // BACKEND_DISABLE 23:16
const uint32_t kRegGbAddrConfig = (0x2000 + 0x063e) * 4;
const uint32_t kRegUserRbBackendDisable = (0x2000 + 0x06df) * 4;  // BACKEND_DISABLE 23:16
const uint32_t kRegGrbmGfxIndex = (0xA000 + 0x2200) * 4;          // SE_INDEX 23:16, SH_INDEX 15:8
const uint32_t kStage3Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                     kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                     kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig};
const uint32_t kStage3RegisterCount = sizeof(kStage3Registers) / sizeof(kStage3Registers[0]);

// Stage 5 adds power, clock-gating, engine and memory-hub state registers.
// Offsets: Linux v6.12 asic_reg headers; bases measured in this host's IP
// discovery table (stage 2), each equal to renoir_ip_offset.h. Each is read
// by Linux as noted. They are read only through the diagnostic interface,
// never at boot; "GFX" ones only while SMUIO reports GFX on.
const uint32_t kRegSmuioGfxMiscCntl = (0x16800 + 0x00c8) * 4; // SMUIO_GFX_MISC_CNTL: smu_v12_0_get_gfxoff_status: PWR_GFXOFF_STATUS 2:1, 2 = GFX on
const uint32_t kRegMp1C2PMsg66 = (0x16000 + 0x0282) * 4; // MP1_SMN_C2PMSG_66: renoir_ppt SMU message register
const uint32_t kRegMp1C2PMsg82 = (0x16000 + 0x0292) * 4; // MP1_SMN_C2PMSG_82: renoir_ppt SMU argument register
const uint32_t kRegMp1C2PMsg90 = (0x16000 + 0x029a) * 4; // MP1_SMN_C2PMSG_90: renoir_ppt SMU response register
const uint32_t kRegMp0C2PMsg35 = (0x16000 + 0x0063) * 4; // MP0_SMN_C2PMSG_35: psp_v12_0: bootloader ready, bit 31
const uint32_t kRegMp0C2PMsg81 = (0x16000 + 0x0091) * 4; // MP0_SMN_C2PMSG_81: psp_v12_0: secure OS sign of life
const uint32_t kRegRlcCgttMgcgOverride = (0xA000 + 0x4c48) * 4; // RLC_CGTT_MGCG_OVERRIDE: gfx_v9_0_get_clockgating_state [GFX]
const uint32_t kRegRlcCgcgCglsCtrl = (0xA000 + 0x4c49) * 4; // RLC_CGCG_CGLS_CTRL: gfx_v9_0_get_clockgating_state [GFX]
const uint32_t kRegRlcCgcgCglsCtrl3d = (0xA000 + 0x4cc5) * 4; // RLC_CGCG_CGLS_CTRL_3D: gfx_v9_0_get_clockgating_state [GFX]
const uint32_t kRegRlcMemSlpCntl = (0xA000 + 0x4c06) * 4; // RLC_MEM_SLP_CNTL: gfx_v9_0_get_clockgating_state [GFX]
const uint32_t kRegCpMemSlpCntl = (0x2000 + 0x1079) * 4; // CP_MEM_SLP_CNTL: gfx_v9_0_get_clockgating_state [GFX]
const uint32_t kRegRlcPgCntl = (0xA000 + 0x4c43) * 4; // RLC_PG_CNTL: gfx_v9_0 power gating (read-modify-write) [GFX]
const uint32_t kRegGrbmStatus2 = (0x2000 + 0x0002) * 4; // GRBM_STATUS2: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegGrbmStatusSe0 = (0x2000 + 0x0005) * 4; // GRBM_STATUS_SE0: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpBusyStat = (0x2000 + 0x019f) * 4; // CP_BUSY_STAT: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpCpfStatus = (0x2000 + 0x0087) * 4; // CP_CPF_STATUS: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpMeCntl = (0x2000 + 0x01b6) * 4; // CP_ME_CNTL: gfx_v9_0_cp_gfx_enable (read-modify-write) [GFX]
const uint32_t kRegCpMecCntl = (0x2000 + 0x008d) * 4; // CP_MEC_CNTL: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegRlcCntl = (0xA000 + 0x4c00) * 4; // RLC_CNTL: gfx_v9_0 RLC state read [GFX]
const uint32_t kRegRlcStat = (0xA000 + 0x4c04) * 4; // RLC_STAT: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpPfpInstrPntr = (0x2000 + 0x01a5) * 4; // CP_PFP_INSTR_PNTR: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpMeInstrPntr = (0x2000 + 0x01a6) * 4; // CP_ME_INSTR_PNTR: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegCpMec1InstrPntr = (0x2000 + 0x01a8) * 4; // CP_MEC1_INSTR_PNTR: gc_reg_list_9 (IP dump) [GFX]
const uint32_t kRegSdma0ClkCtrl = (0x1260 + 0x001b) * 4; // SDMA0_CLK_CTRL: sdma_v4_0_get_clockgating_state
const uint32_t kRegSdma0PowerCntl = (0x1260 + 0x001a) * 4; // SDMA0_POWER_CNTL: sdma_v4_0_get_clockgating_state
const uint32_t kRegSdma0F32Cntl = (0x1260 + 0x002a) * 4; // SDMA0_F32_CNTL: sdma_v4_0: engine halt state
const uint32_t kRegSdma0StatusReg = (0x1260 + 0x0025) * 4; // SDMA0_STATUS_REG: sdma_v4_0 idle checks
const uint32_t kRegSdma0GfxRbCntl = (0x1260 + 0x0080) * 4; // SDMA0_GFX_RB_CNTL: sdma_v4_0 ring enable
const uint32_t kRegHdpMemPowerLs = (0x0F20 + 0x00d4) * 4; // HDP_MEM_POWER_LS: hdp_v4_0_get_clockgating_state
const uint32_t kRegAthubMiscCntl = (0x0C20 + 0x000a) * 4; // ATHUB_MISC_CNTL: athub_v1_0_get_clockgating
const uint32_t kRegAtcL2MiscCg = (0x1A000 + 0x064a) * 4; // ATC_L2_MISC_CG: mmhub_v1_0_get_clockgating
const uint32_t kRegDagb0CntlMisc2 = (0x1A000 + 0x0063) * 4; // DAGB0_CNTL_MISC2: mmhub_v1_0_get_clockgating
const uint32_t kRegMmhubFbLocationBase = (0x1A000 + 0x082c) * 4; // MC_VM_FB_LOCATION_BASE (MMHUB): mmhub_v1_0_get_fb_location
const uint32_t kRegMmhubFbLocationTop = (0x1A000 + 0x082d) * 4; // MC_VM_FB_LOCATION_TOP (MMHUB): mmhub_v1_0_get_fb_location
const uint32_t kRegMmhubVmL2Cntl = (0x1A000 + 0x0680) * 4; // VM_L2_CNTL (MMHUB): mmhub_v1_0 cache setup (read-modify-write)
const uint32_t kRegMmhubVmContext0Cntl = (0x1A000 + 0x06c0) * 4; // VM_CONTEXT0_CNTL (MMHUB): mmhub_v1_0 (read-modify-write)
const uint32_t kRegMmhubMxL1TlbCntl = (0x1A000 + 0x0833) * 4; // MC_VM_MX_L1_TLB_CNTL (MMHUB): mmhub_v1_0 (read-modify-write)
const uint32_t kRegIhRbCntl = (0x10A0 + 0x0080) * 4; // IH_RB_CNTL: vega10_ih ring control
const uint32_t kStage5Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                     kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                     kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                     kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                     kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                     kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                     kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                     kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                     kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                     kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr,
                                     kRegCpMeInstrPntr, kRegCpMec1InstrPntr, kRegSdma0ClkCtrl,
                                     kRegSdma0PowerCntl, kRegSdma0F32Cntl, kRegSdma0StatusReg,
                                     kRegSdma0GfxRbCntl, kRegHdpMemPowerLs, kRegAthubMiscCntl,
                                     kRegAtcL2MiscCg, kRegDagb0CntlMisc2, kRegMmhubFbLocationBase,
                                     kRegMmhubFbLocationTop, kRegMmhubVmL2Cntl,
                                     kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl, kRegIhRbCntl};
const uint32_t kStage5RegisterCount = sizeof(kStage5Registers) / sizeof(kStage5Registers[0]);
// Stage 5 GC registers: read only while SMUIO_GFX_MISC_CNTL reports GFX on.
const uint32_t kGfxGatedRegisters[] = {
    kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl, kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl,
    kRegCpMemSlpCntl, kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0, kRegCpBusyStat,
    kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl, kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr,
    kRegCpMeInstrPntr, kRegCpMec1InstrPntr};
const uint32_t kGfxGatedRegisterCount = sizeof(kGfxGatedRegisters) / sizeof(kGfxGatedRegisters[0]);
const uint32_t kGfxOffStatusMask = 0x6, kGfxOffStatusShift = 1, kGfxOffStatusOn = 2;

// Stage 6: the first reviewed write. SCRATCH_REG0 (GC dword 0x2040, base index
// 1, gc_9_0_offset.h) has no hardware function; Linux v6.12
// gfx_v9_0_ring_test_ring writes 0xCAFEDEAD to it from the CPU.
const uint32_t kRegScratchReg0 = (0xA000 + 0x2040) * 4;
const uint32_t kScratchPattern = 0xCAFEDEAD;
// The adapter maps only this 4 KiB page of BAR5 writable.
const uint32_t kScratchPageOffset = kRegScratchReg0 & ~0xFFFu;
const uint32_t kScratchPageSize = 0x1000; // == kPageSize
const uint32_t kStage6Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                     kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                     kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                     kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                     kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                     kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                     kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                     kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                     kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                     kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr, kRegCpMeInstrPntr,
                                     kRegCpMec1InstrPntr, kRegSdma0ClkCtrl, kRegSdma0PowerCntl,
                                     kRegSdma0F32Cntl, kRegSdma0StatusReg, kRegSdma0GfxRbCntl,
                                     kRegHdpMemPowerLs, kRegAthubMiscCntl, kRegAtcL2MiscCg,
                                     kRegDagb0CntlMisc2, kRegMmhubFbLocationBase, kRegMmhubFbLocationTop,
                                     kRegMmhubVmL2Cntl, kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl,
                                     kRegIhRbCntl, kRegScratchReg0};
const uint32_t kStage6RegisterCount = sizeof(kStage6Registers) / sizeof(kStage6Registers[0]);
// Preconditions (gc_9_0_sh_mask.h): every CP engine halted, GUI idle.
const uint32_t kCpMeHalts = 0x15000000u;  // ME_HALT | PFP_HALT | CE_HALT
const uint32_t kCpMecHalts = 0x50000000u; // MEC_ME1_HALT | MEC_ME2_HALT
const uint32_t kGrbmGuiActive = 0x80000000u;

// Stage 7: the first SMU messages, the two queries Linux v6.12 sends first on
// Renoir (smu_v12_0_check_fw_version -> smu_cmn_get_smc_version), through
// the MP1 mailbox stage 5 reads (renoir_ppt: msg C2PMSG_66, argument
// C2PMSG_82, response C2PMSG_90). Indices: smu_v12_0_ppsmc.h.
const uint32_t kSmuMsgGetSmuVersion = 0x2;
const uint32_t kSmuMsgGetDriverIfVersion = 0x3;
const uint32_t kSmuResponseOk = 0x1; // PPSMC_Result_OK
// smu_cmn's poll limit is usec_timeout * 20 = 2 s; polled in 1 ms pauses.
const uint32_t kSmuPollPauses = 2000;
// The adapter maps only this 4 KiB page of BAR5 writable for a query.
const uint32_t kSmuPageOffset = kRegMp1C2PMsg66 & ~0xFFFu;
const uint32_t kPageSize = 0x1000;
const uint32_t kSmuStage = 7;
// Stage 8: DisallowGfxOff, as smu_v12_0_gfx_off_control(smu, false) sends it
// (renoir_ppt maps it to PPSMC_MSG_DisableGfxOff), then waits up to 500 ms
// for PWR_GFXOFF_STATUS 2. AllowGfxOff (0x7) stays refused.
const uint32_t kSmuMsgDisableGfxOff = 0x8;
const uint32_t kGfxOffConfirmPauses = 500;
const uint32_t kGfxOffStage = 8;

// Stage 9: the SMU metrics table (smu12_driver_if.h SmuMetrics_t, table 7).
// The SMU writes it to a GPU (MC) address set with SetDriverDramAddrHigh/Low
// (smu_v12_0_set_driver_table_location) when asked by TransferTableSmu2Dram
// with argument table_id | (0 << 16) (smu_cmn_update_table).
const uint32_t kSmuMsgSetDriverDramAddrHigh = 0x1A;
const uint32_t kSmuMsgSetDriverDramAddrLow = 0x1B;
const uint32_t kSmuMsgTransferTableSmu2Dram = 0x1C;
const uint32_t kTableSmuMetrics = 7;
const uint32_t kMetricsSize = 148; // 74 uint16_t fields
const uint32_t kMetricsStage = 9;
// One fixed page 1 GiB into the carveout. For this APU, GPU address
// (MC_VM_FB_LOCATION_BASE << 24) + offset is CPU physical
// (MC_VM_FB_OFFSET << 24) + offset (gmc_v9_0_mc_init aper_base); both
// registers are checked against these values before use.
const uint32_t kExpectedFbLocationBase = 0xf400; // MMHUB, stage 5
const uint32_t kExpectedFbOffset = 0x5c0;        // GC, stage 2
const uint64_t kMetricsCarveoutOffset = 0x40000000ull;
const uint64_t kMetricsGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kMetricsCarveoutOffset;
const uint64_t kMetricsPhysical = (uint64_t(kExpectedFbOffset) << 24) + kMetricsCarveoutOffset;
// The page and the 60 KiB after it must read the same twice, about 1 s
// apart (kMetricsStablePauses pauses), before use. Unused carveout DRAM holds
// stale data, so content is not tested (boot 11); the VBIOS reserves no
// carveout memory for firmware (boot 12).
const uint32_t kMetricsCheckSize = 0x10000;
const uint32_t kMetricsStablePauses = 1000;
// Carveout regions the page must avoid: the boot framebuffer and other
// low allocations, and the firmware, PSP and discovery regions at the top.
const uint64_t kCarveoutLowReserve = 64ull << 20;
const uint64_t kCarveoutHighReserve = 64ull << 20;
// SmuMetrics_t field indices, in uint16_t words.
enum MetricsWord : uint32_t {
    kMetricsClockFrequency = 0, // [18], MHz, CLOCK_IDs_e order
    kMetricsAverageGfxclk = 18,
    kMetricsAverageSocclk = 19,
    kMetricsAverageVclk = 20,
    kMetricsAverageFclk = 21,
    kMetricsAverageGfxActivity = 22, // centi-percent
    kMetricsAverageUvdActivity = 23,
    kMetricsVoltage = 24, // [2] mV: VDDCR_VDD, VDDCR_SOC
    kMetricsCurrent = 26, // [2] mA
    kMetricsPower = 28,   // [2] mW
    kMetricsFanPwm = 30,
    kMetricsCurrentSocketPower = 31, // W
    kMetricsCoreFrequency = 32,      // [8] MHz
    kMetricsCorePower = 40,          // [8] mW
    kMetricsCoreTemperature = 48,    // [8] centi-degrees C
    kMetricsL3Frequency = 56,        // [2]
    kMetricsL3Temperature = 58,      // [2]
    kMetricsGfxTemperature = 60,
    kMetricsSocTemperature = 61,
    kMetricsThrottlerStatus = 62,
    kMetricsStapmOriginalLimit = 64,
    kMetricsStapmCurrentLimit = 65,
    kMetricsApuPower = 66,
    kMetricsDgpuPower = 67,
    kMetricsVddTdc = 68,
    kMetricsSocTdc = 69,
    kMetricsVddEdc = 70,
    kMetricsSocEdc = 71,
    kMetricsWordCount = 74,
};

// Stage 10: read-only PSP mailbox and memory-aperture state, before any PSP
// write (psp_v12_0 ring_create, mmhub_v1_0/gfxhub_v1_0
// init_system_aperture_regs). Offsets: mp_12_0_0_offset.h,
// mmhub_1_0_offset.h, gc_9_0_offset.h, measured bases.
const uint32_t kRegMp0C2PMsg36 = (0x16000 + 0x0064) * 4; // bootloader firmware address
const uint32_t kRegMp0C2PMsg64 = (0x16000 + 0x0080) * 4; // ring command and response
const uint32_t kRegMp0C2PMsg67 = (0x16000 + 0x0083) * 4; // ring write pointer
const uint32_t kRegMp0C2PMsg69 = (0x16000 + 0x0085) * 4; // ring address low
const uint32_t kRegMp0C2PMsg70 = (0x16000 + 0x0086) * 4; // ring address high
const uint32_t kRegMp0C2PMsg71 = (0x16000 + 0x0087) * 4; // ring size
const uint32_t kRegMmhubFbOffset = (0x1A000 + 0x0817) * 4;
const uint32_t kRegMmhubDefaultAddrLsb = (0x1A000 + 0x0818) * 4;
const uint32_t kRegMmhubDefaultAddrMsb = (0x1A000 + 0x0819) * 4;
const uint32_t kRegMmhubAgpTop = (0x1A000 + 0x082e) * 4;
const uint32_t kRegMmhubAgpBot = (0x1A000 + 0x082f) * 4;
const uint32_t kRegMmhubAgpBase = (0x1A000 + 0x0830) * 4;
const uint32_t kRegMmhubApertureLow = (0x1A000 + 0x0831) * 4;
const uint32_t kRegMmhubApertureHigh = (0x1A000 + 0x0832) * 4;
const uint32_t kRegGcFbLocationBase = (0x2000 + 0x0980) * 4;
const uint32_t kRegGcFbLocationTop = (0x2000 + 0x0981) * 4;
const uint32_t kRegGcAgpTop = (0x2000 + 0x0982) * 4;
const uint32_t kRegGcAgpBot = (0x2000 + 0x0983) * 4;
const uint32_t kRegGcAgpBase = (0x2000 + 0x0984) * 4;
const uint32_t kRegGcApertureLow = (0x2000 + 0x0985) * 4;
const uint32_t kRegGcApertureHigh = (0x2000 + 0x0986) * 4;
const uint32_t kStage10Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                      kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                      kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                      kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                      kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                      kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                      kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                      kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                      kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                      kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr, kRegCpMeInstrPntr,
                                      kRegCpMec1InstrPntr, kRegSdma0ClkCtrl, kRegSdma0PowerCntl,
                                      kRegSdma0F32Cntl, kRegSdma0StatusReg, kRegSdma0GfxRbCntl,
                                      kRegHdpMemPowerLs, kRegAthubMiscCntl, kRegAtcL2MiscCg,
                                      kRegDagb0CntlMisc2, kRegMmhubFbLocationBase, kRegMmhubFbLocationTop,
                                      kRegMmhubVmL2Cntl, kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl,
                                      kRegIhRbCntl, kRegScratchReg0, kRegMp0C2PMsg36, kRegMp0C2PMsg64,
                                      kRegMp0C2PMsg67, kRegMp0C2PMsg69, kRegMp0C2PMsg70, kRegMp0C2PMsg71,
                                      kRegMmhubFbOffset, kRegMmhubDefaultAddrLsb, kRegMmhubDefaultAddrMsb,
                                      kRegMmhubAgpTop, kRegMmhubAgpBot, kRegMmhubAgpBase,
                                      kRegMmhubApertureLow, kRegMmhubApertureHigh, kRegGcFbLocationBase,
                                      kRegGcFbLocationTop, kRegGcAgpTop, kRegGcAgpBot, kRegGcAgpBase,
                                      kRegGcApertureLow, kRegGcApertureHigh};
const uint32_t kStage10RegisterCount = sizeof(kStage10Registers) / sizeof(kStage10Registers[0]);
// The GC-hub registers take the stage 5 GFX gate.
const uint32_t kStage10GfxGatedRegisters[] = {kRegGcFbLocationBase, kRegGcFbLocationTop, kRegGcAgpTop,
                                              kRegGcAgpBot, kRegGcAgpBase, kRegGcApertureLow,
                                              kRegGcApertureHigh};
const uint32_t kStage10GfxGatedRegisterCount =
    sizeof(kStage10GfxGatedRegisters) / sizeof(kStage10GfxGatedRegisters[0]);
// C2PMSG_64: bit 31 set by the PSP when it has answered; bits 19:16 echo the
// command ID; bits 15:0 status (psp_gfx_if.h). psp_v12_0 waits for
// 0x80000000 under mask 0x8000FFFF, ignoring the echo (boot 15).
const uint32_t kPspResponseFlag = 0x80000000u;
const uint32_t kPspStatusMask = 0xFFFFu;
const uint32_t kPspResponseMask = kPspResponseFlag | kPspStatusMask;
const uint32_t kPspStateStage = 10;

// Stage 11: create and destroy the PSP kernel-mode ring, as Linux v6.12
// psp_v12_0_ring_create and psp_v12_0_ring_stop do; no frame is submitted.
// Command IDs: psp_gfx_if.h; ring type: amdgpu_psp.h (PSP_RING_TYPE__KM = 2,
// so the create command is 2 << 16). Linux first sends two GBR_IH_SET
// (0x00080000) commands (psp_v12_0_reroute_ih) and ignores their results;
// this host's secure OS answers PSP_ERR_UNKNOWN_COMMAND (boot 15), so they
// are not sent.
const uint32_t kPspCmdInitGpcomRing = 0x00020000;
const uint32_t kPspCmdDestroyRings = 0x00030000;
// One 4 KiB ring (psp_ring_init), 1 MiB above the stage 9 metrics page.
const uint64_t kPspRingCarveoutOffset = 0x40100000ull;
const uint64_t kPspRingGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kPspRingCarveoutOffset;
const uint64_t kPspRingPhysical = (uint64_t(kExpectedFbOffset) << 24) + kPspRingCarveoutOffset;
const uint32_t kPspRingSize = 0x1000;
const uint32_t kPspRingCheckSize = 0x10000; // the ring page and the 60 KiB after it
// Linux waits 20 ms after each command, then polls for usec_timeout
// (AMDGPU_MAX_USEC_TIMEOUT, 100 ms); here both in 1 ms pauses.
const uint32_t kPspSettlePauses = 20;
const uint32_t kPspPollPauses = 100;
const uint32_t kPspRingStage = 11;

// Stage 12: the first ring frame, GFX_CMD_ID_SETUP_TMR, then
// GFX_CMD_ID_DESTROY_TMR (psp_gfx_if.h; psp_tmr_load, psp_tmr_unload). The
// "work area" is the ring page and the two pages after it: the command
// buffer and the fence buffer; it is the only memory the CPU writes.
const uint32_t kGfxCmdSetupTmr = 5;
const uint32_t kGfxCmdDestroyTmr = 7;
const uint32_t kPspWorkSize = 0x3000;
const uint32_t kPspCmdPage = 0x1000;   // work-area offset of the command buffer
const uint32_t kPspFencePage = 0x2000; // work-area offset of the fence buffer
const uint64_t kPspCmdGpuAddress = kPspRingGpuAddress + kPspCmdPage;
const uint64_t kPspFenceGpuAddress = kPspRingGpuAddress + kPspFencePage;
// psp_gfx_cmd_resp: cmd_id at +8, the command at +28, psp_gfx_resp (96
// bytes) at +864; 1024 bytes in all.
const uint32_t kPspCmdIdOffset = 8, kPspCmdFieldsOffset = 28, kPspRespOffset = 864, kPspRespSize = 96;
// psp_gfx_rb_frame: 64 bytes, 16 dwords; the write pointer counts dwords
// modulo the ring's 1024.
const uint32_t kPspFrameSize = 64;
const uint32_t kPspFrameDwords = 16;
const uint32_t kPspRingDwords = kPspRingSize / 4;
// PSP_TMR_SIZE (4 MiB), aligned to its size; amdgpu_gmc_vram_mc2pa gives the
// physical address from MC_VM_FB_OFFSET as for every carveout page here.
const uint64_t kPspTmrCarveoutOffset = 0x40400000ull;
const uint32_t kPspTmrSize = 0x400000;
const uint64_t kPspTmrGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kPspTmrCarveoutOffset;
const uint64_t kPspTmrPhysical = (uint64_t(kExpectedFbOffset) << 24) + kPspTmrCarveoutOffset;
const uint32_t kPspTmrFlagVirtPhysAddr = 0x2; // tmr_flags.virt_phy_addr
// psp_cmd_submit_buf waits up to psp_timeout (20000) x 10-100 us; here 2 s.
const uint32_t kPspFencePollPauses = 2000;
const uint32_t kPspTmrStage = 12;

// Stage 13: GFX_CMD_ID_LOAD_IP_FW for SDMA0 after SETUP_TMR
// (psp_load_non_psp_fw: SDMA0 is the first ucode ID, CAP being absent).
// Frames: SETUP_TMR 0, LOAD_IP_FW 1, DESTROY_TMR 2.
const uint32_t kGfxCmdLoadIpFw = 6;
const uint32_t kGfxFwTypeSdma0 = 9; // psp_gfx_if.h GFX_FW_TYPE_SDMA0
// The pinned green_sardine_sdma.bin (linux-firmware 20260916): its
// common_firmware_header fields. amdgpu_ucode_init_single_fw copies
// ucode_size_bytes from ucode_array_offset_bytes; the payload is opaque.
const uint32_t kSdmaImageSize = 17408;
const uint32_t kSdmaUcodeSize = 17152;
const uint32_t kSdmaUcodeOffset = 256;
const uint32_t kSdmaUcodeVersion = 40;
// A page-aligned firmware buffer (amdgpu_ucode_init_bo), 1 MiB above the ring.
const uint64_t kSdmaFwCarveoutOffset = 0x40200000ull;
const uint64_t kSdmaFwGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kSdmaFwCarveoutOffset;
const uint64_t kSdmaFwPhysical = (uint64_t(kExpectedFbOffset) << 24) + kSdmaFwCarveoutOffset;
const uint32_t kSdmaFwBufferSize = 0x5000; // ALIGN(17152, 4096)
const uint32_t kSdmaFwCheckSize = 0x10000;
// SDMA0_UCODE_CHECKSUM: in Linux's sdma_reg_list_4_0 (IP dump); read only.
const uint32_t kRegSdma0UcodeChecksum = (0x1260 + 0x0029) * 4;
const uint32_t kStage13Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                      kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                      kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                      kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                      kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                      kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                      kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                      kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                      kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                      kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr, kRegCpMeInstrPntr,
                                      kRegCpMec1InstrPntr, kRegSdma0ClkCtrl, kRegSdma0PowerCntl,
                                      kRegSdma0F32Cntl, kRegSdma0StatusReg, kRegSdma0GfxRbCntl,
                                      kRegHdpMemPowerLs, kRegAthubMiscCntl, kRegAtcL2MiscCg,
                                      kRegDagb0CntlMisc2, kRegMmhubFbLocationBase, kRegMmhubFbLocationTop,
                                      kRegMmhubVmL2Cntl, kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl,
                                      kRegIhRbCntl, kRegScratchReg0, kRegMp0C2PMsg36, kRegMp0C2PMsg64,
                                      kRegMp0C2PMsg67, kRegMp0C2PMsg69, kRegMp0C2PMsg70, kRegMp0C2PMsg71,
                                      kRegMmhubFbOffset, kRegMmhubDefaultAddrLsb, kRegMmhubDefaultAddrMsb,
                                      kRegMmhubAgpTop, kRegMmhubAgpBot, kRegMmhubAgpBase,
                                      kRegMmhubApertureLow, kRegMmhubApertureHigh, kRegGcFbLocationBase,
                                      kRegGcFbLocationTop, kRegGcAgpTop, kRegGcAgpBot, kRegGcAgpBase,
                                      kRegGcApertureLow, kRegGcApertureHigh, kRegSdma0UcodeChecksum};
const uint32_t kStage13RegisterCount = sizeof(kStage13Registers) / sizeof(kStage13Registers[0]);
const uint32_t kSdmaF32Halt = 0x1; // SDMA0_F32_CNTL.HALT
const uint32_t kPspSdmaStage = 13;

// Stage 14: the SDMA registers sdma_v4_0_hw_init / _start / golden settings
// (golden_settings_sdma_4_3) touch, read only, with SDMA powered up by the
// SMU as Linux does on APUs (smu_v12_0_powergate_sdma; smu_v12_0_ppsmc.h).
// Offsets: sdma0_4_0_offset.h, SDMA0 base 0x1260.
const uint32_t kRegSdma0Cntl = (0x1260 + 0x001c) * 4;
const uint32_t kRegSdma0ChickenBits = (0x1260 + 0x001d) * 4;
const uint32_t kRegSdma0GbAddrConfig = (0x1260 + 0x001e) * 4;
const uint32_t kRegSdma0GbAddrConfigRead = (0x1260 + 0x001f) * 4;
const uint32_t kRegSdma0SemWaitFailTimerCntl = (0x1260 + 0x0021) * 4;
const uint32_t kRegSdma0Utcl1Watermk = (0x1260 + 0x003d) * 4;
const uint32_t kRegSdma0Utcl1Timeout = (0x1260 + 0x0047) * 4;
const uint32_t kRegSdma0Utcl1Page = (0x1260 + 0x0048) * 4;
const uint32_t kRegSdma0GfxRbBase = (0x1260 + 0x0081) * 4;
const uint32_t kRegSdma0GfxRbBaseHi = (0x1260 + 0x0082) * 4;
const uint32_t kRegSdma0GfxRbRptr = (0x1260 + 0x0083) * 4;
const uint32_t kRegSdma0GfxRbRptrHi = (0x1260 + 0x0084) * 4;
const uint32_t kRegSdma0GfxRbWptr = (0x1260 + 0x0085) * 4;
const uint32_t kRegSdma0GfxRbWptrHi = (0x1260 + 0x0086) * 4;
const uint32_t kRegSdma0GfxRbWptrPollCntl = (0x1260 + 0x0087) * 4;
const uint32_t kRegSdma0GfxRbRptrAddrHi = (0x1260 + 0x0088) * 4;
const uint32_t kRegSdma0GfxRbRptrAddrLo = (0x1260 + 0x0089) * 4;
const uint32_t kRegSdma0GfxIbCntl = (0x1260 + 0x008a) * 4;
const uint32_t kRegSdma0GfxDoorbell = (0x1260 + 0x0092) * 4;
const uint32_t kRegSdma0GfxDoorbellOffset = (0x1260 + 0x00ab) * 4;
const uint32_t kRegSdma0GfxRbWptrPollAddrHi = (0x1260 + 0x00b2) * 4;
const uint32_t kRegSdma0GfxRbWptrPollAddrLo = (0x1260 + 0x00b3) * 4;
const uint32_t kRegSdma0GfxMinorPtrUpdate = (0x1260 + 0x00b5) * 4;
const uint32_t kRegSdma0Rlc0RbWptrPollCntl = (0x1260 + 0x0147) * 4;
const uint32_t kRegSdma0Rlc1RbWptrPollCntl = (0x1260 + 0x01a7) * 4;
const uint32_t kStage14Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                      kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                      kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                      kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                      kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                      kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                      kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                      kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                      kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                      kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr, kRegCpMeInstrPntr,
                                      kRegCpMec1InstrPntr, kRegSdma0ClkCtrl, kRegSdma0PowerCntl,
                                      kRegSdma0F32Cntl, kRegSdma0StatusReg, kRegSdma0GfxRbCntl,
                                      kRegHdpMemPowerLs, kRegAthubMiscCntl, kRegAtcL2MiscCg,
                                      kRegDagb0CntlMisc2, kRegMmhubFbLocationBase, kRegMmhubFbLocationTop,
                                      kRegMmhubVmL2Cntl, kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl,
                                      kRegIhRbCntl, kRegScratchReg0, kRegMp0C2PMsg36, kRegMp0C2PMsg64,
                                      kRegMp0C2PMsg67, kRegMp0C2PMsg69, kRegMp0C2PMsg70, kRegMp0C2PMsg71,
                                      kRegMmhubFbOffset, kRegMmhubDefaultAddrLsb, kRegMmhubDefaultAddrMsb,
                                      kRegMmhubAgpTop, kRegMmhubAgpBot, kRegMmhubAgpBase,
                                      kRegMmhubApertureLow, kRegMmhubApertureHigh, kRegGcFbLocationBase,
                                      kRegGcFbLocationTop, kRegGcAgpTop, kRegGcAgpBot, kRegGcAgpBase,
                                      kRegGcApertureLow, kRegGcApertureHigh, kRegSdma0UcodeChecksum,
                                      kRegSdma0Cntl, kRegSdma0ChickenBits, kRegSdma0GbAddrConfig, kRegSdma0GbAddrConfigRead, kRegSdma0SemWaitFailTimerCntl, kRegSdma0Utcl1Watermk, kRegSdma0Utcl1Timeout, kRegSdma0Utcl1Page, kRegSdma0GfxRbBase, kRegSdma0GfxRbBaseHi, kRegSdma0GfxRbRptr, kRegSdma0GfxRbRptrHi, kRegSdma0GfxRbWptr, kRegSdma0GfxRbWptrHi, kRegSdma0GfxRbWptrPollCntl, kRegSdma0GfxRbRptrAddrHi, kRegSdma0GfxRbRptrAddrLo, kRegSdma0GfxIbCntl, kRegSdma0GfxDoorbell, kRegSdma0GfxDoorbellOffset, kRegSdma0GfxRbWptrPollAddrHi, kRegSdma0GfxRbWptrPollAddrLo, kRegSdma0GfxMinorPtrUpdate, kRegSdma0Rlc0RbWptrPollCntl, kRegSdma0Rlc1RbWptrPollCntl};
const uint32_t kStage14RegisterCount = sizeof(kStage14Registers) / sizeof(kStage14Registers[0]);
// The 31 registers read three times: six known from stages 5 and 13, then
// the 25 above.
const uint32_t kSdmaInventory[] = {kRegSdma0F32Cntl, kRegSdma0ClkCtrl, kRegSdma0PowerCntl, kRegSdma0StatusReg,
                                   kRegSdma0GfxRbCntl, kRegSdma0UcodeChecksum, kRegSdma0Cntl, kRegSdma0ChickenBits, kRegSdma0GbAddrConfig, kRegSdma0GbAddrConfigRead, kRegSdma0SemWaitFailTimerCntl, kRegSdma0Utcl1Watermk, kRegSdma0Utcl1Timeout, kRegSdma0Utcl1Page, kRegSdma0GfxRbBase, kRegSdma0GfxRbBaseHi, kRegSdma0GfxRbRptr, kRegSdma0GfxRbRptrHi, kRegSdma0GfxRbWptr, kRegSdma0GfxRbWptrHi, kRegSdma0GfxRbWptrPollCntl, kRegSdma0GfxRbRptrAddrHi, kRegSdma0GfxRbRptrAddrLo, kRegSdma0GfxIbCntl, kRegSdma0GfxDoorbell, kRegSdma0GfxDoorbellOffset, kRegSdma0GfxRbWptrPollAddrHi, kRegSdma0GfxRbWptrPollAddrLo, kRegSdma0GfxMinorPtrUpdate, kRegSdma0Rlc0RbWptrPollCntl, kRegSdma0Rlc1RbWptrPollCntl};
const uint32_t kSdmaInventoryCount = sizeof(kSdmaInventory) / sizeof(kSdmaInventory[0]);
const uint32_t kSmuMsgPowerDownSdma = 0xD;
const uint32_t kSmuMsgPowerUpSdma = 0xE;
const uint32_t kSdmaInventoryStage = 14;

// Stage 16: a read-only inventory for the display, GART and interrupt
// milestones. Byte offsets from dcn_2_1_0_offset.h (DMU bases 0xC0/0x34C0),
// mmhub_1_0_offset.h (0x1A000), osssys_4_0_offset.h (0x10A0) and
// nbio_7_0_offset.h (NBIF 0x14/0xD20/0x10400), bases as measured in the
// stage 2 discovery table; each register is one Linux v6.12 reads.
// VM_INVALIDATE_ENG17_SEM is excluded: a read acquires the semaphore
// (gmc_v9_0_flush_gpu_tlb), so it is not side-effect free (boot 22).
const uint32_t kRegOtg0OtgControl = 0x14004; // OTG0_OTG_CONTROL: optc1_read_otg_state
const uint32_t kRegOtg0OtgHTotal = 0x13fa8; // OTG0_OTG_H_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg0OtgVTotal = 0x13fbc; // OTG0_OTG_V_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg0OtgHBlankStartEnd = 0x13fac; // OTG0_OTG_H_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegOtg0OtgVBlankStartEnd = 0x13fd8; // OTG0_OTG_V_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegHubp0DchubpCntl = 0x0eacc; // HUBP0_DCHUBP_CNTL: hubp2_read_state
const uint32_t kRegHubp0DcsurfSurfaceConfig = 0x0ea94; // HUBP0_DCSURF_SURFACE_CONFIG: hubp2_read_state
const uint32_t kRegHubp0DcsurfPriViewportDimension = 0x0eaa8; // HUBP0_DCSURF_PRI_VIEWPORT_DIMENSION: hubp2_read_state
const uint32_t kRegHubpreq0DcsurfSurfacePitch = 0x0eb1c; // HUBPREQ0_DCSURF_SURFACE_PITCH: hubp2_read_state
const uint32_t kRegHubpreq0DcsurfPrimarySurfaceAddress = 0x0eb28; // HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS: hubp2_read_state
const uint32_t kRegHubpreq0DcsurfPrimarySurfaceAddressHigh = 0x0eb2c; // HUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH: hubp2_read_state
const uint32_t kRegOtg1OtgControl = 0x14204; // OTG1_OTG_CONTROL: optc1_read_otg_state
const uint32_t kRegOtg1OtgHTotal = 0x141a8; // OTG1_OTG_H_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg1OtgVTotal = 0x141bc; // OTG1_OTG_V_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg1OtgHBlankStartEnd = 0x141ac; // OTG1_OTG_H_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegOtg1OtgVBlankStartEnd = 0x141d8; // OTG1_OTG_V_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegHubp1DchubpCntl = 0x0ee3c; // HUBP1_DCHUBP_CNTL: hubp2_read_state
const uint32_t kRegHubp1DcsurfSurfaceConfig = 0x0ee04; // HUBP1_DCSURF_SURFACE_CONFIG: hubp2_read_state
const uint32_t kRegHubp1DcsurfPriViewportDimension = 0x0ee18; // HUBP1_DCSURF_PRI_VIEWPORT_DIMENSION: hubp2_read_state
const uint32_t kRegHubpreq1DcsurfSurfacePitch = 0x0ee8c; // HUBPREQ1_DCSURF_SURFACE_PITCH: hubp2_read_state
const uint32_t kRegHubpreq1DcsurfPrimarySurfaceAddress = 0x0ee98; // HUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS: hubp2_read_state
const uint32_t kRegHubpreq1DcsurfPrimarySurfaceAddressHigh = 0x0ee9c; // HUBPREQ1_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH: hubp2_read_state
const uint32_t kRegOtg2OtgControl = 0x14404; // OTG2_OTG_CONTROL: optc1_read_otg_state
const uint32_t kRegOtg2OtgHTotal = 0x143a8; // OTG2_OTG_H_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg2OtgVTotal = 0x143bc; // OTG2_OTG_V_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg2OtgHBlankStartEnd = 0x143ac; // OTG2_OTG_H_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegOtg2OtgVBlankStartEnd = 0x143d8; // OTG2_OTG_V_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegHubp2DchubpCntl = 0x0f1ac; // HUBP2_DCHUBP_CNTL: hubp2_read_state
const uint32_t kRegHubp2DcsurfSurfaceConfig = 0x0f174; // HUBP2_DCSURF_SURFACE_CONFIG: hubp2_read_state
const uint32_t kRegHubp2DcsurfPriViewportDimension = 0x0f188; // HUBP2_DCSURF_PRI_VIEWPORT_DIMENSION: hubp2_read_state
const uint32_t kRegHubpreq2DcsurfSurfacePitch = 0x0f1fc; // HUBPREQ2_DCSURF_SURFACE_PITCH: hubp2_read_state
const uint32_t kRegHubpreq2DcsurfPrimarySurfaceAddress = 0x0f208; // HUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS: hubp2_read_state
const uint32_t kRegHubpreq2DcsurfPrimarySurfaceAddressHigh = 0x0f20c; // HUBPREQ2_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH: hubp2_read_state
const uint32_t kRegOtg3OtgControl = 0x14604; // OTG3_OTG_CONTROL: optc1_read_otg_state
const uint32_t kRegOtg3OtgHTotal = 0x145a8; // OTG3_OTG_H_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg3OtgVTotal = 0x145bc; // OTG3_OTG_V_TOTAL: optc1_read_otg_state
const uint32_t kRegOtg3OtgHBlankStartEnd = 0x145ac; // OTG3_OTG_H_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegOtg3OtgVBlankStartEnd = 0x145d8; // OTG3_OTG_V_BLANK_START_END: optc1_read_otg_state
const uint32_t kRegHubp3DchubpCntl = 0x0f51c; // HUBP3_DCHUBP_CNTL: hubp2_read_state
const uint32_t kRegHubp3DcsurfSurfaceConfig = 0x0f4e4; // HUBP3_DCSURF_SURFACE_CONFIG: hubp2_read_state
const uint32_t kRegHubp3DcsurfPriViewportDimension = 0x0f4f8; // HUBP3_DCSURF_PRI_VIEWPORT_DIMENSION: hubp2_read_state
const uint32_t kRegHubpreq3DcsurfSurfacePitch = 0x0f56c; // HUBPREQ3_DCSURF_SURFACE_PITCH: hubp2_read_state
const uint32_t kRegHubpreq3DcsurfPrimarySurfaceAddress = 0x0f578; // HUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS: hubp2_read_state
const uint32_t kRegHubpreq3DcsurfPrimarySurfaceAddressHigh = 0x0f57c; // HUBPREQ3_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH: hubp2_read_state
const uint32_t kRegDcnVmFbLocationBase = 0x0e54c; // DCN_VM_FB_LOCATION_BASE: hubbub21_init_dchub
const uint32_t kRegDcnVmFbLocationTop = 0x0e550; // DCN_VM_FB_LOCATION_TOP: hubbub21_init_dchub
const uint32_t kRegDcnVmFbOffset = 0x0e554; // DCN_VM_FB_OFFSET: hubbub21_init_dchub
const uint32_t kRegDcnVmAgpBase = 0x0e560; // DCN_VM_AGP_BASE: hubbub21_init_dchub
const uint32_t kRegDcnVmAgpBot = 0x0e558; // DCN_VM_AGP_BOT: hubbub21_init_dchub
const uint32_t kRegDcnVmAgpTop = 0x0e55c; // DCN_VM_AGP_TOP: hubbub21_init_dchub
const uint32_t kRegDig0DigBeCntl = 0x155bc; // DIG0_DIG_BE_CNTL: dcn10 link encoder state
const uint32_t kRegDig1DigBeCntl = 0x159bc; // DIG1_DIG_BE_CNTL: dcn10 link encoder state
const uint32_t kRegDig2DigBeCntl = 0x15dbc; // DIG2_DIG_BE_CNTL: dcn10 link encoder state
const uint32_t kRegDig3DigBeCntl = 0x161bc; // DIG3_DIG_BE_CNTL: dcn10 link encoder state
const uint32_t kRegDig4DigBeCntl = 0x165bc; // DIG4_DIG_BE_CNTL: dcn10 link encoder state
const uint32_t kRegVmContext0PageTableBaseAddrLo32 = 0x69cac; // VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32: init_gart_aperture_regs
const uint32_t kRegVmContext0PageTableBaseAddrHi32 = 0x69cb0; // VM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32: init_gart_aperture_regs
const uint32_t kRegVmContext0PageTableStartAddrLo32 = 0x69d2c; // VM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32: init_gart_aperture_regs
const uint32_t kRegVmContext0PageTableStartAddrHi32 = 0x69d30; // VM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32: init_gart_aperture_regs
const uint32_t kRegVmContext0PageTableEndAddrLo32 = 0x69dac; // VM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32: init_gart_aperture_regs
const uint32_t kRegVmContext0PageTableEndAddrHi32 = 0x69db0; // VM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32: init_gart_aperture_regs
const uint32_t kRegVmL2ProtectionFaultDefaultAddrLo32 = 0x69a38; // VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_LO32: init_system_aperture_regs
const uint32_t kRegVmL2ProtectionFaultDefaultAddrHi32 = 0x69a3c; // VM_L2_PROTECTION_FAULT_DEFAULT_ADDR_HI32: init_system_aperture_regs
const uint32_t kRegVmL2ProtectionFaultCntl = 0x69a1c; // VM_L2_PROTECTION_FAULT_CNTL: mmhub_v1_0_set_fault_enable_default
const uint32_t kRegVmL2ProtectionFaultCntl2 = 0x69a20; // VM_L2_PROTECTION_FAULT_CNTL2: init_system_aperture_regs
const uint32_t kRegVmL2ProtectionFaultStatus = 0x69a2c; // VM_L2_PROTECTION_FAULT_STATUS: gmc_v9_0_process_interrupt
const uint32_t kRegVmL2Cntl2 = 0x69a04; // VM_L2_CNTL2: init_cache_regs
const uint32_t kRegVmL2Cntl3 = 0x69a08; // VM_L2_CNTL3: init_cache_regs
const uint32_t kRegVmL2Cntl4 = 0x69a5c; // VM_L2_CNTL4: init_cache_regs
const uint32_t kRegVmL2Context1IdentityApertureLowAddrLo32 = 0x69a44; // VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_LO32: disable_identity_aperture
const uint32_t kRegVmL2Context1IdentityApertureLowAddrHi32 = 0x69a48; // VM_L2_CONTEXT1_IDENTITY_APERTURE_LOW_ADDR_HI32: disable_identity_aperture
const uint32_t kRegVmL2Context1IdentityApertureHighAddrLo32 = 0x69a4c; // VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_LO32: disable_identity_aperture
const uint32_t kRegVmL2Context1IdentityApertureHighAddrHi32 = 0x69a50; // VM_L2_CONTEXT1_IDENTITY_APERTURE_HIGH_ADDR_HI32: disable_identity_aperture
const uint32_t kRegVmL2ContextIdentityPhysicalOffsetLo32 = 0x69a54; // VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_LO32: disable_identity_aperture
const uint32_t kRegVmL2ContextIdentityPhysicalOffsetHi32 = 0x69a58; // VM_L2_CONTEXT_IDENTITY_PHYSICAL_OFFSET_HI32: disable_identity_aperture
const uint32_t kRegVmInvalidateEng17Ack = 0x69c18; // VM_INVALIDATE_ENG17_ACK: gmc_v9_0_flush_gpu_tlb (engine 17 via vm_inv_eng0_ack + eng_distance)
const uint32_t kRegVmInvalidateEng0AddrRangeLo32 = 0x69c1c; // VM_INVALIDATE_ENG0_ADDR_RANGE_LO32: program_invalidation
const uint32_t kRegVmInvalidateEng0AddrRangeHi32 = 0x69c20; // VM_INVALIDATE_ENG0_ADDR_RANGE_HI32: program_invalidation
const uint32_t kRegIhRbBase = 0x04484; // IH_RB_BASE: vega10_ih_enable_ring
const uint32_t kRegIhRbBaseHi = 0x04488; // IH_RB_BASE_HI: vega10_ih_enable_ring
const uint32_t kRegIhRbWptr = 0x04490; // IH_RB_WPTR: enable_ring
const uint32_t kRegIhRbRptr = 0x0448c; // IH_RB_RPTR: enable_ring
const uint32_t kRegIhRbWptrAddrLo = 0x04498; // IH_RB_WPTR_ADDR_LO: enable_ring
const uint32_t kRegIhRbWptrAddrHi = 0x04494; // IH_RB_WPTR_ADDR_HI: enable_ring
const uint32_t kRegIhDoorbellRptr = 0x0449c; // IH_DOORBELL_RPTR: enable_ring
const uint32_t kRegIhChicken = 0x048b0; // IH_CHICKEN: vega10_ih_irq_init (Renoir)
const uint32_t kRegIhRbCntlRing1 = 0x044a0; // IH_RB_CNTL_RING1: toggle_interrupts
const uint32_t kRegIhRbCntlRing2 = 0x044c0; // IH_RB_CNTL_RING2: toggle_interrupts
const uint32_t kRegInterruptCntl = 0x03844; // INTERRUPT_CNTL: nbio_v7_0_ih_control
const uint32_t kRegInterruptCntl2 = 0x03848; // INTERRUPT_CNTL2: nbio_v7_0_ih_control
const uint32_t kRegBifIhDoorbellRange = 0x03bc8; // BIF_IH_DOORBELL_RANGE: nbio_v7_0_ih_doorbell_range
const uint32_t kDisplayInventory[] = {kRegOtg0OtgControl, kRegOtg0OtgHTotal, kRegOtg0OtgVTotal, kRegOtg0OtgHBlankStartEnd, kRegOtg0OtgVBlankStartEnd, kRegHubp0DchubpCntl, kRegHubp0DcsurfSurfaceConfig, kRegHubp0DcsurfPriViewportDimension, kRegHubpreq0DcsurfSurfacePitch, kRegHubpreq0DcsurfPrimarySurfaceAddress, kRegHubpreq0DcsurfPrimarySurfaceAddressHigh, kRegOtg1OtgControl, kRegOtg1OtgHTotal, kRegOtg1OtgVTotal, kRegOtg1OtgHBlankStartEnd, kRegOtg1OtgVBlankStartEnd, kRegHubp1DchubpCntl, kRegHubp1DcsurfSurfaceConfig, kRegHubp1DcsurfPriViewportDimension, kRegHubpreq1DcsurfSurfacePitch, kRegHubpreq1DcsurfPrimarySurfaceAddress, kRegHubpreq1DcsurfPrimarySurfaceAddressHigh, kRegOtg2OtgControl, kRegOtg2OtgHTotal, kRegOtg2OtgVTotal, kRegOtg2OtgHBlankStartEnd, kRegOtg2OtgVBlankStartEnd, kRegHubp2DchubpCntl, kRegHubp2DcsurfSurfaceConfig, kRegHubp2DcsurfPriViewportDimension, kRegHubpreq2DcsurfSurfacePitch, kRegHubpreq2DcsurfPrimarySurfaceAddress, kRegHubpreq2DcsurfPrimarySurfaceAddressHigh, kRegOtg3OtgControl, kRegOtg3OtgHTotal, kRegOtg3OtgVTotal, kRegOtg3OtgHBlankStartEnd, kRegOtg3OtgVBlankStartEnd, kRegHubp3DchubpCntl, kRegHubp3DcsurfSurfaceConfig, kRegHubp3DcsurfPriViewportDimension, kRegHubpreq3DcsurfSurfacePitch, kRegHubpreq3DcsurfPrimarySurfaceAddress, kRegHubpreq3DcsurfPrimarySurfaceAddressHigh, kRegDcnVmFbLocationBase, kRegDcnVmFbLocationTop, kRegDcnVmFbOffset, kRegDcnVmAgpBase, kRegDcnVmAgpBot, kRegDcnVmAgpTop, kRegDig0DigBeCntl, kRegDig1DigBeCntl, kRegDig2DigBeCntl, kRegDig3DigBeCntl, kRegDig4DigBeCntl};
const uint32_t kVmInventory[] = {kRegVmContext0PageTableBaseAddrLo32, kRegVmContext0PageTableBaseAddrHi32, kRegVmContext0PageTableStartAddrLo32, kRegVmContext0PageTableStartAddrHi32, kRegVmContext0PageTableEndAddrLo32, kRegVmContext0PageTableEndAddrHi32, kRegVmL2ProtectionFaultDefaultAddrLo32, kRegVmL2ProtectionFaultDefaultAddrHi32, kRegVmL2ProtectionFaultCntl, kRegVmL2ProtectionFaultCntl2, kRegVmL2ProtectionFaultStatus, kRegVmL2Cntl2, kRegVmL2Cntl3, kRegVmL2Cntl4, kRegVmL2Context1IdentityApertureLowAddrLo32, kRegVmL2Context1IdentityApertureLowAddrHi32, kRegVmL2Context1IdentityApertureHighAddrLo32, kRegVmL2Context1IdentityApertureHighAddrHi32, kRegVmL2ContextIdentityPhysicalOffsetLo32, kRegVmL2ContextIdentityPhysicalOffsetHi32, kRegVmInvalidateEng17Ack, kRegVmInvalidateEng0AddrRangeLo32, kRegVmInvalidateEng0AddrRangeHi32};
const uint32_t kIhInventory[] = {kRegIhRbBase, kRegIhRbBaseHi, kRegIhRbWptr, kRegIhRbRptr, kRegIhRbWptrAddrLo, kRegIhRbWptrAddrHi, kRegIhDoorbellRptr, kRegIhChicken, kRegIhRbCntlRing1, kRegIhRbCntlRing2, kRegInterruptCntl, kRegInterruptCntl2, kRegBifIhDoorbellRange};
const uint32_t kDisplayInventoryCount = sizeof(kDisplayInventory) / sizeof(kDisplayInventory[0]);
const uint32_t kVmInventoryCount = sizeof(kVmInventory) / sizeof(kVmInventory[0]);
const uint32_t kIhInventoryCount = sizeof(kIhInventory) / sizeof(kIhInventory[0]);
// Per pipe in kDisplayInventory: 5 OTG then 6 HUBP registers, 11 per pipe,
// 4 pipes, then DCN_VM (6) and DIG_BE_CNTL (5).
const uint32_t kDisplayPipeRegisters = 11, kDisplayPipes = 4;
const uint32_t kOtgMasterEn = 0x1;
const uint32_t kInventory16Stage = 16;
const uint32_t kStage16Registers[] = {kRegC2PMsg33, kRegConfigMemsize, kRegMcVmFbOffset, kRegGrbmStatus,
                                      kRegGrbmGfxIndex, kRegCcShaderArrayConfig, kRegUserShaderArrayConfig,
                                      kRegCcRbBackendDisable, kRegUserRbBackendDisable, kRegGbAddrConfig,
                                      kRegSmuioGfxMiscCntl, kRegMp1C2PMsg66, kRegMp1C2PMsg82,
                                      kRegMp1C2PMsg90, kRegMp0C2PMsg35, kRegMp0C2PMsg81,
                                      kRegRlcCgttMgcgOverride, kRegRlcCgcgCglsCtrl,
                                      kRegRlcCgcgCglsCtrl3d, kRegRlcMemSlpCntl, kRegCpMemSlpCntl,
                                      kRegRlcPgCntl, kRegGrbmStatus2, kRegGrbmStatusSe0,
                                      kRegCpBusyStat, kRegCpCpfStatus, kRegCpMeCntl, kRegCpMecCntl,
                                      kRegRlcCntl, kRegRlcStat, kRegCpPfpInstrPntr, kRegCpMeInstrPntr,
                                      kRegCpMec1InstrPntr, kRegSdma0ClkCtrl, kRegSdma0PowerCntl,
                                      kRegSdma0F32Cntl, kRegSdma0StatusReg, kRegSdma0GfxRbCntl,
                                      kRegHdpMemPowerLs, kRegAthubMiscCntl, kRegAtcL2MiscCg,
                                      kRegDagb0CntlMisc2, kRegMmhubFbLocationBase, kRegMmhubFbLocationTop,
                                      kRegMmhubVmL2Cntl, kRegMmhubVmContext0Cntl, kRegMmhubMxL1TlbCntl,
                                      kRegIhRbCntl, kRegScratchReg0, kRegMp0C2PMsg36, kRegMp0C2PMsg64,
                                      kRegMp0C2PMsg67, kRegMp0C2PMsg69, kRegMp0C2PMsg70, kRegMp0C2PMsg71,
                                      kRegMmhubFbOffset, kRegMmhubDefaultAddrLsb, kRegMmhubDefaultAddrMsb,
                                      kRegMmhubAgpTop, kRegMmhubAgpBot, kRegMmhubAgpBase,
                                      kRegMmhubApertureLow, kRegMmhubApertureHigh, kRegGcFbLocationBase,
                                      kRegGcFbLocationTop, kRegGcAgpTop, kRegGcAgpBot, kRegGcAgpBase,
                                      kRegGcApertureLow, kRegGcApertureHigh, kRegSdma0UcodeChecksum,
                                      kRegSdma0Cntl, kRegSdma0ChickenBits, kRegSdma0GbAddrConfig, kRegSdma0GbAddrConfigRead, kRegSdma0SemWaitFailTimerCntl, kRegSdma0Utcl1Watermk, kRegSdma0Utcl1Timeout, kRegSdma0Utcl1Page, kRegSdma0GfxRbBase, kRegSdma0GfxRbBaseHi, kRegSdma0GfxRbRptr, kRegSdma0GfxRbRptrHi, kRegSdma0GfxRbWptr, kRegSdma0GfxRbWptrHi, kRegSdma0GfxRbWptrPollCntl, kRegSdma0GfxRbRptrAddrHi, kRegSdma0GfxRbRptrAddrLo, kRegSdma0GfxIbCntl, kRegSdma0GfxDoorbell, kRegSdma0GfxDoorbellOffset, kRegSdma0GfxRbWptrPollAddrHi, kRegSdma0GfxRbWptrPollAddrLo, kRegSdma0GfxMinorPtrUpdate, kRegSdma0Rlc0RbWptrPollCntl, kRegSdma0Rlc1RbWptrPollCntl,
                                      kRegOtg0OtgControl,
                                      kRegOtg0OtgHTotal,
                                      kRegOtg0OtgVTotal,
                                      kRegOtg0OtgHBlankStartEnd,
                                      kRegOtg0OtgVBlankStartEnd,
                                      kRegHubp0DchubpCntl,
                                      kRegHubp0DcsurfSurfaceConfig,
                                      kRegHubp0DcsurfPriViewportDimension,
                                      kRegHubpreq0DcsurfSurfacePitch,
                                      kRegHubpreq0DcsurfPrimarySurfaceAddress,
                                      kRegHubpreq0DcsurfPrimarySurfaceAddressHigh,
                                      kRegOtg1OtgControl,
                                      kRegOtg1OtgHTotal,
                                      kRegOtg1OtgVTotal,
                                      kRegOtg1OtgHBlankStartEnd,
                                      kRegOtg1OtgVBlankStartEnd,
                                      kRegHubp1DchubpCntl,
                                      kRegHubp1DcsurfSurfaceConfig,
                                      kRegHubp1DcsurfPriViewportDimension,
                                      kRegHubpreq1DcsurfSurfacePitch,
                                      kRegHubpreq1DcsurfPrimarySurfaceAddress,
                                      kRegHubpreq1DcsurfPrimarySurfaceAddressHigh,
                                      kRegOtg2OtgControl,
                                      kRegOtg2OtgHTotal,
                                      kRegOtg2OtgVTotal,
                                      kRegOtg2OtgHBlankStartEnd,
                                      kRegOtg2OtgVBlankStartEnd,
                                      kRegHubp2DchubpCntl,
                                      kRegHubp2DcsurfSurfaceConfig,
                                      kRegHubp2DcsurfPriViewportDimension,
                                      kRegHubpreq2DcsurfSurfacePitch,
                                      kRegHubpreq2DcsurfPrimarySurfaceAddress,
                                      kRegHubpreq2DcsurfPrimarySurfaceAddressHigh,
                                      kRegOtg3OtgControl,
                                      kRegOtg3OtgHTotal,
                                      kRegOtg3OtgVTotal,
                                      kRegOtg3OtgHBlankStartEnd,
                                      kRegOtg3OtgVBlankStartEnd,
                                      kRegHubp3DchubpCntl,
                                      kRegHubp3DcsurfSurfaceConfig,
                                      kRegHubp3DcsurfPriViewportDimension,
                                      kRegHubpreq3DcsurfSurfacePitch,
                                      kRegHubpreq3DcsurfPrimarySurfaceAddress,
                                      kRegHubpreq3DcsurfPrimarySurfaceAddressHigh,
                                      kRegDcnVmFbLocationBase,
                                      kRegDcnVmFbLocationTop,
                                      kRegDcnVmFbOffset,
                                      kRegDcnVmAgpBase,
                                      kRegDcnVmAgpBot,
                                      kRegDcnVmAgpTop,
                                      kRegDig0DigBeCntl,
                                      kRegDig1DigBeCntl,
                                      kRegDig2DigBeCntl,
                                      kRegDig3DigBeCntl,
                                      kRegDig4DigBeCntl,
                                      kRegVmContext0PageTableBaseAddrLo32,
                                      kRegVmContext0PageTableBaseAddrHi32,
                                      kRegVmContext0PageTableStartAddrLo32,
                                      kRegVmContext0PageTableStartAddrHi32,
                                      kRegVmContext0PageTableEndAddrLo32,
                                      kRegVmContext0PageTableEndAddrHi32,
                                      kRegVmL2ProtectionFaultDefaultAddrLo32,
                                      kRegVmL2ProtectionFaultDefaultAddrHi32,
                                      kRegVmL2ProtectionFaultCntl,
                                      kRegVmL2ProtectionFaultCntl2,
                                      kRegVmL2ProtectionFaultStatus,
                                      kRegVmL2Cntl2,
                                      kRegVmL2Cntl3,
                                      kRegVmL2Cntl4,
                                      kRegVmL2Context1IdentityApertureLowAddrLo32,
                                      kRegVmL2Context1IdentityApertureLowAddrHi32,
                                      kRegVmL2Context1IdentityApertureHighAddrLo32,
                                      kRegVmL2Context1IdentityApertureHighAddrHi32,
                                      kRegVmL2ContextIdentityPhysicalOffsetLo32,
                                      kRegVmL2ContextIdentityPhysicalOffsetHi32,
                                      kRegVmInvalidateEng17Ack,
                                      kRegVmInvalidateEng0AddrRangeLo32,
                                      kRegVmInvalidateEng0AddrRangeHi32,
                                      kRegIhRbBase,
                                      kRegIhRbBaseHi,
                                      kRegIhRbWptr,
                                      kRegIhRbRptr,
                                      kRegIhRbWptrAddrLo,
                                      kRegIhRbWptrAddrHi,
                                      kRegIhDoorbellRptr,
                                      kRegIhChicken,
                                      kRegIhRbCntlRing1,
                                      kRegIhRbCntlRing2,
                                      kRegInterruptCntl,
                                      kRegInterruptCntl2,
                                      kRegBifIhDoorbellRange};
const uint32_t kStage16RegisterCount = sizeof(kStage16Registers) / sizeof(kStage16Registers[0]);

// Stage 15: the first SDMA copy. Values are what Linux v6.12 computes from
// the registers boot 19 measured; the check requires those values first.
const uint32_t kSdmaCopyStage = 15;
// Boot 19, in kSdmaInventory order; STATUS_REG (index 3) is not pinned.
const uint32_t kSdmaBoot19[] = {0x00000001, 0xdf000100, 0x40000050, 0x46dee557, 0x00040000, 0x25a1ba79,
                                0x00000002, 0x00831f07, 0x00100012, 0x00100012, 0x00000000, 0xfffbe1fe,
                                0x00010001, 0x000003e0, 0,          0,          0,          0,
                                0,          0,          0x00401000, 0,          0,          0x00000100,
                                0,          0,          0,          0,          0,          0x00401000,
                                0x00401000};
const uint32_t kSdmaStatusIndex = 3;
// The copy work area: ring, write-back, source and destination pages.
const uint64_t kSdmaWorkCarveoutOffset = 0x40300000ull;
const uint64_t kSdmaWorkGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kSdmaWorkCarveoutOffset;
const uint64_t kSdmaWorkPhysical = (uint64_t(kExpectedFbOffset) << 24) + kSdmaWorkCarveoutOffset;
const uint32_t kSdmaWorkSize = 0x4000;
const uint32_t kSdmaWorkCheckSize = 0x10000;
const uint32_t kSdmaWbPage = 0x1000, kSdmaSrcPage = 0x2000, kSdmaDstPage = 0x3000;
const uint32_t kSdmaWbRptr = 0x000, kSdmaWbPoll = 0x008, kSdmaWbTest = 0x100, kSdmaWbFence = 0x200;
const uint32_t kSdmaTestValue = 0xDEADBEEF; // sdma_v4_0_ring_test_ring
const uint32_t kSdmaCopyBytes = 0x1000;
const uint32_t kSdmaRingDwords = 1024; // RB_SIZE 10
const uint32_t kSdmaFrameDwords = 256; // align_mask 0xff
const uint32_t kSdmaSourceBase = 0x5A5A0000;
// vega10_sdma_pkt_open.h opcodes (sub-op 0: linear).
const uint32_t kSdmaOpNop = 0, kSdmaOpCopy = 1, kSdmaOpWrite = 2, kSdmaOpFence = 5;
const uint32_t kSdmaPollPauses = 100; // adev->usec_timeout, 100 ms
struct SdmaWrite {
    uint32_t offset, value;
};
// golden_settings_sdma_4_3 results from boot 19, in Linux's order.
const SdmaWrite kSdmaGolden[] = {
    {kRegSdma0ChickenBits, 0x02831f07},        {kRegSdma0ClkCtrl, 0x3f000100},
    {kRegSdma0GbAddrConfig, 0x00000002},       {kRegSdma0GbAddrConfigRead, 0x00000002},
    {kRegSdma0GfxRbWptrPollCntl, 0x00403000},  {kRegSdma0PowerCntl, 0x40000051},
    {kRegSdma0Rlc0RbWptrPollCntl, 0x00403000}, {kRegSdma0Rlc1RbWptrPollCntl, 0x00403000},
    {kRegSdma0Utcl1Page, 0x000003e0},          {kRegSdma0Utcl1Watermk, 0x03fbe1fe},
};
// sdma_v4_0_start / sdma_v4_0_gfx_resume for one ring without a doorbell.
const SdmaWrite kSdmaStart[] = {
    {kRegSdma0F32Cntl, 0x00000000},
    {kRegSdma0SemWaitFailTimerCntl, 0},
    {kRegSdma0GfxRbCntl, 0x00040014},
    {kRegSdma0GfxRbRptr, 0},
    {kRegSdma0GfxRbRptrHi, 0},
    {kRegSdma0GfxRbWptr, 0},
    {kRegSdma0GfxRbWptrHi, 0},
    {kRegSdma0GfxRbRptrAddrHi, uint32_t((kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbRptr) >> 32)},
    {kRegSdma0GfxRbRptrAddrLo, uint32_t(kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbRptr)},
    {kRegSdma0GfxRbBase, uint32_t(kSdmaWorkGpuAddress >> 8)},
    {kRegSdma0GfxRbBaseHi, uint32_t(kSdmaWorkGpuAddress >> 40)},
    {kRegSdma0GfxMinorPtrUpdate, 1},
    {kRegSdma0GfxDoorbell, 0},
    {kRegSdma0GfxDoorbellOffset, 0},
    {kRegSdma0GfxRbWptr, 0},
    {kRegSdma0GfxRbWptrHi, 0},
    {kRegSdma0GfxMinorPtrUpdate, 0},
    {kRegSdma0GfxRbWptrPollAddrLo, uint32_t(kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbPoll)},
    {kRegSdma0GfxRbWptrPollAddrHi, uint32_t((kSdmaWorkGpuAddress + kSdmaWbPage + kSdmaWbPoll) >> 32)},
    {kRegSdma0GfxRbWptrPollCntl, 0x00403000},
    {kRegSdma0GfxRbCntl, 0x00041015},
    {kRegSdma0GfxIbCntl, 0x00000101},
    {kRegSdma0Cntl, 0x00000002},
    {kRegSdma0F32Cntl, 0x00000000},
};
// The BAR5 pages the stage 15 SDMA operations may write: the SDMA0
// registers (two pages) and the SMU mailbox (PowerUpSdma/PowerDownSdma).
// The adapter maps these three for kSdmaPageSet and no others.
const uint32_t kSdmaPageSet = 0x4000;
const uint32_t kSdmaPages[] = {0x4000, 0x5000, 0x58000};
// sdma_v4_0_hw_fini: gfx_enable(false), then halt.
const SdmaWrite kSdmaStop[] = {
    {kRegSdma0GfxRbCntl, 0x00041014},
    {kRegSdma0GfxIbCntl, 0x00000100},
    {kRegSdma0F32Cntl, 0x00000001},
};

// The IP discovery binary sits DISCOVERY_TMR_OFFSET below the top of VRAM and
// is DISCOVERY_TMR_SIZE long (amdgpu_discovery.h, v6.12).
const uint32_t kDiscoveryTmrOffset = 64 << 10;
const uint32_t kDiscoveryTmrSize = 10 << 10;
// Linux reads below 2^48 only; Zen 3 physical addresses are 48 bits.
const uint64_t kPhysicalLimit = 1ull << 48;

// Renoir IP bases (renoir_ip_offset.h) the table must confirm: GC segments 0
// and 1, which locate kRegMcVmFbOffset, and MP0 segment 0, which locates
// kRegC2PMsg33 (0x16000 + 0x61).
const uint32_t kExpectedGcBase0 = 0x2000, kExpectedGcBase1 = 0xA000, kExpectedMp0Base0 = 0x16000;
const uint16_t kHwIdGc = 11, kHwIdMp0 = 255; // soc15_hw_ip.h

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
    // Stage 2.
    kCarveoutInvalid,
    kDeviceRangesMalformed,
    kCarveoutOverlapsDevice,
    kTableUnavailable,
    kDiscoverySignature,
    kDiscoveryChecksum,
    kDiscoveryMalformed,
    kDiscoveryUnsupported,
    kDiscoveryBaseMismatch,
    // Stage 3.
    kGcInfoUnavailable,
    kGfxIndexNotSe0Sh0,
    // Stage 5.
    kGfxNotOn,
    // Stage 6.
    kCpNotHalted,
    kRlcEnabled,
    kGfxBusy,
    kScratchUnstable,
    kRegisterWriteFailed,
    kScratchReadbackMismatch,
    kScratchRestoreMismatch,
    kScratchOutOfOrder,
    // Stage 7.
    kSmuBusy,
    kSmuTimeout,
    kSmuResponseNotOk,
    kSmuOutOfOrder,
    // Stage 8.
    kGfxOffTimeout,
    // Stage 9.
    kMetricsAddressMismatch,
    kMetricsTargetInvalid,
    kTableRegionInUse,
    kTableNotWritten,
    kTableOverflow,
    kMetricsOutOfOrder,
    // Stage 11.
    kPspNotRunning,
    kPspNotReady,
    kPspRingExists,
    kPspTimeout,
    kPspResponseNotOk,
    kPspRegionChanged,
    kPspOutOfOrder,
    // Stage 12.
    kPspReadbackMismatch,
    kPspFenceTimeout,
    kPspCommandFailed,
    // Stage 13.
    kSdmaImageInvalid,
    kSdmaNotHalted,
    // Stage 14.
    kSdmaOutOfOrder,
    // Stage 15.
    kSdmaUnexpectedState,
    kSdmaTimeout,
    kSdmaVerifyFailed,
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

// Whether a stage may read a register: stage 1 its two, stage 2 also
// FB_OFFSET, stage 3 and later also the GC configuration registers.
bool registerAllowed(uint32_t offset, uint32_t stage);

// Reads one register a stage allows, refusing unaligned offsets and offsets
// beyond the mapping.
Status readAllowedRegister(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           uint32_t offset, uint32_t *value);

bool gfxGated(uint32_t offset);

// Writes one 32-bit register at a byte offset into BAR5; pause waits about
// 1 ms. Only the stage 6 scratch test uses it.
struct RegisterWriter {
    bool (*write32)(void *context, uint32_t offset, uint32_t value);
    void (*pause)(void *context);
    void *context;
};

// The write allowlist, by register and value: SCRATCH_REG0 (any value; the
// scratch test writes only the pattern and the value it read) from stage 6;
// from stage 7, C2PMSG_90 <- 0, C2PMSG_82 <- 0 and C2PMSG_66 <- 0x2 or 0x3;
// from stage 8 also C2PMSG_66 <- 0x8; from stage 9 also C2PMSG_66 <- 0x1A,
// 0x1B, 0x1C and C2PMSG_82 <- the high and low halves of kMetricsGpuAddress
// and kTableSmuMetrics. Pairs of message and argument are checked by
// smuArgumentAllowed.
bool smuArgumentAllowed(uint32_t message, uint32_t argument, uint32_t stage);
bool writeAllowed(uint32_t offset, uint32_t value, uint32_t stage);

struct ScratchCheck {
    uint32_t gfxMisc, cpMeCntl, cpMecCntl, rlcCntl, grbmStatus;
    uint32_t original, original2; // read twice, ~1 ms apart
};

// Reads the preconditions (GFX on, every CP engine halted, RLC off, GUI idle)
// and SCRATCH_REG0 twice; kScratchUnstable if the two reads differ. No write.
Status checkScratch(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    uint32_t stage, ScratchCheck *check);

// Re-runs checkScratch, requires the same original value, then writes
// kScratchPattern and reads it back (kScratchReadbackMismatch otherwise).
Status writeScratchPattern(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                           uint32_t stage, uint32_t original, uint32_t *readback);

// Writes the original value back and reads it back (kScratchRestoreMismatch).
Status restoreScratch(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t original, uint32_t *readback);

struct SmuMailbox {
    uint32_t message, argument, response; // C2PMSG_66, _82, _90
};

// Reads the mailbox; kSmuBusy if the response register is 0 (a message is in
// flight). No write.
Status checkSmu(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, SmuMailbox *mailbox);

// Sends one version query (kSmuMsgGetSmuVersion or kSmuMsgGetDriverIfVersion,
// argument 0) as smu_cmn does: requires an idle mailbox, writes response <- 0,
// argument <- 0, message <- index, polls the response (kSmuTimeout after
// kSmuPollPauses), and reads the answer only after an OK response
// (kSmuResponseNotOk otherwise).
Status sendSmuQuery(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    uint32_t stage, uint32_t message, uint32_t *response, uint32_t *answer);

// Sends DisableGfxOff the same way (no answer is read, as Linux passes no
// read_arg), then polls PWR_GFXOFF_STATUS until 2 (GFX on), at most
// kGfxOffConfirmPauses pauses (kGfxOffTimeout). gfxMisc is the last
// SMUIO_GFX_MISC_CNTL value read.
Status disallowGfxOff(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t *response, uint32_t *gfxMisc);


// The diagnostic interface's read (stage 4 and later): readAllowedRegister,
// but a GFX-gated register is read only after SMUIO_GFX_MISC_CNTL reports GFX
// on (kGfxNotOn otherwise), as Linux requires before its GC IP dump.
Status readDiagnosticRegister(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                              uint32_t offset, uint32_t *value);

// Diagnostic interface (IOUserClient selectors and their scalars).
const uint32_t kDiagnosticVersion = 12;
enum DiagnosticSelector : uint32_t {
    kDiagnosticGetInfo = 0,       // out: version, stage
    kDiagnosticReadRegister = 1,  // in: offset; out: Status, value
    // Stage 6 scratch test, accepted only in this order per connection.
    kDiagnosticScratchCheck = 2,   // out: Status, GFX misc, CP_ME, CP_MEC, RLC, GRBM, original
    kDiagnosticScratchWrite = 3,   // out: Status, read-back
    kDiagnosticScratchRestore = 4, // out: Status, read-back
    // Stage 7 SMU queries; a query is accepted only after a passing check.
    kDiagnosticSmuCheck = 5,       // out: Status, message, argument, response
    kDiagnosticSmuQuery = 6,       // in: message (0x2 or 0x3); out: Status, response, answer
    // Stage 8; also accepted only after a passing SMU check.
    kDiagnosticGfxOffDisallow = 7, // out: Status, response, SMUIO_GFX_MISC_CNTL
    // Stage 9, accepted only in this order per connection.
    kDiagnosticMetricsCheck = 8,    // out: Status, FB location base, FB offset, GPU address, physical
    kDiagnosticMetricsTransfer = 9, // out: Status, responses to AddrHigh, AddrLow, Transfer
    kDiagnosticMetricsRead = 10,    // out: Status; structure: the 148-byte table
    // Stage 11, accepted only in this order per connection.
    kDiagnosticPspRingCheck = 11,   // out: Status, C2PMSG_81, 64, 67, 69, 70, 71
    kDiagnosticPspRingCreate = 12,  // out: Status, response, command written
    kDiagnosticPspRingObserve = 13, // out: Status, C2PMSG_64, 67, 69, 70, 71, changed in page, changed outside
    kDiagnosticPspRingDestroy = 14, // out: Status, response, C2PMSG_64, 67, 69, 70, 71
    // Stage 12, after a passing check and create (selectors 11 and 12).
    kDiagnosticPspTmrSubmit = 15,   // out: Status, fence, response status, fw_addr lo, hi, tmr_size, C2PMSG_67
    kDiagnosticPspTmrObserve = 16,  // out: Status, unexpected words, first unexpected work-area offset
    kDiagnosticPspTmrTeardown = 17, // out: Status, DESTROY_TMR fence, its status, ring response, C2PMSG_64, 67
    // Stage 13, after a SETUP_TMR submit (selector 15) that fenced with status 0.
    kDiagnosticSdmaLoad = 18,    // out: Status, fence, response status, fw_addr lo, hi, C2PMSG_67
    kDiagnosticSdmaObserve = 19, // out: Status, work unexpected, first; firmware unexpected, first; checksum, F32_CNTL
    // Stage 14, after a LOAD_IP_FW that fenced (selector 18).
    kDiagnosticSdmaInventory = 20, // out: Status, PowerUpSdma response, PowerDownSdma response;
                                   // structure: SdmaInventory
    // Stage 15, after a LOAD_IP_FW that fenced (selector 18), in this order.
    kDiagnosticSdmaCopyCheck = 21, // out: Status, index of the first differing register, its value
    kDiagnosticSdmaStart = 22,     // out: Status, progress, PowerUpSdma response
    kDiagnosticSdmaSubmit = 23,    // in: frame (0 ring test, 1 copy); out: Status, observed, GFX_RB_RPTR,
                                   // GFX_RB_WPTR, F32_CNTL, STATUS_REG
    kDiagnosticSdmaVerify = 24,    // out: Status, GFX_RB_RPTR, unexpected words, first offset, STATUS_REG
    kDiagnosticSdmaStop = 25,      // out: Status, F32_CNTL, PowerDownSdma response, DESTROY_TMR fence, ring response
    kDiagnosticSelectorCount = 26,
};
const uint32_t kScratchStage = 6;
const uint32_t kDiagnosticStage = 4; // first stage that offers the interface

struct BootState {
    uint32_t c2pmsg33;
    uint32_t configMemsize;
    bool ifwiReady;
    uint64_t vramBytes; // configMemsize << 20, as Linux computes it
};

// Reads the stage 1 registers in a fixed order through the reader.
Status readBootState(const RegisterReader &registers, uint64_t apertureLength, BootState *state);

struct Carveout {
    uint32_t fbOffset; // raw MC_VM_FB_OFFSET
    uint64_t base;     // system physical address of VRAM offset 0
    uint64_t size;     // BootState::vramBytes
    uint64_t table;    // physical address of the discovery binary
};

// Reads MC_VM_FB_OFFSET (stage 2) and derives the carveout and table address.
Status readCarveout(const RegisterReader &registers, uint64_t apertureLength, const BootState &boot,
                    Carveout *carveout);

struct Range {
    uint64_t base;
    uint64_t length;
};

// Parses the memory ranges of an IOPCIDevice "assigned-addresses" property
// (5 little-endian 32-bit cells per entry); I/O ranges are skipped.
Status parseAssignedAddresses(const uint8_t *data, uint32_t length, Range *ranges, uint32_t capacity,
                              uint32_t *count);

// The carveout must not overlap any of the device's memory BARs.
Status checkCarveout(const Carveout &carveout, const Range *ranges, uint32_t count);

struct Discovery {
    bool binaryValid; // signature and binary checksum passed
    uint16_t versionMajor, versionMinor, binarySize;
    uint16_t tableVersion, numDies, numIps;
    bool gcFound, mp0Found;
    uint8_t gcMajor, gcMinor, gcRevision;
    uint32_t gcBase0, gcBase1, mp0Base0;
    // GC info table (gc_info_v2_x only; gfx9 parts use version 2).
    bool gcInfoFound;
    uint16_t gcInfoMajor, gcInfoMinor;
    uint32_t gcNumSe, gcCuPerSh, gcShPerSe, gcRbPerSe;
};

// Validates the binary as amdgpu_discovery_init does (signatures and byte-sum
// checksums), walks every die's IP list with bounds checks, and records GC and
// MP0 instance 0. Then requires their bases to match the Renoir constants.
Status parseDiscovery(const uint8_t *binary, uint32_t length, Discovery *discovery);

struct GfxConfig {
    uint32_t grbmStatus, grbmGfxIndex, gbAddrConfig;
    uint32_t ccShaderArrayConfig, userShaderArrayConfig, ccRbBackendDisable, userRbBackendDisable;
    bool guiActive;
    // As gfx_v9_0_get_cu_active_bitmap and _get_rb_active_bitmap compute them
    // for SE 0 / SH 0; valid only when the status is kOK.
    uint32_t cuActiveMask, rbActiveMask, cuActiveCount, rbActiveCount;
};

// Reads the stage 3 registers in a fixed order. Linux selects SE 0 / SH 0 by
// writing GRBM_GFX_INDEX first; this never writes, so the masks are computed
// only if the index already selects SE 0 / SH 0 and the GC info table reports
// one SE with one SH (kGfxIndexNotSe0Sh0 / kGcInfoUnavailable otherwise; the raw
// values are still filled in).
Status readGfxConfig(const RegisterReader &registers, uint64_t apertureLength, const Discovery &discovery,
                     GfxConfig *config);

struct MetricsTarget {
    uint32_t fbLocationBase, fbOffset, configMemsize;
    uint64_t gpuAddress, physical;
};

// Reads MMHUB MC_VM_FB_LOCATION_BASE, MC_VM_FB_OFFSET and RCC_CONFIG_MEMSIZE,
// requires the expected values (kMetricsAddressMismatch), and checks the page
// and its check region lie inside the carveout, outside the reserved low and
// high regions and outside every device range (kMetricsTargetInvalid).
Status checkMetricsTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                          const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// Reads 32-bit words at byte offsets of a read-only memory mapping.
struct MemoryReader {
    bool (*read32)(void *context, uint32_t offset, uint32_t *value);
    void *context;
};

// Reads the region into snapshot (length / 4 words), waits pauses pauses, and
// requires a second read to match word for word (kTableRegionInUse).
Status checkRegionStable(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer,
                         uint32_t pauses, uint32_t *snapshot);

// Sends SetDriverDramAddrHigh, SetDriverDramAddrLow and TransferTableSmu2Dram
// in that order with the stage 7 send-and-poll; stops at the first failure.
Status requestMetrics(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, uint32_t responses[3]);

struct SmuMetrics {
    uint16_t words[kMetricsWordCount];
};

// Checks a page read after the transfer against the snapshot taken before it:
// bytes kMetricsSize.. must be unchanged (kTableOverflow), and the table bytes
// must differ somewhere (kTableNotWritten). Decodes the table.
Status verifyMetricsPage(const MemoryReader &page, const uint32_t *snapshot, SmuMetrics *metrics);

// Stage 11. The PSP mailbox registers stage 10 reads.
struct PspMailbox {
    uint32_t signOfLife;   // C2PMSG_81
    uint32_t command;      // C2PMSG_64
    uint32_t writePointer; // C2PMSG_67
    uint32_t ringLow, ringHigh, ringSize; // C2PMSG_69, _70, _71
};

Status readPspMailbox(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                      PspMailbox *mailbox);

// Each PSP command with its only allowed arguments (C2PMSG_69, _70, _71; 0
// where Linux writes none): the create with the ring's address and size, the
// destroy with none.
bool pspCommandAllowed(uint32_t command, uint32_t low, uint32_t high, uint32_t size, uint32_t stage);

// No writes. Requires the secure OS running (kPspNotRunning), C2PMSG_64
// answered with status 0 (kPspNotReady) and no ring (kPspRingExists), then
// the stage 9 address checks for the ring page and its check region.
Status checkPspRing(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, const Range *ranges,
                    uint32_t rangeCount, PspMailbox *mailbox, MetricsTarget *target);

// Re-checks the mailbox as checkPspRing does, then sends the create: the
// ring's address and size, then the command; kPspSettlePauses, then up to
// kPspPollPauses polls for the response flag (kPspTimeout). The response,
// masked with kPspResponseMask, must be kPspResponseFlag
// (kPspResponseNotOk). written is set once the command is written, whatever
// the response: from then on a ring may exist and must be destroyed.
Status createPspRing(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                     uint32_t stage, uint32_t *response, bool *written);

// Compares the region with the snapshot: counts changed words in the first
// pageSize bytes and after them. kPspRegionChanged if any changed.
Status compareRegion(const MemoryReader &memory, uint32_t length, uint32_t pageSize, const uint32_t *snapshot,
                     uint32_t *changedInPage, uint32_t *changedOutside);

// Sends DESTROY_RINGS, only if created (kPspOutOfOrder otherwise), with the
// same settle, poll and response checks.
Status destroyPspRing(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      uint32_t stage, bool created, uint32_t *response);

// Stage 12. Writes 32-bit words at byte offsets of the work area's writable
// mapping.
struct MemoryWriter {
    bool (*write32)(void *context, uint32_t offset, uint32_t value);
    void *context;
};

// The exact words the driver writes: a command buffer (SETUP_TMR or
// DESTROY_TMR; 0 beyond cmd_id and the fields) and ring frames 0 (SETUP_TMR,
// fence value 1) and 1 (DESTROY_TMR, fence value 2). word is the dword index.
uint32_t pspCommandWord(uint32_t command, uint32_t word);
uint32_t pspFrameWord(uint32_t frame, uint32_t word);

// Memory writes allowed in the work area from stage 12: 0 anywhere in the
// command and fence pages and in frames 0 and 1, or a frame's or either
// command's word at its own offset.
bool pspWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage);

// The TMR region and its placement: the stage 9 page checks for 4 MiB at
// kPspTmrCarveoutOffset. No memory access.
Status checkPspTmrTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                         const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// Sums the region twice, pauses pauses apart (kTableRegionInUse if the sums
// differ). For the TMR, which is too large to snapshot.
Status checkRegionChecksum(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer,
                           uint32_t pauses);

// Writes a command into the work area as psp_cmd_submit_buf and
// psp_ring_cmd_submit do: the whole command page (zeros and the command), the
// fence page zeroed (SETUP_TMR only), and its frame; then reads every written
// word back (kPspReadbackMismatch). Allowed pairs: SETUP_TMR in frame 0,
// DESTROY_TMR in frame 1 (stage 12) or 2 (stage 13), LOAD_IP_FW in frame 1
// (stage 13).
Status writePspCommand(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage, uint32_t command,
                       uint32_t frame);

// Advances C2PMSG_67 from frame * 16 (kPspOutOfOrder otherwise) to
// (frame + 1) * 16 and polls the fence dword for frame + 1, up to
// kPspFencePollPauses pauses (kPspFenceTimeout). Frames 0 and 1 from stage
// 12, frame 2 from stage 13.
Status submitPspFrame(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                      const MemoryReader &work, uint32_t stage, uint32_t frame, uint32_t *fence);

struct PspResponse {
    uint32_t status, fwAddrLo, fwAddrHi, tmrSize; // psp_gfx_resp
};

// Reads psp_gfx_resp from the command page; kPspCommandFailed if status != 0.
Status readPspResponse(const MemoryReader &work, PspResponse *response);

// Compares the 64 KiB from the ring page with the snapshot taken before the
// create: frames below frames and the command page must hold the driver's
// words (the response area may hold anything), the fence dword must be
// fence, the rest of the fence page 0, and every other word unchanged.
// Counts the words that differ (kPspRegionChanged if any).
Status verifyPspWorkArea(const MemoryReader &region, const uint32_t *snapshot, uint32_t frames, uint32_t command,
                         uint32_t fence, uint32_t *unexpected, uint32_t *firstOffset);

// Stage 13. Checks the embedded SDMA image's common_firmware_header against
// the pinned values (kSdmaImageInvalid); the payload is not interpreted.
Status checkSdmaImage(const uint8_t *image, uint32_t length);

// The firmware buffer's word at a byte offset: the image's ucode bytes, then
// zero padding to kSdmaFwBufferSize.
uint32_t sdmaFirmwareWord(const uint8_t *image, uint32_t offset);

// Firmware-buffer writes allowed from stage 13: inside the buffer, only its
// word at that offset.
bool sdmaFirmwareWriteAllowed(const uint8_t *image, uint32_t offset, uint32_t value, uint32_t stage);

// The firmware buffer and its check region: the stage 9 page checks.
Status checkSdmaFirmwareTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                               const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// Writes the whole firmware buffer (checkSdmaImage first), then reads every
// word back (kPspReadbackMismatch).
Status writeSdmaFirmware(const uint8_t *image, uint32_t length, const MemoryReader &buffer,
                         const MemoryWriter &writer, uint32_t stage);

// Compares the 64 KiB from the firmware buffer: the buffer must hold the
// image's words, the rest the snapshot. kPspRegionChanged if any differ.
Status verifySdmaFirmwareRegion(const MemoryReader &region, const uint32_t *snapshot, const uint8_t *image,
                                uint32_t *unexpected, uint32_t *firstOffset);

// Stage 14. Three readings of kSdmaInventory: loaded (gated), after
// PowerUpSdma, after PowerDownSdma.
struct SdmaInventory {
    uint32_t loaded[kSdmaInventoryCount], powered[kSdmaInventoryCount], gated[kSdmaInventoryCount];
};

// Reads kSdmaInventory in order into values.
Status readSdmaInventory(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *values);

// Reads, sends PowerUpSdma, reads, sends PowerDownSdma, reads (the stage 7
// send-and-poll for each message, argument 0). PowerDownSdma is sent
// whenever PowerUpSdma was answered OK, even if the second reading fails.
Status runSdmaInventory(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                        uint32_t stage, SdmaInventory *inventory, uint32_t *upResponse, uint32_t *downResponse);

// Stage 15. Whether a register write is in kSdmaGolden, kSdmaStart or
// kSdmaStop, or a write-pointer value of a frame (1024 or 2048 bytes).
bool sdmaWriteListed(uint32_t offset, uint32_t value);

// The SDMA ring's dword at an index: frame 0 (WRITE_LINEAR test), frame 1
// (COPY_LINEAR and FENCE), plain NOPs elsewhere.
uint32_t sdmaRingWord(uint32_t dword);

// The work area's word at a byte offset: the ring, a zero write-back page,
// the source pattern, a zero destination.
uint32_t sdmaWorkWord(uint32_t offset);

// Work-area writes allowed from stage 15: only its own word.
bool sdmaWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage);

// The work area and its check region: the stage 9 page checks.
Status checkSdmaWorkTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// Requires every kSdmaInventory register except STATUS_REG to read its boot
// 19 value (kSdmaUnexpectedState, with the first index and value).
Status checkSdmaBoot19(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value);

// Writes the whole work area, then reads every word back (kPspReadbackMismatch).
Status writeSdmaWork(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// PowerUpSdma, then kSdmaGolden and kSdmaStart in order. progress: 1 after
// PowerUpSdma was answered OK, 2 once any SDMA register was written.
Status startSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                 uint32_t stage, uint32_t *progress, uint32_t *upResponse);

// Requires GFX_RB_WPTR at frame * 1024 bytes (kSdmaOutOfOrder), writes
// (frame + 1) * 1024 and then GFX_RB_WPTR_HI 0 (the commit), and polls the frame's result word (the test value, or
// fence 1) in the work area, up to kSdmaPollPauses (kSdmaTimeout).
Status submitSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                  const MemoryReader &work, uint32_t stage, uint32_t frame, uint32_t *observed);

// No writes. GFX_RB_RPTR must be 2048; the 64 KiB from the work area must
// hold the ring, the write-back page (read pointer at +0 and +4 not pinned,
// the test value, fence 1, else 0), the source, the source again in the
// destination, then the snapshot. kSdmaVerifyFailed otherwise.
Status verifySdmaCopy(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &region,
                      const uint32_t *snapshot, uint32_t stage, uint32_t *rptr, uint32_t *unexpected,
                      uint32_t *firstOffset);

// Undoes startSdma by progress: kSdmaStop (progress 2), then PowerDownSdma
// (progress >= 1). Attempts every step; returns the first failure.
Status stopSdma(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                uint32_t stage, uint32_t progress, uint32_t *downResponse);

} // namespace cezanne

#endif
