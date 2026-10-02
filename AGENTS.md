# Cezanne driver operating rules

## Target and scope

Build independent hardware, display, and acceleration drivers for the AMD
Cezanne integrated GPU. Initial target: PCI `1002:1638`, revision `c9`, on
the host reporting Ryzen 5 5600GT and macOS 26.4.1. Treat these as the target
specification; record actual host observations separately. Expand support only
after this host works.

The current milestone is read-only discovery: capture the working baseline,
map hardware blocks, and investigate macOS graphics and Metal contracts.
Driver loading and GPU takeover are deferred until experimental booting is
explicitly available.

## Host constraint

- Perform no GPU register writes, PCI configuration writes, driver installation,
  driver loading/unloading, boot changes, or GPU takeover on the working host.
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
  each host experiment. No driver experiment is authorized by this milestone.
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
