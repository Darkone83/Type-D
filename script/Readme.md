# GIF Resize and Format Converter for AnimatedGIF (gif_convert.py)

This script resizes any GIF to **240x240 pixels** and standardizes its format for use with the [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) Arduino library (or any embedded project needing strict GIF specs).

- All frames are resized to 240x240 using high-quality scaling.
- All frames are set to 33ms (for **30 frames per second** playback).
- Output GIF uses a global palette and settings for best compatibility.

---

## Requirements

- Python 3.x
- [Pillow (PIL)](https://python-pillow.org) library

To install Pillow, run:

```bash
pip install Pillow
```

---

## Usage

Run the script from the command line:

```bash
python gif_convert.py input.gif [output.gif]
```

- `input.gif`: Path to your source GIF.
- `output.gif` (optional): Output file name. If omitted, it will save as `input_240x240.gif`.

**Examples:**

```bash
python gif_convert.py boot.gif
python gif_convert.py source.gif resized.gif
```

---

## What It Does

1. Loads the source GIF and iterates over every frame.
2. Resizes each frame to 240x240 pixels with high quality.
3. Converts each frame to a palette-optimized GIF frame (256 colors).
4. Forces each frame duration to 33ms (approx 30fps playback, ideal for most microcontroller libraries).
5. Writes a new GIF file suitable for embedded playback.

---

## Output

- The converted GIF will work reliably with microcontroller GIF players such as AnimatedGIF (bitbank2) and other firmware that expect small, square, fast-refresh GIFs.

---

## Notes

- If your input GIF is not square, it will be scaled and possibly distorted to fit 240x240.
- The script does not preserve per-frame durations; all frames are set to 33ms for consistency and best playback performance.
- Use only for GIFs you want to display on embedded screens or similar constrained environments.

---

## License

MIT or Public Domain.  
No warranty is provided—test on your hardware!
