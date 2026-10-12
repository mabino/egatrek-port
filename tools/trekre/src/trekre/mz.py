"""MS-DOS MZ executables: header, relocations and the load image.

Turbo Pascal links each unit into its own code segment, and every far call or
segment reference to another unit goes through a relocation. The set of
relocated segment values is therefore an exact list of segment bases.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field


class MZError(ValueError):
    pass


@dataclass(frozen=True)
class Header:
    last_page_bytes: int
    pages: int
    relocations: int
    header_paragraphs: int
    min_alloc: int
    max_alloc: int
    ss: int
    sp: int
    checksum: int
    ip: int
    cs: int
    reloc_offset: int
    overlay: int

    @property
    def file_size(self) -> int:
        return (self.pages - 1) * 512 + self.last_page_bytes if self.last_page_bytes else self.pages * 512

    @property
    def header_size(self) -> int:
        return self.header_paragraphs * 16


@dataclass
class Executable:
    header: Header
    image: bytes
    # (segment, offset) of each fixup, relative to the start of the load image.
    relocations: list[tuple[int, int]] = field(default_factory=list)

    @classmethod
    def parse(cls, data: bytes) -> "Executable":
        if len(data) < 28 or data[:2] not in (b"MZ", b"ZM"):
            raise MZError("not an MZ executable")
        header = Header(*struct.unpack_from("<13H", data, 2))
        if header.file_size > len(data) or header.header_size > header.file_size:
            raise MZError("truncated MZ executable")
        end = header.reloc_offset + 4 * header.relocations
        if end > header.header_size:
            raise MZError("relocation table overruns header")
        relocs = [struct.unpack_from("<HH", data, header.reloc_offset + 4 * i)[::-1]
                  for i in range(header.relocations)]
        return cls(header, data[header.header_size:header.file_size], relocs)

    def word(self, linear: int) -> int:
        return struct.unpack_from("<H", self.image, linear)[0]

    def segment_bases(self) -> list[int]:
        """Paragraph numbers referenced by relocations, plus CS and SS, sorted."""
        bases = {self.header.cs, self.header.ss}
        for seg, off in self.relocations:
            linear = seg * 16 + off
            if linear + 2 <= len(self.image):
                bases.add(self.word(linear))
        return sorted(b for b in bases if b * 16 < len(self.image))

    def segments(self) -> list[tuple[int, int]]:
        """(base paragraph, size in bytes) for each segment, in load order."""
        bases = self.segment_bases()
        ends = bases[1:] + [len(self.image) // 16 + (1 if len(self.image) % 16 else 0)]
        return [(b, min(e * 16, len(self.image)) - b * 16) for b, e in zip(bases, ends)]

    def segment_bytes(self, base: int) -> bytes:
        for b, size in self.segments():
            if b == base:
                return self.image[b * 16:b * 16 + size]
        raise KeyError(f"no segment at {base:04x}")
