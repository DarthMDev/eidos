#!/usr/bin/env python3
"""tools/apigen.py -- the language-neutral mod API description (sdk/api/v<N>/).

Reads the mod ABI headers (sdk/mod_api.h, sdk/mod_state.h), the rule
language's name tables (sdk/mod_rule.c), the hookable set
(mod/declared_set.txt) and the settings grammar, and writes:

  sdk/api/v<N>/events.json     every event: id, name, refusable, payload
                               fields (C type, offset, size, writable)
  sdk/api/v<N>/state.json      the state table: calls in table order
  sdk/api/v<N>/services.json   the services table: calls in table order
  sdk/api/v<N>/enums.json      every named constant of the two headers
  sdk/api/v<N>/hooks.json      the declared set: address, name, region,
                               the events bound there
  sdk/api/v<N>/settings.json   the kinds a settings declaration has
  sdk/api/v<N>/rules.json      the rule language: actions, state field
                               names, event names and their fields
  sdk/api/v<N>/eidos_wasm.h    the header a wasm code mod compiles against
  sdk/api/v<N>/layout_check.c  offsetof checks the build runs against the
                               real headers (tests/test_api_layout.c)

docs/mod-code-wasm.md, "Seams kept for a later Lua tier", is the design.
The JSON is the description other tools (kenon, a later Lua binding) read;
the C headers stay the port's own source for now and the layout check
keeps the two in step.

usage: apigen.py [--check]   (--check: fail if the committed files differ)
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRIM = {
    'u8': (1, 1, 'uint8_t'), 's8': (1, 1, 'int8_t'),
    'u16': (2, 2, 'uint16_t'), 's16': (2, 2, 'int16_t'),
    'u32': (4, 4, 'uint32_t'), 's32': (4, 4, 'int32_t'),
    'u64': (8, 8, 'uint64_t'), 's64': (8, 8, 'int64_t'),
    'int': (4, 4, 'int32_t'),
}


def read(rel):
    with open(os.path.join(ROOT, rel), encoding='utf-8') as f:
        return f.read().replace('\r\n', '\n')


def strip_comments(text):
    return re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), text,
                  flags=re.S)


# ---------------------------------------------------------------------------
# Constants

def parse_enums(text):
    """Every NAME = value inside an enum { } block, in order."""
    out = []
    body = strip_comments(text)
    for m in re.finditer(r'enum\s*\{(.*?)\}', body, flags=re.S):
        for name, value in re.findall(r'(\w+)\s*=\s*([0-9xXa-fA-F]+)', m.group(1)):
            out.append((name, int(value, 0)))
    return out


def parse_defines(text, names):
    out = {}
    for name in names:
        m = re.search(r'#define\s+%s\s+([0-9xXa-fA-F]+)u?' % name, text)
        if m:
            out[name] = int(m.group(1).rstrip('uU'), 0)
    return out


# ---------------------------------------------------------------------------
# Structs

def parse_structs(text):
    """typedef struct Name { fields } Name;  ->  {Name: [(type, name, count,
    comment)]}.  Only plain data structs (no function pointers)."""
    structs = {}
    for m in re.finditer(r'typedef struct (\w+) \{\n(.*?)\n\} \1;', text, flags=re.S):
        name, body = m.group(1), m.group(2)
        if '(*' in body:
            continue
        fields = []
        for line in body.split('\n'):
            fm = re.match(r'\s*(\w+)\s+([\w\s,\[\]]+);\s*(?:/\*(.*?)(?:\*/)?)?\s*$', line)
            if not fm:
                continue
            ftype, decls, comment = fm.groups()
            for decl in decls.split(','):
                dm = re.match(r'\s*(\w+)\s*(?:\[(\w+)\])?\s*$', decl)
                fields.append((ftype, dm.group(1), dm.group(2), (comment or '').strip()))
        structs[name] = fields
    return structs


def layout(structs, defines):
    """Natural alignment, the layout the port's compilers give these
    pointer-free structs.  -> {Name: (size, align, [field dicts])}."""
    out = {}

    def get(name):
        if name in out:
            return out[name]
        off = 0
        align = 1
        res = []
        for ftype, fname, count, comment in structs[name]:
            if ftype in PRIM:
                size, al, ctype = PRIM[ftype]
            elif ftype in structs:
                size, al, _ = get(ftype)
                ctype = ftype
            else:
                raise SystemExit('apigen: unknown type %s in %s' % (ftype, name))
            n = 1
            if count is not None:
                n = int(count, 0) if count[0].isdigit() else defines[count]
            off = (off + al - 1) // al * al
            res.append({'name': fname, 'type': ftype, 'offset': off,
                        'size': size * n, 'count': n if count is not None else None,
                        'writable': comment.startswith('*mod'),
                        'doc': comment})
            off += size * n
            align = max(align, al)
        size = (off + align - 1) // align * align
        out[name] = (size, align, res)
        return out[name]

    for name in structs:
        get(name)
    return out


# ---------------------------------------------------------------------------
# Tables of calls

def parse_table(text, tname):
    m = re.search(r'typedef struct %s \{\n(.*?)\n\} %s;' % (tname, tname), text, flags=re.S)
    body = strip_comments(m.group(1))
    calls = []
    for cm in re.finditer(r'([\w ]+?)\s*\(\*(\w+)\)\s*\(([^)]*)\);', body):
        ret, name, params = cm.group(1).strip(), cm.group(2), cm.group(3).strip()
        plist = []
        if params and params != 'void':
            for p in params.split(','):
                p = ' '.join(p.split())
                pm = re.match(r'(.*?)(\w+)$', p)
                ptype, pname = pm.group(1).strip(), pm.group(2)
                plist.append({'type': ptype, 'name': pname})
        calls.append({'name': name, 'ret': ret, 'params': plist})
    return calls


# ---------------------------------------------------------------------------
# The rule language

def parse_rules(rule_c):
    events = []
    em = re.search(r'static const BwRuleEvent g_events\[\] = \{(.*?)\n\};', rule_c, flags=re.S)
    for m in re.finditer(r'\.kind = (\w+), \.name = "([\w.]+)",\s*\.refusable = (\d)', em.group(1)):
        events.append({'kind': m.group(1), 'name': m.group(2), 'refusable': m.group(3) == '1'})
    fields = []
    fm = re.search(r'static const BwRuleStateField g_state_fields\[\] = \{(.*?)\n\};', rule_c, flags=re.S)
    for m in re.finditer(r'\.name = "(\w+)", \.kind = BW_RULE_SK_(\w+),\s*\.writable = \d,\s*'
                         r'\.readable = (\d), \.writable_ok = (\d)', fm.group(1)):
        fields.append({'name': m.group(1),
                       'subscript': {'PLAIN': None, 'MON': 'mon', 'INT': 'int'}[m.group(2)],
                       'readable': m.group(3) == '1', 'writable': m.group(4) == '1'})
    actions = []
    am = re.search(r'enum \{\n(\s*BW_RULE_ACT_REFUSE.*?)\};', rule_c, flags=re.S)
    for m in re.finditer(r'BW_RULE_ACT_\w+,?\s*/\* (.*?) \*/', am.group(1)):
        actions.append(m.group(1))
    version = int(re.search(r'#define BW_RULE_LANGUAGE_VERSION (\d+)u', rule_c).group(1))
    return {'language': version, 'actions': actions, 'state_fields': fields,
            'names': ['event.<field>', 'state.<cell>[key]', 'setting.<key>',
                      'table.<name>[key]'],
            'operators': ['||', '&&', '!', '==', '!=', '<', '<=', '>', '>=',
                          '+', '-', '*', '/', '%'],
            'events': events}


# ---------------------------------------------------------------------------
# The wasm header

WASM_TYPE = {
    'void': 'void', 'int': 'int32_t', 'u8': 'uint32_t', 's8': 'int32_t',
    'u16': 'uint32_t', 's16': 'int32_t', 'u32': 'uint32_t', 's32': 'int32_t',
    'u64': 'uint64_t', 's64': 'int64_t',
}


def wasm_params(params):
    """C parameters of a native call -> the wasm import's parameters.  A
    BwMonRef goes as its five fields; a pointer is an address in the mod's
    own memory; a handler is an index in the mod's function table."""
    out = []
    for p in params:
        t, n = p['type'], p['name']
        if t == 'BwMonRef':
            out += ['uint32_t %s_handle' % n, 'uint32_t %s_place' % n,
                    'uint32_t %s_side' % n, 'uint32_t %s_box' % n,
                    'uint32_t %s_slot' % n]
        elif t.endswith('*'):
            base = t.rstrip('*').strip()
            const = base.startswith('const ')
            core = base[6:] if const else base
            core = {'BwMonRef': 'EidosMonRef', 'BwModStateHeader': 'EidosModStateHeader',
                    'char': 'char', 'void': 'void'}.get(core, PRIM[core][2] if core in PRIM else core)
            out.append('%s%s *%s' % ('const ' if const else '', core, n))
        elif t.endswith('Handler'):
            out.append('uint32_t %s_index' % n)
        else:
            out.append('%s %s' % (WASM_TYPE[t], n))
    return out or ['void']


