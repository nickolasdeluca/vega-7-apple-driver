# Installed IOKit async dispatch and wrappers

On this host, the installed x86_64 IOKit dispatch-queue receive path, async
completion dispatcher, async call wrappers and generated `io_connect_async_method`
client stub match the pinned IOKitUser source and device IDL on every compared
offset, limit, constant and argument order. The installed callouts do not set
up the arguments the dispatcher ignores (buffer size and context). The generated
stub, which is not in the IOKitUser repository, adds client-side count limits and
strict reply checks. It also writes the reply's scalar count back through a
process-wide static used for `NULL` output counts. **This is a user-space
comparison. It does not establish private kernel reply production, message
validity, delivery count, ordering, cancellation or GPU completion.**

This continues the [async-reply source study](xnu-async-replies.md), which
predicted the dispatcher, wrapper and layout behavior compared here. No callbacks
were invoked, no Mach messages were constructed or sent, and no experimental
connections, queues or mappings were created. The public enumeration child may
initialize the existing stack internally; that is not manual private invocation.

## Experiment record

The later [family reply study](ioaccel-family-replies.md) closes the static
producer-layout question for the inspected base-family paths. It does not
establish live message validity, delivery or hardware completion.

Read-only investigation on 2026-10-02:

| Item | Observation |
| --- | --- |
| OS / kernel | macOS 26.4.1 build 25E253; `xnu-12377.101.15~1/RELEASE_X86_64`; `uname -m` x86_64 |
| CPU / GPU | CPU query reports Ryzen 5 5600GT; PCI `1002:1638:c9` reused from the [baseline](hardware-baseline.md) |
| SDK | macOS SDK 26.5 for headers, the `IOKit.tbd` export list and the layout build |
| Source references | Pinned [XNU `f6217f8`](https://github.com/apple-oss-distributions/xnu/commit/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea) and [IOKitUser `323ead8`](https://github.com/apple-oss-distributions/IOKitUser/commit/323ead896d04424f87184d8f6ff0cce811aab106), as collected and hash-verified by the async-reply study |
| Inspected images | Loaded `IOKit.framework` and, for the caller, the private `IOAccelerator.framework` in our own child |

The source revisions differ from the host kernel; agreement is checked only where
installed user-space instructions are compared below.

The unchanged public metadata child from the [loader study](metal-loader-study.md)
was rebuilt; its source is byte-identical to the previous studies' copies. LLDB
launched it as our own child and stopped at `probe.m:48` after enumeration.
Symbol lookup was restricted to the IOKit module. The child's instructions were
read and normalized into function-relative offsets and symbol names, so no load
addresses are carried across launches or into tracked files. A compiled layout
probe, an IDL message-ID calculation and an offline checker then compared the
instructions with source expectations. The checker invokes nothing.

Ignored `out/iokit-async-dispatch/` saves every command, UTC time, stdout/stderr
and exit status:

| Capture | Result |
| --- | --- |
| `os`, `kernel`, `kern-version`, `cpu`, `arch`, `sdk-path`, `sdk-version`, `build` | Exit 0; values above |
| `iokit-symbols` | Exit 0; nine functions plus the `zero` and `temp_reference` statics resolve in IOKit |
| `dispatch-static`, `wrapper-static` | Exit 0; human-readable disassembly of the nine functions |
| `instruction-dump` | Exit 0; stopped at `probe.m:48`; eleven functions normalized, including the caller's wake-port assertion path |
| `caller-cold-paths` | Exit 0; readable disassembly of the caller's two cold paths |
| `layout-build`, `layout`, `mig-async-id`, `mig-method-id` | Exit 0; layout values and IDs 2866/2966 and 2865/2965 |
| `installed-check` | Exit 0; 16 comparison groups pass, including 208,192 arithmetic samples |
| `mutation-check` | Exit 0; the unmodified dump passes and nine controlled variants are rejected by their intended checks |
| `tests` | Exit 0; all 13 existing tests pass |
| `kc-availability`, `kext-bundle-listing` | Exit 0; kernel collections are present and world-readable; the IOAcceleratorFamily2 bundle on disk holds metadata only |

Preserved failures: the first layout build exited 1 because `offsetof` cannot
address the descriptor's `pad2` bit-field. A second layout run exited 0 but
printed values shifted by one field after a format-string edit missed escaped
quotes; it is retained and superseded. The checker's first run exited 1 because
LLDB reports trailing padding with an empty mnemonic, which it did not trim.
The first documented-reproduction run exited 1 when its runner copied a capture
onto itself; its partial directory is retained. Each defect was corrected in the inspection code. None is an installed-code
observation. Earlier non-failing runs were superseded after script refactoring;
their results are unchanged.

## Symbol and declaration availability

The SDK's `IOKit.tbd` exports the compared functions
`IODispatchCalloutFromMessage`, `IODispatchCalloutFromCFMessage`,
`_IODispatchCalloutWithDispatch`, `IONotificationPortSetDispatchQueue` and the
three `IOConnectCallAsync*Method` wrappers. `io_connect_async_method`, the dispatch
block and the two statics are not exported. LLDB resolves them from the loaded
image's symbol table. They are implementation details, not SDK interfaces.

The SDK `IOKitLib.h` differs from the pinned header only in four availability-
attribute hunks; the compared declarations and callback typedefs are identical.
The SDK `OSMessageNotification.h` is byte-identical to the pinned XNU header.
`mach_msg2_internal` and `MACH64_SEND_KOBJECT_CALL` are absent from the SDK's
public `message.h`. The pinned XNU header declares them under `PRIVATE`
([message.h](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/mach/message.h#L1086)).

## Dispatch-queue receive path

Source lines are from the pinned
[IOKitLib.c](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.c#L965).
Offsets are from the received message or the 64-bit notification header as
measured by the layout probe.

| Pinned source | Installed x86_64 instructions | Result |
| --- | --- | --- |
| Setter at 977–999: cancel/release old source, increment internal count, create receive source, set context, event handler and `IONotificationPortRelease` cancel handler, activate | Same call order; the block captures the source at block offset `0x20` | Match; reconfirms the [lifecycle study](ioaccel-lifecycle.md) |
| Event handler calls `dispatch_mig_server` with `MAX_MSG_SIZE`, defined at 974 as 8 KiB minus `MAX_TRAILER_SIZE` | Passes `0x1fbc` (8124); the SDK maximum trailer measures 68 bytes | Match |
| `_IODispatchCalloutWithDispatch` at 965–972 sets `MIG_NO_REPLY`, calls the dispatcher with `NULL`, the message, `msgh_size` and the dispatch context, then returns true | Calls `mig_reply_setup`, stores −305 at reply offset 32 (`RetCode`), passes the message in the second argument register and returns 1. It neither loads the size nor calls `dispatch_mach_msg_get_context`. | **Difference.** The first, third and fourth arguments are not set up; the dispatcher declares them unused |
| `IODispatchCalloutFromMessage` at 1177–1180 passes size −1 | Tail jump without setting the size register | Same kind of difference |

The dispatcher, `IODispatchCalloutFromCFMessage` at 1183–1302:

| Pinned source | Installed x86_64 instructions | Result |
| --- | --- | --- |
| Returns unless `msgh_id` is 53 (1198) | First instruction compares 53 with message offset 20; mismatch returns | Match |
| Complex message: descriptor count, service from `ports[0]`, header after the descriptors (1201–1209) | Sign bit of `msgh_bits`; count at 24; name at 28; header at `28 + 12 × count` | Match |
| Simple message: header after the 24-byte Mach header | Header at offset 24 | Match |
| `leftOver = msgh_size - (header + 72 - msg)` in 32 bits (1212) | 32-bit subtract/add using `msgh_size` at offset 4 | Match |
| Non-null received remote port: deliver only when the task holds at least two send references (1215–1226) | Remote port at offset 8; `mach_port_get_refs` for the send right; fewer than 2 skips callbacks but still deallocates | Match |
| Type mask `0xfff` and size adjustment `type >> 30` (1230) | `andl $0xfff`, `shrl $0x1e`; adjustment applied on the interest and async paths, which use the length | Match |
| Async count `(leftOver - 4) / sizeof(void *)` (1239) | `((leftOver + 0x7fffffffc) >> 3)`, low 32 bits | Equivalent modulo 2³²; 208,192 samples include both ends of the range; zero remaining bytes gives `0xffffffff` |
| Callback from reference slot 1, refcon from slot 2; arity 0, 1, 2 or array plus count (1239–1258) | Header offsets 16 and 24; result at 72; arguments at 76; values passed for counts 0–2, array pointer and 32-bit count otherwise | Match |
| No maximum count, minimum body or function-value check | No comparison with 16; the header's content-size field is never read; the callback is called indirectly without a test | Match: no added validation |
| Matching types call `(refcon, notifier)`; interest type 160 uses its own arity rule | Types 100–102 and 160 present | Match; not further studied |
| Notifier and complex descriptors deallocated even when delivery is suppressed (1290–1297) | Two `mach_port_deallocate` sites | Match |

A linear scan, confirmed by reading the branches, finds the port, CF-size and
context argument registers overwritten before any read. Undefined values in those
registers therefore do not reach dispatch decisions in the inspected function.

Consequences for the saved IOAccel consumer: the installed dispatcher passes an
argument array only for three or more words. The [consumer](xnu-async-replies.md)
needs at least seven words for its loads and does not check the count. IOKit
enforces neither bound; the reply producer must. A short body yields a huge
count with a pointer to whatever follows the message. The 8124-byte receive limit
is far above a generic 64-bit reply. With the 24-byte user header, the maximum of
16 arguments makes 232 bytes. `dispatch_mig_server` itself (oversize handling,
`MIG_NO_REPLY` handling and request destruction) is in libdispatch, which was
not inspected.

## Async wrappers

Wrapper source is at
[IOKitLib.c lines 1904–2053](https://github.com/apple-oss-distributions/IOKitUser/blob/323ead896d04424f87184d8f6ff0cce811aab106/IOKitLib.c#L1904).

| Pinned source | Installed x86_64 instructions | Result |
| --- | --- | --- |
| Scalar wrapper forwards `(connection, selector, wake, reference, count, input, inputCnt, NULL, 0, output, outputCnt, NULL, NULL)` | Exact forwarding sequence | Match |
| Struct wrapper forwards `(…, NULL, 0, inputStruct, size, NULL, NULL, outputStruct, sizePointer)` | Exact forwarding sequence | Match |
| Inputs up to 4096 bytes inband, larger as out-of-line (1932) | `cmpq $0x1000` split | Match |
| NULL output count replaced by `static uint32_t zero` (1942) | `cmovne` with `IOConnectCallAsyncMethod.zero` | Match |
| Output structure split by the caller's size; written back by the original size (1949, 1975) | `cmpq $0x1000` and `cmpq $0x1001` checks | Match |
| `NULL` reference with count 0 becomes `temp_reference` with count 1 (1930, 1959–1962) | Two `sete`, one `testb`, conditional moves to `temp_reference` and 1 | Match; only the combined condition substitutes |
| 17-argument stub call order (1964) | Six registers and eleven stack arguments in source order | Match |

The wrapper does not validate the wake port, selector, a `NULL` reference with a
nonzero count, or an output pointer against its count. The current-launch
`IOAccelCommandQueueCreateWithQoS` call matches the saved caller. A zero wake
port reaches a fatal `__assert_rtn` for the expression `wakePort`; it is not a
recoverable skip. Otherwise the caller passes selector 0, count 3, no scalar
input, and `NULL` output and output count. Before the call it stores
the callback in slot 1 and the queue in slot 2; **slot 0 is never written**.

## Generated client stub

MIG generates `io_connect_async_method` from
[device.defs](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/osfmk/device/device.defs#L629);
generated code is in neither pinned repository. The comparison uses a layout
transcribed from the IDL's LP64 user types (references `array[*:8]`, scalars
`array[*:16]`, inband `array[*:4096]`). The routine ID is counted from the
subsystem base 2800 with `skip` entries and LP64 user branches.

| IDL-derived expectation | Installed stub | Result |
| --- | --- | --- |
| References ≤ 8, scalars ≤ 16, inband input ≤ 4096 | Each is checked before obtaining a reply port or sending; failure returns `MIG_ARRAY_TOO_LARGE` (−307) | Client-side enforcement observed |
| Request ID 2800 + slot 66 = 2866 | `0xb32` in the ID word | Match |
| Complex request; `COPY_SEND` destination; `MAKE_SEND_ONCE` reply port from `mig_get_reply_port` | Header bits `0x80001513` | Match |
| One port descriptor: wake port with `MAKE_SEND` (20) | Count 1 at 24; name at 28; disposition word `0x140000` at 36 | Match |
| References at 52, then the selector; 104 fixed bytes plus `8·refs + 8·scalars + round4(inband)` | `__memcpy_chk` into the 4392-byte request buffer; `addl $0x68` size computation | Match |
| Output capacities sent as `min(*count, limit)` | Clamped to 4096 inband bytes and 16 scalars | Match |
| Synchronous send and receive | `mach_msg2_internal` with `MACH64_SEND_KOBJECT_CALL \| MACH_SEND_MSG \| MACH_RCV_MSG` (`0x200000003`), 4284-byte receive (4276-byte maximum reply plus 8-byte trailer), no timeout | Private send interface; see above |
| Send failures | Invalid data, destination or header keep the reply port; other failures deallocate it; the Mach result is returned | Standard MIG reply-port policy |
| Reply identity | ID 2966 required, otherwise `MIG_REPLY_MISMATCH` (−301); a send-once notification (71) gives `MIG_SERVER_DIED` (−308) | Match |
| Reply validation | Complex reply, nonzero reply remote-port field, size outside 52–4276, inband count above 4096, scalar count above 16 or inexact size give `MIG_TYPE_ERROR` (−300). A 36-byte reply with nonzero `RetCode` returns that code. A nonzero `RetCode` in a full reply is also returned. Malformed replies are destroyed. | Strict client checks observed |
| Output counts | Copies up to the caller's capacity and writes the reply's count back; a count above capacity returns `MIG_ARRAY_TOO_LARGE` after writing it | Standard `CountInOut` behavior |

These checks protect the calling process from malformed replies. They do not
validate requests received by a kernel adapter, because any task can send its
own Mach message. The generated kernel server's checks were not inspected.

## Corrections and refinements

1. The async-reply study left receive-layer checks and installed equivalence
   open. The installed dispatcher now matches the compared source behavior and
   adds no count, length or function check. Only libdispatch's handling inside
   `dispatch_mig_server` remains uninspected on the receive side.
2. The IDL's eight-reference maximum is enforced in the installed client stub
   before sending. That is client protection, not kernel validation; an
   independent adapter must still reject out-of-range counts itself.
3. The queue registration's “no scalar output” ([ABI](ioaccel-abi.md),
   [lifecycle](ioaccel-lifecycle.md)) is refined. With a `NULL` output count, the
   wrapper supplies the shared `zero` static and the stub sends `min(zero, 16)` as
   capacity. A successful reply, or one whose scalar count exceeds that capacity,
   stores its scalar count there. The pinned kernel helper copies the family's
   `scalarOutputCount` back unchanged
   ([IOUserClient.cpp 5311, 5334](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L5311)).
   The generated kernel server was not inspected. If such a reply ever carried a
   nonzero scalar count, the static would stay nonzero for later `NULL`-count
   async calls in the process. Those calls would advertise that capacity, and a
   reply using it would be copied through their `NULL` output pointer. This is a
   conditional consequence derived from source and instructions, not an
   observed failure.
4. “Slot 0 is reserved” is refined. The caller sends an unwritten stack word in
   slot 0. The generic kernel helper replaces it with the wake port and 64-bit
   flag before family dispatch
   ([5264–5275](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/iokit/Kernel/IOUserClient.cpp#L5264)).
5. A nonzero registration result is ambiguous about kernel state. Receive errors,
   `MIG_REPLY_MISMATCH`, `MIG_TYPE_ERROR`, `MIG_SERVER_DIED` and family return
   codes arise after the request was sent. The caller then destroys its
   notification port. That prevents callbacks through this port, but does not
   establish that the family retained no registration or released the wake right.

## Reproduction

Use a new ignored directory `D`, and let `S` be the directory produced by the
[async-reply collector](xnu-async-replies.md#reproduction-and-offline-checks).
Run each command through `tools/baseline.py`'s `capture` helper, retaining
stdout, stderr, exit status and a UTC timestamp. Save the Objective-C example
from [the loader study](metal-loader-study.md) as `D/probe.m`, build it, and save
the LLDB script below as `D/dump-instructions.py`:

```sh
xcrun clang -g -O0 -fobjc-arc -Wall -Wextra -Werror -framework Foundation -framework Metal D/probe.m -o D/probe
xcrun lldb --no-lldbinit --batch \
  -o 'settings set target.disable-aslr false' \
  -o 'breakpoint set --file probe.m --line 48' \
  -o run \
  -o 'image lookup --regex --name "IODispatchCallout|IOConnectCallAsync|io_connect_async_method|IONotificationPortSetDispatchQueue" IOKit' \
  -o 'script exec(open("D/dump-instructions.py").read())' \
  -o quit D/probe > D/inspection.stdout
```

Confirm the stop reason is breakpoint 1.1 at `probe.m:48`; a successful LLDB exit
alone does not prove it. If launch or inspection is denied, preserve the result
and do not bypass protections. The operand patterns are specific to this build;
a changed build makes the comparison unavailable rather than proving absence.

```python
# Run with LLDB's script command after stopping the repository probe.
# Reads instructions of named loaded functions; executes no target functions.
# Output is normalized: load addresses become function-relative offsets or
# symbol names, so it can be saved and compared without runtime addresses.
import json,re,lldb
study_target=lldb.debugger.GetSelectedTarget()
study_process=study_target.GetProcess()
assert study_process.GetState()==lldb.eStateStopped
assert study_target.GetTriple().startswith('x86_64') and study_target.GetAddressByteSize()==8
study_functions=[('IOKit',name) for name in (
    'IODispatchCalloutFromMessage','IODispatchCalloutFromCFMessage',
    '_IODispatchCalloutWithDispatch','__IONotificationPortSetDispatchQueue_block_invoke',
    'IONotificationPortSetDispatchQueue','IOConnectCallAsyncScalarMethod',
    'IOConnectCallAsyncStructMethod','IOConnectCallAsyncMethod','io_connect_async_method')]
study_functions+=[('IOAccelerator','IOAccelCommandQueueCreateWithQoS'),
                  ('IOAccelerator','IOAccelCommandQueueCreateWithQoS.cold.2')]

def study_target_name(comment):
    comment=comment.strip()
    quoted=re.fullmatch(r'"([^"]*)"',comment)
    if quoted: return 'str:"%s"'%quoted.group(1)
    stub=re.fullmatch(r'symbol stub for: (\S+)',comment)
    if stub: return 'stub:'+stub.group(1)
    local=re.fullmatch(r'([A-Za-z_][\w.$]*)(?: \+ \d+)?',comment)
    return 'sym:'+local.group(1) if local else None

study_result={}
for module,name in study_functions:
    contexts=study_target.FindFunctions(name)
    matches=[contexts.GetContextAtIndex(i) for i in range(contexts.GetSize())]
    matches=[c for c in matches if c.GetModule().GetFileSpec().GetFilename()==module]
    assert len(matches)==1, '%s in %s unavailable or ambiguous: %d'%(name,module,len(matches))
    symbol=matches[0].GetSymbol()
    start=symbol.GetStartAddress().GetLoadAddress(study_target)
    end=symbol.GetEndAddress().GetLoadAddress(study_target)
    rows=[]
    for instruction in symbol.GetInstructions(study_target):
        address=instruction.GetAddress().GetLoadAddress(study_target)
        mnemonic=instruction.GetMnemonic(study_target)
        operands=instruction.GetOperands(study_target)
        comment=instruction.GetComment(study_target)
        named=study_target_name(comment) if comment else None
        branch=re.fullmatch(r'0x([0-9a-f]+)',operands)
        if branch:
            destination=int(branch.group(1),16)
            if start<=destination<end: operands='<+%d>'%(destination-start)
            else:
                assert named, 'unnamed external branch in '+name
                operands=named
        elif '(%rip)' in operands:
            operands=re.sub(r'-?0x[0-9a-f]+\(%rip\)',('['+named+']') if named else '[rip:?]',operands)
        assert not re.search(r'0x7ff[0-9a-f]{9}',operands), 'unnormalized address in '+name
        rows.append([address-start,mnemonic,operands])
    study_result[module+'`'+name]={'size':end-start,'instructions':rows}
print('INSTRUCTIONS_JSON '+json.dumps(study_result,sort_keys=True))
```

Create `D/include/IOKit/OSMessageNotification.h` and `D/include/IOKit/IOReturn.h`
symlinks to `S/xnu-OSMessageNotification.h` and `S/xnu-IOReturn.h`. Save the
programs from the collapsed sections below as `D/layout.c`, `D/mig-routine-id.py`,
`D/check-installed.py` and `D/mutation-check.py`, then run:

```sh
xcrun clang -std=c11 -Wall -Wextra -Wformat=2 -Werror -I D/include D/layout.c -o D/layout
D/layout > D/layout.json
python3 D/mig-routine-id.py S/xnu-device.defs io_connect_async_method > D/routine.json
python3 D/check-installed.py D/layout.json D/inspection.stdout D/routine.json S/xnu-message.h
python3 D/mutation-check.py D/layout.json D/inspection.stdout D/routine.json S/xnu-message.h D/mutations
```

The checker must report 16 comparison groups. The mutation check must accept the
unmodified dump and reject all nine variants. Neither invokes target code.

<details>
<summary><code>layout.c</code>: compiled layout and constant probe</summary>

```c
// Project inspection code: measures layouts needed to interpret installed
// IOKit instructions. MIG structs are transcribed from the pinned device.defs
// routine io_connect_async_method; they are not generated Apple code.
#include <IOKit/OSMessageNotification.h>
#include <mach/mach.h>
#include <mach/mig_errors.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#pragma pack(push, 4)
typedef struct {
    mach_msg_header_t Head;
    mach_msg_body_t msgh_body;
    mach_msg_port_descriptor_t wake_port;
    NDR_record_t NDR;
    mach_msg_type_number_t referenceCnt;
    uint64_t reference[8];
    uint32_t selector;
    mach_msg_type_number_t scalar_inputCnt;
    uint64_t scalar_input[16];
    mach_msg_type_number_t inband_inputCnt;
    char inband_input[4096];
    mach_vm_address_t ool_input;
    mach_vm_size_t ool_input_size;
    mach_msg_type_number_t inband_outputCnt;
    mach_msg_type_number_t scalar_outputCnt;
    mach_vm_address_t ool_output;
    mach_vm_size_t ool_output_size;
} AsyncRequest;
typedef struct {
    mach_msg_header_t Head;
    NDR_record_t NDR;
    kern_return_t RetCode;
    mach_msg_type_number_t inband_outputCnt;
    char inband_output[4096];
    mach_msg_type_number_t scalar_outputCnt;
    uint64_t scalar_output[16];
    mach_vm_size_t ool_output_size;
    mach_msg_trailer_t trailer;
} AsyncReply;
#pragma pack(pop)

struct ComplexPrefix { mach_msg_header_t header; mach_msg_body_t body; mach_msg_port_descriptor_t ports[1]; };

static int fields;
static void field(const char *name, long long value) {
    printf("%s\"%s\":%lld", fields++ ? "," : "{", name, value);
}

int main(void) {
    _Static_assert(sizeof(void *) == 8 && sizeof(io_user_reference_t) == 8, "LP64 x86_64 study");
    AsyncRequest *request = NULL;
    AsyncReply *reply = NULL;
    const size_t request_variable = sizeof(request->reference) + sizeof(request->scalar_input) +
        sizeof(request->inband_input);
    const size_t reply_variable = sizeof(reply->inband_output) + sizeof(reply->scalar_output);
    const size_t reply_without_trailer = sizeof(AsyncReply) - sizeof(mach_msg_trailer_t);
    mach_msg_port_descriptor_t wake = {0};
    uint32_t words[3];
    _Static_assert(sizeof(wake) == sizeof(words), "12-byte port descriptor");
    wake.disposition = MACH_MSG_TYPE_MAKE_SEND;
    wake.type = MACH_MSG_PORT_DESCRIPTOR;
    memcpy(words, &wake, sizeof(words));

    field("mach_header", sizeof(mach_msg_header_t));
    field("msgh_size_offset", offsetof(mach_msg_header_t, msgh_size));
    field("msgh_remote_offset", offsetof(mach_msg_header_t, msgh_remote_port));
    field("msgh_local_offset", offsetof(mach_msg_header_t, msgh_local_port));
    field("msgh_id_offset", offsetof(mach_msg_header_t, msgh_id));
    field("port_descriptor", sizeof(mach_msg_port_descriptor_t));
    field("complex_ports_offset", offsetof(struct ComplexPrefix, ports));
    field("notification64", sizeof(struct OSNotificationHeader64));
    field("notification64_type_offset", offsetof(struct OSNotificationHeader64, type));
    field("reference_offset", offsetof(struct OSNotificationHeader64, reference));
    field("completion_args_offset", offsetof(IOAsyncCompletionContent, args));
    field("interest64_argument_offset", offsetof(struct IOServiceInterestContent64, messageArgument));
    field("max_trailer", MAX_TRAILER_SIZE);
    field("dispatch_max_message", 8ul * 1024ul - MAX_TRAILER_SIZE);
    field("mig_reply_error", sizeof(mig_reply_error_t));
    field("retcode_offset", offsetof(mig_reply_error_t, RetCode));
    field("request_max", sizeof(AsyncRequest));
    field("request_fixed", sizeof(AsyncRequest) - request_variable);
    field("request_descriptor_count_offset", offsetof(AsyncRequest, msgh_body));
    field("request_wake_name_offset", offsetof(AsyncRequest, wake_port));
    field("request_wake_disposition_offset", offsetof(AsyncRequest, wake_port) + 2 * sizeof(uint32_t));
    field("request_ndr_offset", offsetof(AsyncRequest, NDR));
    field("request_reference_count_offset", offsetof(AsyncRequest, referenceCnt));
    field("request_reference_offset", offsetof(AsyncRequest, reference));
    field("reply_max_without_trailer", reply_without_trailer);
    field("reply_receive_size", sizeof(AsyncReply));
    field("reply_minimum", reply_without_trailer - reply_variable);
    field("reply_inband_count_offset", offsetof(AsyncReply, inband_outputCnt));
    field("reply_inband_offset", offsetof(AsyncReply, inband_output));
    field("wake_disposition_word", words[2]);
    field("make_send", MACH_MSG_TYPE_MAKE_SEND);
    field("copy_send", MACH_MSG_TYPE_COPY_SEND);
    field("make_send_once", MACH_MSG_TYPE_MAKE_SEND_ONCE);
    field("complex_bit", MACH_MSGH_BITS_COMPLEX);
    field("send_msg", MACH_SEND_MSG);
    field("rcv_msg", MACH_RCV_MSG);
    field("notify_send_once", MACH_NOTIFY_SEND_ONCE);
    field("mig_type_error", MIG_TYPE_ERROR);
    field("mig_reply_mismatch", MIG_REPLY_MISMATCH);
    field("mig_no_reply", MIG_NO_REPLY);
    field("mig_array_too_large", MIG_ARRAY_TOO_LARGE);
    field("mig_server_died", MIG_SERVER_DIED);
    field("send_invalid_data", MACH_SEND_INVALID_DATA);
    field("send_invalid_dest", MACH_SEND_INVALID_DEST);
    field("send_invalid_header", MACH_SEND_INVALID_HEADER);
    printf("}\n");
    return 0;
}
```

</details>

<details>
<summary><code>mig-routine-id.py</code>: IDL routine numbering</summary>

```python
# Count MIG routine/skip slots in pinned device.defs for an LP64 user client.
# Conditional branches are evaluated for: KERNEL_SERVER=0, KERNEL=0, IOKIT=1,
# IOKIT_ALL_IPC=0, __ILP32__=0, __LP64__=1. IOKIT/KERNEL select type definitions
# only. Each skip consumes one message ID.
import json,re,sys
from pathlib import Path
path=Path(sys.argv[1]); target=sys.argv[2]
macros={'KERNEL':0,'IOKIT':1,'KERNEL_SERVER':0,'KOBJECT_SERVER':0,'IOKIT_ALL_IPC':0,'IOKITSIMD':0,'__ILP32__':0,'__LP64__':1}
def evaluate(expr):
    expr=expr.split('/*')[0].strip()
    python=expr.replace('||',' or ').replace('&&',' and ').replace('!',' not ')
    python=re.sub(r'[A-Za-z_][A-Za-z0-9_]*',lambda m:str(macros[m.group(0)]) if m.group(0) in macros else m.group(0),python)
    assert re.fullmatch(r'[01 ()notandr]*',python), 'unsupported condition: '+expr
    return bool(eval(python))
stack=[]; active=True; base=None; index=0; found=None
for number,line in enumerate(path.read_text().splitlines(),1):
    stripped=line.strip()
    directive=re.match(r'#\s*(if|ifdef|ifndef|else|endif|elif)\b(.*)',stripped)
    if directive:
        kind,rest=directive.groups()
        if kind=='if': value=evaluate(rest); stack.append((active,value)); active=active and value
        elif kind=='elif':
            parent,taken=stack[-1]; value=not taken and evaluate(rest)
            stack[-1]=(parent,taken or value); active=parent and value
        elif kind=='else':
            parent,taken=stack[-1]; active=parent and not taken
        elif kind=='endif': parent,_=stack.pop(); active=parent
        else: raise AssertionError('unsupported directive at line %d'%number)
        continue
    if not active: continue
    match=re.search(r'\biokit\s+(\d+)\s*;',stripped)
    if match: base=int(match.group(1)); continue
    routine=re.match(r'(routine|simpleroutine)\s+([A-Za-z0-9_]+)\s*\(',stripped)
    if routine:
        if routine.group(2)==target: found=(number,index)
        index+=1
    elif re.fullmatch(r'skip\s*;',stripped): index+=1
assert base==2800 and found and not stack, (base,found,stack)
print(json.dumps({'subsystem_base':base,'routine':target,'line':found[0],'index':found[1],
                  'request_msgh_id':base+found[1],'reply_msgh_id':base+found[1]+100}))
```

</details>

<details>
<summary><code>check-installed.py</code>: offline instruction comparison</summary>

```python
# Offline comparison of normalized installed instructions with source-derived
# expectations. Reads saved text only; invokes no dispatcher, callback or Mach call.
# Operand patterns are specific to the observed x86_64 build: a mismatch means
# the comparison is unavailable for that build, not that behavior is absent.
import json,random,re,sys
from pathlib import Path
layout=json.loads(Path(sys.argv[1]).read_text())
line=[l for l in Path(sys.argv[2]).read_text().splitlines() if l.startswith('INSTRUCTIONS_JSON ')]
assert len(line)==1, 'expected one instruction dump'
code=json.loads(line[0].split(' ',1)[1])
routine=json.loads(Path(sys.argv[3]).read_text())
private_header=Path(sys.argv[4]).read_text()
kobject=re.search(r'MACH64_SEND_KOBJECT_CALL\s*=\s*(0x[0-9a-fA-F]+)ull',private_header)
assert kobject, 'private option definition unavailable'
kobject_call=int(kobject.group(1),16)
M32,M64=0xffffffff,0xffffffffffffffff
checks=[]

def body(name):
    rows=code[name]['instructions']
    for index,row in enumerate(rows):
        if row[1] in ('retq','jmp') and all(r[1] in ('addb','') for r in rows[index+1:]):
            return rows[:index+1]
    return rows
def pairs(name): return [(m,o) for _,m,o in body(name)]
def has(name,mnemonic,operands):
    assert (mnemonic,operands) in pairs(name), '%s lacks %s %s'%(name,mnemonic,operands)
def seq(name,expected):
    rows=pairs(name)
    for start in range(len(rows)-len(expected)+1):
        if all(rows[start+i][0]==m and (o is None or rows[start+i][1]==o) for i,(m,o) in enumerate(expected)):
            return start
    raise AssertionError('%s lacks sequence %s'%(name,expected))
def calls(name): return [o for m,o in pairs(name) if m in ('callq','jmp') and not o.startswith('<')]
def offset_of(name,mnemonic,operands):
    found=[r[0] for r in body(name) if r[1]==mnemonic and r[2]==operands]
    assert len(found)==1, '%s %s %s occurs %d times'%(name,mnemonic,operands,len(found))
    return found[0]
def imm(value): return '$0x%x'%(value&M32)
def disp(value): return ('-0x%x'%-value) if value<0 else '0x%x'%value
def split(operands):
    parts,depth,current=[],0,''
    for ch in operands:
        depth+=ch=='('; depth-=ch==')'
        if ch==',' and not depth: parts.append(current.strip()); current=''
        else: current+=ch
    return parts+[current.strip()] if current.strip() else parts
def first_use_is_write(name,aliases):
    for offset,mnemonic,operands in body(name):
        parts=split(operands)
        if not any(re.search(r'%'+a+r'\b',p) for p in parts for a in aliases): continue
        destination=parts[-1]
        sources=parts[:-1]
        zeroing=mnemonic.startswith('xor') and len(set(parts))==1
        plain=destination.lstrip('%') in aliases
        reads=any(re.search(r'%'+a+r'\b',p) for p in sources for a in aliases)
        assert plain and (zeroing or (not reads and re.match(r'(mov|lea|xor)',mnemonic))), \
            '%s first uses %s at +%d as a read: %s %s'%(name,aliases[0],offset,mnemonic,operands)
        return offset
    return None

# Header layout and public constants used below.
assert layout['mach_header']==24 and layout['msgh_id_offset']==20 and layout['msgh_size_offset']==4
assert layout['msgh_remote_offset']==8 and layout['port_descriptor']==12 and layout['complex_ports_offset']==28
assert layout['notification64']==72 and layout['reference_offset']==8 and layout['completion_args_offset']==4
assert layout['make_send']==20 and layout['copy_send']==19 and layout['make_send_once']==21
assert layout['complex_bit']==0x80000000 and layout['send_msg']==1 and layout['rcv_msg']==2
inband_max=layout['request_max']-layout['request_fixed']-8*8-16*8
assert inband_max==4096

# 1. Public entry and CF-message dispatcher.
D='IOKit`IODispatchCalloutFromCFMessage'
assert pairs('IOKit`IODispatchCalloutFromMessage')==[('pushq','%rbp'),('movq','%rsp, %rbp'),('popq','%rbp'),('jmp','sym:IODispatchCalloutFromCFMessage')]
checks.append('IODispatchCalloutFromMessage tail-jumps without materializing the source size -1')
assert pairs(D)[0]==('cmpl','$0x%x, 0x%x(%%rsi)'%(53,layout['msgh_id_offset']))
has(D,'leaq','0x%x(%%rsi), %%r14'%layout['mach_header'])
seq(D,[('cmpl','$0x0, (%rsi)'),('js',None)])
has(D,'movl','0x%x(%%rsi), %%ecx'%layout['complex_ports_offset'])
seq(D,[('leaq','(%rax,%rax,2), %rax'),('leaq','(%rsi,%rax,4), %r14'),('addq','$0x%x, %%r14'%layout['complex_ports_offset'])])
assert 3*4==layout['port_descriptor']
has(D,'leaq','0x%x(%%r14), %%r13'%layout['notification64'])
seq(D,[('movl','%esi, %r15d'),('subl','%r13d, %r15d'),('addl','0x%x(%%rsi), %%r15d'%layout['msgh_size_offset'])])
has(D,'movl','0x%x(%%rsi), %%ebx'%layout['msgh_remote_offset'])
seq(D,[('callq','stub:mach_port_get_refs'),('testl','%eax, %eax'),('jne',None),('cmpl','$0x2, -0x34(%rbp)'),('jb',None)])
checks.append('message ID, simple/complex header, 72-byte notification header, remaining-length and notifier send-reference checks')
has(D,'movl','0x%x(%%r14), %%ecx'%layout['notification64_type_offset'])
has(D,'andl','$0xfff, %eax'); has(D,'shrl','$0x1e, %ecx')
seq(D,[('leal','-0x64(%rax), %edx'),('cmpl','$0x3, %edx'),('jae',None)])
has(D,'cmpl','$0xa0, %eax'); has(D,'cmpl','$0x96, %eax')
checks.append('type mask 0xfff, size-adjust shift 30, matching types 100..102, interest 160 and async 150')
seq(D,[('movabsq','$0x7fffffffc, %rcx'),('addq','%rax, %rcx'),('shrq','$0x3, %rcx')])
def installed_count(left): return ((left+0x7fffffffc)>>3)&M32
def source_count(left): return (((left-layout['completion_args_offset'])&M64)//8)&M32
random.seed(20261002)
samples=list(range(0,4096))+[M32-k for k in range(4096)]+[random.randrange(M32+1) for _ in range(200000)]
assert all(installed_count(v)==source_count(v) for v in samples)
assert installed_count(0)==M32 and installed_count(4)==0 and installed_count(80+8*7-72)==7
checks.append('async count arithmetic equals LP64 (leftOver-4)/8 narrowed to 32 bits on %d samples, including underflow to 0xffffffff'%len(samples))
reference=lambda i:layout['reference_offset']+8*i
result=layout['notification64']; args=result+layout['completion_args_offset']
for operands in ('0x%x(%%r14), %%rax'%reference(1),'0x%x(%%r14), %%rdi'%reference(2)): has(D,'movq',operands)
has(D,'movl','0x%x(%%r14), %%esi'%result)
has(D,'movq','0x%x(%%r14), %%rdx'%args); has(D,'movq','0x%x(%%r14), %%rcx'%(args+8))
seq(D,[('addq','$0x%x, %%r14'%args),('movq','%r14, %rdx'),('callq','*%rax')])
count_compares={o for m,o in pairs(D) if m.startswith('cmp') and o.endswith('%ecx')}
assert count_compares=={'$0x2, %ecx','$0x1, %ecx'} and ('testl','%ecx, %ecx') in pairs(D)
assert not any(m.startswith('cmp') and o.startswith('$0x10,') for m,o in pairs(D))
checks.append('callback slot 1, refcon slot 2, result +72, arguments +76; arity 0/1/2/default only; no 16-argument maximum')
r14_reads=[]
for offset,m,o in body(D):
    for match in re.finditer(r'(-?0x[0-9a-f]+)?\(%r14\)',o): r14_reads.append((offset,int(match.group(1) or '0',16)))
header_set=offset_of(D,'leaq','(%rsi,%rax,4), %r14')
assert {d for _,d in r14_reads}=={0,0x4,0x10,0x18,0x20,0x48,0x4c,0x54}
assert all(offset<header_set for offset,d in r14_reads if d==0)
checks.append('content-size field (header offset 0) is never read; only descriptor count is read through the pre-header pointer')
indirect=[o for m,o in pairs(D) if m=='callq' and o.startswith('*')]
assert indirect.count('*%rax')==5 and indirect.count('*0x10(%r14)')==1 and len(indirect)==6
assert calls(D).count('stub:mach_port_deallocate')==2
for aliases in (['rdi','edi','di','dil'],['rdx','edx','dx','dl'],['rcx','ecx','cx','cl']):
    assert first_use_is_write(D,aliases) is not None
checks.append('port, CF size and context argument registers are overwritten before any read in a linear scan')

# 2. Dispatch-queue receive handler.
W='IOKit`_IODispatchCalloutWithDispatch'
assert calls(W)==['stub:mig_reply_setup','sym:IODispatchCalloutFromCFMessage']
has(W,'movq','%rdi, %r14'); has(W,'movq','%rsi, %rbx')
has(W,'movl','%s, 0x%x(%%rbx)'%(imm(-305),layout['retcode_offset']))
seq(W,[('movq','%r14, %rsi'),('callq','sym:IODispatchCalloutFromCFMessage'),('movl','$0x1, %eax')])
assert not any(re.search(r'%(r|e)?(dx|cx|di)\b',o) for m,o in pairs(W)[pairs(W).index(('callq','stub:mig_reply_setup'))+1:])
checks.append('MIG callout sets MIG_NO_REPLY at RetCode +32, passes only the message register and returns true; source msgh_size/context arguments are absent')
B='IOKit`__IONotificationPortSetDispatchQueue_block_invoke'
assert pairs(B)==[('pushq','%rbp'),('movq','%rsp, %rbp'),('movq','0x20(%rdi), %rdi'),
    ('leaq','[sym:_IODispatchCalloutWithDispatch], %rdx'),('movl',imm(layout['dispatch_max_message'])+', %esi'),
    ('popq','%rbp'),('jmp','stub:dispatch_mig_server')]
assert layout['dispatch_max_message']==8*1024-layout['max_trailer']==8124
checks.append('dispatch source handler calls dispatch_mig_server with 8124 = 8 KiB - 68-byte maximum trailer')
S='IOKit`IONotificationPortSetDispatchQueue'
assert calls(S)==['stub:dispatch_source_cancel','stub:dispatch_release','stub:dispatch_source_create',
    'stub:dispatch_set_context','stub:dispatch_source_set_event_handler',
    'stub:dispatch_source_set_cancel_handler_f','stub:dispatch_activate']
seq(S,[('lock',''),('incl','0x20(%rbx)')])
has(S,'leaq','[sym:__IONotificationPortSetDispatchQueue_block_invoke], %rax'); has(S,'movq','%r14, 0x20(%rsi)')
has(S,'leaq','[sym:IONotificationPortRelease], %rsi')
checks.append('notification port setter call order and captured source match the pinned source')

# 3. Async wrappers.
assert pairs('IOKit`IOConnectCallAsyncScalarMethod')==[('pushq','%rbp'),('movq','%rsp, %rbp'),('subq','$0x40, %rsp'),
    ('movl','0x10(%rbp), %eax'),('movups','0x18(%rbp), %xmm0'),('xorps','%xmm1, %xmm1'),('movups','%xmm1, 0x28(%rsp)'),
    ('movups','%xmm0, 0x18(%rsp)'),('movups','%xmm1, 0x8(%rsp)'),('movl','%eax, (%rsp)'),
    ('callq','sym:IOConnectCallAsyncMethod'),('addq','$0x40, %rsp'),('popq','%rbp'),('retq','')]
assert pairs('IOKit`IOConnectCallAsyncStructMethod')==[('pushq','%rbp'),('movq','%rsp, %rbp'),('subq','$0x40, %rsp'),
    ('movq','0x10(%rbp), %rax'),('movups','0x18(%rbp), %xmm0'),('movups','%xmm0, 0x28(%rsp)'),('xorps','%xmm0, %xmm0'),
    ('movups','%xmm0, 0x18(%rsp)'),('movq','%rax, 0x10(%rsp)'),('movq','%r9, 0x8(%rsp)'),('movl','$0x0, (%rsp)'),
    ('xorl','%r9d, %r9d'),('callq','sym:IOConnectCallAsyncMethod'),('addq','$0x40, %rsp'),('popq','%rbp'),('retq','')]
checks.append('scalar and struct wrappers forward the pinned argument order with NULL/0 for the absent payloads')
A='IOKit`IOConnectCallAsyncMethod'
has(A,'cmpq','$0x%x, %%r14'%inband_max); has(A,'cmpq','$0x%x, %%rsi'%inband_max)
seq(A,[('cmpq','$0x%x, (%%rdx)'%(inband_max+1)),('jb',None)])
seq(A,[('testq','%rax, %rax'),('leaq','[sym:IOConnectCallAsyncMethod.zero], %r13'),('cmovneq','%rax, %r13')])
seq(A,[('testq','%rcx, %rcx'),('sete','%sil'),('testl','%r8d, %r8d'),('sete','%r10b'),('testb','%r10b, %sil'),
    ('movl','$0x1, %r10d'),('cmovel','%r8d, %r10d'),('leaq','[sym:IOConnectCallAsyncMethod.temp_reference], %r8'),
    ('cmoveq','%rcx, %r8')])
push_start=seq(A,[('subq','$0x8, %rsp')])
rows=pairs(A); call=rows.index(('callq','sym:io_connect_async_method'))
assert [o for m,o in rows[push_start:call] if m=='pushq']==['%r15','%rax','%r13','0x28(%rbp)','%r12','%rbx','%r14',
    '-0x40(%rbp)','-0x48(%rbp)','%r11','%rax'] and rows[call+1]==('addq','$0x60, %rsp')
for move in (('movl','%edx, %esi'),('movq','%r8, %rdx'),('movl','%r10d, %ecx'),('movl','%r9d, %r8d'),('movq','-0x50(%rbp), %r9')):
    assert move in rows[push_start:call], move
checks.append('generic wrapper: 4096-byte inband split, NULL output-count static, NULL/0 reference substitution and 17-argument stub order')

# 4. Generated client stub.
G='IOKit`io_connect_async_method'
seq(G,[('movl',imm(-307)+', %ebx'),('cmpl','$0x8, %ecx'),('ja',None)])
seq(G,[('cmpl','$0x10, %r15d'),('ja',None)]); seq(G,[('cmpl','$0x%x, %%r13d'%inband_max),('jbe',None)])
bits=layout['complex_bit']|layout['copy_send']|layout['make_send_once']<<8
base_index=seq(G,[('leaq',None),('movl','%eax, 0xc(%rdi)'),('movl',imm(bits)+', (%rdi)')])
base=int(re.fullmatch(r'(-0x[0-9a-f]+)\(%rbp\), %rdi',pairs(G)[base_index][1]).group(1),16)
frame=lambda field:disp(base+layout[field])+'(%rbp)'
has(G,'movl','$0x1, '+frame('request_descriptor_count_offset'))
has(G,'movl','%esi, '+frame('request_wake_name_offset'))
has(G,'movl',imm(layout['wake_disposition_word'])+', '+frame('request_wake_disposition_offset'))
has(G,'leaq',frame('request_reference_offset')+', %rbx')
has(G,'movl','$0x%x, %%ecx'%(layout['request_max']-layout['request_reference_offset']))
has(G,'addl','$0x%x, %%ebx'%layout['request_fixed'])
assert disp(base+layout['request_max'])=='-0x30' and ('movq','%rax, -0x30(%rbp)') in pairs(G)
has(G,'movabsq','$0x%x, %%r8'%(routine['request_msgh_id']<<32))
has(G,'movabsq','$0x%x, %%rsi'%(kobject_call|layout['send_msg']|layout['rcv_msg']))
has(G,'pushq','$0x%x'%layout['reply_receive_size']); assert calls(G).count('stub:mach_msg2_internal')==1
checks.append('request: <=8 references, <=16 scalars, <=4096 inband bytes, 104-byte fixed size, MAKE_SEND wake descriptor, ID 2866, private kobject-call option and 4284-byte receive')
seq(G,[('leal','-0x10000002(%rbx), %eax'),('cmpl','$0xe, %eax'),('ja',None),('movl','$0x4003, %ecx'),('btl','%eax, %ecx')])
assert {0x10000002+i for i in range(15) if 0x4003>>i&1}=={layout['send_invalid_data'],layout['send_invalid_dest'],layout['send_invalid_header']}
assert {'stub:mig_put_reply_port','stub:mig_dealloc_reply_port','stub:mach_msg_destroy'}<=set(calls(G))
has(G,'cmpl','$0x%x, %%eax'%layout['notify_send_once']); has(G,'cmpl','$0x%x, %%eax'%routine['reply_msgh_id'])
for value in ('mig_server_died','mig_reply_mismatch','mig_type_error'): assert any(o.startswith(imm(layout[value])+',') for m,o in pairs(G))
seq(G,[('leal','-0x%x(%%rax), %%ecx'%(layout['reply_max_without_trailer']+1)),('cmpl','$0xffffef7e, %ecx'),('ja',None)])
accepted=lambda size:((size-(layout['reply_max_without_trailer']+1))&M32)>0xffffef7e
assert all(accepted(s)==(layout['reply_minimum']<=s<=layout['reply_max_without_trailer']) for s in list(range(0,9000))+[M32-k for k in range(64)])
has(G,'cmpl','$0x%x, %%eax'%layout['mig_reply_error'])
checks.append('reply: send-error reply-port policy, ID 2966, server-died and mismatch codes, size window 52..4276 and 36-byte error replies')
seq(G,[('movq','0x50(%rbp), %rax'),('movl','(%rax), %eax'),('cmpl','$0x10, %eax'),('movl','$0x10, %ecx'),('cmovbl','%eax, %ecx')])
rows=pairs(G)
writes=[i for i in range(len(rows)-1) if rows[i]==('movq','0x50(%rbp), %rcx') and rows[i+1]==('movl','%eax, (%rcx)')]
assert len(writes)==2
too_large_returns=[r[0] for r in body(G) if r[1]=='movl' and r[2]==imm(-307)+', %ebx']
assert len(too_large_returns)==2 and rows[writes[0]+2]==('jmp','<+%d>'%too_large_returns[1])
assert ('xorl','%ebx, %ebx') in rows[writes[1]+2:writes[1]+5]
checks.append('scalar-output count is read as capacity min(*count,16) and written back on success and scalar-count MIG_ARRAY_TOO_LARGE paths')

# 5. Current IOAccelerator registration call.
Q='IOAccelerator`IOAccelCommandQueueCreateWithQoS'
seq(Q,[('callq','stub:IONotificationPortGetMachPort'),('testl','%eax, %eax'),('je',None),('movl','0x18(%rbx), %edi'),
    ('xorps','%xmm0, %xmm0'),('movups','%xmm0, 0x8(%rsp)'),('movl','$0x0, (%rsp)'),('leaq','-0x70(%rbp), %rcx'),
    ('xorl','%esi, %esi'),('movl','%eax, %edx'),('movl','$0x3, %r8d'),('xorl','%r9d, %r9d'),
    ('callq','stub:IOConnectCallAsyncScalarMethod')])
seq(Q,[('leaq','[sym:ioAccelCommandQueueBlockFenceCallback], %rax'),('movq','%rax, -0x68(%rbp)'),('movq','%rbx, -0x60(%rbp)')])
assert not any(o.endswith('-0x70(%rbp)') for m,o in pairs(Q) if m.startswith('mov'))
zero_port=pairs(Q)[pairs(Q).index(('callq','stub:IONotificationPortGetMachPort'))+2]
cold=[r[0] for r in body(Q) if (r[1],r[2])==('callq','sym:IOAccelCommandQueueCreateWithQoS.cold.2')]
assert len(cold)==1 and zero_port==('je','<+%d>'%cold[0])
C2='IOAccelerator`IOAccelCommandQueueCreateWithQoS.cold.2'
assert ('leaq','[str:"wakePort"], %rcx') in pairs(C2) and pairs(C2)[-1]==('callq','stub:__assert_rtn')
checks.append('IOAccelerator registration: zero wake port asserts "wakePort", selector 0, reference count 3, slots 1/2 set, slot 0 unwritten, NULL output count')
print(json.dumps({'checks':len(checks),'arithmetic_samples':len(samples),'results':checks,
    'limits':'static instruction comparison on one boot; no callbacks, Mach messages, connections or GPU work'},indent=1))
```

</details>

<details>
<summary><code>mutation-check.py</code>: controlled checker failure paths</summary>

```python
# Controlled failure-path check for check-installed.py. Mutates the saved,
# normalized instruction dump in memory, writes each variant to an ignored
# directory and requires the checker to reject it. Executes no target code.
# Usage: mutation-check.py LAYOUT DUMP ROUTINE PRIVATE_MESSAGE_H VARIANT_DIR
import copy,json,subprocess,sys
from pathlib import Path
layout,dump,routine,header,variants=sys.argv[1:6]
checker=Path(__file__).with_name('check-installed.py')
variants=Path(variants); variants.mkdir(exist_ok=False)
saved=[l for l in Path(dump).read_text().splitlines() if l.startswith('INSTRUCTIONS_JSON ')]
assert len(saved)==1
original=json.loads(saved[0].split(' ',1)[1])

def replace(dump,name,old,new):
    hits=[r for r in dump[name]['instructions'] if (r[1],r[2])==old]
    assert len(hits)==1, 'mutation target missing or ambiguous: %s %s'%(name,old)
    hits[0][1],hits[0][2]=new
def insert_after(dump,name,anchor,new):
    rows=dump[name]['instructions']
    index=[i for i,r in enumerate(rows) if (r[1],r[2])==anchor]
    assert len(index)==1, 'mutation anchor missing or ambiguous: %s %s'%(name,anchor)
    rows.insert(index[0]+1,[rows[index[0]][0],new[0],new[1]])
D='IOKit`IODispatchCalloutFromCFMessage'; G='IOKit`io_connect_async_method'
mutations={
    'message-id':lambda d:replace(d,D,('cmpl','$0x35, 0x14(%rsi)'),('cmpl','$0x36, 0x14(%rsi)')),
    'reference-bound':lambda d:replace(d,G,('cmpl','$0x8, %ecx'),('cmpl','$0x9, %ecx')),
    'argument-maximum':lambda d:insert_after(d,D,('shrq','$0x3, %rcx'),('cmpl','$0x10, %ecx')),
    'dispatch-size':lambda d:replace(d,'IOKit`__IONotificationPortSetDispatchQueue_block_invoke',('movl','$0x1fbc, %esi'),('movl','$0x2000, %esi')),
    'content-size-read':lambda d:insert_after(d,D,('leaq','0x48(%r14), %r13'),('movl','(%r14), %edx')),
    'slot-zero-write':lambda d:insert_after(d,'IOAccelerator`IOAccelCommandQueueCreateWithQoS',('movq','%rbx, -0x60(%rbp)'),('movq','$0x0, -0x70(%rbp)')),
    'request-id':lambda d:replace(d,G,('movabsq','$0xb3200000000, %r8'),('movabsq','$0xb3100000000, %r8')),
    'wake-port-assertion':lambda d:replace(d,'IOAccelerator`IOAccelCommandQueueCreateWithQoS.cold.2',('leaq','[str:"wakePort"], %rcx'),('leaq','[str:"queue"], %rcx')),
    'missing-dump':None}

def run(name,variant):
    path=variants/(name+'.stdout')
    path.write_text('' if variant is None else 'INSTRUCTIONS_JSON '+json.dumps(variant,sort_keys=True)+'\n')
    process=subprocess.run([sys.executable,str(checker),layout,str(path),routine,header],
                           capture_output=True,text=True,timeout=60)
    lines=process.stderr.strip().splitlines() or ['']
    frames=[l for l in lines if 'check-installed.py", line ' in l]
    source=None
    if frames:
        number=int(frames[-1].rsplit('line ',1)[1].split(',')[0])
        source=checker.read_text().splitlines()[number-1].strip()
    return {'exit_code':process.returncode,'last_stderr_line':lines[-1],'rejecting_check':source}
results={'unmodified':run('unmodified',copy.deepcopy(original))}
assert results['unmodified']['exit_code']==0, results['unmodified']
for name,mutate in mutations.items():
    variant=None if mutate is None else copy.deepcopy(original)
    if mutate: mutate(variant)
    results[name]=run(name,variant)
    assert results[name]['exit_code']==1 and results[name]['last_stderr_line'].startswith('AssertionError'), (name,results[name])
print(json.dumps({'accepted':['unmodified'],'rejected':list(mutations),'results':results},indent=1))
```

</details>

These are project inspection programs. The layout structures are transcribed
from the IDL, not copied from generated or Apple implementation code.

## Remaining interfaces and gates

The user-space side of the async path is now accounted for on this build. Open
interfaces:

- **Private reply production.** IOAcceleratorFamily2 decides when queue replies are
  sent, their argument count and words, and the reference/wake-right release
  schedule. It is a general Apple family, not an AMD binary. Its on-disk bundle
  holds only metadata; locating its code in the world-readable kernel collections
  is the next read-only task.
- Kernel MIG server validation, libdispatch's `dispatch_mig_server`, installed
  mapping reclamation, vendor/indirect alias access, cross-thread reuse and
  complete commit dispatch remain separate.

These gates add to the [async-reply gates](xnu-async-replies.md#remaining-interfaces-and-gates)
for our future adapter:

| Gate | Measurable success criterion |
| --- | --- |
| Output-count conformance | Never report more scalar or structure output than the caller's requested capacity; registrations with a `NULL` output count always report zero scalar outputs. |
| Request validation | Out-of-range reference, scalar and structure counts are rejected in the kernel, independently of client-stub checks. |
| Registration result | Every nonzero result leaves no retained registration or wake right, including failures after the request is received. |
| Reply shape | Replies consumed through IOKit's dispatcher have the argument count required by their consumer; for the observed queue callback, at least seven 64-bit words. |

No private reply production, delivery count, order, cancellation, GPU completion
or independent Metal admission has been established.
