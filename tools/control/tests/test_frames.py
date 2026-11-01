import os

import pytest

from bagi_control_client import (
    OPCODE_CLOSE,
    OPCODE_CONTINUATION,
    OPCODE_PING,
    OPCODE_PONG,
    OPCODE_TEXT,
    FrameReader,
    _apply_mask,
    accept_key,
    encode_frame,
)

RFC_MASK = bytes.fromhex("37fa213d")


def test_accept_key_of_rfc_6455():
    assert accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="


def test_encodes_the_examples_of_rfc_6455():
    assert encode_frame(OPCODE_TEXT, b"Hello") == bytes.fromhex("810548656c6c6f")
    assert encode_frame(OPCODE_TEXT, b"Hello", RFC_MASK) == bytes.fromhex("818537fa213d7f9f4d5158")
    assert encode_frame(OPCODE_PING, b"Hello") == bytes.fromhex("890548656c6c6f")
    assert encode_frame(OPCODE_PONG, b"Hello", RFC_MASK) == bytes.fromhex("8a8537fa213d7f9f4d5158")


def test_encodes_the_length_in_the_shortest_form():
    assert encode_frame(OPCODE_TEXT, b"a" * 125)[:2] == bytes.fromhex("817d")
    assert encode_frame(OPCODE_TEXT, b"a" * 126)[:4] == bytes.fromhex("817e007e")
    assert encode_frame(OPCODE_TEXT, b"a" * 65535)[:4] == bytes.fromhex("817effff")
    assert encode_frame(OPCODE_TEXT, b"a" * 65536)[:10] == bytes.fromhex("817f0000000000010000")


def test_masked_frame_of_rfc_6455_is_read():
    reader = FrameReader()
    reader.feed(bytes.fromhex("818537fa213d7f9f4d5158"))
    assert reader.pop() == (True, OPCODE_TEXT, b"Hello")
    assert reader.pop() is None


def test_fragments_of_rfc_6455_are_read_one_by_one():
    reader = FrameReader()
    reader.feed(bytes.fromhex("010348656c") + bytes.fromhex("80026c6f"))
    assert reader.pop() == (False, OPCODE_TEXT, b"Hel")
    assert reader.pop() == (True, OPCODE_CONTINUATION, b"lo")
    assert reader.pop() is None


@pytest.mark.parametrize("length", [0, 1, 125, 126, 127, 65535, 65536, 100000])
def test_frames_of_every_length_form_round_trip(length):
    payload = bytes(i % 251 for i in range(length))
    for mask in (None, os.urandom(4)):
        reader = FrameReader()
        reader.feed(encode_frame(OPCODE_TEXT, payload, mask))
        assert reader.pop() == (True, OPCODE_TEXT, payload)


def test_bytes_may_arrive_one_at_a_time():
    frame = encode_frame(OPCODE_TEXT, b'{"id":1}', RFC_MASK)
    reader = FrameReader()
    for index, byte in enumerate(frame):
        assert reader.pop() is None
        reader.feed(bytes([byte]))
    assert reader.pop() == (True, OPCODE_TEXT, b'{"id":1}')


def test_several_frames_in_one_piece_come_out_in_order():
    reader = FrameReader()
    reader.feed(encode_frame(OPCODE_TEXT, b"one") + encode_frame(OPCODE_TEXT, b"two") + encode_frame(OPCODE_CLOSE, b"\x03\xe8"))
    assert reader.pop() == (True, OPCODE_TEXT, b"one")
    assert reader.pop() == (True, OPCODE_TEXT, b"two")
    assert reader.pop() == (True, OPCODE_CLOSE, b"\x03\xe8")
    assert reader.pop() is None


@pytest.mark.parametrize("length", range(0, 21))
def test_fast_mask_equals_the_byte_loop(length):
    payload = os.urandom(length)
    mask = os.urandom(4)
    assert _apply_mask(payload, mask) == bytes(byte ^ mask[index % 4] for index, byte in enumerate(payload))