def wasm_header(api, enums, events, lay, state, services):
    L = []
    w = L.append
    w('/*')
    w(' * eidos_wasm.h -- what a wasm code mod calls and exports (api %d).' % api)
    w(' *')
    w(' * Generated by eidos tools/apigen.py from sdk/mod_api.h and')
    w(' * sdk/mod_state.h; do not edit.  docs/mod-code-wasm.md is the design.')
    w(' *')
    w(' * A mod is built for wasm32-freestanding: no libc, no WASI.  It reaches')
    w(' * the game only through the imports of module "eidos" below.  A pointer')
    w(' * argument is an address in the mod\'s own memory; a Pokemon reference')
    w(' * goes as its five fields; a handler is an index in the mod\'s function')
    w(' * table (export the table).  The payload a handler gets is a copy in the')
    w(' * mod\'s memory; the port reads the writable fields back after it returns.')
    w(' */')
    w('')
    w('#ifndef EIDOS_WASM_H')
    w('#define EIDOS_WASM_H')
    w('')
    w('#include <stdint.h>')
    w('')
    w('#define EIDOS_API_VERSION %d' % api)
    w('')
    w('#define EIDOS_IMPORT(name) \\')
    w('    __attribute__((import_module("eidos"), import_name(name)))')
    w('#define EIDOS_EXPORT(name) __attribute__((export_name(name)))')
    w('')
    w('/* The constants of sdk/mod_api.h and sdk/mod_state.h, BW_ spelled EIDOS_. */')
    w('enum {')
    for i, (name, value) in enumerate(enums):
        sep = ',' if i + 1 < len(enums) else ''
        w('    %s = %d%s' % ('EIDOS_' + name[3:], value, sep))
    w('};')
    w('')
    seen = set()
    order = ['BwEvent', 'BwMonRef', 'BwModStateHeader'] + [e['struct'] for e in events]
    for sname in order:
        if sname in seen or sname not in lay:
            continue
        seen.add(sname)
        size, _, fields = lay[sname]
        ename = 'Eidos' + sname[2:]
        w('typedef struct %s {' % ename)
        for f in fields:
            t = PRIM[f['type']][2] if f['type'] in PRIM else 'Eidos' + f['type'][2:]
            arr = '[%d]' % f['count'] if f['count'] is not None else ''
            doc = '  /* %s */' % f['doc'] if f['doc'] else ''
            w('    %s %s%s;%s' % (t, f['name'], arr, doc))
        w('} %s;  /* %d bytes */' % (ename, size))
        w('')
    w('/* A handler: return EIDOS_EVENT_CONTINUE or EIDOS_EVENT_REFUSE. */')
    w('typedef int32_t (*EidosHandler)(EidosEvent *event);')
    w('')
    w('/* The core imports. */')
    w('EIDOS_IMPORT("log") void eidos_log(const char *text, uint32_t len);')
    w('EIDOS_IMPORT("on") int32_t eidos_on(uint32_t kind, uint32_t handler_index);')
    w('EIDOS_IMPORT("override") int32_t eidos_override(uint32_t region, uint32_t addr,')
    w('                                                uint32_t handler_index);')
    w('EIDOS_IMPORT("rd8") uint32_t eidos_rd8(uint32_t ds_addr);')
    w('EIDOS_IMPORT("rd16") uint32_t eidos_rd16(uint32_t ds_addr);')
    w('EIDOS_IMPORT("rd32") uint32_t eidos_rd32(uint32_t ds_addr);')
    w('EIDOS_IMPORT("wr8") void eidos_wr8(uint32_t ds_addr, uint32_t value);')
    w('EIDOS_IMPORT("wr16") void eidos_wr16(uint32_t ds_addr, uint32_t value);')
    w('EIDOS_IMPORT("wr32") void eidos_wr32(uint32_t ds_addr, uint32_t value);')
    w('')
    for title, table in (('The state calls (BwModState).', state),
                         ('The services (BwModServices).', services)):
        w('/* %s */' % title)
        for c in table:
            ret = WASM_TYPE.get(c['ret'], 'int32_t')
            w('EIDOS_IMPORT("%s") %s eidos_%s(%s);' % (c['name'], ret, c['name'],
                                                     ', '.join(wasm_params(c['params']))))
        w('')
    w('/* What the module must export. */')
    w('EIDOS_EXPORT("eidos_mod_start") int32_t eidos_mod_start(void);')
    w('EIDOS_EXPORT("eidos_mod_stop") void eidos_mod_stop(void);')
    w('')
    w('#endif /* EIDOS_WASM_H */')
    return '\n'.join(L) + '\n'


