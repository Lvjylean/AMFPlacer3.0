#!/usr/bin/env python3
"""Fetch public OpenPARF inputs, retaining archives and provenance on the server."""
import argparse
import collections
import concurrent.futures
import datetime
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import tarfile
import threading
import time
from urllib.parse import urlsplit
import zipfile

import requests


SOURCES = [
    ("ispd2017", "ispd2017.tar.gz", 1214145129,
     "https://drive.usercontent.google.com/download?id=1Uf9qIZ8WL_jk03sIlAoS9dIrvYH3d1pz&export=download"),
    ("ispd2017-flexshelf", "ispd2017_flexshelf.tar.gz", 149133767,
     "https://drive.usercontent.google.com/download?id=1smt4lGUFdhs0TjPBzi9PqiyfA9n2Uwoy&export=download"),
    ("mlcad2023-updated", "updated-mlcad-2023-contest-benchmark.zip", 14556198536,
     "https://www.kaggle.com/api/v1/datasets/download/ismailbustany/updated-mlcad-2023-contest-benchmark"),
]


def utc():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def save_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n")
    temporary.replace(path)


class DownloadForm(HTMLParser):
    def __init__(self):
        super().__init__()
        self.action = None
        self.fields = {}

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "form":
            self.action = attrs.get("action")
        if tag == "input" and attrs.get("name"):
            self.fields[attrs["name"]] = attrs.get("value", "")


def open_download(session, url, offset, end=None):
    headers = {"Range": "bytes={}-{}".format(offset, end if end is not None else "")} if offset or end is not None else {}
    response = session.get(url, headers=headers, stream=True, timeout=(20, 60))
    response.raise_for_status()
    if "text/html" in response.headers.get("Content-Type", ""):
        if urlsplit(url).hostname != "drive.usercontent.google.com":
            response.close()
            raise RuntimeError("Unexpected HTML response instead of an archive")
        form = DownloadForm()
        form.feed(response.text)
        response.close()
        if not form.action or urlsplit(form.action).hostname != "drive.usercontent.google.com":
            raise RuntimeError("Expected public Google Drive download confirmation form")
        response = session.get(form.action, params=form.fields, headers=headers,
                               stream=True, timeout=(20, 60))
        response.raise_for_status()
    if "text/" in response.headers.get("Content-Type", ""):
        response.close()
        raise RuntimeError("Download endpoint returned text instead of an archive")
    if (offset or end is not None) and response.status_code != 206:
        response.close()
        raise RuntimeError("Server did not honor partial download Range; partial file preserved")
    if offset and not response.headers.get("Content-Range", "").startswith("bytes {}-".format(offset)):
        response.close()
        raise RuntimeError("Unexpected Content-Range")
    return response


def segmented_download(url, partial, expected, update):
    prefix = partial.stat().st_size if partial.exists() else 0
    if prefix == expected:
        return
    directory = partial.parent / (partial.name + ".segments")
    directory.mkdir(exist_ok=True)
    width = (expected - prefix + 7) // 8
    ranges = [(start, min(start + width, expected) - 1)
              for start in range(prefix, expected, width)]
    parts = [directory / ("{}-{}.part".format(start, end)) for start, end in ranges]
    lock = threading.Lock()
    last_report = [0.0]

    def progress(force=False):
        with lock:
            if force or time.monotonic() - last_report[0] >= 15:
                update(bytes=prefix + sum(p.stat().st_size for p in parts if p.exists()),
                       download_connections=len(ranges))
                last_report[0] = time.monotonic()

    def segment(item):
        (start, end), part = item
        for attempt in range(3):
            size = part.stat().st_size if part.exists() else 0
            if size == end - start + 1:
                return
            if size > end - start + 1:
                raise RuntimeError("Unexpected oversized download segment")
            try:
                with requests.Session() as session, open_download(session, url, start + size, end) as response:
                    with part.open("ab" if part.exists() else "xb") as out:
                        for chunk in response.iter_content(4 * 1024 * 1024):
                            if size + len(chunk) > end - start + 1:
                                raise RuntimeError("Response exceeds requested byte range")
                            out.write(chunk)
                            size += len(chunk)
                            progress()
                if size != end - start + 1:
                    raise RuntimeError("Truncated download segment")
                progress(True)
                return
            except Exception:
                if attempt == 2:
                    raise

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
        list(executor.map(segment, zip(ranges, parts)))
    update(state="assembling", bytes=expected)
    with partial.open("ab" if partial.exists() else "xb") as out:
        for part in parts:
            with part.open("rb") as stream:
                shutil.copyfileobj(stream, out, length=8 * 1024 * 1024)
    if partial.stat().st_size != expected:
        raise RuntimeError("Assembled archive length mismatch")
    # These are only this downloader's temporary shards; the archive is retained.
    for part in parts:
        part.unlink()
    directory.rmdir()


