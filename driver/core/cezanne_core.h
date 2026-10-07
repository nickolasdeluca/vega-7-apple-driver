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
const uint32_t kMaxStage = 21;

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

// Stage 17: GART (MMHUB VM context 0, one-level table) and IH ring 0, every
// value pinned from boot 22 (docs/test-boot.md, stage 17). Offsets from
// mmhub_1_0_offset.h (base 0x1A000); engine 17 is gmc_v9_0_flush_gpu_tlb's.
const uint32_t kGartStage = 17;
const uint32_t kRegVmInvalidateEng17Sem = 0x69b88; // VM_INVALIDATE_ENG17_SEM: read only inside the flush, a read acquires it
const uint32_t kRegVmInvalidateEng17Req = 0x69bd0; // VM_INVALIDATE_ENG17_REQ: gmc_v9_0_flush_gpu_tlb, written only
const uint32_t kRegVmInvalidateEng17AddrRangeLo32 = 0x69ca4; // VM_INVALIDATE_ENG17_ADDR_RANGE_LO32: program_invalidation
const uint32_t kRegVmInvalidateEng17AddrRangeHi32 = 0x69ca8; // VM_INVALIDATE_ENG17_ADDR_RANGE_HI32: program_invalidation
// Read from stage 17 on, in addition to kStage16Registers.
const uint32_t kStage17Registers[] = {kRegVmInvalidateEng17AddrRangeLo32, kRegVmInvalidateEng17AddrRangeHi32};
const uint32_t kStage17RegisterCount = sizeof(kStage17Registers) / sizeof(kStage17Registers[0]);
// The GART work area: page table, dummy page, IH ring, IH write-pointer
// write-back; just above the TMR.
const uint64_t kGartWorkCarveoutOffset = 0x40800000ull;
const uint64_t kGartWorkGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kGartWorkCarveoutOffset;
const uint64_t kGartWorkPhysical = (uint64_t(kExpectedFbOffset) << 24) + kGartWorkCarveoutOffset;
const uint32_t kGartWorkSize = 0x4000;
const uint32_t kGartWorkCheckSize = 0x10000;
const uint32_t kGartTablePage = 0x0000, kGartDummyPage = 0x1000, kIhRingPage = 0x2000, kIhWbPage = 0x3000;
const uint32_t kIhRingBytes = 0x1000;  // RB_SIZE 10
const uint32_t kIhEntryBytes = 32;     // vega10_ih_decode_iv: 8 dwords
const uint32_t kIhWptrOverflow = 0x1;  // IH_RB_WPTR.RB_OVERFLOW
const uint32_t kIhWptrOffsetMask = 0x3fffc; // IH_RB_WPTR.OFFSET, bytes
// PTE 0 maps the stage 15 source page: VALID | SYSTEM | EXECUTABLE |
// READABLE | WRITEABLE, MTYPE_UC (3) at bit 57; not SNOOPED.
const uint64_t kGartPte0 = (kSdmaWorkPhysical + kSdmaSrcPage) | 0x73ull | (3ull << 57);
const uint32_t kGartTableEntries = 512; // PAGE_TABLE_END_ADDR 0x1ff: 2 MiB of GART
const uint64_t kGartTableAddress = kGartWorkPhysical + kGartTablePage;  // amdgpu_gmc_pd_addr: physical
const uint64_t kGartDummyAddress = kGartWorkPhysical + kGartDummyPage;  // >> 12 for the default pages
const uint64_t kIhRingGpuAddress = kGartWorkGpuAddress + kIhRingPage;
const uint64_t kIhWbGpuAddress = kGartWorkGpuAddress + kIhWbPage;
// The stage 15 work area grows by a second destination page and frame 2.
const uint32_t kGartSdmaWorkSize = 0x5000;
const uint32_t kSdmaDst2Page = 0x4000;
const uint32_t kSdmaWbFence2 = 0x204;
const uint64_t kGartSourceAddress = 0; // GART page 0, through PTE 0
const uint32_t kSdmaOpTrap = 6;
const uint32_t kSdmaFrame2Dwords = 13; // COPY 7, FENCE 4, TRAP 2
const uint32_t kSdmaCntlTrap = 0x3;     // SDMA0_CNTL + TRAP_ENABLE (sdma_v4_0_set_trap_irq_state)
const uint32_t kSdmaCntlBoot = 0x2;     // as kSdmaStart leaves it
// IH entry dw0: client 8 (SOC15_IH_CLIENTID_SDMA0), source 224 (SDMA_TRAP).
const uint32_t kIhClientSdma0 = 8, kIhSrcSdmaTrap = 224;
// gmc_v9_0_get_invalidate_req(0, 0): VMID 0, L2 PTEs, PDE0-2, L1 PTEs.
const uint32_t kGartInvalidateReq = 0x007c0001;
const uint32_t kGartPollPauses = 100; // adev->usec_timeout, polled in 1 ms pauses
struct GartWrite {
    uint32_t offset, boot22, value;
};
// mmhub_v1_0_gart_enable's writes in Linux's order; restored to boot22.
const GartWrite kGartEnable[] = {
    {kRegVmContext0PageTableBaseAddrLo32, 0, uint32_t(kGartTableAddress | 1)},
    {kRegVmContext0PageTableBaseAddrHi32, 0, uint32_t(kGartTableAddress >> 32)},
    {kRegVmContext0PageTableStartAddrLo32, 0, 0},
    {kRegVmContext0PageTableStartAddrHi32, 0, 0},
    {kRegVmContext0PageTableEndAddrLo32, 0, kGartTableEntries - 1},
    {kRegVmContext0PageTableEndAddrHi32, 0, 0},
    {kRegMmhubDefaultAddrLsb, 0, uint32_t(kGartDummyAddress >> 12)},
    {kRegMmhubDefaultAddrMsb, 0, uint32_t(kGartDummyAddress >> 44)},
    {kRegVmL2ProtectionFaultDefaultAddrLo32, 0, uint32_t(kGartDummyAddress >> 12)},
    {kRegVmL2ProtectionFaultDefaultAddrHi32, 0, uint32_t(kGartDummyAddress >> 44)},
    {kRegVmL2ProtectionFaultCntl2, 0x000a0000, 0x000e0000},
    {kRegMmhubMxL1TlbCntl, 0x00002501, 0x00003d59},
    {kRegMmhubVmL2Cntl, 0x00080602, 0x00080603},
    {kRegVmL2Cntl2, 0, 0x00000003},
    {kRegVmL2Cntl3, 0x80100007, 0x8014800c},
    {kRegVmL2Cntl4, 0x000000c1, 0x00000001},
    {kRegMmhubVmContext0Cntl, 0x007ffe80, 0x007ffe01},
    {kRegVmL2Context1IdentityApertureLowAddrLo32, 0, 0xffffffff},
    {kRegVmL2Context1IdentityApertureLowAddrHi32, 0, 0x0000000f},
    {kRegVmInvalidateEng17AddrRangeLo32, 0, 0xffffffff},
    {kRegVmInvalidateEng17AddrRangeHi32, 0, 0x0000001f},
};
const uint32_t kGartEnableCount = sizeof(kGartEnable) / sizeof(kGartEnable[0]);
const uint32_t kGartContext0Index = 16; // restored first
// vega10_ih_enable_ring for ring 0 in Linux's order; restored to boot22.
const GartWrite kIhEnable[] = {
    {kRegIhRbBase, 0, uint32_t(kIhRingGpuAddress >> 8)},
    {kRegIhRbBaseHi, 0, uint32_t(kIhRingGpuAddress >> 40) & 0xff},
    {kRegIhRbCntl, 0x40610000, 0xc0110114},
    {kRegIhRbWptrAddrLo, 0, uint32_t(kIhWbGpuAddress)},
    {kRegIhRbWptrAddrHi, 0, uint32_t(kIhWbGpuAddress >> 32) & 0xffff},
    {kRegIhRbWptr, 0x00080000, 0},
    {kRegIhRbRptr, 0, 0},
    {kRegIhDoorbellRptr, 0, 0},
};
const uint32_t kIhEnableCount = sizeof(kIhEnable) / sizeof(kIhEnable[0]);
const uint32_t kIhRbCntlOff = 0xc0110114; // also the ring-off write that starts the restore
const uint32_t kIhRbCntlOn = 0xc0110115;  // + RB_ENABLE; ENABLE_INTR stays 0
// Display pipe 0 at boot 22 (kDisplayInventory's first 11). DCHUBP_CNTL's
// live status fields are never compared: HUBP_NO_OUTSTANDING_REQ (bit 1),
// HUBP_IN_BLANK (bit 3) and HUBP_XRQ_NO_OUTSTANDING_REQ (19:16). Boot 24 read
// 0x000e0000 against boot 22's 0x000f0002 while pipe 0 scanned out.
const uint32_t kDisplayPipe0Boot22[] = {0x80011301, 0x00000897, 0x00000464, 0x00c00840, 0x00290461, 0x000f0002,
                                        0x00000008, 0x04380780, 0x00000780, 0x00000000, 0x000000f4};
const uint32_t kHubpLiveStatus = 0x000f000a;
const uint32_t kDchubpCntlIndex = 5; // in each pipe's 11
// The BAR5 pages the stage 17 operations may write: IH and SDMA0 (0x4000),
// the MMHUB VM L2, context and engine 17 page, and the MMHUB MC page (L1
// TLB and default address). The adapter maps these three for kGartPageSet.
const uint32_t kGartPageSet = 0x69000;
const uint32_t kGartPages[] = {0x4000, 0x69000, 0x6a000};
// The precondition list: kGartEnable, kIhEnable, SDMA0_CNTL, display pipe 0.
const uint32_t kGartCheckCount = kGartEnableCount + kIhEnableCount + 1 + 11;
// The first IH entries reported by the verify.
const uint32_t kIhReportEntries = 32;

