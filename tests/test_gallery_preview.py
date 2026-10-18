from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GalleryPreviewTests(unittest.TestCase):
    def test_firmware_gallery_and_preview(self):
        headers = {
            "Arduino.h": r"""
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
using String = std::string;
using SemaphoreHandle_t = void *;
inline uint32_t testMillis = 0;
inline uint32_t millis() { return testMillis; }
inline void yield() {}
inline void *ps_malloc(size_t length) { return malloc(length); }
inline void *heap_caps_malloc(size_t length, int) { return malloc(length); }
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define PROGMEM
#define pgm_read_word(address) (*(address))
""",
            "esp_heap_caps.h": '#include "Arduino.h"\n',
            "esp_camera.h": "#pragma once\nstruct camera_fb_t {};\n",
            "Adafruit_GFX.h": r"""
#pragma once
#include "Arduino.h"
#include "gfxfont.h"
class Adafruit_GFX {
 public:
  void drawRGBBitmap(int16_t, int16_t, uint16_t *, int16_t, int16_t) {}
  void setFont(const void *) {}
  void setTextColor(uint16_t) {}
  void setTextSize(int) {}
  void setTextWrap(bool) {}
  void setCursor(int16_t, int16_t) {}
  void print(const char *) {}
};
""",
            "Adafruit_SPITFT.h": r"""
#pragma once
#include "Adafruit_GFX.h"
class Adafruit_SPITFT : public Adafruit_GFX {};
""",
            "Display.h": "#pragma once\n",
            "FS.h": r"""
#pragma once
#include "Arduino.h"
#include <algorithm>
#include <map>
#include <vector>
struct TestFile {
  std::vector<uint8_t> bytes;
  time_t modified = 0;
};
inline std::map<std::string, TestFile> testFiles;
inline size_t testWriteLimit = 400000;
inline time_t testModified = 1000;
class File {
 public:
  File() = default;
  File(std::string path, bool directory = false)
      : path_(path), directory_(directory), valid_(true) {}
  explicit operator bool() const { return valid_; }
  bool isDirectory() const { return directory_; }
  const char *name() const { return path_.c_str(); }
  File openNextFile() {
    auto iterator = testFiles.begin();
    for (size_t i = 0; i < next_ && iterator != testFiles.end(); ++i) {
      ++iterator;
    }
    ++next_;
    return iterator == testFiles.end() ? File() : File(iterator->first);
  }
  size_t size() const { return testFiles.at(path_).bytes.size(); }
  time_t getLastWrite() const { return testFiles.at(path_).modified; }
  size_t read(uint8_t *data, size_t length) {
    const auto &bytes = testFiles.at(path_).bytes;
    length = std::min(length, bytes.size());
    memcpy(data, bytes.data(), length);
    return length;
  }
  size_t write(const uint8_t *data, size_t length) {
    length = std::min(length, testWriteLimit);
    testFiles[path_].bytes.assign(data, data + length);
    testFiles[path_].modified = testModified++;
    return length;
  }
  void flush() {}
  void close() {}
 private:
  std::string path_;
  bool directory_ = false;
  bool valid_ = false;
  size_t next_ = 0;
};
#define FILE_READ "r"
#define FILE_WRITE "w"
""",
            "SD_MMC.h": r"""
#pragma once
#include "FS.h"
inline bool testSdMount = true;
struct TestSd {
  bool setPins(int, int, int) { return true; }
  bool begin(const char *, bool) { return testSdMount; }
  bool mkdir(const char *) { return true; }
  bool exists(const char *path) { return testFiles.count(path) > 0; }
  bool remove(const char *path) { return testFiles.erase(path) > 0; }
  File open(const char *path, const char *mode = FILE_READ) {
    if (strcmp(path, "/somnium") == 0) {
      return File(path, true);
    }
    if (strcmp(mode, FILE_WRITE) == 0) {
      testFiles[path] = TestFile{};
    }
    return exists(path) ? File(path) : File();
  }
};
inline TestSd SD_MMC;
""",
        }
        harness = r"""
#include <cassert>
#include <cstdarg>
#include <tuple>
#include <vector>
#include "SD_MMC.h"
#include "Image.h"
#include "Console.h"
#include "Ui.h"
#include "Chrono.h"
#include "KitchenAnim.h"
#include "App.cpp"

static bool testDecodeResult = true;
static int testDecodeCount = 0;
static int testDirectDecodeCount = 0;
static uint32_t testDecodeDuration = 0;
static std::vector<std::string> testText;
using TestTitle = std::tuple<std::string, int16_t, int16_t, int16_t, int16_t,
                             uint16_t>;
static std::vector<TestTitle> testTitles;
static std::string testLog;
static bool containsText(const char *text) {
  return std::find(testText.begin(), testText.end(), text) != testText.end();
}

namespace Somnium {
void Console::println(const char *message) { testLog += message; }
void Console::printf(const char *format, ...) {
  char buffer[256];
  va_list arguments;
  va_start(arguments, format);
  vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  testLog += buffer;
}
bool Image::looksLikeJpeg(const uint8_t *data, size_t length) {
  return length >= 4 && data[0] == 0xff && data[1] == 0xd8 &&
         data[length - 2] == 0xff && data[length - 1] == 0xd9;
}
bool Image::decodeRgb565(const uint8_t *, size_t, uint16_t *, int16_t, int16_t) {
  ++testDecodeCount;
  testMillis += testDecodeDuration;
  return testDecodeResult;
}
bool Image::drawJpeg(const uint8_t *, size_t, Adafruit_GFX *) {
  ++testDirectDecodeCount;
  return false;
}
namespace Ui {
Theme &Ui::theme() { static Theme theme; return theme; }
void Canvas::bind(Adafruit_SPITFT *, int16_t width, int16_t height) {
  width_ = width;
  height_ = height;
}
void Canvas::clear(Color) { testText.clear(); testTitles.clear(); }
void Canvas::fillRect(Rect, Color) {}
void Canvas::drawRect(Rect, Color) {}
void Canvas::hLine(int16_t, int16_t, int16_t, Color) {}
void Canvas::blitRgb565(int16_t, int16_t, int16_t, int16_t, const uint16_t *) {
  testText.push_back("image pixels");
}
Size Text::measure(const char *, uint8_t) { return Size(0, 0); }
void Text::draw(Canvas &, const char *, int16_t, int16_t, const TextStyle &) {}
void Text::drawIn(Canvas &, const char *text, Rect, const TextStyle &) {
  testText.push_back(text);
}
void Text::drawFontIn(Canvas &, const char *text, Rect box, const GFXfont &,
                       Color color) {
  testTitles.emplace_back(text, box.x, box.y, box.w, box.h, color.value);
}
}
namespace App {
void Chrono::formatDateTime(char *buffer, size_t length) {
  if (length > 0) { buffer[0] = '\0'; }
}
void KitchenAnim::drawFire(Ui::Canvas &, int16_t, int16_t, uint32_t, Ui::Color) {}
void KitchenAnim::drawSalt(Ui::Canvas &, int16_t, int16_t, uint32_t, Ui::Color) {}
void KitchenAnim::drawWhisk(Ui::Canvas &, int16_t, int16_t, uint32_t, Ui::Color) {}
void KitchenAnim::drawProgressPill(Ui::Canvas &, Ui::Rect, float, Ui::Color,
                                   Ui::Color) {}
}
}

int main() {
  using namespace Somnium;
  using namespace Somnium::App;
  const uint8_t jpeg[] = {0xff, 0xd8, 1, 2, 0xff, 0xd9};
  Ui::Canvas canvas;
  canvas.bind(nullptr, 320, 240);
  ModePage mode;
  mode.onEnter();
  testMillis = 1000;
  mode.draw(canvas);
  const auto initialTitles = testTitles;
  assert(!initialTitles.empty());
  assert(std::find(testText.begin(), testText.end(), "image pixels") != testText.end());
  assert(modeAt(0).preview != nullptr);
  assert(modeAt(1).preview != nullptr);
  assert(modeAt(2).preview != nullptr);
  assert(modeAt(3).preview == nullptr);
  assert(modeAt(4).preview != nullptr);
  mode.nudge(1);
  mode.draw(canvas);
  assert(testTitles == initialTitles);
  assert(mode.selectedIndex() == 1);
  testMillis += 65;
  mode.update(65);
  mode.draw(canvas);
  for (size_t i = 1; i < testTitles.size(); ++i) {
    assert(std::get<3>(testTitles[i - 1]) <= std::get<3>(testTitles[i]));
  }
  assert(std::get<0>(testTitles.back()) == modeAt(0).label);
  testMillis += 195;
  mode.update(195);
  mode.draw(canvas);
  const auto settledTitles = testTitles;
  assert(std::get<0>(settledTitles.back()) == modeAt(1).label);

  ModePage queuedMode;
  queuedMode.onEnter();
  queuedMode.nudge(2);
  queuedMode.draw(canvas);
  assert(testTitles == initialTitles);
  testMillis += 260;
  queuedMode.update(260);
  queuedMode.draw(canvas);
  assert(testTitles == settledTitles);
  testMillis += 260;
  queuedMode.update(260);
  assert(queuedMode.selectedIndex() == 2);
  queuedMode.nudge(-2);
  testMillis += 260;
  queuedMode.update(260);
  queuedMode.draw(canvas);
  assert(testTitles == settledTitles);
  testMillis += 260;
  queuedMode.update(260);
  queuedMode.draw(canvas);
  assert(queuedMode.selectedIndex() == 0);
  assert(testTitles == initialTitles);
  queuedMode.nudge(-1);
  assert(queuedMode.selectedIndex() == kModeCount - 1);

  PreviewPage review;
  review.beginReview("POP COMIC", jpeg, sizeof(jpeg));
  review.onEnter();
  testMillis = 10000;
  review.update(10000);
  assert(!review.autoDone());
  review.draw(canvas);
  assert(containsText("plating..."));
  testDecodeDuration = 5000;
  review.update(1);
  assert(!review.autoDone());
  review.draw(canvas);
  assert(containsText("image pixels"));
  testMillis += 2999;
  review.update(90000);
  assert(!review.autoDone());
  ++testMillis;
  review.update(1);
  assert(review.autoDone());
  review.onExit();

  PreviewPage storagePreview;
  testDecodeResult = false;
  testDecodeCount = 0;
  storagePreview.beginStorage(0, 1);
  storagePreview.setPhoto(jpeg, sizeof(jpeg));
  storagePreview.onEnter();
  storagePreview.update(10000);
  storagePreview.draw(canvas);
  storagePreview.update(1);
  storagePreview.draw(canvas);
  assert(containsText("image unavailable"));
  for (int i = 0; i < 5; ++i) {
    storagePreview.update(1000);
    storagePreview.draw(canvas);
  }
  assert(testDecodeCount == 1);
  assert(testDirectDecodeCount == 0);

  ProcessPage process;
  process.onEnter();
  process.setStatus("dream crc mismatch");
  process.setFailed();
  process.draw(canvas);
  assert(containsText("Kitchen trouble"));

  testFiles["/somnium/popcomic_1_0_orig.jpg"] = {{jpeg, jpeg + sizeof(jpeg)}, 1};
  testFiles["/somnium/popcomic_2_1_dream.jpg"] = {{jpeg, jpeg + sizeof(jpeg)}, 2};
  assert(Storage::begin());
  assert(Storage::nextShotNumber("popcomic", 1) == 3);
  savePair("popcomic", 3, jpeg, sizeof(jpeg), jpeg, sizeof(jpeg));
  assert(browseCount() == 4);
  assert(gGalCount == 0);
  assert(testFiles.at("/somnium/popcomic_1_0_orig.jpg").modified == 1);
  assert(testFiles.at("/somnium/popcomic_2_1_dream.jpg").modified == 2);
  assert(loadStorageIndex(0));
  assert(gStorageLen == sizeof(jpeg));
  freeStorageJpeg();

  testWriteLimit = 2;
  savePair("popcomic", 4, jpeg, sizeof(jpeg), jpeg, sizeof(jpeg));
  assert(browseCount() == 6);
  assert(gGalCount == 2);
  assert(!testFiles.count("/somnium/popcomic_4_0_orig.jpg"));
  assert(!testFiles.count("/somnium/popcomic_4_1_dream.jpg"));
  assert(loadStorageIndex(4));
  assert(gStorageJpeg == gGal[0].data);
  assert(memcmp(gStorageJpeg, jpeg, sizeof(jpeg)) == 0);
  assert(testLog.find("ERR SD save") != std::string::npos);
  assert(testLog.find("GAL 6 plates (sd=4 ram=2)") != std::string::npos);
  freeStorageJpeg();

  testSdMount = false;
  assert(!Storage::begin());
  savePair("anime", 1, jpeg, sizeof(jpeg), jpeg, sizeof(jpeg));
  assert(browseCount() == 4);
  assert(loadStorageIndex(3));
  assert(gStorageJpeg == gGal[3].data);
  freeStorageJpeg();
  testSdMount = true;

  for (int i = 0; i < 80; ++i) {
    char path[64];
    snprintf(path, sizeof(path), "/somnium/anime_%d_1_dream.jpg", i);
    testFiles[path] = {{jpeg, jpeg + sizeof(jpeg)}, 2000 + i};
  }
  assert(Storage::begin());
  assert(Storage::photoCount() == 64);
  char path[64];
  assert(Storage::photoPathAt(0, path, sizeof(path)));
  assert(strcmp(path, "/somnium/anime_16_1_dream.jpg") == 0);
  assert(Storage::photoPathAt(63, path, sizeof(path)));
  assert(strcmp(path, "/somnium/anime_79_1_dream.jpg") == 0);
  return 0;
}
"""
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for name, contents in headers.items():
                destination = directory / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(contents)
            (directory / "harness.cpp").write_text(harness)
            executable = directory / "gallery_preview_test"
            compilation = subprocess.run(
                [
                    "g++",
                    "-std=c++17",
                    "-ffunction-sections",
                    "-fdata-sections",
                    "-Wl,--gc-sections",
                    f"-I{directory}",
                    f"-I{ROOT / 'lib/Somnium'}",
                    f"-I{ROOT / 'lib/SomniumUi'}",
                    f"-I{ROOT / 'lib/SomniumApp'}",
                    f"-I{ROOT / '.pio/libdeps/somnium/Adafruit GFX Library'}",
                    str(directory / "harness.cpp"),
                    str(ROOT / "lib/Somnium/Storage.cpp"),
                    str(ROOT / "lib/SomniumApp/Pages.cpp"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compilation.returncode, 0, compilation.stderr)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
