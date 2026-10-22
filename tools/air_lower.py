#!/usr/bin/env python3
"""Offline prototype: lower an AIR compute module to AMDGPU LLVM IR.

Input is AIR as public LLVM 20 prints it (`llvm-dis` of a `.air` file, opaque
pointers). Output is a module for `amdgcn-amd-amdhsa` that LLVM's own AMDGPU
target compiles for gfx90c. Nothing here runs on, loads into or talks to a GPU.

Scope (see docs/metal-frontend.md, "AIR lowering prototype"):
- kernels only (`!air.kernel`); vertex and fragment functions are refused;
- buffers in device (1), constant (2) and threadgroup (3) memory, at most one
  threadgroup buffer, and the eight compute built-ins in the inventory;
- relaxed atomics, barriers, fast/precise math and conversions;
- textures, samplers, argument buffers and SIMD-group operations are refused.

The kernel ABI is this prototype's own choice, not Apple's: buffer slot n is
the 8-byte pointer at kernarg offset 8n, sizes come from the HSA dispatch
packet, and a threadgroup buffer is the kernel's dynamic LDS, which LLVM places
right after the kernel's static LDS (`.group_segment_fixed_size`).
"""
import re
import sys

TRIPLE = 'amdgcn-amd-amdhsa'
SIMD_WIDTH = 64


class Unsupported(Exception):
    pass


# ---- metadata ---------------------------------------------------------------

def metadata_nodes(text):
    nodes = {}
    for m in re.finditer(r'^!(\d+) = (?:distinct )?!\{(.*)\}$', text, re.M):
        nodes[int(m.group(1))] = m.group(2)
    return nodes


def split_top(s, sep=','):
    out, depth, cur = [], 0, ''
    for ch in s:
        if ch in '([{<':
            depth += 1
        elif ch in ')]}>':
            depth -= 1
        if ch == sep and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def named_list(text, name):
    m = re.search(r'^!%s = !\{(.*)\}$' % re.escape(name), text, re.M)
    if not m:
        return []
    return [int(x.strip()[1:]) for x in m.group(1).split(',') if x.strip()]


def parse_arg(node):
    """One kernel argument node -> dict(index, kind, slot, space, name)."""
    items = split_top(node)
    arg = {'index': int(items[0].split()[1]), 'kind': items[1].strip('!"'), 'slot': None, 'space': None}
    for i, item in enumerate(items):
        if item == '!"air.location_index"':
            arg['slot'] = int(items[i + 1].split()[1])
        elif item == '!"air.address_space"':
            arg['space'] = int(items[i + 1].split()[1])
        elif item == '!"air.arg_name"':
            arg['name'] = items[i + 1].strip('!"')
    return arg


def kernels(text):
    nodes = metadata_nodes(text)
    found = []
    for k in named_list(text, 'air.kernel'):
        parts = split_top(nodes[k])
        name = re.match(r'ptr @([\w.$]+)', parts[0]).group(1)
        args = [parse_arg(nodes[int(a.strip()[1:])]) for a in split_top(nodes[int(parts[2][1:])])]
        found.append({'name': name, 'args': args})
    for stage in ('air.vertex', 'air.fragment'):
        if named_list(text, stage):
            raise Unsupported('%s functions are not lowered by this prototype' % stage)
    return found


# ---- types ------------------------------------------------------------------

def llvm_type(suffix):
    """AIR type suffix (f32, v4f16, i32, v2i32) -> LLVM type."""
    m = re.fullmatch(r'(?:v(\d+))?([fiu])(\d+)', suffix)
    if not m:
        raise Unsupported('type suffix %r' % suffix)
    count, kind, bits = m.groups()
    scalar = {'f16': 'half', 'f32': 'float', 'f64': 'double'}.get(kind + bits) if kind == 'f' else 'i' + bits
    if scalar is None:
        raise Unsupported('type suffix %r' % suffix)
    return '<%s x %s>' % (count, scalar) if count else scalar


def param_types(define_line):
    inner = define_line[define_line.index('(') + 1:define_line.rindex(')')]
    types = []
    for p in split_top(inner):
        p = re.sub(r'"[^"]*"', '', p)
        t = re.match(r'(ptr(?: addrspace\(\d+\))?|<\d+ x \w+>|\w+)', p).group(1)
        types.append(t)
    return types