// Stage 18: IH interrupt delivery through MSI (docs/test-boot.md, stage 18),
// after a passing stage 17 verify and undone before its restore. The kext
// owns the MSI handler; the core writes only the registers below.
const uint32_t kIntrStage = 18;
// nbio_v7_0_ih_control: INTERRUPT_CNTL2 <- the dummy page >> 8.
const uint32_t kInterruptCntl2Dummy = uint32_t(kGartDummyAddress >> 8);
// vega10_ih_enable_ring with MSI: + RPTR_REARM (bit 21); then
// vega10_ih_toggle_interrupts(true): + RB_ENABLE, RB_GPU_TS_ENABLE (bit 7)
// and ENABLE_INTR (bit 17). kIhRbCntlRearm is also the restore's ring-off.
const uint32_t kIhRbCntlRearm = kIhRbCntlOff | (1u << 21);
const uint32_t kIhRbCntlIntr = kIhRbCntlRearm | 1u | (1u << 7) | (1u << 17);
// Frame 3, the ring's last quarter: FENCE 3 and TRAP; the write pointer
// reaches the ring's end (4096 bytes), unmasked as sdma_v4_0_ring_set_wptr.
const uint32_t kSdmaWbFence3 = 0x208;
const uint32_t kSdmaFrame3Dwords = 6; // FENCE 4, TRAP 2
const uint32_t kIntrSettlePauses = 100; // the wait after the acknowledgement
// The precondition registers, all 0 at boot 22.
const uint32_t kIntrCheck[] = {kRegInterruptCntl, kRegInterruptCntl2, kRegBifIhDoorbellRange};
const uint32_t kIntrCheckCount = sizeof(kIntrCheck) / sizeof(kIntrCheck[0]);
// The BAR5 pages the stage 18 operations may write: NBIO (INTERRUPT_CNTL2)
// and IH. The adapter maps these two for kIntrPageSet.
const uint32_t kIntrPageSet = 0x3000;
const uint32_t kIntrPages[] = {0x3000, 0x4000};
// PCI configuration space: the MSI capability, read only.
const uint8_t kCapabilityMsi = 0x05;

// Stage 19: a display test pattern by flipping pipe 0's surface
// (docs/test-boot.md, stage 19). Offsets from dcn_2_1_0_offset.h (DCN base
// segment 2, 0x34C0); none measured before this stage.
const uint32_t kDisplayStage = 19;
const uint32_t kRegHubpreq0DcsurfFlipControl = 0x0eb6c; // HUBPREQ0_DCSURF_FLIP_CONTROL: hubp2_is_flip_pending
const uint32_t kRegHubpreq0DcsurfSurfaceEarliestInuse = 0x0eb94; // HUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE: hubp2_is_flip_pending
const uint32_t kRegHubpreq0DcsurfSurfaceEarliestInuseHigh = 0x0eb98; // HUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE_HIGH: hubp2_is_flip_pending
const uint32_t kRegHubp0DcsurfTilingConfig = 0x0ea9c; // HUBP0_DCSURF_TILING_CONFIG: hubp2_read_state
const uint32_t kRegHubpreq0DcsurfSurfaceControl = 0x0eb68; // HUBPREQ0_DCSURF_SURFACE_CONTROL: program_surface_flip_and_addr
const uint32_t kRegHubpreq0DcsurfPrimaryMetaSurfaceAddress = 0x0eb48; // HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS: program_surface_flip_and_addr
const uint32_t kRegHubpreq0DcsurfPrimaryMetaSurfaceAddressHigh = 0x0eb4c; // HUBPREQ0_DCSURF_PRIMARY_META_SURFACE_ADDRESS_HIGH: program_surface_flip_and_addr
const uint32_t kRegHubpreq0VmidSettings0 = 0x0eb24; // HUBPREQ0_VMID_SETTINGS_0: program_surface_flip_and_addr
const uint32_t kRegOtg0OtgStatusFrameCount = 0x14030; // OTG0_OTG_STATUS_FRAME_COUNT: optc1_get_vblank_counter
// Read from stage 19 on, in addition to kStage16Registers and kStage17Registers.
const uint32_t kStage19Registers[] = {kRegHubpreq0DcsurfFlipControl,
                                      kRegHubpreq0DcsurfSurfaceEarliestInuse,
                                      kRegHubpreq0DcsurfSurfaceEarliestInuseHigh,
                                      kRegHubp0DcsurfTilingConfig,
                                      kRegHubpreq0DcsurfSurfaceControl,
                                      kRegHubpreq0DcsurfPrimaryMetaSurfaceAddress,
                                      kRegHubpreq0DcsurfPrimaryMetaSurfaceAddressHigh,
                                      kRegHubpreq0VmidSettings0,
                                      kRegOtg0OtgStatusFrameCount};
const uint32_t kStage19RegisterCount = sizeof(kStage19Registers) / sizeof(kStage19Registers[0]);
// DCSURF_FLIP_CONTROL: SURFACE_UPDATE_LOCK (0), SURFACE_FLIP_TYPE (1),
// SURFACE_FLIP_PENDING (8); TILING_CONFIG.SW_MODE (4:0).
const uint32_t kFlipUpdateLock = 0x1, kFlipTypeImmediate = 0x2, kFlipPending = 0x100;
const uint32_t kTilingSwMode = 0x1f;
const uint32_t kOtgFrameCountMask = 0x00ffffff;
// The pattern surface: carveout 0x41000000, 8 MiB, inside DCN's FB aperture.
const uint64_t kPatternCarveoutOffset = 0x41000000ull;
const uint64_t kPatternGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kPatternCarveoutOffset;
const uint64_t kPatternPhysical = (uint64_t(kExpectedFbOffset) << 24) + kPatternCarveoutOffset;
const uint32_t kPatternSize = 8u << 20;
// 1920 x 1080 ARGB8888, 1920 pixels per line: 8 bands of 135 lines, grey
// lines 2 pixels wide at x = 0, 240, ..., 1680 and 1918.
const uint32_t kPatternWidth = 1920, kPatternHeight = 1080, kPatternBandLines = 135;
const uint32_t kPatternBands[] = {0xFFFFFFFF, 0xFFFFFF00, 0xFF00FFFF, 0xFF00FF00,
                                  0xFFFF00FF, 0xFFFF0000, 0xFF0000FF, 0xFF000000};
const uint32_t kPatternLine = 0xFF808080, kPatternLineSpacing = 240;
// The GOP surface, as boot 22 read it.
const uint64_t kGopSurfaceAddress = uint64_t(kExpectedFbLocationBase) << 24;
const uint32_t kDisplayFlipPauses = 100; // ~100 ms, about 6 frames at 60 Hz
// The BAR5 page the stage 19 flips write (HUBPREQ0).
const uint32_t kDisplayPageOffset = 0xe000;
// Pipe 0's surface-address entries in kDisplayInventory.
const uint32_t kDisplayAddressIndex = 9, kDisplayAddressHighIndex = 10;
// The precondition list: display pipe 0 at boot 22, then these.
struct DisplayExpect {
    uint32_t offset, mask, value;
};
const DisplayExpect kDisplayExpect[] = {
    {kRegHubpreq0DcsurfFlipControl, kFlipUpdateLock | kFlipTypeImmediate | kFlipPending, 0},
    {kRegHubpreq0DcsurfSurfaceEarliestInuse, 0xFFFFFFFF, uint32_t(kGopSurfaceAddress)},
    {kRegHubpreq0DcsurfSurfaceEarliestInuseHigh, 0xFFFFFFFF, uint32_t(kGopSurfaceAddress >> 32)},
    {kRegHubp0DcsurfTilingConfig, kTilingSwMode, 0},
    {kRegHubpreq0DcsurfSurfaceControl, 0xFFFFFFFF, 0},
    {kRegHubpreq0DcsurfPrimaryMetaSurfaceAddress, 0xFFFFFFFF, 0},
    {kRegHubpreq0DcsurfPrimaryMetaSurfaceAddressHigh, 0xFFFFFFFF, 0},
    {kRegHubpreq0VmidSettings0, 0xFFFFFFFF, 0},
};
const uint32_t kDisplayExpectCount = sizeof(kDisplayExpect) / sizeof(kDisplayExpect[0]);
const uint32_t kDisplayCheckCount = 11 + kDisplayExpectCount;

// Stage 20: SDMA fills the pattern region, then pipe 0 flips to it with
// HUBP0's flip interrupt enabled (docs/test-boot.md, stage 20), after the
// stage 18 acknowledgement and undone before its restore. Offsets from
// dcn_2_1_0_offset.h (DCN base segment 2, 0x34C0).
const uint32_t kFlipStage = 20;
const uint32_t kRegHubpreq0DcsurfSurfaceFlipInterrupt = 0x0eb80; // HUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT: irq_service_dcn21 pflip_int_entry
const uint32_t kRegDchubInterruptDest2 = 0x0d83c; // DCHUB_INTERRUPT_DEST2: HUBP0_IHC_FLIP_INTERRUPT_DEST, never written
const uint32_t kRegDispInterruptStatusContinue17 = 0x0d7ec; // DISP_INTERRUPT_STATUS_CONTINUE17: HUBP0_IHC_FLIP_INTERRUPT
// Read from stage 20 on, in addition to the stage 16, 17 and 19 registers.
const uint32_t kStage20Registers[] = {kRegHubpreq0DcsurfSurfaceFlipInterrupt, kRegDchubInterruptDest2,
                                      kRegDispInterruptStatusContinue17};
