from trekre import pstrings


def test_back_to_back_strings():
    data = b"\x90\x05Hello\x06World!\x02ok\xc3"
    assert pstrings.find(data) == [(1, "Hello"), (7, "World!")]
    assert pstrings.find(data, 2) == [(1, "Hello"), (7, "World!"), (14, "ok")]


def test_rejects_unprintable_and_truncated():
    assert pstrings.find(b"\x05Hel\x01o") == []
    assert pstrings.find(b"\x09short") == []
