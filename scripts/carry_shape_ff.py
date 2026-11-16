"""Read relative, primary C/E/H FF slots in UltraScale CARRY_CHAIN DCP templates."""
import json
from pathlib import Path
import subprocess
from zipfile import ZipFile

KEY = 'CARRY shape FF file'
HEADER = 'AMF_CARRY_SHAPE_FF 2'

def varint(buf, pos):
    value = shift = 0
    while pos < len(buf) and shift < 70:
        byte = buf[pos]
        pos += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, pos
        shift += 7
    raise ValueError('Invalid shape varint')

def messages(buf, start, end):
    while start < end:
        length, pos = varint(buf, start)
        stop = pos + length
        if stop > end:
            raise ValueError('Truncated shape record')
        fields = []
        while pos < stop:
            tag, pos = varint(buf, pos)
            wire = tag & 7
            if wire == 0:
                value, pos = varint(buf, pos)
            elif wire == 2:
                size, pos = varint(buf, pos)
                value = buf[pos:pos + size]
                pos += size
            elif wire in (1, 5):
                size = 8 if wire == 1 else 4
                value = int.from_bytes(buf[pos:pos + size], 'little')
                pos += size
            else:
                raise ValueError('Unknown shape field encoding')
            fields.append((tag >> 3, value))
        if pos != stop:
            raise ValueError('Shape record length mismatch')
        yield fields
        start = stop

def template_slots(members):
    carries = {(x,y): cell for cell,(x,y,kind,row) in members if kind == 4 and row == 7}
    for cell,(x,y,kind,row) in members:
        if kind == 2 and row in (2,4,7) and (x,y) in carries:
            yield carries[x,y], 'ABCDEFGH'[row] + 'FF', cell

def extract(dcp):
    with ZipFile(dcp) as archive:
        files = [n for n in archive.namelist() if n.endswith('.shape')]
        if not files:
            return []
        if len(files) != 1:
            raise ValueError('Ambiguous DCP shape archive')
        data = archive.read(files[0])
    header = dict(next(messages(data, 0, len(data))))
    if header.get(3) != 10 or header.get(8) != len(data) or not (0 < header[6] <= header[7] <= header[8]):
        raise ValueError('Unsupported DCP shape archive format')
    templates = {}
    template = None
    in_instances = False
    members = []
    carry_chain = False
    triples = set()
    for fields in messages(data, 0, header[6]):
        keys = tuple(k for k,v in fields)
        if fields == [(1, 3203355989)]:
            in_instances = True
        elif not in_instances:
            if keys[:3] == (1,2,2) and len(keys)>3:
                template = fields[0][1]
                templates[template] = []
            elif template is not None and keys == (1,2,3,4):
                templates[template].append(tuple(v for k,v in fields))
        elif fields == [(1,3203373465)]:
            members = []
            carry_chain = False
        elif fields == [(1,b'CARRY_CHAIN')]:
            carry_chain = True
        elif keys == (1,2,3):
            members.append((fields[0][1],fields[1][1]))
        elif keys == (2,3,4,5,6,7,8,9) and carry_chain:
            offsets = templates[fields[0][1]]
            triples.update(template_slots([(name, offsets[index]) for name,index in members]))
    # Resolve only names used by the small set of requested slot bindings.
    ids = {i for a,b,c in triples for i in (a,c)}
    string_ids = {f[0][1]:f[1][1] for f in messages(data, header[6],header[7])
                  if len(f)==4 and f[0][1] in ids and tuple(k for k,v in f)==(1,3,4,5)}
    wanted = set(string_ids.values())
    names = {f[1][1]:f[0][1].decode() for f in messages(data,header[7],header[8])
             if len(f)==3 and f[1][1] in wanted and isinstance(f[0][1],bytes)}
    resolved = sorted((names[string_ids[a]],b,names[string_ids[c]]) for a,b,c in triples)
    if len({(c,b) for c,b,f in resolved}) != len(resolved):
        raise ValueError('Ambiguous CARRY shape FF slot')
    return resolved

def materialize(dcp, out, schema=2):
    if schema not in (1, 2):
        raise ValueError('Unsupported CARRY shape FF schema')
    rows = [row for row in extract(dcp) if schema == 2 or row[1] != 'CFF']
    out.write_text(f'AMF_CARRY_SHAPE_FF {schema}'+'\n'+''.join(f'{a}\t{b}\t{c}\n' for a,b,c in rows))
    return rows

def configure(config, root, dcp, binary, dest):
    caps = json.loads(subprocess.check_output([str(binary),'--capabilities'],text=True,timeout=15))
    schema = caps.get('carry_shape_ff_schema')
    if schema not in (1, 2):
        if config.get(KEY):
            raise ValueError('Selected AMF does not support CARRY shape FF bindings')
        return
    out = dest / 'carry_shape_ff.tsv'
    materialize(dcp,out,schema)
    if config.get(KEY) and (root/config[KEY]).read_bytes() != out.read_bytes():
        raise ValueError('Configured CARRY shape FF bindings differ from the input DCP')
    config[KEY] = str(out)
