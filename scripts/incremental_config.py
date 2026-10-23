"""Canonical incremental enable key and read-only legacy compatibility."""
import json
import subprocess

KEY = "IncrementalCompilation"
LEGACY_KEY = "IncremenralCompilation"


def incremental_enabled(config):
    values = {}
    for key in (KEY, LEGACY_KEY):
        if key not in config:
            continue
        value = config[key]
        if type(value) is bool:
            values[key] = value
        elif isinstance(value, str) and value in ("true", "false"):
            values[key] = value == "true"
        else:
            raise ValueError(key + " must be true or false")
    if len(values) == 2 and values[KEY] != values[LEGACY_KEY]:
        raise ValueError("Conflicting IncrementalCompilation and legacy IncremenralCompilation")
    return values.get(KEY, values.get(LEGACY_KEY, False))


def normalize_incremental_config(config):
    """Mutate a newly loaded config, preserving job bool / AMF string form."""
    incremental_enabled(config)
    if LEGACY_KEY in config:
        config.setdefault(KEY, config[LEGACY_KEY])
        del config[LEGACY_KEY]
    return config


def validate_incremental_binary(config, binary):
    """Fail before a legacy executable can silently ignore the corrected key."""
    normalize_incremental_config(config)
    if not incremental_enabled(config):
        return
    result = subprocess.run([str(binary), "--capabilities"], capture_output=True, text=True, timeout=15)
    try:
        cap = json.loads(result.stdout)
        supported = result.returncode == 0 and cap.get("incremental_compilation_key_schema") == 1
    except (ValueError, AttributeError):
        supported = False
    if not supported:
        raise ValueError("Binary does not support IncrementalCompilation; refusing silent fallback")
