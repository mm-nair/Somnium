import ctypes
import io
import pathlib
import re
import subprocess
import tempfile
import unittest

from PIL import Image


PROJECT = pathlib.Path(__file__).resolve().parents[1]
DECODER = PROJECT / ".pio/libdeps/somnium/TJpg_Decoder/src"
FIRMWARE_MAP = PROJECT / ".pio/build/somnium/firmware.map"
HARNESS = r"""
#include <stdint.h>
#include <string.h>
#include "tjpgd.h"

typedef struct {
  const uint8_t *data;
  size_t length;
  size_t offset;
  uint16_t *pixels;
} JpegSource;

static size_t readJpeg(JDEC *decoder, uint8_t *buffer, size_t length) {
  JpegSource *source = decoder->device;
  if (length > source->length - source->offset) {
    length = source->length - source->offset;
  }
  if (buffer != NULL) {
    memcpy(buffer, source->data + source->offset, length);
  }
  source->offset += length;
  return length;
}

static int drawJpeg(JDEC *decoder, void *bitmap, JRECT *rectangle) {
  JpegSource *source = decoder->device;
  uint16_t *pixels = bitmap;
  size_t width = rectangle->right - rectangle->left + 1;
  for (size_t row = rectangle->top; row <= rectangle->bottom; ++row) {
    memcpy(source->pixels + row * decoder->width + rectangle->left,
           pixels, width * sizeof(uint16_t));
    pixels += width;
  }
  return 1;
}

int decodeJpeg(const uint8_t *data, size_t length, uint16_t *pixels,
               size_t capacity, uint16_t *width, uint16_t *height) {
  JDEC decoder = {0};
  JpegSource source = {data, length, 0, pixels};
  uint8_t workspace[TJPGD_WORKSPACE_SIZE];
  JRESULT result = jd_prepare(&decoder, readJpeg, workspace,
                              sizeof(workspace), &source);
  if (result != JDR_OK) {
    return result;
  }
  *width = decoder.width;
  *height = decoder.height;
  if ((size_t)decoder.width * decoder.height > capacity) {
    return JDR_PAR;
  }
  decoder.swap = 0;
  return jd_decomp(&decoder, drawJpeg, 0);
}
"""


class ImageDecodeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="somnium-jpeg-test-")
        cls.addClassCleanup(cls.directory.cleanup)
        directory = pathlib.Path(cls.directory.name)
        source = directory / "decode.c"
        library = directory / "decode.so"
        source.write_text(HARNESS)
        subprocess.run(
            [
                "cc",
                "-shared",
                "-fPIC",
                "-O2",
                "-I",
                str(DECODER),
                str(source),
                str(DECODER / "tjpgd.c"),
                "-o",
                str(library),
            ],
            check=True,
            capture_output=True,
        )
        cls.library = ctypes.CDLL(str(library))
        cls.library.decodeJpeg.argtypes = [
            ctypes.c_char_p,
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint16),
            ctypes.c_size_t,
            ctypes.POINTER(ctypes.c_uint16),
            ctypes.POINTER(ctypes.c_uint16),
        ]
        cls.library.decodeJpeg.restype = ctypes.c_int

    def decode(self, data, size):
        pixels = (ctypes.c_uint16 * (size[0] * size[1]))()
        width = ctypes.c_uint16()
        height = ctypes.c_uint16()
        result = self.library.decodeJpeg(
            data,
            len(data),
            pixels,
            len(pixels),
            ctypes.byref(width),
            ctypes.byref(height),
        )
        self.assertEqual(result, 0)
        self.assertEqual((width.value, height.value), size)
        return pixels

    def test_firmware_links_matching_decoder(self):
        firmware_map = FIRMWARE_MAP.read_text()
        for symbol in ("jd_prepare", "jd_decomp"):
            with self.subTest(symbol=symbol):
                definition = re.search(rf"^{symbol}\s+(\S+)$", firmware_map, re.M)
                self.assertIsNotNone(definition)
                self.assertIn("libTJpg_Decoder.a(tjpgd.c.o)", definition.group(1))

    def test_native_rgb565_primary_colors(self):
        for color in ((255, 0, 0), (0, 255, 0), (0, 0, 255)):
            with self.subTest(color=color):
                image = Image.new("RGB", (32, 16), color)
                output = io.BytesIO()
                image.save(output, format="JPEG", quality=100, subsampling=0)
                pixels = self.decode(output.getvalue(), image.size)
                for pixel in pixels:
                    actual = (
                        ((pixel >> 11) & 31) * 255 // 31,
                        ((pixel >> 5) & 63) * 255 // 63,
                        (pixel & 31) * 255 // 31,
                    )
                    for expected_channel, actual_channel in zip(color, actual):
                        self.assertLessEqual(abs(expected_channel - actual_channel), 9)

    def test_portrait_and_partial_blocks(self):
        for size in ((240, 320), (320, 240), (31, 19)):
            with self.subTest(size=size):
                image = Image.new("RGB", size, (255, 0, 0))
                output = io.BytesIO()
                image.save(output, format="JPEG", quality=95, subsampling=2)
                pixels = self.decode(output.getvalue(), size)
                self.assertTrue(all((pixel & 0xF800) == 0xF800 for pixel in pixels))

    def test_saved_jpegs_decode(self):
        for path in (PROJECT / "demos").rglob("*.jpg"):
            with self.subTest(path=path.relative_to(PROJECT)):
                with Image.open(path) as image:
                    image.load()
                    self.decode(path.read_bytes(), image.size)


if __name__ == "__main__":
    unittest.main()
