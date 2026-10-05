# IOAcceleratorFamily2 queue reply production

Read-only inspection of the installed family now connects the queue submission
entry to seven-word scheduling/completion replies and identifies the registration
owner's wake-port release. The ordinary per-entry implementation attempts one
scheduling send and queues a block fence for a later completion send; its early
error implementation attempts both sends immediately. **These are static paths,
not evidence of delivery, exactly-once callbacks, cancellation drain or GPU
completion.** Virtual overrides and concurrent execution remain unverified.

This continues the [installed dispatcher comparison](iokit-async-dispatch.md),
[generic async-reply study](xnu-async-replies.md) and
[user-space block ownership study](ioaccel-block-ownership.md).

## Experiment and evidence boundaries

Inspection on 2026-10-05 used ordinary file reads only. No kernel debugger,
callbacks, messages, connections, queues, mappings or GPU work were invoked.
No collection or extension was rewritten, loaded, installed or rebuilt.

| Item | Observation |
| --- | --- |
| OS / architecture | macOS 26.4.1 build 25E253; Darwin 25.4.0, x86_64 |
| Device / CPU | PCI `1002:1638:c9`, reported Ryzen 5 5600GT reused from the verified baseline; fresh CPU and `kern.version` sysctls were denied with exit 1 |
| Family | `com.apple.iokit.IOAcceleratorFamily2`, bundle version 487.4.3; embedded in `/System/Library/KernelCollections/SystemKernelExtensions.kc` |
| Tools | cctools-1040; Apple LLVM/clang 21.0.0; the previously verified official LLVM 20.1.7 archive from the [shader study](shader-target.md) |
| Readability | Family has 4,705 symbol-table entries and 357,364 bytes in its `__text` section; the outer fileset's symbol table is empty |
| Generic references | Installed `IOUserClient` names resolved in the `com.apple.kernel` entry of `BootKernelExtensions.kc`; behavioral comparison uses the previously pinned XNU source, which differs from the running kernel |

Ignored `out/ioaccel-family-replies-20261005/` preserves commands, UTC times,
stdout/stderr, statuses, collection digests, the inspection utility and readable
and normalized instructions. The original parked captures remain in
`out/ioaccel-family-replies/`. Machine UUIDs, raw file virtual addresses and full
symbol/disassembly captures remain untracked. The bundle version is metadata;
it does not establish the provenance of any runtime patch.

