"""Preview progress backed by atomic records and an OS-held build lock."""
import fcntl
import json
import os
import time

CACHE_NAME = ".viewer-preview-hard-resources-v1"


def read_json(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}


def atomic_json(path, data):
    temporary = path.with_name(path.name + ".tmp-" + str(os.getpid()))
    temporary.write_text(json.dumps(data, ensure_ascii=False) + "\n")
    temporary.replace(path)


def signature(sources):
    return [[p.name, p.stat().st_size, p.stat().st_mtime_ns] for p in sources]


def lock_is_held(path):
    try:
        with path.open("r") as handle:
            try:
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return True
            fcntl.flock(handle, fcntl.LOCK_UN)
    except FileNotFoundError:
        pass
    return False


def preview_status(directory, sources):
    root = directory / CACHE_NAME
    total = len(sources)
    state = read_json(root / "build-status.json")
    manifest = read_json(root / "manifest.json")
    active = lock_is_held(root / "build.lock")
    current = signature(sources)
    existing = sum(
        (root / (p.name + ".webp")).is_file()
        and (root / (p.name + ".webp")).stat().st_size > 0
        for p in sources
    )
    result = {
        "state": "idle", "phase": "idle", "completed": 0, "total": total,
        "percent": 0.0, "active": active, "ready": False,
    }
    if active:
        same_sources = state.get("source_signature") == current
        result.update(
            state="running",
            phase=state.get("phase", "starting"),
            completed=min(existing, state.get("completed", 0)) if same_sources else 0,
            pid=state.get("pid"),
            updated_at=state.get("updated_at"),
            elapsed_seconds=max(0, time.time() - state.get("started_at", time.time())),
        )
    elif total and manifest.get("source_signature") == current and existing == total:
        result.update(state="ready", phase="ready", completed=total, ready=True,
                      elapsed_seconds=manifest.get("seconds"), bytes=manifest.get("bytes"))
    elif state.get("state") == "failed":
        result.update(state="failed", phase="failed", completed=existing,
                      message=state.get("message", "预览生成失败"))
    elif state.get("state") == "running":
        result.update(state="interrupted", phase="interrupted", completed=existing,
                      message="生成进程已结束，缓存尚未完成")
    elif manifest or state:
        result.update(state="stale", phase="stale", completed=existing,
                      message="缓存与当前轨迹不一致或文件不完整")
    elif existing:
        result.update(state="partial", phase="partial", completed=existing,
                      message="已有部分图片，但没有生成进程")
    if total:
        result["percent"] = round(100 * result["completed"] / total, 1)
    return result
