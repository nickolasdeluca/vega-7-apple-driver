# Cezanne driver operating rules

## Starting or resuming work

- Read [the project handoff](docs/handoff.md) for the current checkpoint, next
  task, evidence locations and verification commands. Read its linked studies
  as needed; conversation history is not required to resume.
- Check `git status --short` and recent history before making changes. Preserve
  unrelated work and do not redo completed investigation merely because the
  context is fresh.
- Treat ignored `out/` captures as local evidence, not files guaranteed to exist
  in another checkout. If missing, use the tracked reproduction instructions
  within the host constraint below, and record unavailable evidence explicitly.
- Update the handoff after each completed batch when the checkpoint, next task,
  verification or blocking requirements change.

## Target and scope

Build independent hardware, display, and acceleration drivers for the AMD
Cezanne integrated GPU. Initial target: PCI `1002:1638`, revision `c9`, on
the host reporting Ryzen 5 5600GT and macOS 26.4.1. Treat these as the target
specification; record actual host observations separately. Expand support only
after this host works.

The current milestone is read-only discovery: capture the working baseline,
map hardware blocks, and investigate macOS graphics and Metal contracts.
Experimental booting is available only through the USB test EFI in
[docs/test-boot.md](docs/test-boot.md), at the stage that document authorizes.

## Host constraint

- In the known-good boot, perform no GPU register writes, PCI configuration
  writes, driver installation, driver loading/unloading, boot changes, or GPU
  takeover.
- Driver code runs only when the user boots the USB test EFI, injected by that
  EFI's OpenCore config, and only at the authorized stage in
  [docs/test-boot.md](docs/test-boot.md) (currently stage 0, passive;
  stage 1, read-only device access; stage 2, a write-free read of the IP
  discovery table from the carveout; stage 3, read-only GC configuration
  registers; stage 4, a root-only, read-only diagnostic interface; stage 5,
  38 more read-only state registers through that interface; and stage 6, the
  first reviewed write, a reversible `SCRATCH_REG0` test run on request; and
  stage 7, the first SMU messages, two version queries run on request; and
  stage 8, `DisallowGfxOff` sent on request; and stage 9, the SMU metrics
  table written to one checked carveout page on request; and stage 10, 21
  more read-only PSP mailbox and aperture registers through the diagnostic
  interface; and stage 11, the first PSP commands, creating and destroying
  the kernel-mode ring at one checked carveout page on request; and stage 12,
  the first CPU writes to carveout memory and the first ring frames,
  `SETUP_TMR` then `DESTROY_TMR`, on request; and stage 13, the first firmware
  load, the pinned SDMA0 image through `LOAD_IP_FW` with the engine left
  halted, on request; and stage 14, `PowerUpSdma`/`PowerDownSdma` and 25
  read-only SDMA registers, on request; and stage 15, starting SDMA0 with
  exact register values and the first 4 KiB copy with a fence, on request;
  and stage 16, 92 read-only display, memory-hub VM and interrupt registers;
  and stage 17, GART (MMHUB context 0) and IH ring 0 with values pinned from
  boot 22, an SDMA copy through GART with a fence and a trap, then a restore
  of every register, on request; and stage 18, IH interrupt delivery through
  the GPU's MSI vector to one counting handler, a fence and a trap, one
  acknowledgement, then a restore, on request).
  Advancing
  a stage needs a reviewed update to that document and the user's approval.
  Never modify the internal EFI partition, install driver code on the macOS
  volume, or load it with `kmutil`. The user performs disk, EFI and reboot steps.
- Use read-only OS queries. Tools may write reports and build artifacts inside
  the repository or a requested output directory, but must not mutate graphics
  or system configuration. Do not use privileged probes to bypass restrictions.
- Preserve command failures, stderr, exit status, and unavailable fields.
  Missing evidence is not a zero value or proof that a capability is absent.
- Keep machine identifiers and full raw registry captures out of tracked files;
  commit a reviewed summary and reproducible collection instructions.

## Architecture boundaries

- **Cezanne hardware core:** register definitions/access abstraction, firmware
  validation/loading, GPU memory translation, interrupts, queues, fences,
  display control, and power state transitions. Keep OS policy outside it.
- **IOKit adapter:** PCI ownership and matching, memory/DMA mappings, interrupts,
  lifetime/security, framebuffer and accelerator services, and versioned
  user-client communication. Keep hardware algorithms in the core.
- **User-space graphics driver:** device discovery, resource/command management,
  shared surfaces, synchronization, rendering and presentation. Shader
  compilation is a separate backend with independently verified outputs.
- General Apple OS frameworks remain dependencies. Apple AMD driver binaries
  are excluded from the finished stack. Observing the existing stack is allowed.
  From scratch applies to host-side code; AMD-provided microcode is permitted.
  Firmware bring-up must address signed images and validation.
- Use AMD documentation and upstream AMDGPU as hardware references, preserve
  source attribution, and review licensing before incorporating code or firmware.
  Distinguish source-backed facts, local observations, hypotheses, and unknowns.
- Investigate Metal discovery before substantial hardware bring-up.
  IOFramebuffer alone does not establish desktop or Metal compatibility.

## Verification and milestones

- Verify each batch before committing. Run checks appropriate to the change
  and use concise commit messages explaining its purpose.
- Test diagnostic parsing and failure paths using controlled inputs. Compare
  the captured baseline with independent system queries. Preserve raw evidence
  locally so reported values can be audited.
- Documents must cite primary references, identify unresolved interfaces, and
  give the next experiments and measurable success criteria.
- Do not claim hardware functionality based on documentation or fixtures.
  Record the OS build, device/revision, commands, results, and limitations for
  each host experiment. Driver experiments are limited to the staged USB test
  boot.
- Future stages: PCI ownership/diagnostics; firmware, mappings, interrupts and
  verified DMA copy/fence; one connector/mode test pattern; known shader with
  correct results; own Metal discovery/rendering/surfaces/presentation; desktop
  composition, recovery, sleep/wake and broader workloads.

## Committing rules

- Never add agents, models, or AI tools as co-authors. Do not append agent
  `Co-authored-by` trailers. Use the configured Git author identity.
- Work in small, logically complete batches. Each batch and commit should
  serve one clear purpose; keep unrelated changes separate.
- Verify each batch before committing. Run checks appropriate to the change
  and use concise commit messages explaining its purpose.
- Commit this root AGENTS.md as the first focused batch, before implementing
  diagnostic tooling. Stage only the files belonging to the batch.