# ---------------------------------------------------------------------------

def generate():
    api_h = read('sdk/mod_api.h')
    state_h = read('sdk/mod_state.h')
    rule_c = read('sdk/mod_rule.c')
    api = parse_defines(api_h, ['BW_MOD_API_VERSION'])['BW_MOD_API_VERSION']
    defines = parse_defines(api_h, ['BW_SCRIPT_ARGS_MAX'])
    structs = parse_structs(api_h)
    structs.update(parse_structs(state_h))
    lay = layout(structs, defines)
    enums = parse_enums(api_h) + parse_enums(state_h)
    enum_map = dict(enums)
    rules = parse_rules(rule_c)

    # The events: the id, the name and refusal from the rule table (every
    # port event has a row there), the payload struct from the payload
    # union of sdk/mod_event.h.
    event_h = read('sdk/mod_event.h')
    union = re.search(r'typedef union BwEventPayload \{(.*?)\} BwEventPayload;', event_h, flags=re.S).group(1)
    member_struct = dict((m.group(2), m.group(1)) for m in re.finditer(r'(Bw\w+)\s+(\w+);', union))
    events = []
    for e in rules['events']:
        member = e['name'].replace('.', '_')
        sname = member_struct.get(member)
        if sname is None:
            raise SystemExit('apigen: no payload member for %s' % e['name'])
        size, _, fields = lay[sname]
        events.append({'id': enum_map[e['kind']], 'name': e['name'],
                       'refusable': e['refusable'], 'struct': sname, 'size': size,
                       'fields': [dict((k, v) for k, v in f.items() if k != 'doc')
                                  for f in fields if f['name'] != 'h']})
    events.sort(key=lambda e: e['id'])
    rules_out = dict(rules)
    rules_out['events'] = [e['name'] for e in events]

    state = parse_table(state_h, 'BwModState')
    services = parse_table(state_h, 'BwModServices')

    hooks = []
    for line in read('mod/declared_set.txt').split('\n'):
        if not line.strip() or line.startswith('#'):
            continue
        cols = line.split('\t')
        hooks.append({'address': cols[0], 'name': cols[1], 'region': cols[2],
                      'events': [x for x in (cols[3].split(',') if len(cols) > 3 else []) if x]})

    settings = {
        'line': 'option <key> <kind> "<label>" <value>',
        'kinds': [
            {'kind': 'bool', 'value': 'on | off', 'setting_get': '1 or 0'},
            {'kind': 'choice', 'value': 'name|name|...', 'setting_get': 'the chosen index; the first is the default'},
            {'kind': 'int', 'value': '<min> <max> <default>', 'setting_get': 'the number'},
        ],
    }

    layout_c = ['/* Generated by tools/apigen.py; do not edit.  The offsets sdk/api/v%d/' % api,
                ' * events.json states, checked against the real headers. */',
                '#include <stddef.h>', '#include "mod_api.h"', '#include "mod_state.h"', '',
                '#define LAYOUT(s, f, o) _Static_assert(offsetof(s, f) == (o), #s "." #f)',
                '#define SIZE(s, n) _Static_assert(sizeof(s) == (n), #s)', '']
    for sname in sorted(set(['BwEvent', 'BwMonRef'] + [e['struct'] for e in events])):
        size, _, fields = lay[sname]
        layout_c.append('SIZE(%s, %d);' % (sname, size))
        for f in fields:
            layout_c.append('LAYOUT(%s, %s, %d);' % (sname, f['name'], f['offset']))
    layout_c.append('')

    def js(obj):
        return json.dumps(obj, indent=2) + '\n'

    d = 'sdk/api/v%d' % api
    return {
        d + '/events.json': js({'api': api, 'events': events}),
        d + '/state.json': js({'api': api, 'table': 'BwModState', 'calls': state}),
        d + '/services.json': js({'api': api, 'table': 'BwModServices', 'calls': services}),
        d + '/enums.json': js({'api': api, 'constants': [{'name': n, 'value': v} for n, v in enums]}),
        d + '/hooks.json': js({'api': api, 'hooks': hooks}),
        d + '/settings.json': js(dict({'api': api}, **settings)),
        d + '/rules.json': js(dict({'api': api}, **rules_out)),
        d + '/eidos_wasm.h': wasm_header(api, enums, events, lay, state, services),
        d + '/layout_check.c': '\n'.join(layout_c),
    }


def main():
    check = '--check' in sys.argv[1:]
    files = generate()
    stale = []
    for rel, text in files.items():
        path = os.path.join(ROOT, rel)
        old = None
        if os.path.exists(path):
            with open(path, encoding='utf-8') as f:
                old = f.read().replace('\r\n', '\n')
        if old == text:
            continue
        if check:
            stale.append(rel)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text)
        print('apigen: wrote', rel)
    if stale:
        print('apigen: out of date (run tools/apigen.py):', ', '.join(stale))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
