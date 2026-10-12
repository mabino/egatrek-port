"""Turbo Pascal string constants: a length byte followed by that many characters."""

from __future__ import annotations

PRINTABLE = set(range(0x20, 0x7F))


def find(data: bytes, min_len: int = 4) -> list[tuple[int, str]]:
    """Return (offset of length byte, text) for plausible Pascal strings.

    Turbo Pascal stores string literals inline in the code segment just before
    the code that uses them, usually back to back. A candidate is accepted when
    its length byte is followed by exactly that many printable characters.
    Accepted strings are skipped over, so the next length byte is found where
    the previous string ends.
    """
    out: list[tuple[int, str]] = []
    i = 0
    while i < len(data):
        n = data[i]
        body = data[i + 1:i + 1 + n]
        if n >= min_len and len(body) == n and all(c in PRINTABLE for c in body):
            out.append((i, body.decode("ascii")))
            i += 1 + n
        else:
            i += 1
    return out