const uint32_t kStage20RegisterCount = sizeof(kStage20Registers) / sizeof(kStage20Registers[0]);
// DCSURF_SURFACE_FLIP_INTERRUPT: SURFACE_FLIP_INT_MASK (0, 1 = enabled),
// SURFACE_FLIP_CLEAR (8), SURFACE_FLIP_OCCURRED (16), SURFACE_FLIP_INT_STATUS
// (17); DCHUB_INTERRUPT_DEST2.HUBP0_IHC_FLIP_INTERRUPT_DEST (0);
// DISP_INTERRUPT_STATUS_CONTINUE17.HUBP0_IHC_FLIP_INTERRUPT (2).
const uint32_t kFlipIntEnable = 0x1, kFlipIntClear = 0x100, kFlipIntOccurred = 0x10000, kFlipIntStatus = 0x20000;
// Both interrupt enables, SURFACE_FLIP_INT_MASK and SURFACE_FLIP_AWAY_INT_MASK
// (2). Only these are checked: the status latches hold the firmware's
// flips (boot 29 read 0x00050000, OCCURRED and FLIP_AWAY_OCCURRED).
const uint32_t kFlipIntEnables = kFlipIntEnable | 0x4;
const uint32_t kFlipIntDest = 0x1, kFlipIntContinue17 = 0x4;
// The values written, in dal_irq_service_set's order (acknowledge, then
// enable or disable): clear; enable; clear while enabled; disable.
const uint32_t kFlipIntWrites[] = {kFlipIntClear, kFlipIntEnable, kFlipIntClear | kFlipIntEnable, 0};
// IH entry dw0: client 4 (SOC15_IH_CLIENTID_DCE), source 0x4f
// (DCN_1_0__SRCID__HUBP0_FLIP_INTERRUPT).
const uint32_t kIhClientDce = 4, kIhSrcHubp0Flip = 0x4f;
// Frame 4, the ring's first quarter again: 9 CONST_FILLs and FENCE 4; the
// write pointer continues from 4096 to 5120.
const uint32_t kSdmaOpConstFill = 11;
// CONST_FILL's FILLSIZE (31:30) 2: dword fills (Mesa 25.2 radv_sdma_fill_memory).
// Linux leaves it 0, which fills bytes with the data's low byte (boot 30:
// only the white band came out right); Linux fills with 0, where it does
// not matter.
const uint32_t kSdmaFillDword = 2u << 30;
const uint32_t kSdmaWbFence4 = 0x20c;
const uint32_t kFillCount = 9; // the 8 bands, then the tail
const uint32_t kFillBandBytes = kPatternBandLines * kPatternWidth * 4;
const uint32_t kFillTailOffset = 8 * kFillBandBytes;
const uint32_t kFillMaxBytes = 0x400000; // sdma_v4_0 fill_max_bytes
const uint32_t kSdmaFrame4Dwords = kFillCount * 5 + 4; // CONST_FILL 5 each, FENCE 4
const uint32_t kFlipWptr = 5 * kSdmaFrameDwords * 4;
// How many new IH entries a flip report keeps.
const uint32_t kFlipReportEntries = 4;

// Stage 21: the graphics engine (docs/test-boot.md, stage 21). The PSP loads
// the GFX firmware, the RLC and the CP start, the CP runs a ring test, a
// fence and DMA fills shown on pipe 0, then every GC register written goes
// back to the value the check read. Offsets from gc_9_0_offset.h (GC bases
// 0x2000 and 0xA000).
const uint32_t kGfxStage = 21;
const uint32_t kRegCpIntCntlRing0 = 0x0c1a8; // CP_INT_CNTL_RING0: gfx_v9_0_enable_gui_idle_interrupt
const uint32_t kRegRlcCsibAddrHi = 0x3b28c; // RLC_CSIB_ADDR_HI: gfx_v9_0_init_csb
const uint32_t kRegRlcCsibAddrLo = 0x3b288; // RLC_CSIB_ADDR_LO: gfx_v9_0_init_csb
const uint32_t kRegRlcCsibLength = 0x3b290; // RLC_CSIB_LENGTH: gfx_v9_0_init_csb
const uint32_t kRegRlcSrmCntl = 0x3b200; // RLC_SRM_CNTL: gfx_v9_0_enable_save_restore_machine
const uint32_t kRegRlcSpmMcCntl = 0x3b1c4; // RLC_SPM_MC_CNTL: gfx_v9_0_update_spm_vmid_internal
const uint32_t kRegRlcSerdesCuMasterBusy = 0x3b184; // RLC_SERDES_CU_MASTER_BUSY: gfx_v9_0_wait_for_rlc_serdes
const uint32_t kRegRlcSerdesNoncuMasterBusy = 0x3b188; // RLC_SERDES_NONCU_MASTER_BUSY: gfx_v9_0_wait_for_rlc_serdes
const uint32_t kRegCpRbWptrDelay = 0x08704; // CP_RB_WPTR_DELAY: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRbVmid = 0x0c144; // CP_RB_VMID: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRb0Cntl = 0x0c104; // CP_RB0_CNTL: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRb0Wptr = 0x0c150; // CP_RB0_WPTR: gfx_v9_0_ring_set_wptr_gfx
const uint32_t kRegCpRb0WptrHi = 0x0c154; // CP_RB0_WPTR_HI: gfx_v9_0_ring_set_wptr_gfx
const uint32_t kRegCpRb0RptrAddr = 0x0c10c; // CP_RB0_RPTR_ADDR: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRb0RptrAddrHi = 0x0c110; // CP_RB0_RPTR_ADDR_HI: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRbWptrPollAddrLo = 0x0c118; // CP_RB_WPTR_POLL_ADDR_LO: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRbWptrPollAddrHi = 0x0c11c; // CP_RB_WPTR_POLL_ADDR_HI: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRb0Base = 0x0c100; // CP_RB0_BASE: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpRb0BaseHi = 0x0c2c4; // CP_RB0_BASE_HI: gfx_v9_0_cp_gfx_resume
const uint32_t kRegCpMaxContext = 0x0c2b8; // CP_MAX_CONTEXT: gfx_v9_0_cp_gfx_start
const uint32_t kRegCpDeviceId = 0x0c12c; // CP_DEVICE_ID: gfx_v9_0_cp_gfx_start
const uint32_t kRegCpRb0Rptr = 0x08700; // CP_RB0_RPTR: gfx_v9_0_ring_get_rptr_gfx (read)
const uint32_t kRegCpRbDoorbellControl = 0x0c164; // CP_RB_DOORBELL_CONTROL: gfx_v9_0_cp_gfx_resume (read)
const uint32_t kRegGcMxL1TlbCntl = 0x0a61c; // MC_VM_MX_L1_TLB_CNTL: gfxhub_v1_0 (GC hub, read)
const uint32_t kRegGcVmL2Cntl = 0x0a100; // VM_L2_CNTL: gfxhub_v1_0 (GC hub, read)
const uint32_t kRegGcVmContext0Cntl = 0x0a200; // VM_CONTEXT0_CNTL: gfxhub_v1_0 (GC hub, read)
const uint32_t kRegCpStat = 0x08680; // CP_STAT: gc_reg_list_9 (IP dump)
const uint32_t kRegCpCpcStatus = 0x08210; // CP_CPC_STATUS: gc_reg_list_9 (IP dump)
const uint32_t kRegCpCeInstrPntr = 0x0869c; // CP_CE_INSTR_PNTR: gc_reg_list_9 (IP dump)
const uint32_t kRegCbHwControl = 0x09a00; // CB_HW_CONTROL: golden_settings_gc_9_1_rn (read)
const uint32_t kRegCbHwControl2 = 0x09a08; // CB_HW_CONTROL_2: golden_settings_gc_9_1_rn (read)
const uint32_t kRegDbDebug2 = 0x09834; // DB_DEBUG2: golden_settings_gc_9_1_rn (read)
const uint32_t kRegGbAddrConfigRead = 0x09908; // GB_ADDR_CONFIG_READ: golden_settings_gc_9_1_rn (read)
const uint32_t kRegPaScEnhance = 0x08bf0; // PA_SC_ENHANCE: golden_settings_gc_9_1_rn (read)
const uint32_t kRegPaScEnhance1 = 0x08bf4; // PA_SC_ENHANCE_1: golden_settings_gc_9_1_rn (read)
const uint32_t kRegPaScLineStippleState = 0x30a04; // PA_SC_LINE_STIPPLE_STATE: golden_settings_gc_9_1_rn (read)
const uint32_t kRegTaCntlAux = 0x09508; // TA_CNTL_AUX: golden_settings_gc_9_1_rn (read)
const uint32_t kRegTcpChanSteerHi = 0x0ac10; // TCP_CHAN_STEER_HI: golden_settings_gc_9_1_rn (read)
const uint32_t kRegTcpChanSteerLo = 0x0ac0c; // TCP_CHAN_STEER_LO: golden_settings_gc_9_1_rn (read)
const uint32_t kRegCpStalledStat1 = 0x08674; // CP_STALLED_STAT1: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpStalledStat2 = 0x08678; // CP_STALLED_STAT2: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpCpfStalledStat1 = 0x08224; // CP_CPF_STALLED_STAT1: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpCpfBusyStat = 0x08220; // CP_CPF_BUSY_STAT: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpGfxError = 0x0c0ec; // CP_GFX_ERROR: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpCeHeaderDump = 0x08690; // CP_CE_HEADER_DUMP: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpPfpHeaderDump = 0x08688; // CP_PFP_HEADER_DUMP: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegCpMeHeaderDump = 0x08684; // CP_ME_HEADER_DUMP: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegRlcGpmGeneral6 = 0x3b1a4; // RLC_GPM_GENERAL_6: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegRlcSafeMode = 0x3b014; // RLC_SAFE_MODE: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegRlcIntStat = 0x3b060; // RLC_INT_STAT: gc_reg_list_9 (IP dump), after boot 32
const uint32_t kRegGceaProbeMap = 0x09c30; // GCEA_PROBE_MAP: golden_settings_gc_9_1_rn (gfx_v9_0.c defines it), after boot 33
const uint32_t kRegCpfUtcl1Status = 0x0c6d8; // CPF_UTCL1_STATUS: gc_reg_list_9 (IP dump), after boot 33
const uint32_t kRegCpcUtcl1Status = 0x0c6d4; // CPC_UTCL1_STATUS: gc_reg_list_9 (IP dump), after boot 33
const uint32_t kRegCpgUtcl1Status = 0x0c6d0; // CPG_UTCL1_STATUS: gc_reg_list_9 (IP dump), after boot 33
const uint32_t kRegGcVmL2ProtectionFaultStatus = 0x0a12c; // VM_L2_PROTECTION_FAULT_STATUS: gc_reg_list_9 (GC hub), after boot 33
// Read from stage 21 on (all GFX-gated), in addition to the earlier lists.
// The last 11 were added after boot 32 (the CP stalled): stall reasons and
// the last packet headers; then, after boot 33 (the CP waited on its ring
// fetch), GCEA_PROBE_MAP and the CP's memory-path status.
const uint32_t kStage21Registers[] = {
    kRegCpIntCntlRing0,     kRegRlcCsibAddrHi,      kRegRlcCsibAddrLo,      kRegRlcCsibLength,
    kRegRlcSrmCntl,         kRegRlcSpmMcCntl,       kRegRlcSerdesCuMasterBusy, kRegRlcSerdesNoncuMasterBusy,
    kRegCpRbWptrDelay,      kRegCpRbVmid,           kRegCpRb0Cntl,          kRegCpRb0Wptr,
    kRegCpRb0WptrHi,        kRegCpRb0RptrAddr,      kRegCpRb0RptrAddrHi,    kRegCpRbWptrPollAddrLo,
    kRegCpRbWptrPollAddrHi, kRegCpRb0Base,          kRegCpRb0BaseHi,        kRegCpMaxContext,
    kRegCpDeviceId,         kRegCpRb0Rptr,          kRegCpRbDoorbellControl, kRegGcMxL1TlbCntl,
    kRegGcVmL2Cntl,         kRegGcVmContext0Cntl,   kRegCpStat,             kRegCpCpcStatus,
    kRegCpCeInstrPntr,      kRegCbHwControl,        kRegCbHwControl2,       kRegDbDebug2,
    kRegGbAddrConfigRead,   kRegPaScEnhance,        kRegPaScEnhance1,       kRegPaScLineStippleState,
    kRegTaCntlAux,          kRegTcpChanSteerHi,     kRegTcpChanSteerLo,
    kRegCpStalledStat1, kRegCpStalledStat2, kRegCpCpfStalledStat1, kRegCpCpfBusyStat, kRegCpGfxError, kRegCpCeHeaderDump, kRegCpPfpHeaderDump, kRegCpMeHeaderDump, kRegRlcGpmGeneral6, kRegRlcSafeMode, kRegRlcIntStat,
    kRegGceaProbeMap, kRegCpfUtcl1Status, kRegCpcUtcl1Status, kRegCpgUtcl1Status, kRegGcVmL2ProtectionFaultStatus};