# ---- intrinsic library ------------------------------------------------------

UNARY = {'sin': 'llvm.sin', 'cos': 'llvm.cos', 'exp': 'llvm.exp', 'exp2': 'llvm.exp2', 'log': 'llvm.log',
         'log2': 'llvm.log2', 'sqrt': 'llvm.sqrt', 'fabs': 'llvm.fabs', 'floor': 'llvm.floor',
         'ceil': 'llvm.ceil', 'trunc': 'llvm.trunc', 'rint': 'llvm.rint'}
BINARY = {'fmin': 'llvm.minnum', 'fmax': 'llvm.maxnum'}
RMW = {'add': 'add', 'sub': 'sub', 'and': 'and', 'or': 'or', 'xor': 'xor', 'xchg': 'xchg'}
SCOPES = {'global': ('addrspace(1)', 'agent'), 'local': ('addrspace(3)', 'workgroup')}


def mangle(t):
    m = re.fullmatch(r'<(\d+) x (\w+)>', t)
    base = m.group(2) if m else t
    short = {'float': 'f32', 'half': 'f16', 'double': 'f64'}.get(base, base)
    return 'v%s%s' % (m.group(1), short) if m else short


def define_intrinsic(name):
    """Return (definition text, set of LLVM declarations needed) for one air.* name."""
    n = name[len('air.'):]
    fast = n.startswith('fast_')
    if fast:
        n = n[len('fast_'):]
    flags = 'fast ' if fast else ''

    if n == 'wg.barrier':
        return ('define internal void @air.wg.barrier(i32 %flags, i32 %scope) alwaysinline {\n'
                '  fence syncscope("workgroup") release\n  call void @llvm.amdgcn.s.barrier()\n'
                '  fence syncscope("workgroup") acquire\n  ret void\n}\n', {'declare void @llvm.amdgcn.s.barrier()'})
    if n == 'simdgroup.barrier':
        return ('define internal void @air.simdgroup.barrier(i32 %flags, i32 %scope) alwaysinline {\n'
                '  fence syncscope("wavefront") release\n  call void @llvm.amdgcn.wave.barrier()\n'
                '  fence syncscope("wavefront") acquire\n  ret void\n}\n', {'declare void @llvm.amdgcn.wave.barrier()'})

    m = re.fullmatch(r'atomic\.(global|local)\.cmpxchg\.weak\.(i32)', n)
    if m:
        space, scope = SCOPES[m.group(1)]
        return ('define internal i32 @%s(ptr %s %%p, ptr %%expected, i32 %%desired, i32 %%os, i32 %%of, i32 %%scope, i1 %%v) alwaysinline {\n'
                '  %%e = load i32, ptr %%expected, align 4\n'
                '  %%r = cmpxchg weak ptr %s %%p, i32 %%e, i32 %%desired syncscope("%s") monotonic monotonic, align 4\n'
                '  %%old = extractvalue { i32, i1 } %%r, 0\n  store i32 %%old, ptr %%expected, align 4\n  ret i32 %%old\n}\n'
                % (name, space, space, scope), set())
    m = re.fullmatch(r'atomic\.(global|local)\.(add|sub|max|min|and|or|xor|xchg)(?:\.([su]))?\.(i32|f32)', n)
    if m:
        where, op, sign, ty = m.groups()
        space, scope = SCOPES[where]
        t = llvm_type(ty)
        if ty == 'f32':
            rmw = {'add': 'fadd', 'sub': 'fsub', 'max': 'fmax', 'min': 'fmin', 'xchg': 'xchg'}.get(op)
        elif op in ('max', 'min'):
            rmw = op if sign == 's' else 'u' + op
        else:
            rmw = RMW.get(op)
        if rmw is None:
            raise Unsupported(name)
        return ('define internal %s @%s(ptr %s %%p, %s %%v, i32 %%order, i32 %%scope, i1 %%vol) alwaysinline {\n'
                '  %%old = atomicrmw %s ptr %s %%p, %s %%v syncscope("%s") monotonic, align 4\n  ret %s %%old\n}\n'
                % (t, name, space, t, rmw, space, t, scope, t), set())

    m = re.fullmatch(r'convert\.([fsu])\.(\w+?)\.([fsu])\.(\w+)', n)
    if m:
        dk, dt, sk, st = m.groups()
        dty, sty = llvm_type(dt), llvm_type(st)
        if dk == 'f' and sk == 'f':
            dbits, sbits = int(re.search(r'\d+$', dt).group()), int(re.search(r'\d+$', st).group())
            op = 'fptrunc' if dbits < sbits else 'fpext' if dbits > sbits else 'bitcast'
        elif dk == 'f':
            op = 'sitofp' if sk == 's' else 'uitofp'
        elif sk == 'f':
            op = 'fptosi' if dk == 's' else 'fptoui'
        else:
            raise Unsupported(name)
        return ('define internal %s @%s(%s %%x) alwaysinline {\n  %%r = %s %s %%x to %s\n  ret %s %%r\n}\n'
                % (dty, name, sty, op, sty, dty, dty), set())

    m = re.fullmatch(r'(\w+?)\.(\w+)', n)
    if not m:
        raise Unsupported(name)
    op, suffix = m.groups()
    t = llvm_type(suffix)
    tm = mangle(t)
    if op in UNARY:
        f = '%s.%s' % (UNARY[op], tm)
        return ('define internal %s @%s(%s %%x) alwaysinline {\n  %%r = call %s%s @%s(%s %%x)\n  ret %s %%r\n}\n'
                % (t, name, t, flags, t, f, t, t), {'declare %s @%s(%s)' % (t, f, t)})
    if op in BINARY:
        f = '%s.%s' % (BINARY[op], tm)
        return ('define internal %s @%s(%s %%a, %s %%b) alwaysinline {\n  %%r = call %s%s @%s(%s %%a, %s %%b)\n  ret %s %%r\n}\n'
                % (t, name, t, t, flags, t, f, t, t, t), {'declare %s @%s(%s, %s)' % (t, f, t, t)})
    if op == 'fma':
        f = 'llvm.fma.' + tm
        return ('define internal %s @%s(%s %%a, %s %%b, %s %%c) alwaysinline {\n  %%r = call %s%s @%s(%s %%a, %s %%b, %s %%c)\n  ret %s %%r\n}\n'
                % (t, name, t, t, t, flags, t, f, t, t, t, t), {'declare %s @%s(%s, %s, %s)' % (t, f, t, t, t)})
    if op == 'rsqrt':
        f = 'llvm.sqrt.' + tm
        one = splat(t, '1.0')
        return ('define internal %s @%s(%s %%x) alwaysinline {\n  %%s = call %s%s @%s(%s %%x)\n  %%r = fdiv %s%s %s, %%s\n  ret %s %%r\n}\n'
                % (t, name, t, flags, t, f, t, flags, t, one, t), {'declare %s @%s(%s)' % (t, f, t)})
    if op == 'pow':
        # pow(x, y) = exp2(y * log2(x)); the fast form allows this approximation.
        if not fast:
            raise Unsupported('precise %s' % name)
        lg, ex = 'llvm.log2.' + tm, 'llvm.exp2.' + tm
        return ('define internal %s @%s(%s %%x, %s %%y) alwaysinline {\n  %%l = call fast %s @%s(%s %%x)\n'
                '  %%m = fmul fast %s %%y, %%l\n  %%r = call fast %s @%s(%s %%m)\n  ret %s %%r\n}\n'
                % (t, name, t, t, t, lg, t, t, t, ex, t, t),
                {'declare %s @%s(%s)' % (t, lg, t), 'declare %s @%s(%s)' % (t, ex, t)})
    if op == 'fract':
        # Metal: fract(x) = min(x - floor(x), 0x1.fffffep-1).
        fl, mn = 'llvm.floor.' + tm, 'llvm.minnum.' + tm
        return ('define internal %s @%s(%s %%x) alwaysinline {\n  %%f = call %s @%s(%s %%x)\n  %%d = fsub %s%s %%x, %%f\n'
                '  %%r = call %s @%s(%s %%d, %s %s)\n  ret %s %%r\n}\n'
                % (t, name, t, t, fl, t, flags, t, t, mn, t, t, splat(t, '0x3FEFFFFFE0000000'), t),
                {'declare %s @%s(%s)' % (t, fl, t), 'declare %s @%s(%s, %s)' % (t, mn, t, t)})
    if op == 'clamp':
        mx, mn = 'llvm.maxnum.' + tm, 'llvm.minnum.' + tm
        return ('define internal %s @%s(%s %%x, %s %%lo, %s %%hi) alwaysinline {\n  %%a = call %s @%s(%s %%x, %s %%lo)\n'
                '  %%r = call %s @%s(%s %%a, %s %%hi)\n  ret %s %%r\n}\n'
                % (t, name, t, t, t, t, mx, t, t, t, mn, t, t, t),
                {'declare %s @%s(%s, %s)' % (t, mx, t, t), 'declare %s @%s(%s, %s)' % (t, mn, t, t)})
    if op == 'dot':
        m2 = re.fullmatch(r'<(\d+) x (\w+)>', t)
        if not m2:
            raise Unsupported(name)
        el = m2.group(2)
        f = 'llvm.vector.reduce.fadd.' + tm
        return ('define internal %s @%s(%s %%a, %s %%b) alwaysinline {\n  %%p = fmul %s%s %%a, %%b\n'
                '  %%r = call reassoc %s @%s(%s -0.0, %s %%p)\n  ret %s %%r\n}\n'
                % (el, name, t, t, flags, t, el, f, el, t, el), {'declare %s @%s(%s, %s)' % (el, f, el, t)})
    raise Unsupported(name)


