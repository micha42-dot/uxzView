"""Regenerate platform icons from Michael Förtsch's logo (Pillow, build-time only)."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"


def main():
    original = Image.open(RESOURCES / "logo.png").convert("RGB")
    background = original.getpixel((0, 0))
    side = max(original.size)
    canvas = Image.new("RGB", (side, side), background)
    left, top = (side - original.width) // 2, (side - original.height) // 2
    canvas.paste(original, (left, top))
    # Merge equal horizontal runs vertically: keep the supplied pixel shapes exact.
    active, rectangles = {}, []
    for y in range(side + 1):
        runs = set()
        if y < side:
            x = 0
            while x < side:
                color = canvas.getpixel((x, y))
                start = x
                while x < side and canvas.getpixel((x, y)) == color:
                    x += 1
                if color != background:
                    runs.add((start, x, color))
        for run in sorted(active):
            if run not in runs:
                rectangles.append((*run, active.pop(run), y))
        for run in sorted(runs):
            active.setdefault(run, y)
    def rgb(color):
        return "#" + "".join(f"{v:02x}" for v in color)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}">',
           f'<rect width="{side}" height="{side}" fill="{rgb(background)}"/>']
    for x1, x2, color, y1, y2 in rectangles:
        svg.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" fill="{rgb(color)}"/>')
    svg.append('</svg>')
    (RESOURCES / "ixview.svg").write_text("\n".join(svg) + "\n", encoding="utf-8")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    variants = {n: canvas.resize((n, n), Image.Resampling.NEAREST) for n in sizes}
    variants[256].save(RESOURCES / "uxzView.ico", sizes=[(n, n) for n in sizes],
                       append_images=[variants[n] for n in sizes if n != 256])
    mac_sizes = [16, 32, 64, 128, 256, 512, 1024]
    mac = [canvas.resize((n, n), Image.Resampling.NEAREST) for n in mac_sizes]
    mac[-1].save(RESOURCES / "uxzView.icns", append_images=mac[:-1])
    print("Updated SVG, Windows ICO and macOS ICNS from resources/logo.png.")


if __name__ == "__main__":
    main()
