#include <catch2/catch.hpp>

#include "libslic3r/PNGReadWrite.hpp"
#include "libslic3r/GCode/Thumbnails.hpp"

#include <boost/filesystem.hpp>
#include <boost/nowide/fstream.hpp>

#include <cstdint>
#include <vector>

using namespace Slic3r;

namespace {

// Independent 1x2 fixtures encoded with Python zlib, without using Orca's writer.
const std::vector<uint8_t> rgba_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x99, 0x81, 0xb6, 0x27, 0x00, 0x00, 0x00,
    0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0x00, 0x46, 0x0d, 0x00, 0x12, 0x78, 0x03,
    0x7e, 0x22, 0x51, 0x00, 0xa8, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

const std::vector<uint8_t> rgb_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x02, 0x00, 0x00, 0x00, 0x16, 0xe3, 0x21, 0x70, 0x00, 0x00, 0x00,
    0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0x00, 0x04, 0xff, 0x19, 0x00, 0x08, 0xff, 0x01,
    0xff, 0x40, 0x21, 0x2c, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

const std::vector<uint8_t> gray_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x08, 0x00, 0x00, 0x00, 0x00, 0xbc, 0xea, 0xe9, 0xfb, 0x00, 0x00, 0x00,
    0x0c, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x50, 0x60, 0x38, 0x00, 0x00, 0x01, 0x24, 0x00, 0xe1, 0xe8,
    0xe7, 0x9c, 0xaa, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

const std::vector<uint8_t> rgba16_png = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x10, 0x06, 0x00, 0x00, 0x00, 0xc9, 0x11, 0x6a, 0x64, 0x00, 0x00, 0x00,
    0x0f, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0xf8, 0x0f, 0x85, 0x0d, 0x50, 0x08, 0x00, 0x3d, 0xde,
    0x05, 0xfd, 0x22, 0x03, 0x1f, 0xb5, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t pos)
{
    return (uint32_t(bytes[pos]) << 24) | (uint32_t(bytes[pos + 1]) << 16) | (uint32_t(bytes[pos + 2]) << 8) | uint32_t(bytes[pos + 3]);
}

void write_u32(std::vector<uint8_t>& bytes, size_t pos, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes[pos + i] = static_cast<uint8_t>(value >> (24 - 8 * i));
}

uint32_t chunk_crc(const std::vector<uint8_t>& bytes, size_t pos, size_t size)
{
    uint32_t crc = 0xffffffffu;
    for (size_t i = pos; i < pos + size; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0u);
    }
    return crc ^ 0xffffffffu;
}

std::vector<uint8_t> corrupt_adler(std::vector<uint8_t> bytes)
{
    // IHDR is followed by one IDAT. Change the inner zlib checksum and repair
    // the outer PNG CRC, reproducing the incident rather than a CRC failure.
    const size_t idat   = 33;
    const size_t length = read_u32(bytes, idat);
    bytes[idat + 8 + length - 1] ^= 1;
    write_u32(bytes, idat + 8 + length, chunk_crc(bytes, idat + 4, length + 4));
    return bytes;
}

bool decode_color(const std::vector<uint8_t>& bytes, png::ImageColorscale& image)
{
    return png::decode_colored_png({bytes.data(), bytes.size()}, image);
}

bool decode_gray(const std::vector<uint8_t>& bytes, png::ImageGreyscale& image)
{
    return png::decode_png(png::ReadBuf{bytes.data(), bytes.size()}, image);
}

struct TemporaryThumbnailPath
{
    boost::filesystem::path path = boost::filesystem::temp_directory_path() /
                                   boost::filesystem::unique_path("orca-thumbnail-%%%%-%%%%.png");

    ~TemporaryThumbnailPath()
    {
        boost::system::error_code error;
        boost::filesystem::remove(path, error);
    }

    void write(const std::vector<uint8_t>& bytes) const
    {
        boost::nowide::ofstream output(path.string(), std::ios::binary);
        REQUIRE(output.good());
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        output.close();
        REQUIRE(output.good());
    }
};

} // namespace

TEST_CASE("PNG thumbnails retain row order and alpha", "[PNG][Thumbnails]")
{
    png::ImageColorscale image;
    REQUIRE(decode_color(rgba_png, image));
    REQUIRE(image.cols == 1);
    REQUIRE(image.rows == 2);
    REQUIRE(image.bytes_per_pixel == 4);
    // Colored decoder returns bottom row first, as required by ThumbnailData.
    REQUIRE(image.buf == std::vector<uint8_t>{0, 255, 0, 128, 255, 0, 0, 255});

    REQUIRE(decode_color(rgb_png, image));
    REQUIRE(image.bytes_per_pixel == 3);
    REQUIRE(image.buf == std::vector<uint8_t>{0, 255, 0, 255, 0, 0});
}

