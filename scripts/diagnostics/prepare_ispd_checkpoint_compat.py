#!/usr/bin/env python3
"""Copy a legacy ISPD DCP without its incompatible incremental timing cache.

Never changes the original or the EDIF/XDC bytes. This prepares a migration
input, not a physical implementation or a replacement for the source archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def prepare(source, output):
    source, output = Path(source).resolve(), Path(output).resolve()
    manifest = output.with_suffix('.compat.json')
    if output.exists() or manifest.exists():
        raise ValueError('Output or manifest already exists')
    with zipfile.ZipFile(source) as original:
        xml = ET.fromstring(original.read('dcp.xml'))
        removed = []
        for element in list(xml):
            if element.tag == 'File' and element.attrib.get('Type') == 'INCR':
                removed.append(element.attrib['Name'])
                xml.remove(element)
        if not removed:
            raise ValueError('No INCR entry found; no compatibility change made')
        for name in removed:
            if name not in original.namelist():
                raise ValueError('Cache manifest entry is absent from archive: ' + name)
        output.parent.mkdir(parents=True, exist_ok=True)
        retained = {}
        with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED) as derived:
            for name in original.namelist():
                if name in removed:
                    continue
                if name == 'dcp.xml':
                    data = ET.tostring(xml, encoding='utf-8', xml_declaration=True)
                else:
                    data = original.read(name)
                    retained[name] = hashlib.sha256(data).hexdigest()
                derived.writestr(name, data)
    with zipfile.ZipFile(output) as derived:
        if derived.testzip() is not None:
            raise ValueError('Derived archive failed CRC verification')
        for name, expected in retained.items():
            if hashlib.sha256(derived.read(name)).hexdigest() != expected:
                raise ValueError('Retained entry changed: ' + name)
    result = dict(source=str(source), source_sha256=digest(source),
                  output=str(output), output_sha256=digest(output),
                  removed_cache_entries=removed, unchanged_entries_sha256=retained,
                  part=xml.find('Part').attrib['Name'], top=xml.find('Top').attrib['Name'])
    manifest.write_text(json.dumps(result, indent=2) + '\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print(json.dumps(prepare(args.source, args.output), indent=2))
