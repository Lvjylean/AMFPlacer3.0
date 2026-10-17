#!/usr/bin/env python3
"""Verify the complete published SLL source tree and optional server-only inputs."""
import argparse
import json
from pathlib import Path

from verify_srl_release import digest

ROOT = Path(__file__).resolve().parents[1]


def verify(root, check_inputs=False):
    release = json.loads((root / 'docs/provenance/sll-quantity-release-20261017.json').read_text())
    base_path = root / release['base_manifest']
    if digest(base_path) != release['base_manifest_sha256']:
        raise ValueError('Published Pro provenance changed')
    base = json.loads(base_path.read_text())
    expected = dict(base['max2_publication_source_sha256'])
    expected.update(release['source_overrides_sha256'])
    # Keep the original release manifest immutable; layer this integration on it.
    extension = json.loads((root / 'docs/provenance/sll-legacy-components-20261017.json').read_text())
    if digest(root / 'docs/provenance/sll-quantity-release-20261017.json') != extension['base_manifest_sha256']:
        raise ValueError('Original SLL publication manifest changed')
    for path, record in extension['source_changes'].items():
        if expected.get(path) != record['before_sha256']:
            raise ValueError('Invalid component integration base: ' + path)
        expected[path] = record['after_sha256']
    actual = {str(p.relative_to(root)): digest(p)
              for p in (root / 'src').rglob('*') if p.is_file()}
    differences = sorted(k for k in actual.keys() | expected.keys()
                         if actual.get(k) != expected.get(k))
    if differences:
        raise ValueError('SLL source differs from publication: ' + ', '.join(differences))
    checked = 0
    if check_inputs:
        for key, record in base['inputs'].items():
            if digest(root / record['relative_path']) != record['sha256']:
                raise ValueError('Input hash mismatch: ' + key)
            checked += 1
    return dict(result='passed', source_files=len(actual), inputs_verified=checked,
                implementation_commit=release['implementation_commit'],
                component_origin_commit=extension['upstream_component_commit'],
                integrated_source_files=len(extension['source_changes']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--inputs', action='store_true')
    args = parser.parse_args()
    try:
        print(json.dumps(verify(ROOT, args.inputs), indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
