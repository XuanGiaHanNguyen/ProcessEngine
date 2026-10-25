"""Assembles docs/images/frames/*.png into docs/images/<name> (default play-mode.gif), then deletes
the frames."""
import shutil
import sys
from pathlib import Path

from PIL import Image

out = Path(sys.argv[1] if len(sys.argv) > 1 else "../docs/images")
name = sys.argv[2] if len(sys.argv) > 2 else "play-mode.gif"
frames_dir = out / "frames"
frames = []
for path in sorted(frames_dir.glob("*.png")):
    img = Image.open(path).convert("RGB")
    img = img.resize((img.width * 3 // 4, img.height * 3 // 4), Image.LANCZOS)
    frames.append(img.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE))
frames[0].save(
    out / name,
    save_all=True,
    append_images=frames[1:],
    duration=900,
    loop=0,
    optimize=True,
)
shutil.rmtree(frames_dir)
print(f"wrote {out / name} ({len(frames)} frames)")
