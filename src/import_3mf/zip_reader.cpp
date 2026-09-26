#include "zip_reader.hpp"
#include <zlib.h>
#include <fstream>
#include <cstring>

namespace dune3d {

namespace {
uint16_t read_u16(const uint8_t *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16)
           | (static_cast<uint32_t>(p[3]) << 24);
}

constexpr uint32_t EOCD_SIGNATURE = 0x06054b50;
constexpr uint32_t CENTRAL_DIR_SIGNATURE = 0x02014b50;
constexpr uint32_t LOCAL_FILE_SIGNATURE = 0x04034b50;
constexpr size_t EOCD_SIZE = 22;
constexpr size_t CENTRAL_DIR_HEADER_SIZE = 46;
constexpr size_t LOCAL_FILE_HEADER_SIZE = 30;

std::optional<std::vector<uint8_t>> inflate_raw(const uint8_t *data, size_t compressed_size,
                                                size_t uncompressed_size)
{
    std::vector<uint8_t> out(uncompressed_size);
    if (uncompressed_size == 0)
        return out;

    z_stream strm{};
    // Negative window bits: raw DEFLATE, no zlib/gzip header -- what ZIP
    // entries actually contain.
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK)
        return std::nullopt;

    strm.next_in = const_cast<Bytef *>(data);
    strm.avail_in = static_cast<uInt>(compressed_size);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out.size());

    const auto ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    if (ret != Z_STREAM_END)
        return std::nullopt;
    return out;
}
} // namespace

std::optional<std::vector<uint8_t>> read_zip_entry(const std::filesystem::path &zip_path,
                                                    const std::string &entry_name)
{
    std::ifstream f(zip_path, std::ios::binary);
    if (!f)
        return std::nullopt;
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (buf.size() < EOCD_SIZE)
        return std::nullopt;

    // The EOCD record can be followed by up to 65535 bytes of comment, so
    // it isn't necessarily the last 22 bytes -- search backwards for it.
    const size_t search_start = buf.size() > EOCD_SIZE + 65535 ? buf.size() - EOCD_SIZE - 65535 : 0;
    std::optional<size_t> eocd_pos;
    for (size_t i = buf.size() - EOCD_SIZE + 1; i-- > search_start;) {
        if (read_u32(&buf[i]) == EOCD_SIGNATURE) {
            eocd_pos = i;
            break;
        }
    }
    if (!eocd_pos)
        return std::nullopt;

    const auto &eocd = *eocd_pos;
    const uint16_t total_entries = read_u16(&buf[eocd + 10]);
    const uint32_t cd_size = read_u32(&buf[eocd + 12]);
    const uint32_t cd_offset = read_u32(&buf[eocd + 16]);
    if (static_cast<size_t>(cd_offset) + cd_size > buf.size())
        return std::nullopt;

    size_t pos = cd_offset;
    for (uint16_t i = 0; i < total_entries; i++) {
        if (pos + CENTRAL_DIR_HEADER_SIZE > buf.size())
            return std::nullopt;
        if (read_u32(&buf[pos]) != CENTRAL_DIR_SIGNATURE)
            return std::nullopt;

        const uint16_t method = read_u16(&buf[pos + 10]);
        const uint32_t compressed_size = read_u32(&buf[pos + 20]);
        const uint32_t uncompressed_size = read_u32(&buf[pos + 24]);
        const uint16_t name_len = read_u16(&buf[pos + 28]);
        const uint16_t extra_len = read_u16(&buf[pos + 30]);
        const uint16_t comment_len = read_u16(&buf[pos + 32]);
        const uint32_t local_header_offset = read_u32(&buf[pos + 42]);

        const size_t name_pos = pos + CENTRAL_DIR_HEADER_SIZE;
        if (name_pos + name_len > buf.size())
            return std::nullopt;
        const std::string name(reinterpret_cast<const char *>(&buf[name_pos]), name_len);

        if (name == entry_name) {
            if (static_cast<size_t>(local_header_offset) + LOCAL_FILE_HEADER_SIZE > buf.size())
                return std::nullopt;
            if (read_u32(&buf[local_header_offset]) != LOCAL_FILE_SIGNATURE)
                return std::nullopt;
            const uint16_t local_name_len = read_u16(&buf[local_header_offset + 26]);
            const uint16_t local_extra_len = read_u16(&buf[local_header_offset + 28]);
            const size_t data_offset =
                    local_header_offset + LOCAL_FILE_HEADER_SIZE + local_name_len + local_extra_len;
            if (data_offset + compressed_size > buf.size())
                return std::nullopt;

            if (method == 0) {
                if (compressed_size != uncompressed_size)
                    return std::nullopt;
                return std::vector<uint8_t>(buf.begin() + data_offset, buf.begin() + data_offset + compressed_size);
            }
            else if (method == 8) {
                return inflate_raw(&buf[data_offset], compressed_size, uncompressed_size);
            }
            else {
                return std::nullopt; // unsupported compression method
            }
        }

        pos = name_pos + name_len + extra_len + comment_len;
    }

    return std::nullopt;
}

} // namespace dune3d