const uint32_t kStage21RegisterCount = sizeof(kStage21Registers) / sizeof(kStage21Registers[0]);

// Part A: the GFX firmware. The pinned linux-firmware 20260916 files (their
// SHA-256 in driver/kext/build.sh), and the nine images
// psp_load_non_psp_fw loads, in AMDGPU_UCODE_ID order
// (amdgpu_ucode_init_single_fw cuts them; psp_gfx_if.h gives the types).
enum GfxFile : uint32_t { kGfxFileCe, kGfxFilePfp, kGfxFileMe, kGfxFileMec, kGfxFileRlc, kGfxFileCount };
struct GfxFiles {
    const uint8_t *data[kGfxFileCount];
    uint32_t length[kGfxFileCount];
};
// Header words the core requires (file, byte offset, value): the common
// header (size, version 1.0 or 2.1, IP 9.3, ucode version, size, offset 256),
// MEC's jump table and RLC v2.1's save/restore lists.
struct GfxHeaderPin {
    uint32_t file, offset, value;
};
const GfxHeaderPin kGfxHeaderPins[] = {
    {kGfxFileCe, 0, 36608},   {kGfxFileCe, 8, 0x00000001},   {kGfxFileCe, 12, 0x00030009},
    {kGfxFileCe, 16, 80},     {kGfxFileCe, 20, 36352},       {kGfxFileCe, 24, 256},
    {kGfxFilePfp, 0, 85760},  {kGfxFilePfp, 8, 0x00000001},  {kGfxFilePfp, 12, 0x00030009},
    {kGfxFilePfp, 16, 197},   {kGfxFilePfp, 20, 85504},      {kGfxFilePfp, 24, 256},
    {kGfxFileMe, 0, 69376},   {kGfxFileMe, 8, 0x00000001},   {kGfxFileMe, 12, 0x00030009},
    {kGfxFileMe, 16, 167},    {kGfxFileMe, 20, 69120},       {kGfxFileMe, 24, 256},
    {kGfxFileMec, 0, 268224}, {kGfxFileMec, 8, 0x00000001},  {kGfxFileMec, 12, 0x00030009},
    {kGfxFileMec, 16, 483},   {kGfxFileMec, 20, 267968},     {kGfxFileMec, 24, 256},
    {kGfxFileMec, 36, 66768}, {kGfxFileMec, 40, 224},
    {kGfxFileRlc, 0, 39928},  {kGfxFileRlc, 8, 0x00010002},  {kGfxFileRlc, 12, 0x00030009},
    {kGfxFileRlc, 16, 60},    {kGfxFileRlc, 20, 16896},      {kGfxFileRlc, 24, 256},
    {kGfxFileRlc, 116, 592},  {kGfxFileRlc, 120, 26832},     {kGfxFileRlc, 132, 2560},
    {kGfxFileRlc, 136, 27424}, {kGfxFileRlc, 148, 9944},     {kGfxFileRlc, 152, 29984},
};
const uint32_t kGfxHeaderPinCount = sizeof(kGfxHeaderPins) / sizeof(kGfxHeaderPins[0]);
const uint32_t kGfxFileLengths[kGfxFileCount] = {36608, 85760, 69376, 268224, 39928};
struct GfxImage {
    uint32_t file, payloadOffset, payloadSize, fwType, slot; // slot: offset in the firmware buffer
};
const GfxImage kGfxImages[] = {
    {kGfxFileCe, 256, 36352, 3, 0x00000},     // CP_CE
    {kGfxFilePfp, 256, 85504, 2, 0x09000},    // CP_PFP
    {kGfxFileMe, 256, 69120, 1, 0x1e000},     // CP_ME
    {kGfxFileMec, 256, 267072, 4, 0x2f000},   // CP_MEC1: ucode_size_bytes - jt_size * 4
    {kGfxFileMec, 267328, 896, 5, 0x71000},   // CP_MEC1_JT: at jt_offset * 4 from the ucode
    {kGfxFileRlc, 26832, 592, 22, 0x72000},   // RLC_RESTORE_LIST_CNTL
    {kGfxFileRlc, 27424, 2560, 20, 0x73000},  // RLC_RESTORE_LIST_GPM_MEM
    {kGfxFileRlc, 29984, 9944, 21, 0x74000},  // RLC_RESTORE_LIST_SRM_MEM
    {kGfxFileRlc, 256, 16896, 8, 0x77000},    // RLC_G, last
};
const uint32_t kGfxImageCount = sizeof(kGfxImages) / sizeof(kGfxImages[0]);
const uint64_t kGfxFwCarveoutOffset = 0x40900000ull;
const uint64_t kGfxFwGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kGfxFwCarveoutOffset;
const uint64_t kGfxFwPhysical = (uint64_t(kExpectedFbOffset) << 24) + kGfxFwCarveoutOffset;
const uint32_t kGfxFwBufferSize = 0x7c000; // 124 pages
const uint32_t kGfxFwCheckSize = 0x100000;
// The PSP work area's command for image k is the pseudo command
// kPspGfxLoad + k (GFX_CMD_ID_LOAD_IP_FW with that image's fields), sent as
// frame k + 1; DESTROY_TMR follows as frame 10.
const uint32_t kPspGfxLoad = 0x100;
const uint32_t kGfxDestroyFrame = kGfxImageCount + 1;

// Parts B-D: the GFX work area (ring, write-back, clear-state buffer).
const uint64_t kGfxWorkCarveoutOffset = 0x40a00000ull;
const uint64_t kGfxWorkGpuAddress = (uint64_t(kExpectedFbLocationBase) << 24) + kGfxWorkCarveoutOffset;
const uint64_t kGfxWorkPhysical = (uint64_t(kExpectedFbOffset) << 24) + kGfxWorkCarveoutOffset;
const uint32_t kGfxWorkSize = 0x4000;
const uint32_t kGfxWorkCheckSize = 0x10000;
const uint32_t kGfxWbPage = 0x2000, kGfxCsbPage = 0x3000;
const uint32_t kGfxWbRptr = 0x000, kGfxWbPoll = 0x008, kGfxWbFence1 = 0x100, kGfxWbFence2 = 0x108;
const uint32_t kGfxRingDwords = 2048; // 8 KiB: RB_BUFSZ 10
const uint32_t kGfxFrameStarts[] = {0, 1024, 1280, 1536, 2048}; // frame k: dwords [start k, start k+1)
const uint32_t kGfxCsbDwords = 904; // gfx_v9_0_get_csb_size
const uint32_t kGfxStartDwords = kGfxCsbDwords + 4 + 3; // cp_gfx_start: + SET_BASE, VGT_INDEX_TYPE
const uint32_t kGfxPollPauses = 100, kGfxDrawPauses = 1000;
// PM4 (soc15d.h).
const uint32_t kPm4Nop = 0x10, kPm4SetBase = 0x11, kPm4ClearState = 0x12, kPm4ContextControl = 0x28,
               kPm4ReleaseMem = 0x49, kPm4PreambleCntl = 0x4a, kPm4DmaData = 0x50, kPm4SetContextReg = 0x69,
               kPm4SetUconfigReg = 0x79;
