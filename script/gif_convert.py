import sys
import os
from PIL import Image, ImageSequence

def main():
    if len(sys.argv) < 2:
        print("Usage: python gif_resize_for_animatedgif.py input.gif [output.gif]")
        return

    input_path = sys.argv[1]
    if len(sys.argv) > 2:
        output_path = sys.argv[2]
    else:
        base, ext = os.path.splitext(input_path)
        output_path = f"{base}_240x240.gif"

    # --- PROCESSING ---
    with Image.open(input_path) as im:
        frames = []
        durations = []

        for frame in ImageSequence.Iterator(im):
            fr = frame.convert("RGBA")
            fr = fr.resize((240, 240), Image.LANCZOS)
            fr = fr.convert("P", palette=Image.ADAPTIVE, colors=256)
            frames.append(fr)
            durations.append(33)  # Force all frames to 33ms (30 fps)

        frames[0].save(
            output_path,
            save_all=True,
            append_images=frames[1:],
            duration=durations,
            loop=0,
            optimize=False,
            disposal=2
        )

    print(f"Saved: {output_path} (forced 30fps, duration=33ms per frame)")

if __name__ == "__main__":
    main()
