#include <catch2/catch.hpp>

#include "libslic3r/GCode/Thumbnails.hpp"

using namespace Slic3r;

SCENARIO("Thumbnail aspect-fit resize keeps the whole source image visible.", "[Thumbnails]") {
    GIVEN("a non-square thumbnail with visible right and bottom edges") {
        ThumbnailData source;
        source.set(376, 460);

        for (unsigned int y = 0; y < source.height; ++y) {
            for (unsigned int x = 0; x < source.width; ++x) {
                const size_t offset = 4 * (y * source.width + x);
                if (x >= 330) {
                    source.pixels[offset + 0] = 255;
                    source.pixels[offset + 1] = 0;
                    source.pixels[offset + 2] = 0;
                    source.pixels[offset + 3] = 255;
                } else if (y >= 410) {
                    source.pixels[offset + 0] = 0;
                    source.pixels[offset + 1] = 255;
                    source.pixels[offset + 2] = 0;
                    source.pixels[offset + 3] = 255;
                } else {
                    source.pixels[offset + 0] = 235;
                    source.pixels[offset + 1] = 235;
                    source.pixels[offset + 2] = 235;
                    source.pixels[offset + 3] = 255;
                }
            }
        }

        WHEN("it is resized into a square small thumbnail without cropping") {
            ThumbnailData small = GCodeThumbnails::resize_thumbnail_fit(source, 128, 128);

            THEN("the result has the requested size and still contains the right and bottom edges") {
                REQUIRE(small.width == 128);
                REQUIRE(small.height == 128);
                REQUIRE(small.pixels.size() == 128 * 128 * 4);

                unsigned int red_pixels = 0;
                unsigned int green_pixels = 0;
                for (size_t i = 0; i < small.pixels.size(); i += 4) {
                    const unsigned char r = small.pixels[i + 0];
                    const unsigned char g = small.pixels[i + 1];
                    const unsigned char b = small.pixels[i + 2];
                    const unsigned char a = small.pixels[i + 3];
                    if (r > 180 && g < 80 && b < 80 && a > 180)
                        ++red_pixels;
                    if (g > 180 && r < 80 && b < 80 && a > 180)
                        ++green_pixels;
                }

                REQUIRE(red_pixels > 0);
                REQUIRE(green_pixels > 0);
            }
        }
    }
}