def inventory(archive, output):
    suffixes = collections.Counter()
    roots = collections.Counter()
    count = total = 0
    with output.open("w") as out:
        def record(name, size):
            nonlocal count, total
            count += 1
            total += size
            suffixes[Path(name).suffix.lower() or "<none>"] += 1
            roots[name.split("/")[0]] += 1
            out.write(json.dumps({"path": name, "bytes": size}) + "\n")
        if archive.suffix == ".zip":
            with zipfile.ZipFile(archive) as package:
                for member in package.infolist():
                    if not member.is_dir():
                        record(member.filename, member.file_size)
                bad = package.testzip()
                if bad:
                    raise RuntimeError("ZIP CRC verification failed: " + bad)
            verification = "all ZIP member CRCs verified"
        else:
            with tarfile.open(archive, "r|gz") as package:
                for member in package:
                    if member.isfile():
                        record(member.name, member.size)
            # Read the gzip footer as well, validating its full-stream CRC.
            import gzip
            with gzip.open(archive, "rb") as stream:
                while stream.read(8 * 1024 * 1024):
                    pass
            verification = "tar directory readable and gzip full-stream CRC verified"
    return {"file_count": count, "uncompressed_file_bytes": total,
            "extensions": dict(suffixes), "top_level": dict(roots), "verification": verification}


def fetch(root, source):
    name, filename, expected, url = source
    archive = root / "archives" / filename
    partial = archive.with_name(filename + ".part")
    status_path = root / (name + ".status.json")
    status = {"name": name, "source_url": url, "archive": str(archive),
              "expected_bytes_at_preflight": expected, "started_at": utc()}

    def update(**changes):
        status.update(changes)
        status["updated_at"] = utc()
        save_json(status_path, status)

    try:
        if not archive.exists():
            for attempt in range(1, 4):
                try:
                    offset = partial.stat().st_size if partial.exists() else 0
                    update(state="downloading", bytes=offset, attempt=attempt)
                    if expected > 2 * 1024 ** 3:
                        segmented_download(url, partial, expected, update)
                        partial.rename(archive)
                        break
                    with requests.Session() as session, open_download(session, url, offset) as response:
                        length = response.headers.get("Content-Length")
                        if length and int(length) + offset != expected:
                            raise RuntimeError("Source size changed since preflight; review new dataset version")
                        last_report = time.monotonic()
                        with partial.open("ab" if partial.exists() else "xb") as out:
                            for chunk in response.iter_content(4 * 1024 * 1024):
                                out.write(chunk)
                                offset += len(chunk)
                                if time.monotonic() - last_report >= 15:
                                    update(bytes=offset)
                                    last_report = time.monotonic()
                    if partial.stat().st_size != expected:
                        raise RuntimeError("Archive length mismatch")
                    partial.rename(archive)
                    break
                except Exception as error:
                    update(last_error=str(error))
                    if attempt == 3:
                        raise
        if archive.stat().st_size != expected:
            raise RuntimeError("Existing archive size differs; preserved without overwrite")
        update(state="verifying", bytes=archive.stat().st_size)
        digest = hashlib.sha256()
        with archive.open("rb") as stream:
            while True:
                chunk = stream.read(8 * 1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
        update(sha256=digest.hexdigest())
        details = inventory(archive, root / (name + ".inventory.jsonl"))
        update(state="complete", completed_at=utc(), exit_status=0, **details)
    except Exception as error:
        update(state="failed", error=str(error), exit_status=1)
    print(json.dumps(status, ensure_ascii=False), flush=True)
    return status


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--destination", required=True, type=Path)
    args = parser.parse_args()
    root = args.destination
    root.mkdir(parents=True, exist_ok=True)
    (root / "archives").mkdir(exist_ok=True)
    if shutil.disk_usage(root).free < 25 * 1024 ** 3:
        raise RuntimeError("Less than 25 GiB available for archive downloads")
    started = utc()
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as executor:
        records = list(executor.map(lambda source: fetch(root, source), SOURCES))
    manifest = {"started_at": started, "finished_at": utc(), "archives_only": True,
                "source_note": "Public URLs linked by official OpenPARF repositories; no RTL or placement reproduction claimed.",
                "downloads": records}
    save_json(root / "manifest.json", manifest)
    successful = [r for r in records if r["state"] == "complete"]
    (root / "SHA256SUMS").write_text("".join(
        "{}  archives/{}\n".format(r["sha256"], Path(r["archive"]).name) for r in successful))
    return 0 if len(successful) == len(SOURCES) else 1


if __name__ == "__main__":
    raise SystemExit(main())
