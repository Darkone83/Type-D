#ifndef DISP_CFG_H
#define DISP_CFG_H

#pragma once

// --- TFT Display GC9A01 (SPI) ---
#define TFT_CS     5    // Chip Select
#define TFT_DC     27   // Data/Command
#define TFT_RST    33   // Reset
#define TFT_MOSI   15   // SPI MOSI (SDA)
#define TFT_SCLK   14   // SPI SCK (SCL)



// --- SD Card (SD_MMC 4-bit mode) ---
#define SD_MMC_CMD   11
#define SD_MMC_CLK   10
#define SD_MMC_D0    9
#define SD_MMC_D1    8
#define SD_MMC_D2    12
#define SD_MMC_D3    13

// --- Touch Panel CST816S ---
#define TOUCH_SCL   22
#define TOUCH_SDA   21
#define TOUCH_RST   4
#define TOUCH_INT   19

// --- Other Options ---
#define DISP_ROTATION 0


// --- Version number ---
static constexpr char     VERSION_TEXT[]    = "v0.3";

#endif // DISP_CFG_H
