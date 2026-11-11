"""Order of DTR/RTS versus open. No real serial port and no hardware."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))

import console_port  # noqa: E402


class FakeSerial:
    def __init__(self, *args, **kwargs):
        self.events = []
        self.events.append(("Serial", args, kwargs))
        self.is_open = False
        self.port = None
        self.baudrate = None
        self.timeout = None
        self.xonxoff = None
        self.rtscts = None
        self.dsrdtr = None
        self.dtr = True
        self.rts = True
        self.fail_open = False

    def open(self):
        self.events.append(
            (
                "open",
                {
                    "is_open": self.is_open,
                    "port": self.port,
                    "baudrate": self.baudrate,
                    "timeout": self.timeout,
                    "xonxoff": self.xonxoff,
                    "rtscts": self.rtscts,
                    "dsrdtr": self.dsrdtr,
                    "dtr": self.dtr,
                    "rts": self.rts,
                },
            )
        )
        if self.fail_open:
            raise OSError("open failed")
        self.is_open = True

    def close(self):
        self.events.append(("close", self.is_open))
        self.is_open = False


class FakeModule:
    def __init__(self, serial_cls):
        self.Serial = serial_cls
        self.created = None

    def __call_serial(self):
        raise AssertionError("unused")


def _module(cls):
    mod = FakeModule(None)

    def factory(*args, **kwargs):
        obj = cls(*args, **kwargs)
        mod.created = obj
        return obj

    mod.Serial = factory
    return mod


def test_lines_are_false_before_open():
    mod = _module(FakeSerial)
    ser = console_port.open_console_port(
        "COM4", baud=115200, timeout=0.2, serial_module=mod
    )
    assert ser.is_open
    kind, args, kwargs = ser.events[0]
    assert kind == "Serial"
    assert args == ()
    assert kwargs == {}
    assert ser.events[1][0] == "open"
    snap = ser.events[1][1]
    assert snap["is_open"] is False
    assert snap["port"] == "COM4"
    assert snap["baudrate"] == 115200
    assert snap["dtr"] is False
    assert snap["rts"] is False
    assert snap["xonxoff"] is False
    assert snap["rtscts"] is False
    assert snap["dsrdtr"] is False
    assert "close" not in [item[0] for item in ser.events]


def test_open_failure_closes_and_reraises():
    class Boom(FakeSerial):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.fail_open = True

    mod = _module(Boom)
    try:
        console_port.open_console_port("COM4", serial_module=mod)
    except OSError as exc:
        assert "open failed" in str(exc)
    else:
        raise AssertionError("open failure was swallowed")
    ser = mod.created
    assert [item[0] for item in ser.events] == ["Serial", "open", "close"]
    assert ser.is_open is False


def test_already_open_constructor_does_not_continue():
    class OpenEarly(FakeSerial):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, **kwargs)
            self.is_open = True

    mod = _module(OpenEarly)
    try:
        console_port.open_console_port("COM4", serial_module=mod)
    except RuntimeError as exc:
        assert "before DTR" in str(exc)
    else:
        raise AssertionError("early open was accepted")
    ser = mod.created
    assert ser.events[-1][0] == "close"
    assert not any(item[0] == "open" for item in ser.events)


def test_port_assignment_must_not_open():
    class OpensOnPort(FakeSerial):
        def __setattr__(self, name, value):
            object.__setattr__(self, name, value)
            if name == "port" and value and not self.is_open:
                object.__setattr__(self, "is_open", True)

    mod = _module(OpensOnPort)
    try:
        console_port.open_console_port("COM4", serial_module=mod)
    except RuntimeError as exc:
        assert "before DTR" in str(exc)
    else:
        raise AssertionError("open-on-port-assign was accepted")
    ser = mod.created
    assert ser.events[-1][0] == "close"
    assert not any(item[0] == "open" for item in ser.events)


if __name__ == "__main__":
    test_lines_are_false_before_open()
    test_open_failure_closes_and_reraises()
    test_already_open_constructor_does_not_continue()
    test_port_assignment_must_not_open()
    print("test_console_port: ok")