The stock `llvm-objdump --macho --syms` returned exit 0 with only the outer
collection's empty table. That did not show that the embedded family lacked
symbols. A small offline utility instead used LLVM's
[`createMachOObjectFile(..., MachOFilesetEntryOffset)`](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.7/llvm/include/llvm/Object/ObjectFile.h#L393)
on the original read-only buffer and the selected `LC_FILESET_ENTRY` offset.
It used LLVM's [disassembler API](https://github.com/llvm/llvm-project/blob/llvmorg-20.1.7/llvm/include/llvm-c/Disassembler.h)
on the family's sections. No standalone extension image was produced.

External near calls go through the collection's `__BRANCH_STUBS` and
`__BRANCH_GOTS`. The inspected chains use pointer format 11
(`DYLD_CHAINED_PTR_X86_64_KERNEL_CACHE`). The SDK
`mach-o/fixup-chains.h` defines the 30-bit target and two-bit cache level in
[`dyld_chained_ptr_64_kernel_cache_rebase`](https://github.com/apple-oss-distributions/dyld/blob/main/include/mach-o/fixup-chains.h).
For these references, the level-0 target plus the boot collection's lowest
segment address resolves to the named kernel symbol; level 1 resolves within the
system collection. Vtable slots were checked against the same encoded pointers.
This is file linkage, not an observation of a live object's vtable or a loaded
kernel address. Folded cold symbols share addresses and cannot be uniquely
attributed by name.

The linear disassembly includes 250 undecoded single-byte fallback rows. Claims
below use bounded named methods and verified call/field instructions. A direct
branch scan is not an inventory of indirect calls, vendor overrides or all
possible runtime paths.

## Registration storage and release

Offsets below are bytes relative to each named function or object in this build.
They are observations, not an SDK layout or a versioned independent protocol.

| Site | Observed path |
| --- | --- |
| `IOAccelCommandQueue::s_set_notification_port`, `+0x4..+0x19` | Reads the async-reference pointer at external-arguments offset `0x10`; rejects null with `0xe00002c2`, otherwise tail-calls the setter. This wrapper does not read the reference count. |
| `IOAccelCommandQueue::set_notification_port`, `+0x58..+0xec` | Existing queue registration at object offset `0x580` gives `0xe00002c9`. Otherwise allocates/constructs an 80-byte `IOAccelBlockFencePort2`, stores it at `0x580`, invokes its initializer, and on initializer failure releases/clears it and returns `0xe00002bd`. |
| `IOAccelBlockFencePort2::initWithAsyncRef64`, `+0x24..+0x38` | Loads incoming reference words 0, 1 and 2 and calls `IOUserClient::setAsyncReference64` with destination `portObject + 0x10`; marks initialization with bit 0 at `portObject + 0x0c`. No new wake-port acquisition is visible here. |
| `IOAccelBlockFencePort2::free`, `+0x9..+0x18` | Tests that bit, then calls `IOUserClient::releaseAsyncReference64(portObject + 0x10)` before superclass cleanup. |

The initializer uses the four-argument setter, not the task-taking overload.
Under the pinned generic async-method path, incoming slot 0 includes the
helper's wake-port value and flags. The pinned
[setter](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L1360)
copies these values without acquiring another right. The
[release helper](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L2237)
masks flags and releases the held non-null naked send right. Installed names and
family calls are verified here; equivalence of those installed helper bodies to
the pinned source is not asserted.

The initializer passes incoming slots 0–2 to the setter; the pinned setter writes
those three slots. Allocation zeroing and the remaining installed reference words
are not established by this batch. No release of
the incoming reference is visible in the setter's duplicate-registration or
allocation/initialization rejection branches. Installed MIG/error cleanup remains
an unresolved boundary; this observation alone does not prove a leak.
The minimum three-word requirement also cannot be delegated to the wrapper's
null-pointer check. The complete installed kernel validation remains open.

## Reply schema and send sites

The two block-fence send sites call the resolved
`IOUserClient::sendAsyncResult64` with callback result **0** and count **7**:

| Caller | Send offset | Reference / producer |
| --- | --- | --- |
| `IOAccelFenceMachine::sendBlockFenceNotification` | `+0x51` | Its supplied reference pointer; used for scheduling and immediate errors |
| `IOAccelBlockFence::notifyClient` | `+0xab` | Retained port object at fence offset `0xd8`, reference at port offset `0x10`; used on the inspected completed-fence path |

Both construct the same seven-word schema:

| Argument word / byte offset | Written value | Saved user-space consumer |
| --- | --- | --- |
| 0 / 0 | Opaque value supplied for the user block | Loads it as the block invocation argument |
| 1 / 8 | Second opaque value; queue callers supply zero | Not read by the saved callback |
| 2 / 16 | Full 64-bit first time value | Forwarded to the block |
| 3 / 24 | First time value shifted right 32 | Not read by the saved callback |
| 4 / 32 | Full 64-bit second time value | Forwarded to the block |
| 5 / 40 | Second time value shifted right 32 | Not read by the saved callback |
| 6 / 48 | 32-bit status zero-extended to 64 bits | Low 32 bits forwarded to the block |

The queue's 24-byte entry contains two resource IDs and the copied scheduling
and completion block values at entry offsets 8 and 16. The batch loop loads them
at `submit_command_buffers +0x302/+0x307` and passes them to the per-entry method.
The kernel treats the block values as integers; no Objective-C block invocation,
copy or release is visible in these producers.

The count meets the [consumer's seven-word requirement](xnu-async-replies.md#dispatcher-and-the-saved-consumer).
With the pinned generic 64-bit sender and the compared installed dispatcher,
the expected simple wire envelope is 160 bytes (`104 + 7*8`); the argument array
is 56 bytes. This is a source-derived expectation, not a captured message.
The zero callback result is separate from payload status and the send helper's
Mach return. Both producers ignore that return; neither supplies retry or
user-space block disposal on a failed send.

There is also a display-pipe wrapper that tail-calls the same generic sender at
`IOAccelDisplayPipeUserClient2::sendAsyncResult64 +0x5`. Its callers belong to
vblank/transaction notifications and are outside the command-queue schema above.
The observed direct release-helper caller is the block-fence port's `free`.
These are direct-call observations, not proof that indirect helper use is absent.

## Per-entry attempts, errors and time values

`IOAccelCommandQueue::submit_command_buffer` preserves the incoming scheduling
and completion values and reads `mach_absolute_time` at `+0x20`.

| Branch | Static attempts and ownership |
| --- | --- |
| No initial error and block-fence initialization succeeds | Creates a block fence using the completion value and second opaque value 0. After `process_command_buffer`, reads `mach_absolute_time` again and makes the scheduling send at `+0x129`, with those two host times and the queue's current status. Copies a nonzero processing status into fence offset `0xc0`, then inserts the fence through `addEventFence` at `+0x1a3`. |
| Initial queue error, fence allocation/initialization failure, or queue error observed after initialization and before command-buffer processing | Allocation/init failure sets status 8. Makes scheduling and completion sends at `+0x20b` and `+0x23c`; both times equal the initial host time, and both sends carry the current queue status. Releases/clears any constructed fence. |
| Batch rejected before the per-entry loop | The batch setter returns a nonzero method result without entering these per-entry sends. The user-space wrapper's local error cleanup remains a separate path. |

The successful per-entry branch contains one scheduling attempt and one pending
completion object; the error branch contains two immediate attempts. That is
consistent with the user-space two-block/two-retain design. It does not prove
that each accepted entry reaches either branch exactly once, that a completed
fence is notified once under races/reset, or that an attempted send is delivered.
Per-entry errors can be reported in replies while the batch method returns zero:
the batch loop does not consume a per-entry return and sets its own result to
zero after the loop (`+0x33d`).

The scheduling times measure the host interval around command-buffer processing;
they are native `mach_absolute_time` values, with no conversion observed here.
They are not established GPU timestamps. The base block-fence `getHostTime`
reads fence offsets `0x18/0x20`; `notifyClient +0x3a..+0x5a` falls back to those
fields when its virtual time getter yields a zero second value. The inspected
fence-machine paths populate these fields from `mach_absolute_time` after fence
predicate checks. A vendor time-getter override and the predicates' relationship
to actual GPU completion remain unverified.

## Cancellation, completion and teardown

`IOAccelBlockFence::init +0x26..+0x3e` stores and retains the port object at
fence offset `0xd8`. Its `free +0x9..+0x16` releases and clears that owner.
The base event-fence initializer stores the submitter as a raw pointer at `0xb8`;
it does not retain that submitter in the inspected instructions.

The local block-fence vtable maps slot `0x120` to `notifyClient`, `0x128` to its
initializer and `0x130` to its time getter. These identify the static base-class
targets of the virtual calls; the runtime subclass is not established here.

| Path | Visible behavior and limit |
| --- | --- |
| `process_eventfences` | Checks start/end fence predicates through virtual slot `0x180`. On end-predicate success, records a host time, unlinks the pending entry, appends it to the completed list, decrements the pending count, optionally calls the submitter's retirement hook, then calls fence slot `0x120` and clears the raw submitter pointer (`+0x74..+0x12c`). |
| `getCompletedEventFences` / `IOGraphicsAccelerator2::collectBlockFences` | Transfers the completed list under the machine lock; the collector unlinks each transferred entry and calls its release slot. Notification precedes this collector release in the inspected path. Concurrent collector ordering and reference counts are not established. |
| `IOAccelCommandQueue::commandQueueStop` | Releases/clears the queue's port-object owner at `+0xb..+0x20`, then calls `cancelBlockFenceCallback` at `+0x96`. A block fence can still own that port object. Queue stop is not immediate wake-right release or callback drain. |
| `cancelBlockFenceCallback` | Under the machine lock, removes the submitter from the active-submitter list, splices its pending fences onto the machine list at `0x50`, clears the submitter's pending head, and clears each fence's raw submitter pointer (`+0x16..+0xc4`). It does not clear the block value, release the port, change status or send a reply. The event notifier later processes the machine's `0x50` list through the same predicate-checking method. Cancellation does not prove immediate completion, notification suppression or hardware cancellation. |
| `IOAccelFenceMachine::free` | Walks active-submitter and machine `0x50` lists, unlinks entries and releases them (`+0x79..+0x167`). No notification call appears in those release loops. A block fence's destructor chain can release its port owner; no user-space block-disposal protocol is established for such a dropped notification. Other lists/callers and complete shutdown ordering remain open. |
| Reset/error helpers | `guiltyForHardwareReset` can write fence status at `0xc0`; `setErrorOnEventFenceWithID` writes it only when the current status is zero. They do not themselves send a reply or establish terminal predicate behavior. |

The pinned generic sender's null/dead-port success cases and Mach-right lifetime
limits still apply as source constraints. Releasing the registration right and
releasing user-space copied blocks are different ownership mechanisms. These
static paths establish neither complete loss/duplicate cleanup nor a safe outer
command-buffer/storage reuse boundary.

## Reproduction and checks

Use a fresh ignored directory `D`. Reuse the verified LLVM 20.1.7 archive from
[shader-target.md](shader-target.md); do not install anything.
Save the utility and scripts below in `D`. `build.py` takes `D` and the unpacked
LLVM root, obtains its library flags and links the local utility with a runtime
path to that directory. On this archive, `llvm-config --system-libs` includes
`/usr/local/lib/libzstd.a`; if unavailable, record a build failure rather than
installing or substituting an unverified dependency.

```sh
python3 D/build.py D out/shader-target/llvm/LLVM-20.1.7-macOS-X64
python3 D/capture.py D family-symbols D/inspect /System/Library/KernelCollections/SystemKernelExtensions.kc com.apple.iokit.IOAcceleratorFamily2 symbols
python3 D/capture.py D family-text D/inspect /System/Library/KernelCollections/SystemKernelExtensions.kc com.apple.iokit.IOAcceleratorFamily2 text
python3 D/capture.py D kernel-symbols D/inspect /System/Library/KernelCollections/BootKernelExtensions.kc com.apple.kernel symbols
python3 D/capture.py D analysis python3 D/analyze.py D
python3 D/capture.py D fixtures python3 D/fixtures.py D
python3 D/capture.py D independent-check python3 D/check.py D
```

Re-record `sw_vers`, `uname -r`, tool/bundle versions and collection SHA-256
digests with the same capture wrapper. Preserve denied sysctls and all failures.
The scripts are offline study utilities, not a supported arbitrary-binary parser
or an extension extraction tool. The instruction map must be reviewed afresh on
another build; symbol names and these offsets are not stable interfaces.

The synthetic fixture has one embedded symbol and eight bytes of code. Its
symbol and instruction reads passed; a missing entry and a truncated header
returned exit 1. No fixture code was executed. The eight independent check groups
compare the original collection's embedded headers and symbol/string tables,
every text byte, branch-stub linkage and selected schema/ownership instructions.
Fresh extraction of the documented scripts reproduced identical symbol,
instruction and normalized outputs in
`out/ioaccel-family-replies-reproduced-20261005/`. All 42 current repository tests
passed; results and captures are indexed locally. These checks establish inspection accuracy, not
installed runtime behavior.

Preserved failures include unsupported help flags, `dyld_info -validate_only`
aborting in its `Array.h` assertion, the initial inspector build's `lc_str`
union-member error, missing C++ ABI linkage, a missing local runtime path, and
the normalizer's initial handling of undecoded-byte rows, and four corrected
offset transcriptions in the first checker draft. These are inspection
tool failures, not evidence of installed family failure. The corrected utility
reads the family without elevation or changing protections. LLVM library/header
license notices remain in the verified archive; no Apple implementation body or
driver binary is incorporated into the project.

## Remaining gates

The static producer/consumer layout question is answered for these base-family
paths. The current [handoff's shader-frontend task](handoff.md#offline-task-while-waiting-metal-shader-frontend-boundary)
can continue independently. Further queue-contract work must establish:

- Installed generic async helper and kernel MIG request/error cleanup behavior,
  including reference counts below three and failed/repeated registration.
- Actual vendor overrides, full-reference initialization and the complete
  negotiated external-method contract.
- Ordering, cancellation/reset exclusivity, send loss/duplicates and complete
  cleanup of user-space blocks/queue retains; static send attempts are insufficient.
- Fence predicate meaning, collector concurrency, mapping/backing reclamation
  and storage reuse; none is established by a readable notification producer.

These remain read-only source/static questions where evidence is available.
For the next family-contract batch, inspect bounded installed kernel methods for
the four-argument setter, release helper, sender and async-method validation/error
cleanup. Compare every reference read, wake-right acquisition/release and count
check with the pinned source. Success means a byte-backed map of those branches
and a list of agreements/differences, or explicit unavailable evidence; it cannot
establish live callback delivery. Then map relevant vendor vtable overrides from
readable collection files, with success limited to named static dispatch targets
and unresolved indirect paths.

Runtime queue experiments and independent-driver bring-up require the separately
authorized experimental environment and recovery path described in [AGENTS.md](../AGENTS.md).

<details>
<summary><code>inspect.cpp</code></summary>

```cpp
#include "llvm/Object/MachO.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm-c/Disassembler.h"
#include <map>
#include <string>
#include <cstring>

extern "C" void LLVMInitializeX86TargetInfo();
extern "C" void LLVMInitializeX86TargetMC();
extern "C" void LLVMInitializeX86Disassembler();
using namespace llvm;
using namespace llvm::object;

int main(int argc, char **argv) {
  if (argc != 4) { errs() << "usage: inspect collection entry symbols|text\n"; return 2; }
  auto buffer = MemoryBuffer::getFile(argv[1], false, false);
  if (!buffer) { errs() << buffer.getError().message() << '\n'; return 1; }
  auto outer = ObjectFile::createMachOObjectFile((*buffer)->getMemBufferRef());
  if (!outer) { errs() << toString(outer.takeError()) << '\n'; return 1; }
  uint64_t offset = 0;
  for (const auto &lc : (*outer)->load_commands()) {
    if (lc.C.cmd != MachO::LC_FILESET_ENTRY) continue;
    auto entry = (*outer)->getFilesetEntryLoadCommand(lc);
    if (entry.entry_id.offset >= lc.C.cmdsize) { errs() << "entry name out of bounds\n"; return 1; }
    StringRef bytes(lc.Ptr + entry.entry_id.offset, lc.C.cmdsize - entry.entry_id.offset);
    size_t end = bytes.find('\0');
    if (end == StringRef::npos) { errs() << "unterminated entry name\n"; return 1; }
    if (bytes.take_front(end) == argv[2]) { if (offset) return 1; offset = entry.fileoff; }
  }
  if (!offset) { errs() << "entry unavailable\n"; return 1; }
  auto object = ObjectFile::createMachOObjectFile((*buffer)->getMemBufferRef(), 0, 0, offset);
  if (!object) { errs() << toString(object.takeError()) << '\n'; return 1; }
  if (auto error = (*object)->checkSymbolTable()) { errs() << toString(std::move(error)) << '\n'; return 1; }
  outs() << "ENTRY " << argv[2] << " FILEOFF " << offset << '\n';
  std::map<uint64_t, std::string> names;
  unsigned count = 0;
  for (auto symbol : (*object)->symbols()) {
    auto name = symbol.getName();
    auto address = symbol.getAddress();
    if (!name || !address) {
      if (!name) errs() << toString(name.takeError()) << '\n';
      if (!address) errs() << toString(address.takeError()) << '\n';
      return 1;
    }
    count++;
    if (*address && !name->empty()) names[*address] = name->str();
    if (strcmp(argv[3], "symbols") == 0)
      outs() << format_hex(*address, 18) << '\t' << *name << '\t' << demangle(name->str()) << '\n';
  }
  outs() << "SYMBOL_COUNT " << count << '\n';
  LLVMInitializeX86TargetInfo(); LLVMInitializeX86TargetMC(); LLVMInitializeX86Disassembler();
  auto dc = LLVMCreateDisasm("x86_64-apple-darwin", nullptr, 0, nullptr, nullptr);
  if (!dc) { errs() << "no disassembler\n"; return 1; }
  LLVMSetDisasmOptions(dc, LLVMDisassembler_Option_PrintImmHex);
  for (auto section : (*object)->sections()) {
    auto name = section.getName();
    if (!name) { errs() << toString(name.takeError()) << '\n'; return 1; }
    outs() << "SECTION " << *name << " ADDRESS " << format_hex(section.getAddress(),18)
           << " SIZE " << section.getSize() << " TEXT " << section.isText() << '\n';
    if (strcmp(argv[3], "text") != 0 || !section.isText()) continue;
    auto bytes = section.getContents();
    if (!bytes) { errs() << toString(bytes.takeError()) << '\n'; return 1; }
    uint64_t cursor = 0;
    while (cursor < bytes->size()) {
      uint64_t pc = section.getAddress() + cursor;
      auto found = names.find(pc);
      if (found != names.end()) outs() << "FUNCTION " << format_hex(pc,18) << ' ' << found->second << '\n';
      char text[512];
      auto length = LLVMDisasmInstruction(dc, reinterpret_cast<uint8_t*>(const_cast<char*>(bytes->data()+cursor)),
                                         bytes->size()-cursor, pc, text, sizeof(text));
      if (!length) { outs() << format_hex(pc,18) << "\tBYTE " << format_hex((unsigned char)(*bytes)[cursor],4) << '\n'; cursor++; continue; }
      outs() << format_hex(pc,18) << '\t';
      for (unsigned i=0;i<length;i++) outs() << format_hex_no_prefix((unsigned char)(*bytes)[cursor+i],2);
      outs() << '\t' << text << '\n';
      cursor += length;
    }
  }
  LLVMDisasmDispose(dc);
  return 0;
}
```

</details>

<details>
<summary><code>capture.py</code></summary>

```python
import datetime
import importlib.util
import json
import sys
from pathlib import Path

spec = importlib.util.spec_from_file_location('baseline', 'tools/baseline.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)
root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
name = sys.argv[2]
result = baseline.capture(name, sys.argv[3:], root, timeout=120)
result['captured_at_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(root / (name + '.capture.json')).write_text(json.dumps(result, indent=2) + '\n')
print(name, result['status'], result['exit_code'])
```

</details>

<details>
<summary><code>build.py</code></summary>

```python
import importlib.util
import json
import shlex
import datetime
import sys
from pathlib import Path
spec = importlib.util.spec_from_file_location('baseline', 'tools/baseline.py')
baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(baseline)
root = Path(sys.argv[1])
llvm = Path(sys.argv[2])
argv = [str(llvm/'bin/llvm-config'), '--cxxflags', '--ldflags', '--libs', 'object', 'support', 'x86disassembler', 'x86desc', '--system-libs']
flags = baseline.capture('build-flags', argv, root)
flags['captured_at_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(root/'build-flags.capture.json').write_text(json.dumps(flags,indent=2)+'\n')
if flags['exit_code'] != 0: raise SystemExit('llvm-config failed')
args = shlex.split((root/'build-flags.stdout').read_text())
build = ['xcrun', 'clang++', str(root/'inspect.cpp'), '-o', str(root/'inspect'), *args, '-lc++abi', '-Wl,-rpath,'+str((llvm/'lib').resolve())]
result = baseline.capture('build', build, root,timeout=120)
result['captured_at_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
(root/'build.capture.json').write_text(json.dumps(result,indent=2)+'\n')
print(result['status'], result['exit_code'])
```

</details>

<details>
<summary><code>analyze.py</code></summary>

```python
import bisect
import json
import mmap
import re
import struct
import sys
from pathlib import Path

root = Path(sys.argv[1])
family_names = {}
kernel_names = {}
for filename, target in [('family-symbols.stdout', family_names), ('kernel-symbols.stdout', kernel_names)]:
    for line in (root/filename).read_text().splitlines():
        if line.startswith('0x'):
            address, name, demangled = line.split('\t', 2)
            address = int(address, 16)
            if address:
                target.setdefault(address, []).append((name, demangled))

file = Path('/System/Library/KernelCollections/SystemKernelExtensions.kc').open('rb')
data = mmap.mmap(file.fileno(), 0, access=mmap.ACCESS_READ)
def segments(path):
    with path.open('rb') as stream:
        header = stream.read(32)
        assert struct.unpack_from('<I',header)[0] == 0xfeedfacf
        commands = stream.read(struct.unpack_from('<I',header,20)[0])
        cursor = 0
        result = {}
        for _ in range(struct.unpack_from('<I',header,16)[0]):
            cmd,size = struct.unpack_from('<II',commands,cursor)
            assert size>=8 and cursor+size<=len(commands)
            if cmd==25:
                name,addr,vsize,offset,fsize=struct.unpack_from('<16sQQQQ',commands,cursor+8)
                result[name.split(b'\0')[0].decode()] = (addr,offset,fsize)
            cursor += size
        assert cursor == len(commands)
        return result

outer_segments=segments(Path('/System/Library/KernelCollections/SystemKernelExtensions.kc'))
boot_segments=segments(Path('/System/Library/KernelCollections/BootKernelExtensions.kc'))
boot_base=min(value[0] for value in boot_segments.values())
stub_addr,stub_offset,stub_size=outer_segments['__BRANCH_STUBS']
got_addr,got_offset,got_size=outer_segments['__BRANCH_GOTS']

def label(address):
    if address in family_names:
        if len(family_names[address])>4:
            return '[shared folded symbol: '+str(len(family_names[address]))+' aliases]'
        return ' / '.join(x[1] for x in family_names[address])
    if address in kernel_names:
        return ' / '.join(x[1] for x in kernel_names[address])
    return hex(address)

def stub_label(address):
    if not (stub_addr <= address <= stub_addr + stub_size - 6):
        return label(address)
    stub_pos=stub_offset+address-stub_addr
    if data[stub_pos:stub_pos+2] != b'\xff\x25':
        return label(address)
    slot = address + 6 + struct.unpack_from('<i', data, stub_pos+2)[0]
    assert got_addr <= slot <= got_addr+got_size - 8
    raw = struct.unpack_from('<Q', data, got_offset+slot-got_addr)[0]
    level = (raw >> 30) & 3
    assert level in (0, 1) and raw >> 63 == 0
    target = (raw & 0x3fffffff) + (boot_base if level == 0 else 0)
    return label(target) + ' [branch stub]'

functions = {}
current = None
for line in (root/'family-text.stdout').read_text().splitlines():
    if line.startswith('FUNCTION '):
        _, address, name = line.split(' ', 2)
        current = name
        functions[name] = {'address': int(address,16), 'rows': []}
    elif line.startswith('0x') and current:
        fields = line.split('\t',2)
        if len(fields) == 2:
            address, text = fields
            encoded = ''
        else:
            address, encoded, text = fields
        address = int(address,16)
        raw = bytes.fromhex(encoded) if encoded != 'BYTE' else b''
        text = text.strip()
        if len(raw)==5 and raw[0] in (0xe8,0xe9):
            target=address+5+struct.unpack_from('<i',raw,1)[0]
            text += ' => ' + stub_label(target)
        elif len(raw)==2 and (raw[0]==0xeb or 0x70<=raw[0]<=0x7f):
            target=address+2+struct.unpack_from('<b',raw,1)[0]
            text += ' => +' + hex(target-functions[current]['address'])
        elif len(raw)==6 and raw[0]==0x0f and 0x80<=raw[1]<=0x8f:
            target=address+6+struct.unpack_from('<i',raw,2)[0]
            text += ' => +' + hex(target-functions[current]['address'])
        functions[current]['rows'].append([address-functions[current]['address'],encoded,text])

(root/'functions.json').write_text(json.dumps(functions,indent=2)+'\n')
with (root/'normalized.txt').open('w') as output:
    for name, function in functions.items():
        output.write(name+'\n')
        for offset,raw,text in function['rows']:
            output.write(f'  +{offset:04x}\t{raw}\t{text}\n')
with (root/'async-sites.txt').open('w') as output:
    for name, function in functions.items():
        for offset,raw,text in function['rows']:
            if any(x in text for x in ('sendAsyncResult','releaseAsyncReference','setAsyncReference','sendBlockFenceNotification','notifyClient')):
                output.write(f'{name} +{offset:x}: {text}\n')
print('functions',len(functions),'async sites saved')
```

</details>

<details>
<summary><code>fixtures.py</code></summary>

```python
import json
import struct
import subprocess
import sys
from pathlib import Path

root=Path(sys.argv[1])
name=b'fixture\0'
entry_size=40
outer=struct.pack('<8I',0xfeedfacf,0x1000007,3,12,1,entry_size,0,0)
outer+=struct.pack('<IIQQII',0x80000035,entry_size,0x400,0x400,32,0)+name
code=bytes.fromhex('b90700000031f6c3')
text=struct.pack('<II16sQQQQIIII',25,152,b'__TEXT',0x400,0xc10,0x400,0xc10,5,5,1,0)
text+=struct.pack('<16s16sQQIIIIIIII',b'__text',b'__TEXT',0x1000,len(code),0x1000,0,0,0,0x80000400,0,0,0)
link=struct.pack('<II16sQQQQIIII',25,72,b'__LINKEDIT',0x1010,0x20,0x1010,0x20,1,1,0,0)
strings=b'\0_fixture\0'
sym=struct.pack('<6I',2,24,0x1010,1,0x1020,len(strings))
child=struct.pack('<8I',0xfeedfacf,0x1000007,3,11,3,len(text+link+sym),0,0)+text+link+sym
data=bytearray(0x2000)
data[:len(outer)]=outer
data[0x400:0x400+len(child)]=child
data[0x1000:0x1000+len(code)]=code
data[0x1010:0x1020]=struct.pack('<IBBHQ',1,15,1,0,0x1000)
data[0x1020:0x1020+len(strings)]=strings
(root/'fixture.kc').write_bytes(data)
(root/'truncated.kc').write_bytes(data[:20])
cases=[('fixture-symbols',root/'fixture.kc','fixture','symbols',0),
       ('fixture-text',root/'fixture.kc','fixture','text',0),
       ('missing-entry',root/'fixture.kc','missing','symbols',1),
       ('truncated-input',root/'truncated.kc','fixture','symbols',1)]
for case,path,entry,mode,expected in cases:
    run=subprocess.run([sys.executable,str(root/'capture.py'),str(root),case,str(root/'inspect'),str(path),entry,mode],check=True)
    saved=json.loads((root/(case+'.capture.json')).read_text())
    assert saved['exit_code']==expected,(case,saved)
text=(root/'fixture-text.stdout').read_text()
assert 'SYMBOL_COUNT 1' in text and 'SIZE 8 TEXT 1' in text
assert 'movl\t$0x7, %ecx' in text and 'xorl\t%esi, %esi' in text and 'retq' in text
assert '_fixture' in (root/'fixture-symbols.stdout').read_text()
print('four fileset/parser/disassembler cases pass; synthetic code only')
```

</details>

<details>
<summary><code>check.py</code></summary>

```python
import json
import struct
import sys
from pathlib import Path

root=Path(sys.argv[1])
functions=json.loads((root/'functions.json').read_text())
checks=[]

def function(fragment):
    matches=[f for name,f in functions.items() if fragment in name and '.cold.' not in name]
    assert len(matches)==1,(fragment,len(matches))
    return matches[0]

def expect(fragment,offset,text):
    rows=function(fragment)['rows']
    actual=next((r[2].split(' => ')[0] for r in rows if r[0]==offset),None)
    assert actual==text,(fragment,hex(offset),actual,text)

def method_calls(fragment,target):
    return [(off,text) for off,raw,text in function(fragment)['rows'] if '=> '+target in text]

def commands(data,offset):
    header=struct.unpack_from('<8I',data,offset)
    assert header[0]==0xfeedfacf
    cursor=offset+32
    end=cursor+header[5]
    assert end<=len(data)
    rows=[]
    for _ in range(header[4]):
        cmd,size=struct.unpack_from('<II',data,cursor)
        assert size>=8 and cursor+size<=end
        rows.append((cmd,data[cursor:cursor+size]))
        cursor+=size
    assert cursor==end
    return rows

system=Path('/System/Library/KernelCollections/SystemKernelExtensions.kc').read_bytes()
boot=Path('/System/Library/KernelCollections/BootKernelExtensions.kc').read_bytes()
outer=commands(system,0)
entry=[]
segments={}
for cmd,raw in outer:
    if cmd==25:
        name,addr,vsize,off,size=struct.unpack_from('<16sQQQQ',raw,8)
        segments[name.split(b'\0')[0].decode()]=(addr,off,size)
    if cmd==0x80000035:
        nameoff=struct.unpack_from('<I',raw,24)[0]
        if raw[nameoff:].split(b'\0')[0]==b'com.apple.iokit.IOAcceleratorFamily2':
            entry.append(struct.unpack_from('<Q',raw,16)[0])
assert len(entry)==1
child=commands(system,entry[0])
symraw=next(raw for cmd,raw in child if cmd==2)
_,_,symoff,count,stroff,strsize=struct.unpack('<6I',symraw)
assert symoff+count*16<=len(system) and stroff+strsize<=len(system)
strings=system[stroff:stroff+strsize]
independent=set()
for n in range(count):
    ix,typ,sect,desc,value=struct.unpack_from('<IBBHQ',system,symoff+n*16)
    assert ix<strsize
    end=strings.find(b'\0',ix)
    assert end>=ix
    independent.add((value,strings[ix:end].decode()))
dump=set()
for line in (root/'family-symbols.stdout').read_text().splitlines():
    if line.startswith('0x'):
        addr,name,_=line.split('\t',2)
        dump.add((int(addr,16),name))
assert independent==dump and count==4705
checks.append('independent embedded-header/nlist/string-table comparison: 4705 entries')

section=None
for cmd,raw in child:
    if cmd!=25:continue
    nsect=struct.unpack_from('<I',raw,64)[0]
    for n in range(nsect):
        values=struct.unpack_from('<16s16sQQIIIIIIII',raw,72+n*80)
        if values[0].split(b'\0')[0]==b'__text':section=values
assert section is not None
addr,size,fileoff=section[2:5]
assert size==357364 and fileoff+size<=len(system)
code=system[fileoff:fileoff+size]
cursor=addr
decoded=[]
byte_rows=0
for line in (root/'family-text.stdout').read_text().splitlines():
    if not line.startswith('0x'):continue
    fields=line.split('\t',2)
    pc=int(fields[0],16)
    assert pc==cursor,(hex(pc),hex(cursor))
    if len(fields)==2:
        assert fields[1].startswith('BYTE ')
        raw=bytes([int(fields[1][5:],16)])
        byte_rows+=1
    else:
        raw=bytes.fromhex(fields[1])
        decoded.append((pc,raw))
    assert raw==code[pc-addr:pc-addr+len(raw)]
    cursor+=len(raw)
assert cursor==addr+size
assert (len(decoded),byte_rows)==(95984,250)
checks.append('complete byte coverage matches original section: 95984 decoded rows, 250 byte fallbacks')

kernel={}
for line in (root/'kernel-symbols.stdout').read_text().splitlines():
    if line.startswith('0x'):
        a,n,d=line.split('\t',2)
        if int(a,16):kernel.setdefault(int(a,16),set()).add(n)
base=min(struct.unpack_from('<Q',raw,24)[0] for cmd,raw in commands(boot,0) if cmd==25)
stubaddr,stuboff,stubsize=segments['__BRANCH_STUBS']
gotaddr,gotoff,gotsize=segments['__BRANCH_GOTS']
targets={}
for off in range(stubsize-5):
    if system[stuboff+off:stuboff+off+2]!=b'\xff\x25':continue
    va=stubaddr+off
    slot=va+6+struct.unpack_from('<i',system,stuboff+off+2)[0]
    assert gotaddr<=slot<=gotaddr+gotsize-8
    pointer=struct.unpack_from('<Q',system,gotoff+slot-gotaddr)[0]
    level=(pointer>>30)&3
    if level!=0:continue
    assert pointer>>63==0
    target=base+(pointer&0x3fffffff)
    for name in kernel.get(target,[]):
        if any(x in name for x in ('sendAsyncResult64','releaseAsyncReference64','setAsyncReference64')):
            targets[va]=name
raw_sites=set()
for off in range(size-4):
    if code[off] not in (0xe8,0xe9):continue
    target=addr+off+5+struct.unpack_from('<i',code,off+1)[0]
    if target in targets:raw_sites.add((addr+off,targets[target]))
decoded_sites=set()
for pc,raw in decoded:
    if len(raw)!=5 or raw[0] not in (0xe8,0xe9):continue
    target=pc+5+struct.unpack_from('<i',raw,1)[0]
    if target in targets:decoded_sites.add((pc,targets[target]))
assert raw_sites==decoded_sites
counts={key:sum(key in name for pc,name in raw_sites) for key in ('17sendAsyncResult64','23releaseAsyncReference64','19setAsyncReference64')}
assert counts=={'17sendAsyncResult64':3,'23releaseAsyncReference64':1,'19setAsyncReference64':2},counts
checks.append('independent E8/E9 byte scan and kernel-linked branch stubs: sends 3, release 1, setters 2; indirect calls excluded')

expect('sendBlockFenceNotificationEP',0x47,'xorl\t%esi, %esi')
expect('sendBlockFenceNotificationEP',0x4c,'movl\t$0x7, %ecx')
expect('sendBlockFenceNotificationEP',0x43,'movq\t%rcx, 0x30(%rax)')
expect('sendBlockFenceNotificationEP',0x40,'movl\t%r8d, %ecx')
expect('sendBlockFenceNotificationEP',0x2c,'shrq\t$0x20, %r9')
expect('sendBlockFenceNotificationEP',0x3c,'movq\t%rsi, 0x28(%rax)')
assert [off for off,text in method_calls('sendBlockFenceNotificationEP','IOUserClient::sendAsyncResult64')]==[0x51]
checks.append('immediate producer: zero callback result, seven words, status width and time high halves')
expect('IOAccelBlockFence12notifyClientEv',0xa4,'xorl\t%esi, %esi')
expect('IOAccelBlockFence12notifyClientEv',0xa6,'movl\t$0x7, %ecx')
expect('IOAccelBlockFence12notifyClientEv',0x95,'movq\t%rax, 0x30(%rdx)')
expect('IOAccelBlockFence12notifyClientEv',0x8f,'movl\t0xc0(%rbx), %eax')
checks.append('deferred producer: zero callback result, seven words and zero-extended status')
assert [off for off,text in method_calls('IOAccelCommandQueue21submit_command_bufferE','IOAccelFenceMachine::sendBlockFenceNotification')]==[0x129,0x20b,0x23c]
expect('IOAccelCommandQueue21submit_command_bufferE',0x1cf,'movl\t$0x8, 0x610(%rbx)')
expect('IOAccelCommandQueue21submit_command_bufferE',0x237,'xorl\t%ecx, %ecx')
checks.append('one normal scheduling site; two immediate error sites; status 8 on allocation/init failure')
expect('BlockFencePort218initWithAsyncRef64',0x2f,'leaq\t0x10(%rbx), %rdi')
expect('BlockFencePort24freeEv',0xf,'leaq\t0x10(%rbx), %rdi')
assert [off for off,text in method_calls('BlockFencePort24freeEv','IOUserClient::releaseAsyncReference64')]==[0x13]
expect('IOAccelBlockFence4freeEv',0x13,'callq\t*0x28(%rax)')
checks.append('registration storage at +0x10; guarded wake release; fence releases port owner')
expect('cancelBlockFenceCallbackEP',0xb2,'movq\t$0x0, 0xb8(%rax)')
expect('process_eventfencesEP',0x126,'callq\t*0x120(%rax)')
checks.append('cancellation clears raw submitter; completed-fence path makes notify virtual call')
report={'checks':checks,'limits':'static file checks only; no dispatch, callback, hardware work or runtime ownership proof'}
(root/'verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
```

</details>
