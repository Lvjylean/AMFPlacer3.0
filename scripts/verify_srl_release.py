#!/usr/bin/env python3
"""Verify the frozen algorithm source and, optionally, server-only GETRF inputs."""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def verify(root, variant, check_inputs=False):
    manifest = json.loads((root / 'docs/provenance/amf3pro-srl-compatible-20261012.json').read_text())
    expected = manifest.get(variant + '_publication_source_sha256', manifest[variant + '_source_sha256'])
    actual = {str(p.relative_to(root)): digest(p) for p in (root / 'src').rglob('*') if p.is_file()}
    differences = sorted(k for k in actual.keys() | expected.keys() if actual.get(k) != expected.get(k))
    if differences:
        raise ValueError('Algorithm source differs from frozen ' + variant + ': ' + ', '.join(differences))
    # The stored overlay must reconstruct the complete merged source exactly.
    if variant == 'max2':
        reconstructed = dict(actual)
        for name in manifest['merged_overlay_files']:
            reconstructed[name] = digest(root / 'variants/merged' / name)
        if reconstructed != manifest['merged_source_sha256']:
            raise ValueError('Merged overlay does not reproduce the frozen source')
    checked = 0
    if check_inputs:
        for key, record in manifest['inputs'].items():
            path = root / record['relative_path']
            if digest(path) != record['sha256']:
                raise ValueError('Input hash mismatch: ' + key)
            checked += 1
    return dict(variant=variant, source_files=len(actual), inputs_verified=checked,
                source_commit=manifest[variant + '_source_commit'], result='passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--variant', choices=['max2', 'merged'], default='max2')
    parser.add_argument('--inputs', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(verify(ROOT, args.variant, args.inputs), ensure_ascii=False, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
