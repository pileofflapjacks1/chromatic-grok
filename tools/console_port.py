"""Open the Chromatic USB console port.

pyserial 3.5 (serial/serialutil.py, SerialBase.__init__) starts with
``_dtr_state`` and ``_rts_state`` True. If a port is passed to ``Serial()``,
``open()`` runs at the end of ``__init__`` with those defaults. The ``dtr``
and ``rts`` setters only store the level while the port is closed.
``Serial.open`` applies that stored level (pyserial docs for ``open``,
``rts``, and ``dtr``).

Call ``Serial()`` with no port, set ``dtr`` and ``rts`` False, leave
``xonxoff``, ``rtscts``, and ``dsrdtr`` False, then ``open()``. Do not
construct ``Serial(port)`` and clear the lines afterwards.

The same ``open()`` docs say some operating systems or drivers still assert
RTS or DTR as soon as the port is opened, so a glitch is possible when the
requested level is not the default True. Setting the lines first does not
prove that glitch is gone. Joe, via Carty, reported one Windows COM4 open
with this ordering that printed ``VOICE_BOT`` and ``VOICE_READY`` and did
not freeze. That is one report. The microphone path was not part of it.
"""

def open_console_port(port, baud=115200, timeout=0.2, serial_module=None):
    """Return an open port. Control lines are set before ``open()``."""
    if serial_module is None:
        import serial as serial_module

    ser = serial_module.Serial()
    if getattr(ser, "is_open", False):
        _close_quietly(ser)
        raise RuntimeError("serial port opened before DTR and RTS were set")

    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.xonxoff = False
    ser.rtscts = False
    ser.dsrdtr = False
    ser.dtr = False
    ser.rts = False

    if getattr(ser, "is_open", False):
        _close_quietly(ser)
        raise RuntimeError("serial port opened before DTR and RTS were set")

    try:
        ser.open()
    except Exception:
        _close_quietly(ser)
        raise
    return ser


def _close_quietly(ser):
    try:
        ser.close()
    except Exception:
        pass
