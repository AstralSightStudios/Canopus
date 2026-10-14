"""CPC2 frames for the Band 11 firmware-emulation scripts (the only device protocol)."""
import struct

NATIVE_STAGE = 13
RESTORE_AFTER_BOOT = 14
HEADER = 36


def frame(op, payload=b"", request_id=1):
    return struct.pack("<IHHHHIIIIII", 0x43504332, HEADER, 1, 1, 0, HEADER + len(payload),
                       op, request_id, 0, 0, len(payload)) + payload


def native_stage(stage):
    return frame(NATIVE_STAGE, struct.pack("<I", stage))


def restore_after_boot():
    return frame(RESTORE_AFTER_BOOT)


def legacy(command, arg):
    """Map the old (CPC1 command, arg) pairs used by the emulation scripts."""
    return restore_after_boot() if command == 0x4351000A else native_stage(arg)
