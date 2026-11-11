"""Choose a laptop input and skip the Chromatic USB device by default.

No sounddevice import. Callers pass the device list from query_devices().
Indexes are the positions sounddevice uses for InputStream(device=).
"""


def input_devices(devices):
    """Return input-only devices as {index, name, max_input_channels}."""
    found = []
    for position, dev in enumerate(devices or []):
        try:
            channels = int(dev.get("max_input_channels") or 0)
        except (TypeError, ValueError):
            channels = 0
        if channels <= 0:
            continue
        name = str(dev.get("name") or "")
        found.append(
            {
                "index": position,
                "name": name,
                "max_input_channels": channels,
            }
        )
    return found


def _chromatic(name):
    return "chromatic" in name.lower()


def format_inputs(devices):
    rows = input_devices(devices)
    if not rows:
        return "(no input devices)"
    return "\n".join("[%d] %s" % (row["index"], row["name"]) for row in rows)


def select_input(devices, default_index, mic):
    """Pick an input.

    mic is None for the default: the host default if it is a real input,
    otherwise the first input whose name does not contain Chromatic.
    If none remain, raise ValueError and list the inputs.
    mic may be an index (digits) or a case-insensitive name substring.
    One match is used, including a Chromatic device when asked for.
    Several matches or a missing index raise ValueError.
    """
    rows = input_devices(devices)
    spec = "" if mic is None else str(mic).strip()
    if spec == "":
        if default_index is not None:
            for row in rows:
                if row["index"] == default_index and not _chromatic(row["name"]):
                    return row["index"], row["name"]
        for row in rows:
            if not _chromatic(row["name"]):
                return row["index"], row["name"]
        raise ValueError(
            "No microphone left after skipping inputs whose name contains "
            "Chromatic. Pass --mic INDEX or --mic NAME.\n" + format_inputs(devices)
        )

    if spec.isdigit():
        index = int(spec)
        for row in rows:
            if row["index"] == index:
                return row["index"], row["name"]
        raise ValueError(
            "No input device with index %s.\n%s" % (spec, format_inputs(devices))
        )

    matches = [row for row in rows if spec.lower() in row["name"].lower()]
    if len(matches) == 1:
        return matches[0]["index"], matches[0]["name"]
    if not matches:
        raise ValueError("No input matches %r.\n%s" % (spec, format_inputs(devices)))
    raise ValueError(
        "Mic name %r matches more than one input. Pass the index.\n%s"
        % (spec, "\n".join("[%d] %s" % (row["index"], row["name"]) for row in matches))
    )