TEST_CASE("PNG checksum errors return false instead of aborting", "[PNG][Thumbnails]")
{
    png::ImageColorscale image;
    REQUIRE(decode_color(rgba_png, image));
    const auto damaged = corrupt_adler(rgba_png);
    REQUIRE_FALSE(decode_color(damaged, image));
    REQUIRE(image.buf.empty());
    REQUIRE(image.cols == 0);
    REQUIRE(image.rows == 0);
    REQUIRE(image.bytes_per_pixel == 0);
    // Error recovery must not poison subsequent decode operations.
    REQUIRE(decode_color(rgba_png, image));
}

TEST_CASE("PNG short reads and invalid headers do not expose partial images", "[PNG][Thumbnails]")
{
    const size_t         end    = rgba_png.size();
    const auto           length = GENERATE(size_t(0), size_t(7), size_t(15), size_t(32), size_t(42));
    std::vector<uint8_t> damaged(rgba_png.begin(), rgba_png.begin() + length);
    png::ImageColorscale image;
    REQUIRE_FALSE(decode_color(damaged, image));
    REQUIRE(image.buf.empty());
    REQUIRE(image.cols == 0);
    REQUIRE(image.rows == 0);

    damaged.assign(rgba_png.begin(), rgba_png.begin() + end - 1);
    REQUIRE_FALSE(decode_color(damaged, image));
    damaged = rgba_png;
    damaged[29] ^= 1; // IHDR CRC.
    REQUIRE_FALSE(decode_color(damaged, image));
    damaged    = rgba_png;
    damaged[0] = 0; // Signature.
    REQUIRE_FALSE(decode_color(damaged, image));
    REQUIRE(decode_color(rgba_png, image));
}

TEST_CASE("Unsupported thumbnail pixel formats leave no decoded image", "[PNG][Thumbnails]")
{
    png::ImageColorscale image;
    REQUIRE_FALSE(decode_color(gray_png, image));
    REQUIRE(image.buf.empty());
    REQUIRE(image.rows == 0);
    REQUIRE(image.cols == 0);
    REQUIRE_FALSE(decode_color(rgba16_png, image));
    REQUIRE(image.buf.empty());
    REQUIRE(image.bytes_per_pixel == 0);
}

TEST_CASE("Grayscale PNG shares safe error recovery and validates the stream end", "[PNG]")
{
    png::ImageGreyscale image;
    REQUIRE(decode_gray(gray_png, image));
    REQUIRE(image.cols == 1);
    REQUIRE(image.rows == 2);
    REQUIRE(image.buf == std::vector<uint8_t>{32, 192});
    REQUIRE_FALSE(decode_gray(corrupt_adler(gray_png), image));
    REQUIRE(image.buf.empty());
    REQUIRE(image.rows == 0);
    REQUIRE(image.cols == 0);
    const std::vector<uint8_t> truncated(gray_png.begin(), gray_png.end() - 1);
    REQUIRE_FALSE(decode_gray(truncated, image));
    REQUIRE(decode_gray(gray_png, image));
}

TEST_CASE("PNG thumbnail files normalize RGB and preserve RGBA for all exporters", "[PNG][Thumbnails]")
{
    TemporaryThumbnailPath file;
    ThumbnailData          thumbnail;

    file.write(rgb_png);
    REQUIRE(GCodeThumbnails::load_png_thumbnail(file.path.string(), thumbnail));
    REQUIRE(thumbnail.is_valid());
    REQUIRE(thumbnail.width == 1);
    REQUIRE(thumbnail.height == 2);
    REQUIRE(thumbnail.pixels == std::vector<uint8_t>{0, 255, 0, 255, 255, 0, 0, 255});
    REQUIRE(GCodeThumbnails::resize_thumbnail_fit(thumbnail, 512, 512).is_valid());

    file.write(rgba_png);
    REQUIRE(GCodeThumbnails::load_png_thumbnail(file.path.string(), thumbnail));
    REQUIRE(thumbnail.is_valid());
    REQUIRE(thumbnail.pixels == std::vector<uint8_t>{0, 255, 0, 128, 255, 0, 0, 255});
}

TEST_CASE("PNG thumbnail file errors clear stale images and allow recovery", "[PNG][Thumbnails]")
{
    TemporaryThumbnailPath file;
    ThumbnailData          thumbnail;
    thumbnail.set(1, 1);

    SECTION("missing file") {}
    SECTION("directory instead of a file") { REQUIRE(boost::filesystem::create_directory(file.path)); }
    SECTION("invalid checksum") { file.write(corrupt_adler(rgba_png)); }
    SECTION("truncated stream") { file.write(std::vector<uint8_t>(rgba_png.begin(), rgba_png.end() - 1)); }

    REQUIRE_FALSE(GCodeThumbnails::load_png_thumbnail(file.path.string(), thumbnail));
    REQUIRE(thumbnail.width == 0);
    REQUIRE(thumbnail.height == 0);
    REQUIRE(thumbnail.pixels.empty());

    TemporaryThumbnailPath valid;
    valid.write(rgba_png);
    REQUIRE(GCodeThumbnails::load_png_thumbnail(valid.path.string(), thumbnail));
    REQUIRE(thumbnail.is_valid());
}
