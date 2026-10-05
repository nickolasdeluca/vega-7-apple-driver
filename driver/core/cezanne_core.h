// Cezanne hardware core: identity, PCI state, boot-state registers and the
// IP discovery table.
//
// Freestanding C++ shared by the kext and the host unit tests: it uses no
// IOKit, libc or allocation. The adapter supplies read callbacks and, for the
// stage 6 scratch test and stage 7 SMU queries, one write callback. The write
// allowlist names exact registers and values: SCRATCH_REG0 (stage 6) and the
// three SMU mailbox writes of a version query (stage 7), plus the
// DisableGfxOff message (stage 8), plus the three metrics-table messages and
// their arguments (stage 9). Widening it is a reviewed stage change
// (docs/test-boot.md).
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
const uint32_t kMaxStage = 9;

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
const uint32_t kDiagnosticVersion = 5;
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
    kDiagnosticSelectorCount = 11,
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

} // namespace cezanne

#endif