inline uint32_t pm4(uint32_t op, uint32_t count)
{
    return (3u << 30) | ((count & 0x3fff) << 16) | ((op & 0xff) << 8);
}
// gfx_v9_0_ring_emit_fence: CACHE_FLUSH_AND_INV_TS_EVENT (0x14), index 5,
// TCL1, TC, TC_MD and TC_WB actions; DATA_SEL 1 (32 bits), INT_SEL 0.
const uint32_t kReleaseMemEvent = 0x00238514, kReleaseMemData = 0x20000000;
// DMA_DATA: CP_SYNC, SRC_SEL 2 (data), DST_SEL 3 (through L2), engine ME.
const uint32_t kDmaDataFill = 0xc0300000;
const uint32_t kGfxDrawBytes = 1382400; // 180 lines of 7680 bytes, below the 21-bit count
const uint32_t kGfxDrawFills = 6;
const uint32_t kGfxBands[] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF}; // red, green, blue: 360 lines each
const uint32_t kScratchRingTest = 0xDEADBEEF, kScratchBefore = 0xCAFEDEAD; // gfx_v9_0_ring_test_ring
// CP_RB0_CNTL: RB_BUFSZ 10, RB_BLKSZ 8; CP_MAX_CONTEXT max_hw_contexts - 1.
const uint32_t kCpRb0Cntl = 0x0000080a, kCpMaxContext = 7, kCpDeviceId = 1;
const uint32_t kCpGuiIdleInts = 0x003c0000; // CP_INT_CNTL_RING0 bits 18-21
const uint32_t kRlcEnableF32 = 0x1, kRlcSrmEnable = 0x1, kRlcSpmVmidMask = 0xf;
const uint32_t kGrbmSelectSe0Sh0 = 0x40000000, kGrbmBroadcast = 0xe0000000;
const uint32_t kRlcSerdesNoncuMask = 0x000dffff;
const uint32_t kCpDoorbellEnable = 0x40000000;

// The registers this stage writes, in Linux's order, and SCRATCH_REG0: the
// check snapshots them, the restore writes the snapshot back in this order.
const uint32_t kGfxSnapshotRegisters[] = {
    kRegRlcCntl,          kRegCpIntCntlRing0,  kRegGrbmGfxIndex,       kRegRlcCgcgCglsCtrl,
    kRegRlcCsibAddrHi,    kRegRlcCsibAddrLo,   kRegRlcCsibLength,      kRegRlcSrmCntl,
    kRegRlcSpmMcCntl,     kRegCpRbWptrDelay,   kRegCpRbVmid,           kRegCpRb0Cntl,
    kRegCpRb0Wptr,        kRegCpRb0WptrHi,     kRegCpRb0RptrAddr,      kRegCpRb0RptrAddrHi,
    kRegCpRbWptrPollAddrLo, kRegCpRbWptrPollAddrHi, kRegCpRb0Base,     kRegCpRb0BaseHi,
    kRegCpMaxContext,     kRegCpDeviceId,      kRegCpMeCntl,           kRegScratchReg0,
    kRegCbHwControl,      kRegCbHwControl2,    kRegDbDebug2,           kRegGbAddrConfig,
    kRegGbAddrConfigRead, kRegPaScEnhance,     kRegPaScEnhance1,       kRegPaScLineStippleState,
    kRegTaCntlAux,        kRegTcpChanSteerHi,  kRegTcpChanSteerLo,     kRegGceaProbeMap};
const uint32_t kGfxSnapshotCount = sizeof(kGfxSnapshotRegisters) / sizeof(kGfxSnapshotRegisters[0]);
// The values written besides the snapshot: (snapshot & ~mask) | value; a
// mask of all ones is a fixed value.
struct GfxWrite {
    uint32_t offset, mask, value;
};
const uint64_t kGfxCsbGpuAddress = kGfxWorkGpuAddress + kGfxCsbPage;
const uint64_t kGfxRptrGpuAddress = kGfxWorkGpuAddress + kGfxWbPage + kGfxWbRptr;
const uint64_t kGfxPollGpuAddress = kGfxWorkGpuAddress + kGfxWbPage + kGfxWbPoll;
const GfxWrite kGfxWrites[] = {
    {kRegRlcCntl, kRlcEnableF32, 0},                 // rlc_stop
    {kRegRlcCntl, kRlcEnableF32, kRlcEnableF32},     // rlc_start
    {kRegCpIntCntlRing0, kCpGuiIdleInts, 0},         // enable_gui_idle_interrupt(false)
    {kRegGrbmGfxIndex, 0xFFFFFFFF, kGrbmSelectSe0Sh0}, // wait_for_rlc_serdes
    {kRegGrbmGfxIndex, 0xFFFFFFFF, kGrbmBroadcast},
    {kRegRlcCgcgCglsCtrl, 0xFFFFFFFF, 0},            // rlc_resume: disable CG
    {kRegRlcCsibAddrHi, 0xFFFFFFFF, uint32_t(kGfxCsbGpuAddress >> 32)},
    {kRegRlcCsibAddrLo, 0xFFFFFFFF, uint32_t(kGfxCsbGpuAddress) & 0xfffffffc},
    {kRegRlcCsibLength, 0xFFFFFFFF, kGfxCsbDwords},
    {kRegRlcSrmCntl, kRlcSrmEnable, kRlcSrmEnable},
    {kRegRlcSpmMcCntl, kRlcSpmVmidMask, 0xf},
    {kRegCpRbWptrDelay, 0xFFFFFFFF, 0},
    {kRegCpRbVmid, 0xFFFFFFFF, 0},
    {kRegCpRb0Cntl, 0xFFFFFFFF, kCpRb0Cntl},
    {kRegCpRb0Wptr, 0xFFFFFFFF, 0},
    {kRegCpRb0Wptr, 0xFFFFFFFF, 1024},
    {kRegCpRb0Wptr, 0xFFFFFFFF, 1280},
    {kRegCpRb0Wptr, 0xFFFFFFFF, 1536},
    {kRegCpRb0Wptr, 0xFFFFFFFF, 2048},
    {kRegCpRb0WptrHi, 0xFFFFFFFF, 0},
    {kRegCpRb0RptrAddr, 0xFFFFFFFF, uint32_t(kGfxRptrGpuAddress)},
    {kRegCpRb0RptrAddrHi, 0xFFFFFFFF, uint32_t(kGfxRptrGpuAddress >> 32) & 0xffff},
    {kRegCpRbWptrPollAddrLo, 0xFFFFFFFF, uint32_t(kGfxPollGpuAddress)},
    {kRegCpRbWptrPollAddrHi, 0xFFFFFFFF, uint32_t(kGfxPollGpuAddress >> 32)},
    {kRegCpRb0Base, 0xFFFFFFFF, uint32_t(kGfxWorkGpuAddress >> 8)},
    {kRegCpRb0BaseHi, 0xFFFFFFFF, uint32_t(kGfxWorkGpuAddress >> 40)},
    {kRegCpMaxContext, 0xFFFFFFFF, kCpMaxContext},
    {kRegCpDeviceId, 0xFFFFFFFF, kCpDeviceId},
    {kRegCpMeCntl, kCpMeHalts, 0},                   // cp_gfx_enable(true)
    // golden_settings_gc_9_1_rn (after boot 33), soc15_program_register_sequence.
    {kRegCbHwControl, 0xfffdf3cf, 0x00014104},
    {kRegCbHwControl2, 0xff7fffff, 0x0a000000},
    {kRegDbDebug2, 0xf00fffff, 0x00000400},
    {kRegGbAddrConfig, 0xf3e777ff, 0x24000042},
    {kRegGbAddrConfigRead, 0xf3e777ff, 0x24000042},
    {kRegPaScEnhance, 0x3fffffff, 0x00000001},
    {kRegPaScEnhance1, 0xffffffff, 0x04040000},
    {kRegPaScLineStippleState, 0x0000ff0f, 0x00000000},
    {kRegTaCntlAux, 0xfffffeef, 0x010b0000},
    {kRegTcpChanSteerHi, 0xffffffff, 0x00000000},
    {kRegTcpChanSteerLo, 0xffffffff, 0x00003120},
    {kRegGceaProbeMap, 0xffffffff, 0x0000cccc},
};
// The golden writes are kGfxWrites' last kGfxGoldenCount entries, in Linux's order.
const uint32_t kGfxGoldenCount = 12;
const uint32_t kGfxWriteCount = sizeof(kGfxWrites) / sizeof(kGfxWrites[0]);
// The precondition list (offset, mask, value), after the image headers and
// GFX on: RLC off, CP and MEC halted, no doorbell, and the GC hub as MMHUB
// read at boot 22 with the FB aperture of stage 10.
struct GfxExpect {
    uint32_t offset, mask, value;
};
const GfxExpect kGfxExpect[] = {
    {kRegRlcCntl, 0xFFFFFFFF, 0},
    {kRegCpMeCntl, kCpMeHalts, kCpMeHalts},
    {kRegCpMecCntl, kCpMecHalts, kCpMecHalts},
    {kRegCpRbDoorbellControl, kCpDoorbellEnable, 0},
    {kRegGcMxL1TlbCntl, 0xFFFFFFFF, 0x00002501},
    {kRegGcVmL2Cntl, 0xFFFFFFFF, 0x00080602},
    {kRegGcVmContext0Cntl, 0xFFFFFFFF, 0x007ffe80},
    {kRegGcFbLocationBase, 0xFFFFFFFF, kExpectedFbLocationBase},
    {kRegGcFbLocationTop, 0xFFFFFFFF, 0x0000f47f},
    {kRegMcVmFbOffset, 0xFFFFFFFF, kExpectedFbOffset},
};
const uint32_t kGfxExpectCount = sizeof(kGfxExpect) / sizeof(kGfxExpect[0]);
// Every register a stage 21 report shows: the snapshot list, then status,
// instruction pointers, the serdes, doorbell and GC hub, and the golden
// registers (recorded for the shader stage).
const uint32_t kGfxStateRegisters[] = {
    kRegRlcCntl, kRegCpIntCntlRing0, kRegGrbmGfxIndex, kRegRlcCgcgCglsCtrl, kRegRlcCsibAddrHi, kRegRlcCsibAddrLo,
    kRegRlcCsibLength, kRegRlcSrmCntl, kRegRlcSpmMcCntl, kRegCpRbWptrDelay, kRegCpRbVmid, kRegCpRb0Cntl,
    kRegCpRb0Wptr, kRegCpRb0WptrHi, kRegCpRb0RptrAddr, kRegCpRb0RptrAddrHi, kRegCpRbWptrPollAddrLo,
    kRegCpRbWptrPollAddrHi, kRegCpRb0Base, kRegCpRb0BaseHi, kRegCpMaxContext, kRegCpDeviceId, kRegCpMeCntl,
    kRegScratchReg0, kRegCbHwControl, kRegCbHwControl2, kRegDbDebug2, kRegGbAddrConfig, kRegGbAddrConfigRead,
    kRegPaScEnhance, kRegPaScEnhance1, kRegPaScLineStippleState, kRegTaCntlAux, kRegTcpChanSteerHi,
    kRegTcpChanSteerLo, kRegGceaProbeMap, kRegRlcStat, kRegGrbmStatus, kRegGrbmStatus2, kRegCpStat,
    kRegCpCpfStatus, kRegCpCpcStatus, kRegCpBusyStat, kRegCpMecCntl, kRegCpRb0Rptr, kRegCpPfpInstrPntr,
    kRegCpMeInstrPntr, kRegCpCeInstrPntr, kRegCpMec1InstrPntr, kRegRlcSerdesCuMasterBusy,
    kRegRlcSerdesNoncuMasterBusy, kRegCpRbDoorbellControl, kRegGcMxL1TlbCntl, kRegGcVmL2Cntl,
    kRegGcVmContext0Cntl, kRegCpStalledStat1, kRegCpStalledStat2, kRegCpCpfStalledStat1, kRegCpCpfBusyStat,
    kRegCpGfxError, kRegCpCeHeaderDump, kRegCpPfpHeaderDump, kRegCpMeHeaderDump, kRegRlcGpmGeneral6,
    kRegRlcSafeMode, kRegRlcIntStat, kRegCpfUtcl1Status, kRegCpcUtcl1Status, kRegCpgUtcl1Status,
    kRegGcVmL2ProtectionFaultStatus};
