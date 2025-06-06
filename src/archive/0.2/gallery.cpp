#include "gallery.h"
#include <FS.h>
using namespace fs;
#include <WiFiManager.h>
#include <SD_MMC.h>
#include "imagedisplay.h"
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
#include <algorithm>
#include <random>
#include "CST816S.h"

namespace Gallery {

static TFT_eSPI* _tft = nullptr;

static Mode _mode = Mode::None;

static std::vector<String> _imageFiles; // List of image filenames in current folder
static int _currentIndex = 0;

static int _scrollOffset = 0; // For scrolling menu if too many items
static int _maxVisibleItems = 5;

static bool _imageDisplayed = false;

static int currentIndex = 0;
static int totalImages = 0;
static Mode randomMode = Mode::MODE_JPG;

// Helper to get file count for a mode (stub: real code should scan SD for .jpg/.gif)
int countFiles(Mode mode) {
    // Replace with your own SD scan if needed
    if (mode == Mode::MODE_JPG) return 10;  // example
    if (mode == Mode::MODE_GIF) return 5;   // example
    return 0;
}

// Scan directory for images with given extension
static void scanImages(const char* folder, const char* extension) {
    _imageFiles.clear();
    File root = SD_MMC.open(folder);
    if (!root) return;
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(extension)) {
                _imageFiles.push_back(name);
            }
        }
        file = root.openNextFile();
    }
    std::sort(_imageFiles.begin(), _imageFiles.end());
}

// Pick a random image index
static int randomIndex() {
    if (_imageFiles.empty()) return -1;
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, _imageFiles.size() - 1);
    return dis(gen);
}

// Draw gallery menu items with scrolling support
static void drawMenu() {
    if (!_tft) return;

    _tft->fillScreen(_tft->color565(16, 124, 16)); // Xbox green background
    _tft->setTextFont(4);
    _tft->setTextColor(TFT_WHITE);
    _tft->setTextDatum(MC_DATUM);
    _tft->drawString("Gallery", _tft->width() / 2, 30);

    // Menu options
    const char* items[] = {"JPG", "GIF", "Random JPG", "Random GIF", "Back"};
    const int itemCount = 5;

    int startY = 70 - _scrollOffset;
    int itemHeight = 40;

    for (int i = 0; i < itemCount; ++i) {
        int y = startY + i * itemHeight;
        if (y < 40 || y > _tft->height() - 30) continue; // Draw only visible

        _tft->setTextFont(2);
        _tft->setTextColor(TFT_WHITE);
        _tft->drawString(items[i], _tft->width() / 2, y);
    }
}

// Draw current image (JPG or GIF)
static void drawCurrentImage() {
    if (!_tft) return;
    if (_imageFiles.empty()) return;

    String fullPath;

    switch (_mode) {
        case Mode::BrowseJPG:
        case Mode::RandomJPG:
            fullPath = "/jpg" + _imageFiles[_currentIndex];
            break;
        case Mode::BrowseGIF:
        case Mode::RandomGIF:
            fullPath = "/gif" + _imageFiles[_currentIndex];
            break;
        default:
            return;
    }

    if (_mode == Mode::BrowseJPG || _mode == Mode::RandomJPG) {
        // Draw JPG filling screen at (0,0)
        ImageDisplay::displayImage(fullPath.c_str());
    } else {
        // TODO: Implement GIF display with AnimatedGIF library
    }
}

// Open gallery submenu with specified mode
void open(Mode mode) {
    _mode = mode;
    _imageDisplayed = false;
    _currentIndex = 0;
    _scrollOffset = 0;

    switch (_mode) {
        case Mode::BrowseJPG:
        case Mode::RandomJPG:
            scanImages("/jpg", ".jpg");
            break;
        case Mode::BrowseGIF:
        case Mode::RandomGIF:
            scanImages("/gif", ".gif");
            break;
        default:
            break;
    }

    drawMenu();
}

// Close gallery submenu
void close() {
    _mode = Mode::None;
    _imageFiles.clear();
    _currentIndex = 0;
    _imageDisplayed = false;
}

// Handle user input gesture
void handleGesture(uint8_t gesture, int16_t x, int16_t y) {
    switch (_mode) {
        case Mode::BrowseJPG:
        case Mode::BrowseGIF:
            if (gesture == SWIPE_RIGHT) {
                _currentIndex = (_currentIndex + 1) % _imageFiles.size();
                drawCurrentImage();
            } else if (gesture == SWIPE_LEFT) {
                _currentIndex = (_currentIndex - 1 + _imageFiles.size()) % _imageFiles.size();
                drawCurrentImage();
            } else if (gesture == SINGLE_CLICK) {
                _imageDisplayed = true;
                drawCurrentImage();
            } else if (gesture == SWIPE_UP) {
                close();
            }
            break;

        case Mode::RandomJPG:
        case Mode::RandomGIF:
            if (!_imageDisplayed) {
                _currentIndex = randomIndex();
                drawCurrentImage();
                _imageDisplayed = true;
            }
            // Optionally add periodic image change logic here
            if (gesture == SWIPE_UP) {
                close();
            }
            break;

        default:
            break;
    }
}

// Draw gallery UI (menu or image)
void draw() {
    if (_mode == Mode::None) return;

    if (!_imageDisplayed) {
        drawMenu();
    } else {
        drawCurrentImage();
    }
}

void update() {
    // Update animations, random cycling, etc. if needed
}

void begin(TFT_eSPI* tft) {
    _tft = tft;
}

void showImage(int index, Mode mode) {
    // Replace with your real path logic
    char path[32];
    if (mode == Mode::MODE_JPG)
        snprintf(path, sizeof(path), "/gallery/img%03d.jpg", index);
    else
        snprintf(path, sizeof(path), "/gallery/anim%03d.gif", index);

    ImageDisplay::displayImage(path); // Replace with your actual image show code
}

void nextImage() {
    currentIndex++;
    if (currentIndex >= countFiles(randomMode)) currentIndex = 0;
    showImage(currentIndex, randomMode);
}

void prevImage() {
    currentIndex--;
    if (currentIndex < 0) currentIndex = countFiles(randomMode) - 1;
    showImage(currentIndex, randomMode);
}

void startRandomMode(Mode mode) {
    randomMode = mode;
    totalImages = countFiles(mode);
    if (totalImages > 0) {
        currentIndex = random(0, totalImages);
        showImage(currentIndex, mode);
    }
}

} // namespace Gallery
