"""Regenerate platform icons from Michael Förtsch's logo (Pillow, build-time only)."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"


def square_logo(source):
    original = Image.open(source).convert("RGBA")
    side = max(original.size)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    left, top = (side - original.width) // 2, (side - original.height) // 2
    canvas.paste(original, (left, top))
    return canvas


def write_svg(canvas, destination):
    side = canvas.width
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
                if color[3]:
                    runs.add((start, x, color))
        for run in sorted(active):
            if run not in runs:
                rectangles.append((*run, active.pop(run), y))
        for run in sorted(runs):
            active.setdefault(run, y)
    def rgb(color):
        return "#" + "".join(f"{v:02x}" for v in color[:3])
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {side} {side}">']
    for x1, x2, color, y1, y2 in rectangles:
        opacity = f' fill-opacity="{color[3] / 255:.8g}"' if color[3] != 255 else ""
        svg.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" fill="{rgb(color)}"{opacity}/>')
    svg.append('</svg>')
    destination.write_text("\n".join(svg) + "\n", encoding="utf-8", newline="\n")


def main():
    variants = {theme: square_logo(RESOURCES / f"logo-{theme}.png") for theme in ("light", "dark")}
    for theme, canvas in variants.items():
        write_svg(canvas, RESOURCES / f"ixview-{theme}.svg")
    # Installed launchers have a fixed icon. The supplied grey mark remains
    # legible against light and dark backgrounds; runtime windows follow the UI.
    canvas = variants["dark"]
    write_svg(canvas, RESOURCES / "ixview.svg")
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    variants = {n: canvas.resize((n, n), Image.Resampling.NEAREST) for n in sizes}
    variants[256].save(RESOURCES / "uxzView.ico", sizes=[(n, n) for n in sizes],
                       append_images=[variants[n] for n in sizes if n != 256])
    mac_sizes = [16, 32, 64, 128, 256, 512, 1024]
    mac = [canvas.resize((n, n), Image.Resampling.NEAREST) for n in mac_sizes]
    mac[-1].save(RESOURCES / "uxzView.icns", append_images=mac[:-1])
    print("Updated transparent theme SVGs, Windows ICO and macOS ICNS from logo-light.png/logo-dark.png.")


if __name__ == "__main__":
    main()