const uint32_t kGfxStateCount = sizeof(kGfxStateRegisters) / sizeof(kGfxStateRegisters[0]);
// The BAR5 pages the stage 21 register writes use: CP_ME_CNTL,
// CP_RB_WPTR_DELAY and PA_SC_ENHANCE(_1); CB, DB, GB_ADDR_CONFIG, TA and
// GCEA; TCP_CHAN_STEER; the CP ring registers; GRBM_GFX_INDEX,
// SCRATCH_REG0 and PA_SC_LINE_STIPPLE_STATE; the RLC. The adapter maps these
// six for kGfxPageSet.
const uint32_t kGfxPageSet = 0x8000;
const uint32_t kGfxPages[] = {0x8000, 0x9000, 0xa000, 0xc000, 0x30000, 0x3b000};
const uint32_t kGfxPageCount = sizeof(kGfxPages) / sizeof(kGfxPages[0]);

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
    // Stage 17.
    kGartUnexpectedState,
    kGartSemaphoreTimeout,
    kGartAckTimeout,
    kGartFault,
    kIhNoTrap,
    kGartVerifyFailed,
    kGartNotRestored,
    kGartOutOfOrder,
    // Stage 18.
    kIntrNoMsi,
    kIntrUnexpectedState,
    kIntrSourceFailed,
    kIntrNotDelivered,
    kIntrVerifyFailed,
    kIntrRefired,
    kIntrNotRestored,
    kIntrOutOfOrder,
    // Stage 19.
    kDisplayUnexpectedState,
    kDisplayFlipTimeout,
    kDisplayVerifyFailed,
    kDisplayNotRestored,
    kDisplayOutOfOrder,
    // Stage 20.
    kFlipUnexpectedState,
    kFlipFillMismatch,
    kFlipIntrNotDelivered,
    kFlipIntrVerifyFailed,
    kFlipVerifyFailed,
    kFlipNotRestored,
    kFlipOutOfOrder,
    // Stage 21.
    kGfxImageInvalid,
    kGfxUnexpectedState,
    kGfxCpTimeout,
    kGfxRingTestFailed,
    kGfxFenceTimeout,
    kGfxDrawMismatch,
    kGfxVerifyFailed,
    kGfxNotRestored,
    kGfxOutOfOrder,
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
const uint32_t kDiagnosticVersion = 17;
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
    kDiagnosticSdmaSubmit = 23,    // in: frame (0 ring test, 1 copy, 2 GART copy and trap from stage 17,
                                   // 3 fence and trap from stage 18);
                                   // out: Status, observed, GFX_RB_RPTR, GFX_RB_WPTR, F32_CNTL, STATUS_REG
    kDiagnosticSdmaVerify = 24,    // out: Status, GFX_RB_RPTR, unexpected words, first offset, STATUS_REG
    kDiagnosticSdmaStop = 25,      // out: Status, F32_CNTL, PowerDownSdma response, DESTROY_TMR fence, ring response
                                   // (from stage 17 it first runs the GART restore if needed)
    // Stage 17, after a passing stage 15 verify (selector 24), in this order;
    // frame 2 is submitted with selector 23 between enable and verify.
    kDiagnosticGartCheck = 26,   // out: Status, index into the precondition list, its value
    kDiagnosticGartEnable = 27,  // out: Status, progress, ENG17_ACK
    kDiagnosticGartVerify = 28,  // out: Status; structure: GartReport
    kDiagnosticGartRestore = 29, // out: Status, index of the first register not restored, its value, IH_RB_WPTR
                                 // (from stage 18 it first runs the interrupt restore if needed)
    // Stage 18, after a passing stage 17 verify (selector 28), in this order;
    // frame 3 is submitted with selector 23 between enable and verify.
    kDiagnosticIntrCheck = 30,   // out: Status, index, value, MSI index, MSI control, address lo, hi, data
    kDiagnosticIntrEnable = 31,  // out: Status, progress, MSI control, address lo, hi, data
    kDiagnosticIntrVerify = 32,  // out: Status; structure: IntrReport
    kDiagnosticIntrAck = 33,     // out: Status, IH_RB_RPTR written, MSI count before, after, write-back after
    kDiagnosticIntrRestore = 34, // out: Status, index, value, MSI control, address lo, hi, data
    // Stage 19, in this order on one connection; independent of the others.
    kDiagnosticDisplayCheck = 35,   // out: Status, index, value, frame count, checksum lo, hi
    kDiagnosticDisplayFlip = 36,    // out: Status, in-use lo, hi, pauses, frame count
    kDiagnosticDisplayVerify = 37,  // out: Status; structure: DisplayReport
    kDiagnosticDisplayRestore = 38, // out: Status, in-use lo, hi, pauses, index, value
    // Stage 20, after a passing stage 18 acknowledgement (selector 33) on the
    // same connection, in this order; undone before the stage 18 restore.
    kDiagnosticFlipCheck = 39,   // out: Status, index, value, frame count, checksum lo, hi, DEST2, CONTINUE17
    kDiagnosticFlipFill = 40,    // out: Status, step, fence 4, GFX_RB_RPTR, unexpected words, first offset,
                                 // MSI change, value at the first offset
    kDiagnosticFlipShow = 41,    // out: Status, SURFACE_FLIP_INTERRUPT after the arm; structure: FlipReport
    kDiagnosticFlipAck = 42,     // out: Status, SURFACE_FLIP_INTERRUPT, IH_RB_RPTR written, MSI before, after,
                                 // write-back after
    kDiagnosticFlipVerify = 43,  // out: Status, MSI change during the hold; structure: DisplayReport
    kDiagnosticFlipRestore = 44, // out: Status, index, value, IH_RB_RPTR written; structure: FlipReport
    // Stage 21, after a passing SETUP_TMR (selector 15) for the check's
    // owner, in this order; every step returns a GfxState as its structure.
    kDiagnosticGfxCheck = 45,   // out: Status, index into the check list, value
    kDiagnosticGfxLoad = 46,    // out: Status, images loaded, last fence, last response status, fw_addr lo, hi
    kDiagnosticGfxRlc = 47,     // out: Status, writes made, serdes CU busy, NONCU busy
    kDiagnosticGfxCp = 48,      // out: Status, writes made, read-pointer write-back, CP_RB0_RPTR
    kDiagnosticGfxTest = 49,    // out: Status, SCRATCH_REG0, fence 1, read-pointer write-back
    kDiagnosticGfxDraw = 50,    // out: Status, step, fence 2, unexpected words, first offset, its value
    kDiagnosticGfxVerify = 51,  // out: Status; structure: DisplayReport (not a GfxState)
    kDiagnosticGfxRestore = 52, // out: Status, index into the snapshot list, value, flip-back status
    kDiagnosticSelectorCount = 53,
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

// Work-area writes allowed from stage 15: only its own word; from stage 17
// also frame 2's words and zeros in the second destination page.
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
// (frame + 1) * 1024 and then GFX_RB_WPTR_HI 0 (the commit), and polls the
// frame's result word (the test value, fence 1, or from stage 17 fence 2 for
// frame 2) in the work area, up to kSdmaPollPauses (kSdmaTimeout).
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

// Stage 17. VM_INVALIDATE_ENG17_SEM is never in the read allowlist: only
// flushGart reads it, through a reader the adapter grants for the stage 17
// page set alone.
bool semaphoreReadAllowed(uint32_t offset, uint32_t stage);

// Whether a register write is a kGartEnable or kIhEnable value or its boot
// 22 value, the IH ring on or off, the engine 17 request or semaphore
// release, SDMA0_CNTL with TRAP_ENABLE, or frame 2's write pointer (3072).
bool gartWriteListed(uint32_t offset, uint32_t value);

// The GART work area's word at a byte offset: PTE 0, else zero.
uint32_t gartWorkWord(uint32_t offset);

// GART work-area writes allowed from stage 17: only its own word.
bool gartWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage);