def splat(t, value):
    m = re.fullmatch(r'<(\d+) x (\w+)>', t)
    if not m:
        return value
    return '<%s>' % ', '.join(['%s %s' % (m.group(2), value)] * int(m.group(1)))


# ---- built-ins --------------------------------------------------------------

BUILTINS = ('air.thread_position_in_grid', 'air.thread_position_in_threadgroup',
            'air.threadgroup_position_in_grid', 'air.threads_per_threadgroup', 'air.threads_per_grid',
            'air.thread_index_in_simdgroup', 'air.simdgroup_index_in_threadgroup', 'air.threads_per_simdgroup')


def builtin_prologue():
    """Wrapper code computing every built-in as <3 x i32> or i32 values."""
    lines = ['  %dp = call ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()']
    for i, c in enumerate('xyz'):
        lines += ['  %%tid.%s = call i32 @llvm.amdgcn.workitem.id.%s()' % (c, c),
                  '  %%wg.%s = call i32 @llvm.amdgcn.workgroup.id.%s()' % (c, c),
                  # hsa_kernel_dispatch_packet_t: workgroup_size_x/y/z (u16) at 4/6/8,
                  # grid_size_x/y/z (u32) at 12/16/20.
                  '  %%wsp.%s = getelementptr i8, ptr addrspace(4) %%dp, i64 %d' % (c, 4 + 2 * i),
                  '  %%ws16.%s = load i16, ptr addrspace(4) %%wsp.%s, align 2' % (c, c),
                  '  %%ws.%s = zext i16 %%ws16.%s to i32' % (c, c),
                  '  %%gsp.%s = getelementptr i8, ptr addrspace(4) %%dp, i64 %d' % (c, 12 + 4 * i),
                  '  %%gs.%s = load i32, ptr addrspace(4) %%gsp.%s, align 4' % (c, c),
                  '  %%base.%s = mul i32 %%wg.%s, %%ws.%s' % (c, c, c),
                  '  %%gid.%s = add i32 %%base.%s, %%tid.%s' % (c, c, c)]
    for vec, comp in (('gid', 'gid'), ('tid', 'tid'), ('wg', 'wg'), ('ws', 'ws'), ('gs', 'gs')):
        prev = 'poison'
        for i, c in enumerate('xyz'):
            cur = '%%%s.v%d' % (vec, i)
            lines.append('  %s = insertelement <3 x i32> %s, i32 %%%s.%s, i32 %d' % (cur, prev, comp, c, i))
            prev = cur
    lines += ['  %lane.lo = call i32 @llvm.amdgcn.mbcnt.lo(i32 -1, i32 0)',
              '  %lane = call i32 @llvm.amdgcn.mbcnt.hi(i32 -1, i32 %lane.lo)',
              '  %flat.y = mul i32 %tid.y, %ws.x',
              '  %plane = mul i32 %ws.x, %ws.y',
              '  %flat.z = mul i32 %tid.z, %plane',
              '  %flat.xy = add i32 %tid.x, %flat.y',
              '  %flat = add i32 %flat.xy, %flat.z',
              '  %%sg = udiv i32 %%flat, %d' % SIMD_WIDTH]
    values = {'air.thread_position_in_grid': '%gid.v2', 'air.thread_position_in_threadgroup': '%tid.v2',
              'air.threadgroup_position_in_grid': '%wg.v2', 'air.threads_per_threadgroup': '%ws.v2',
              'air.threads_per_grid': '%gs.v2', 'air.thread_index_in_simdgroup': '%lane',
              'air.simdgroup_index_in_threadgroup': '%sg', 'air.threads_per_simdgroup': str(SIMD_WIDTH)}
    return lines, values


