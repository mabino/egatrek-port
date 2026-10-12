#include "core/mz_image.h"

#include <fstream>
#include <iterator>

#include "core/byte_reader.h"

namespace trek {

MzImage MzImage::parse(std::span<const std::byte> file) {
    ByteReader r(file);
    const auto sig = r.fixed_string(2);
    if (sig != "MZ" && sig != "ZM") throw ReadError("not an MZ executable");
    const std::size_t last = r.u16(), pages = r.u16(), relocs = r.u16(), header = r.u16() * 16u;
    r.skip(8);  // min/max alloc, SS, SP
    r.skip(2);  // checksum
    MzImage out;
    out.ip_ = r.u16();
    out.cs_ = r.u16();
    out.relocations_ = relocs;
    const std::size_t size = last ? (pages - 1) * 512 + last : pages * 512;
    if (size > file.size() || header > size) throw ReadError("truncated MZ executable");
    auto img = file.subspan(header, size - header);
    out.image_.assign(img.begin(), img.end());
    return out;
}

MzImage MzImage::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ReadError("cannot open " + path.string());
    std::vector<char> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parse(std::as_bytes(std::span(raw)));
}

std::span<const std::byte> MzImage::at(std::uint16_t segment, std::uint16_t offset, std::size_t size) const {
    const std::size_t linear = segment * 16u + offset;
    if (linear > image_.size() || size > image_.size() - linear)
        throw ReadError("segment range outside the load image");
    return std::span(image_).subspan(linear, size);
}

}  // namespace trek