// The SDMA ring with frame 2 (COPY from GART 0 to the second destination,
// FENCE 2, TRAP) at dwords 512..767; sdmaRingWord elsewhere.
uint32_t gartRingWord(uint32_t dword);

// The GART work area and its check region: the stage 9 page checks.
Status checkGartWorkTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                           const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// No writes. Requires every kGartEnable and kIhEnable register at its boot
// 22 value, SDMA0_CNTL at kSdmaCntlBoot, and display pipe 0 at boot 22
// (HUBP_IN_BLANK ignored): kGartUnexpectedState with the index into that
// list and the value read.
Status checkGartBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value);

// Reads kDisplayInventory in order into values.
Status readDisplayInventory(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                            uint32_t *values);

// Writes the whole GART work area, then reads every word back
// (kPspReadbackMismatch).
Status writeGartWork(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// Writes frame 2 into the SDMA ring and zeroes the second destination, then
// reads both back (kPspReadbackMismatch). work spans kGartSdmaWorkSize.
Status writeSdmaFrame2(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// gmc_v9_0_flush_gpu_tlb for MMHUB engine 17: read the semaphore until it
// reads 1 (kGartSemaphoreTimeout, nothing written), write the request, poll
// ACK bit 0 (kGartAckTimeout), and always release the semaphore after an
// acquire. kGartPollPauses each.
Status flushGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                 uint32_t stage, uint32_t *ack);

// kGartEnable, the flush, kIhEnable, the ring on, SDMA0_CNTL with
// TRAP_ENABLE. progress: 1 once any GART register was written, 2 any IH
// register, 3 SDMA0_CNTL.
Status enableGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                  uint32_t stage, uint32_t *progress, uint32_t *ack);

struct GartReport {
    uint32_t rptr, fence2, faultStatus;
    uint32_t ihWriteback, ihWptr, ihRptr;  // the write-back dword, IH_RB_WPTR, IH_RB_RPTR
    uint32_t ihEntries, sdmaTraps, otherEntries;
    uint32_t sdmaUnexpected, sdmaFirst, gartUnexpected, gartFirst;
    uint32_t displayChanged, displayFirst; // index into kDisplayInventory
    uint32_t ring[kIhReportEntries * kIhEntryBytes / 4];
};

// Reads only. Polls the IH write-back for an SDMA0 trap entry (up to
// kSdmaPollPauses), then checks: GFX_RB_RPTR 3072, fence 2, no VM fault, the
// trap, the SDMA region (stage 15 words, fence 2, both destinations equal to
// the source, the snapshot beyond), the GART region (table and dummy page,
// ring entries up to the write pointer and zero after it, the write-back
// dword, the snapshot beyond), and the display inventory against display
// (HUBP_IN_BLANK ignored). kGartFault, kIhNoTrap or kGartVerifyFailed.
Status verifyGart(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &sdmaRegion,
                  const uint32_t *sdmaSnapshot, const MemoryReader &gartRegion, const uint32_t *gartSnapshot,
                  const uint32_t *display, const RegisterWriter &writer, uint32_t stage, GartReport *report);

// Undoes enableGart by progress: the IH ring off and kIhEnable to boot 22
// (progress >= 2), SDMA0_CNTL to kSdmaCntlBoot (3), then VM_CONTEXT0_CNTL
// and the rest of kGartEnable to boot 22 and a second flush (>= 1).
// Attempts every step; returns the first failure.
Status restoreGart(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t progress, uint32_t *ack);

// No writes. Requires every kGartEnable and kIhEnable register and
// SDMA0_CNTL back at boot 22 (kGartNotRestored with the index and value),
// except IH_RB_WPTR, which the IH maintains: its value is reported.
Status checkGartRestored(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                         uint32_t *value, uint32_t *ihWptr);

// Stage 18. The MSI capability as configuration space holds it (read only).
struct MsiCapability {
    uint8_t offset; // 0 when absent
    uint16_t control;
    uint32_t addressLo, addressHi; // addressHi 0 without 64-bit addressing
    uint16_t data;
};

// Walks the capability list as readPciState does and reads the MSI
// capability; kIntrNoMsi if there is none.
Status readMsiCapability(const ConfigReader &config, MsiCapability *msi);

// Whether a register write is a stage 18 value: INTERRUPT_CNTL2 to the dummy
// page or back to 0, IH_RB_CNTL with RPTR_REARM (off) or with ENABLE_INTR
// (on), IH_RB_RPTR to a 32-byte entry boundary inside the ring (the
// acknowledgement), or frame 3's write pointer (4096).
bool intrWriteListed(uint32_t offset, uint32_t value);

// The SDMA ring with frame 3 (FENCE 3, TRAP) at dwords 768..1023;
// gartRingWord elsewhere.
uint32_t intrRingWord(uint32_t dword);

// No writes. Requires kIntrCheck at boot 22 (kIntrUnexpectedState with the
// index and value).
Status checkIntrBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                       uint32_t *value);

// Writes frame 3 into the SDMA ring and reads it back (kPspReadbackMismatch).
Status writeSdmaFrame3(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// vega10_ih_irq_init up to the interrupt toggle, from the stage 17 state: the
// ring off, its pointers 0, the ring and write-back pages zeroed (gart is the
// GART work area), INTERRUPT_CNTL2 <- the dummy page, IH_RB_CNTL with
// RPTR_REARM, the pointers 0 again. progress 1 once any register was written.
Status armIntr(const MemoryReader &gartWork, const MemoryWriter &gart, const RegisterWriter &writer,
               uint32_t stage, uint32_t *progress);

// vega10_ih_toggle_interrupts(true): IH_RB_CNTL <- kIhRbCntlIntr. The kext
// calls it only once its handler is enabled. progress 2 once written.
Status startIntr(const RegisterWriter &writer, uint32_t stage, uint32_t *progress);

// The kext's MSI counter, read without a lock.
struct InterruptCounter {
    uint32_t (*count)(void *context);
    void *context;
};

const uint32_t kIntrTimes = 8; // the first MSIs whose times the kext records
struct IntrReport {
    uint32_t msiCount, msiBefore, fence3, rptr, faultStatus, ihRbCntl; // msiBefore: the count at the submit
    uint32_t ihWriteback, ihWptr, ihRptr;
    uint32_t ihEntries, sdmaTraps, otherEntries;
    uint32_t sdmaUnexpected, sdmaFirst, gartUnexpected, gartFirst;
    uint32_t displayChanged, displayFirst;
    // Filled in by the kext: from the submit to the first MSI after it; the
    // submit and the first kIntrTimes MSIs, from the ENABLE_INTR write.
    uint32_t latencyMicroseconds, submitMicroseconds;
    uint32_t msiMicroseconds[kIntrTimes];
    uint32_t ring[kIhReportEntries * kIhEntryBytes / 4];
};

// Reads only. Polls (up to kSdmaPollPauses) until fence 3, an SDMA0 trap
// entry and an MSI after the submit have all arrived, then checks as
// verifyGart does with frame 3 in the ring and fence 3 in the write-back
// page. msiBefore is the count when frame 3 was submitted: MSIs before it
// (boot 26: one at ENABLE_INTR) are reported, not counted. kGartFault,
// kIhNoTrap, kIntrNotDelivered (no MSI after the submit), or
// kIntrVerifyFailed (fence, more than one MSI after the submit, unexpected
// words or a display change).
Status verifyIntr(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &sdmaRegion,
                  const uint32_t *sdmaSnapshot, const MemoryReader &gartRegion, const uint32_t *gartSnapshot,
                  const uint32_t *display, const RegisterWriter &writer, const InterruptCounter &counter,
                  uint32_t msiBefore, uint32_t stage, IntrReport *report);

// amdgpu_ih_process's acknowledgement: IH_RB_RPTR <- the write-back's offset.
// Then kIntrSettlePauses pauses; kIntrRefired if the MSI count or the
// write-back changed meanwhile.
Status ackIntr(const MemoryReader &gartRegion, const RegisterWriter &writer, const InterruptCounter &counter,
               uint32_t stage, uint32_t *rptr, uint32_t *countBefore, uint32_t *countAfter, uint32_t *writeback);

// vega10_ih_irq_disable: IH_RB_CNTL <- kIhRbCntlRearm (interrupts and ring
// off), the pointers 0, one pause. Before the kext removes its handler.
Status quiesceIntr(const RegisterWriter &writer, uint32_t stage);

// After the handler is removed: INTERRUPT_CNTL2 back to 0, then requires
// kIntrCheck at boot 22 (kIntrNotRestored with the index and value).
Status restoreIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *index, uint32_t *value);

// Stage 19. Whether a register write is a flip's: PRIMARY_SURFACE_ADDRESS_HIGH
// to the unchanged 0xf4, PRIMARY_SURFACE_ADDRESS to the pattern or the GOP
// surface.
bool displayWriteListed(uint32_t offset, uint32_t value);

// The pattern region's word at a byte offset; zero past the 1920 x 1080 image.
uint32_t patternWord(uint32_t offset);

// Pattern writes allowed from stage 19: only its own word.
bool patternWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage);

// The pattern region: the stage 9 page checks for 8 MiB.
Status checkPatternTarget(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                          const Range *ranges, uint32_t rangeCount, MetricsTarget *target);

// Reads the region twice, pauses apart; kTableRegionInUse if the sums
// differ. sum is the first.
Status regionChecksum(const MemoryReader &memory, uint32_t length, const RegisterWriter &writer, uint32_t pauses,
                      uint64_t *sum);

// No writes. Display pipe 0 at boot 22 (live DCHUBP_CNTL bits ignored), then
// kDisplayExpect: kDisplayUnexpectedState with the index into that list and
// the value read. frameCount is OTG0's.
Status checkDisplayBoot22(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                          uint32_t *value, uint32_t *frameCount);

// Writes the whole pattern region, then reads every word back
// (kPspReadbackMismatch).
Status writePattern(const MemoryReader &pattern, const MemoryWriter &writer, uint32_t stage);