BUILTIN_DECLS = ['declare ptr addrspace(4) @llvm.amdgcn.dispatch.ptr()',
                 'declare i32 @llvm.amdgcn.mbcnt.lo(i32, i32)', 'declare i32 @llvm.amdgcn.mbcnt.hi(i32, i32)'] + \
    ['declare i32 @llvm.amdgcn.workitem.id.%s()' % c for c in 'xyz'] + \
    ['declare i32 @llvm.amdgcn.workgroup.id.%s()' % c for c in 'xyz']


def builtin_value(kind, ty, values, lines, n):
    v = values[kind]
    if v.endswith('.v2'):
        if ty == '<3 x i32>':
            return v
        if ty == 'i32':
            lines.append('  %%b%d = extractelement <3 x i32> %s, i32 0' % (n, v))
            return '%%b%d' % n
        if ty == '<2 x i32>':
            lines.append('  %%b%d = shufflevector <3 x i32> %s, <3 x i32> poison, <2 x i32> <i32 0, i32 1>' % (n, v))
            return '%%b%d' % n
        raise Unsupported('built-in %s as %s' % (kind, ty))
    if ty != 'i32':
        raise Unsupported('built-in %s as %s' % (kind, ty))
    return v


# ---- module rewrite ---------------------------------------------------------

