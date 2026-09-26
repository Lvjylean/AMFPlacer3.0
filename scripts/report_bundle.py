"""Allowlisted light report transport. DCP and symlink content never enters a bundle."""
import io
from pathlib import Path, PurePosixPath
import tarfile
import tempfile

EXTENSIONS = {'.json', '.rpt', '.log', '.md', '.tsv', '.csv', '.svg'}


def eligible_files(root):
    root = Path(root)
    for name in ['manifest.json', 'status.json', 'config.json']:
        path = root / name
        if path.is_file() and not path.is_symlink():
            yield path
    for name in ['reports', 'logs']:
        directory = root / name
        if directory.is_symlink():
            continue
        for path in sorted(directory.rglob('*')):
            if (path.is_file() and path.suffix.lower() in EXTENSIONS and not path.is_symlink()
                    and not any(p.is_symlink() for p in path.parents if p != root and root in p.parents)):
                yield path


def write_bundle(root, stream):
    root = Path(root)
    with tarfile.open(fileobj=stream, mode='w|gz') as archive:
        for path in eligible_files(root):
            archive.add(path, arcname=str(path.relative_to(root)), recursive=False)


def extract_bundle(blob, target):
    target = Path(target).resolve()
    with tarfile.open(fileobj=io.BytesIO(blob), mode='r:gz') as archive:
        members = archive.getmembers()
        # Validate the whole archive before writing any member.
        for member in members:
            path = PurePosixPath(member.name)
            if (not member.isfile() or path.is_absolute() or '..' in path.parts
                    or path.suffix.lower() not in EXTENSIONS):
                raise ValueError('Disallowed report member: ' + member.name)
            destination = target / member.name
            if destination.is_symlink() or any(p.is_symlink() for p in destination.parents if target in p.parents):
                raise ValueError('Refusing extraction through symlink: ' + member.name)
        for member in members:
            destination = target / member.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
                temporary = Path(output.name)
                with archive.extractfile(member) as source:
                    output.write(source.read())
            temporary.replace(destination)
    return len(members)