// hubp21_program_surface_flip_and_addr for a linear surface: ADDRESS_HIGH,
// then ADDRESS (which latches at the next vsync); then polls up to
// kDisplayFlipPauses until FLIP_PENDING is 0 and EARLIEST_INUSE is address
// (kDisplayFlipTimeout). address is kPatternGpuAddress or kGopSurfaceAddress.
Status flipDisplay(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint64_t address, uint64_t *inuse, uint32_t *pauses);

struct DisplayReport {
    uint32_t inuseLo, inuseHi, flipControl;
    uint32_t frameCount, framesAdvanced; // OTG0's, and since the flip (24-bit)
    uint32_t displayChanged, displayFirst; // against the check's reading, the address excepted
    uint32_t patternUnexpected, patternFirst;
};

// Reads only. EARLIEST_INUSE must be the pattern, the frame count must have
// advanced since flipFrames, the display inventory must match display
// except pipe 0's address (now the pattern), and the region must hold the
// pattern. kDisplayVerifyFailed otherwise.
Status verifyDisplay(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                     const uint32_t *display, uint32_t flipFrames, uint32_t stage, DisplayReport *report);

// No writes. Pipe 0 back at boot 22 (kDisplayNotRestored with the index and
// value).
Status checkDisplayRestored(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                            uint32_t *index, uint32_t *value);

// Stage 20. Whether a register write is a stage 20 value: one of
// kFlipIntWrites to SURFACE_FLIP_INTERRUPT, or frame 4's write pointer.
bool flipWriteListed(uint32_t offset, uint32_t value);

// The region SDMA fills: stage 19's bands in reverse order, no grey lines,
// zero past the image.
uint32_t fillWord(uint32_t offset);

// The SDMA ring with frame 4 (9 CONST_FILLs, FENCE 4) at dwords 0..255;
// intrRingWord elsewhere.
uint32_t fillRingWord(uint32_t dword);

// No writes. SURFACE_FLIP_INTERRUPT's enables 0 (index 0) and DCHUB_INTERRUPT_DEST2's
// HUBP0 flip destination 0, the host (index 1): kFlipUnexpectedState with the
// index and value. dest2 and continue17 are recorded.
Status checkFlipIntr(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *index,
                     uint32_t *value, uint32_t *dest2, uint32_t *continue17);

// Zeroes the pattern region and reads it back (kPspReadbackMismatch).
Status clearPattern(const MemoryReader &pattern, const MemoryWriter &writer, uint32_t stage);

// Writes frame 4 into the SDMA ring and reads it back (kPspReadbackMismatch).
Status writeSdmaFrame4(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// Reads the whole region against fillWord: kFlipFillMismatch with the count,
// the first offset and the value found there.
Status checkFill(const MemoryReader &pattern, uint32_t stage, uint32_t *unexpected, uint32_t *first,
                 uint32_t *firstValue);

// dal_irq_service_set(true): SURFACE_FLIP_INTERRUPT <- clear, then <- enable.
// readback is the register afterwards; kFlipUnexpectedState unless enabled.
Status armFlipIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *readback);

struct FlipReport {
    uint32_t inuseLo, inuseHi, pauses, msiPauses; // the poll's pauses, then those until the MSI
    uint32_t frameCount;                           // OTG0's after the flip
    uint32_t msiBefore, msiAfter;
    uint32_t flipInterrupt, continue17; // after the flip
    uint32_t ihStart, ihWriteback;      // IH_RB_RPTR before, the write-back after
    uint32_t flipEntries, otherEntries; // new IH entries
    uint32_t latencyMicroseconds;       // filled in by the kext: the address writes to the MSI
    uint32_t entries[kFlipReportEntries * kIhEntryBytes / 4];
};

// flipDisplay to address, then up to kDisplayFlipPauses more pauses for an
// MSI after msiBefore; then reads the new IH entries (from IH_RB_RPTR to the
// write-back). The flip's status first; then kFlipIntrNotDelivered (no MSI),
// or kFlipIntrVerifyFailed (not exactly one MSI and one HUBP0 flip entry).
Status flipWithIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                    const MemoryReader &gartRegion, const InterruptCounter &counter, uint32_t stage, uint64_t address,
                    uint32_t msiBefore, FlipReport *report);

// dal_irq_service_ack: SURFACE_FLIP_INTERRUPT <- clear | enable. readback is
// the register afterwards.
Status ackFlipIntr(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   uint32_t stage, uint32_t *readback);

// Reads only: verifyDisplay against fillWord, and no MSI since msiAtAck
// (kFlipVerifyFailed). msiChange is the count's change.
Status verifyFlip(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                  const uint32_t *display, uint32_t flipFrames, const InterruptCounter &counter, uint32_t msiAtAck,
                  uint32_t stage, DisplayReport *report, uint32_t *msiChange);

// The restore: flipWithIntr back to the GOP surface (its MSI recorded, not
// required), dal_irq_service_set(false) (clear | enable, then 0), then pipe 0 at
// boot 22 and SURFACE_FLIP_INTERRUPT's enables 0 (index 11): kFlipNotRestored with the
// index and value. The flip's status comes first.
Status restoreFlip(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   const MemoryReader &gartRegion, const InterruptCounter &counter, uint32_t stage, uint32_t msiBefore,
                   FlipReport *report, uint32_t *index, uint32_t *value);

// Stage 21. The checks on the five firmware files (kGfxImageInvalid with the
// file in index): their lengths and kGfxHeaderPins.
Status checkGfxFiles(const GfxFiles &files, uint32_t *index);

// The firmware buffer's word at a byte offset: each image's payload in its
// slot, zero elsewhere.
uint32_t gfxFirmwareWord(const GfxFiles &files, uint32_t offset);
bool gfxFirmwareWriteAllowed(const GfxFiles &files, uint32_t offset, uint32_t value, uint32_t stage);

// Writes the whole firmware buffer, then reads it back (kPspReadbackMismatch).
Status writeGfxFirmware(const GfxFiles &files, const MemoryReader &buffer, const MemoryWriter &writer,
                        uint32_t stage);

// The ring's dwords (four frames, NOP-padded), the clear-state buffer's, and
// the work area's (ring, zeroed write-back page, clear-state buffer).
uint32_t gfxRingWord(uint32_t dword);
uint32_t gfxCsbWord(uint32_t dword);
uint32_t gfxWorkWord(uint32_t offset);
bool gfxWorkWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage);
Status writeGfxWork(const MemoryReader &work, const MemoryWriter &writer, uint32_t stage);

// The image the CP draws: three bands of 360 lines, zero past the image.
uint32_t gfxWord(uint32_t offset);

// Whether a register write is a stage 21 value given the check's snapshot
// (kGfxSnapshotCount values in kGfxSnapshotRegisters order): the snapshot
// value itself (the restore), or a kGfxWrites value.
bool gfxWriteAllowed(uint32_t offset, uint32_t value, uint32_t stage, const uint32_t *snapshot);

// The stage 9 page checks for the firmware buffer (1 MiB) and the work area
// (64 KiB).
Status checkGfxTargets(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage,
                       const Range *ranges, uint32_t rangeCount);

// No writes. GFX on, then kGfxExpect (kGfxUnexpectedState with the index and
// value); then snapshots kGfxSnapshotRegisters.
Status checkGfxBoot(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, uint32_t *snapshot,
                    uint32_t *index, uint32_t *value);

// Reads kGfxStateRegisters into values.
struct GfxState {
    uint32_t values[kGfxStateCount];
};
Status readGfxState(const RegisterReader &registers, uint64_t apertureLength, uint32_t stage, GfxState *state);

// golden_settings_gc_9_1_rn (gfx_v9_0_init_golden_registers), then
// gfx_v9_0_rlc_resume as it applies to GC 9.3.0 (part B). writes counts the
// writes made; cuBusy and noncuBusy are the serdes readings (Linux logs a
// timeout and continues, as here).
Status startRlc(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                uint32_t stage, const uint32_t *snapshot, uint32_t *writes, uint32_t *cuBusy, uint32_t *noncuBusy);

// gfx_v9_0_cp_gfx_resume and cp_gfx_start without a doorbell (part C), then
// frame 0: polls the read-pointer write-back for 1024 (kGfxCpTimeout).
Status startCp(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
               const MemoryReader &work, uint32_t stage, const uint32_t *snapshot, uint32_t *writes, uint32_t *rptr,
               uint32_t *cpRptr);

// gfx_v9_0_ring_test_ring (frame 1: kGfxRingTestFailed), then fence 1
// (frame 2: kGfxFenceTimeout).
Status testGfxRing(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                   const MemoryReader &work, uint32_t stage, const uint32_t *snapshot, uint32_t *scratch,
                   uint32_t *fence, uint32_t *rptr);

// Frame 3 (the fills and fence 2: kGfxFenceTimeout), after the CPU clear.
Status submitGfxDraw(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                     const MemoryReader &work, uint32_t stage, const uint32_t *snapshot, uint32_t *fence);

// Reads the pattern region against gfxWord (kGfxDrawMismatch).
Status checkGfxDraw(const MemoryReader &pattern, uint32_t stage, uint32_t *unexpected, uint32_t *first,
                    uint32_t *firstValue);

// Reads only: verifyDisplay against gfxWord (kGfxVerifyFailed).
Status verifyGfxDraw(const RegisterReader &registers, uint64_t apertureLength, const MemoryReader &pattern,
                     const uint32_t *display, uint32_t flipFrames, uint32_t stage, DisplayReport *report);

// The restore: CP_ME_CNTL <- the snapshot (halted), rlc_stop, every
// kGfxSnapshotRegisters entry <- its snapshot in list order, then all read
// back (kGfxNotRestored with the index and value). The first write error is
// returned before that.
Status restoreGfx(const RegisterReader &registers, uint64_t apertureLength, const RegisterWriter &writer,
                  uint32_t stage, const uint32_t *snapshot, uint32_t *index, uint32_t *value);

} // namespace cezanne

#endif
