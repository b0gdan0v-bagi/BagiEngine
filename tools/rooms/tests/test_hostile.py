"""Tests of the pure parts of tools/rooms/hostile.py: the frame builder, the one-byte writer and the list of scenarios. Run: python3 -m unittest discover tools/rooms/tests"""

import struct
import sys
import unittest
from pathlib import Path

# tools/rooms, where hostile.py and smoke.py are (the tests run from a start folder of their own)
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import hostile
from bagi_control_client import FrameReader


class FakeSocket:
    """Records every sendall as a write of its own."""

    def __init__(self) -> None:
        self.writes: list[bytes] = []

    def sendall(self, data: bytes) -> None:
        self.writes.append(data)


class BuildFrameTest(unittest.TestCase):
    def test_a_short_payload_has_a_two_byte_header_and_is_masked(self) -> None:
        frame = hostile.build_frame(hostile.OPCODE_TEXT, b"abc", mask=b"\x01\x02\x03\x04")
        self.assertEqual(frame[:2], bytes([0x81, 0x80 | 3]))
        self.assertEqual(frame[2:6], b"\x01\x02\x03\x04")
        self.assertEqual(frame[6:], bytes([ord("a") ^ 1, ord("b") ^ 2, ord("c") ^ 3]))

    def test_the_length_encodings_change_at_126_and_65536(self) -> None:
        for length, header_size, marker in ((125, 2, 125), (126, 4, 126), (65535, 4, 126), (65536, 10, 127)):
            frame = hostile.build_frame(hostile.OPCODE_BINARY, b"x" * length, masked=False)
            self.assertEqual(frame[1], marker, length)
            self.assertEqual(len(frame), header_size + length, length)
        self.assertEqual(struct.unpack("!H", hostile.build_frame(hostile.OPCODE_TEXT, b"x" * 126, masked=False)[2:4])[0], 126)
        self.assertEqual(struct.unpack("!Q", hostile.build_frame(hostile.OPCODE_TEXT, b"x" * 65536, masked=False)[2:10])[0], 65536)

    def test_an_unmasked_frame_has_no_mask_bit_and_the_payload_as_it_is(self) -> None:
        frame = hostile.build_frame(hostile.OPCODE_TEXT, b"hi", masked=False)
        self.assertEqual(frame, bytes([0x81, 2]) + b"hi")

    def test_fin_and_opcode(self) -> None:
        self.assertEqual(hostile.build_frame(hostile.OPCODE_TEXT, fin=False, masked=False)[0], 0x01)
        self.assertEqual(hostile.build_frame(hostile.OPCODE_PING, masked=False)[0], 0x89)

    def test_a_declared_length_can_differ_from_the_payload(self) -> None:
        frame = hostile.build_frame(hostile.OPCODE_TEXT, b"abc", masked=False, declared_length=1 << 40)
        self.assertEqual(frame[1], 127)
        self.assertEqual(struct.unpack("!Q", frame[2:10])[0], 1 << 40)
        self.assertEqual(frame[10:], b"abc")

    def test_the_control_clients_reader_reads_what_the_builder_wrote(self) -> None:
        reader = FrameReader()
        reader.feed(hostile.build_frame(hostile.OPCODE_TEXT, b"hello world", mask=b"\xaa\xbb\xcc\xdd"))
        self.assertEqual(reader.pop(), (True, hostile.OPCODE_TEXT, b"hello world"))


class FragmentsTest(unittest.TestCase):
    def test_only_the_first_has_the_opcode_and_only_the_last_has_fin(self) -> None:
        frames = hostile.split_fragments(b"abcdef", [2, 2, 2])
        reader = FrameReader()
        for frame in frames:
            reader.feed(frame)
        self.assertEqual([reader.pop() for _ in range(3)], [(False, hostile.OPCODE_TEXT, b"ab"), (False, hostile.OPCODE_CONTINUATION, b"cd"), (True, hostile.OPCODE_CONTINUATION, b"ef")])

    def test_sizes_that_do_not_add_up_are_refused(self) -> None:
        with self.assertRaises(hostile.SmokeFailure):
            hostile.split_fragments(b"abcdef", [2, 2])


class MessageTest(unittest.TestCase):
    def test_padding_brings_a_message_to_the_size(self) -> None:
        padded = hostile.message_bytes({"type": "ping"}, hostile.MAX_MESSAGE_BYTES)
        self.assertEqual(len(padded), hostile.MAX_MESSAGE_BYTES)
        self.assertEqual(padded.rstrip(), hostile.message_bytes({"type": "ping"}))

    def test_a_message_longer_than_the_size_is_not_cut(self) -> None:
        self.assertGreater(len(hostile.message_bytes({"type": "ping"}, 3)), 3)


class TrickleTest(unittest.TestCase):
    def test_every_byte_is_a_write_of_its_own(self) -> None:
        fake = FakeSocket()
        hostile.write_one_byte_at_a_time(fake, b"\x81\x84abc", pause=0.0)  # type: ignore[arg-type]
        self.assertEqual(fake.writes, [bytes([byte]) for byte in b"\x81\x84abc"])


class ScenarioListTest(unittest.TestCase):
    def test_the_names_are_unique(self) -> None:
        names = [scenario.name for scenario in hostile.SCENARIOS]
        self.assertEqual(len(names), len(set(names)))

    def test_every_scenario_can_run(self) -> None:
        for scenario in hostile.SCENARIOS:
            self.assertTrue(callable(scenario.run), scenario.name)
            self.assertTrue(all(flag for flag in scenario.flags), scenario.name)


if __name__ == "__main__":
    unittest.main()