def drop_function(text, name):
    text = re.sub(r'^define [^\n]*@%s\(.*?^}\n' % re.escape(name), '', text, flags=re.M | re.S)
    # Metadata nodes that named the dropped function now name nothing.
    return re.sub(r'ptr @%s(?=[\s,}])' % re.escape(name), 'null', text)


def lower(text, only=None):
    ks = kernels(text)
    if only is not None:
        if only not in [k['name'] for k in ks]:
            raise Unsupported('no kernel named %s' % only)
        for k in ks:
            if k['name'] != only:
                text = drop_function(text, k['name'])
        ks = [k for k in ks if k['name'] == only]
    if not ks:
        raise Unsupported('no air.kernel functions')
    out = text
    out = re.sub(r'^target datalayout = .*\n', '', out, flags=re.M)
    out = re.sub(r'^target triple = .*$', 'target triple = "%s"' % TRIPLE, out, flags=re.M)
    out = re.sub(r'^!(air\.[\w.]+|llvm\.module\.flags) = .*\n', '', out, flags=re.M)
    out = out.replace('addrspace(2)', 'addrspace(4)')
    # AIR allocas are generic (0); AMDGPU allocas live in private memory (5).
    out = re.sub(r'^(\s*)(%[\w.]+) = alloca (.*)$',
                 lambda m: '%s%%__p5.%s = alloca %s, addrspace(5)\n%s%s = addrspacecast ptr addrspace(5) %%__p5.%s to ptr'
                 % (m.group(1), m.group(2)[1:], m.group(3), m.group(1), m.group(2), m.group(2)[1:]), out, flags=re.M)
    out = re.sub(r'^\s*call void @llvm\.lifetime\.(start|end)\.p0\(.*\n', '', out, flags=re.M)

    wrappers, decls = [], set()
    for k in ks:
        name = k['name']
        define = re.search(r'^define [^\n]*@%s\([^\n]*$' % re.escape(name), out, re.M).group(0)
        types = param_types(define)
        out = re.sub(r'@%s(?=[\s,(])' % re.escape(name), '@__air.%s' % name, out)
        new_define = define.replace('@%s(' % name, '@__air.%s(' % name).replace('define ', 'define internal ', 1)
        new_define = re.sub(r'\) (local_unnamed_addr )?#', lambda m: ') %salwaysinline #' % (m.group(1) or ''), new_define, count=1)
        out = out.replace(define.replace('@%s(' % name, '@__air.%s(' % name), new_define)
        wrappers.append(wrapper(k, types, decls))

    declared = set(re.findall(r'^declare [^@\n]*@(air\.[\w.]+)\(', out, re.M))
    called = set(re.findall(r'call [^@\n]*@(air\.[\w.]+)\(', out))
    used = sorted(declared & called)
    for name in declared - called:
        out = re.sub(r'^declare [^@\n]*@%s\(.*\n' % re.escape(name), '', out, flags=re.M)
    check_calls(out)
    library = []
    for name in used:
        body, needs = define_intrinsic(name)
        library.append(body)
        decls |= needs
        out = re.sub(r'^declare [^@\n]*@%s\(.*\n' % re.escape(name), '', out, flags=re.M)
    existing = set(re.findall(r'^declare [^\n]*?(@[\w.]+)\(', out, re.M))
    decls = sorted(d for d in decls if d.startswith('@') or re.search(r'(@[\w.]+)\(', d).group(1) not in existing)
    return out.rstrip('\n') + '\n\n' + '\n'.join(wrappers) + '\n' + '\n'.join(library) + '\n' + '\n'.join(decls) + '\n'


def wrapper(k, types, decls):
    name, args = k['name'], k['args']
    by_index = {a['index']: a for a in args}
    slots = [a['slot'] for a in args if a['kind'] == 'air.buffer' and a['space'] in (1, 2)]
    nslots = max(slots) + 1 if slots else 0
    params = ['ptr addrspace(1) %%slot%d' % s for s in range(nslots)]
    body, values = builtin_prologue()
    decls.update(BUILTIN_DECLS)
    call_args, tg = [], 0
    for i, ty in enumerate(types):
        a = by_index.get(i)
        if a is None:
            raise Unsupported('%s: argument %d has no metadata' % (name, i))
        if a['kind'] == 'air.buffer' and a['space'] in (1, 2):
            if a['space'] == 2:
                params[a['slot']] = 'ptr addrspace(4) %%slot%d' % a['slot']
            call_args.append('%s %%slot%d' % (ty, a['slot']))
        elif a['kind'] == 'air.buffer' and a['space'] == 3:
            if tg:
                raise Unsupported('%s: more than one threadgroup buffer' % name)
            tg += 1
            decls.add('@__air.dynamic_lds = external addrspace(3) global [0 x i8], align 16')
            call_args.append('ptr addrspace(3) @__air.dynamic_lds')
        elif a['kind'] in BUILTINS:
            call_args.append('%s %s' % (ty, builtin_value(a['kind'], ty, values, body, i)))
        else:
            raise Unsupported('%s: argument kind %s' % (name, a['kind']))
    return ('define amdgpu_kernel void @%s(%s) {\n%s\n  call void @__air.%s(%s)\n  ret void\n}\n'
            % (name, ', '.join(params), '\n'.join(body), name, ', '.join(call_args)))


def check_calls(text):
    """Refuse non-relaxed atomics: the library lowers every atomic as monotonic."""
    for m in re.finditer(r'call [^@\n]*@(air\.atomic\.[\w.]+)\((.*)\)', text):
        name, ops = m.group(1), split_top(m.group(2))
        orders = ops[-4:-2] if 'cmpxchg' in name else ops[-3:-2]
        for o in orders:
            if o != 'i32 0':
                raise Unsupported('%s with memory order %s (only relaxed is lowered)' % (name, o))


def main(argv):
    only = None
    if len(argv) == 5 and argv[1] == '--kernel':
        only, argv = argv[2], argv[:1] + argv[3:]
    if len(argv) != 3:
        print('usage: air_lower.py [--kernel NAME] IN.ll OUT.ll', file=sys.stderr)
        return 2
    try:
        result = lower(open(argv[1]).read(), only)
    except Unsupported as e:
        print('unsupported: %s' % e, file=sys.stderr)
        return 1
    open(argv[2], 'w').write(result)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
