"""Render actual SDK menus using the menu construction blocks from WiFiConfigApp.

Recording HAL, approximate host font, no firmware build; not a device screenshot.
"""

from pathlib import Path
import subprocess
import tempfile
import argparse
from html import escape

root = Path(__file__).resolve().parents[2]
sdk = root.parent / "lilka-sdk"
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path("/tmp/keira-wifi-menu.png"))
args = parser.parse_args()
source = (root / "src/apps/wificonfig/wificonfig.cpp").read_text()


def block(start):
    at = source.index(start)
    return source[at : source.index("while (!", at)]


blocks = [
    ("WiFi", block('lilka::Menu menu("WiFi");'), "menu"),
    ("Saved networks", block("const auto names = service.knownNetworks();"), "menu"),
    ("Network actions", block("lilka::Menu actions("), "actions"),
    ("Forget confirmation", block("lilka::Menu confirmation(K_S_WIFI_CONFIG_FORGET_CONFIRM);"), "confirmation"),
]
ui = (sdk / "lib/lilka/src/lilka/ui.h").read_text()
menu = ui[ui.index("typedef void (*PMenuItemCallback)") : ui.index("/// Клас для відображення сповіщення.")]
with tempfile.TemporaryDirectory(prefix="wifi-menu-render-") as directory:
    out = Path(directory)
    mock = (
        (sdk / "tests/menu/mock_graphics.h")
        .read_text()
        .replace(
            "enum Button { UP, DOWN, LEFT, RIGHT, A, COUNT };", "enum Button { UP, DOWN, LEFT, RIGHT, A, B, C, COUNT };"
        )
    )
    mock = mock.replace(
        "draw.y += y;",
        "draw.y += y;\n                if (!draw.boundWidth) { draw.boundX=x; draw.boundY=y; draw.boundWidth=canvas->width(); draw.boundHeight=canvas->height(); }",
    )
    (out / "mock_graphics.h").write_text(mock)
    (out / "ui.h").write_text('#include "mock_graphics.h"\nnamespace lilka {\n' + menu + "\n}\n")
    (out / "menu.cpp").write_text((sdk / "lib/lilka/src/lilka/menu.cpp").read_text())
    body = """#include "ui.h"
#include <iostream>
#include "keira/localizations/lang_en.h"
#define K_BTN_BACK lilka::Button::B
struct NetworkCredentials {
    static String displayName(const String& name) {
        return name == "🦄🌈🎉" ? "[U+1F984][U+1F308][U+1F389]" : name;
    }
};
struct NetworkService {
    std::vector<String> knownNetworks() { return {"Home WiFi", "🦄🌈🎉", "Open network"}; }
};
int main() {
    NetworkService service;
    const String ssid = "🦄🌈🎉";
"""
    for title, construction, variable in blocks:
        body += (
            "{\n"
            + construction
            + f"""lilka::Canvas canvas(240, 280);
    {variable}.draw(&canvas);
    std::cout << "FRAME\\t{title}\\n";
    for (const auto& draw : canvas.draws) {{
        std::cout << draw.x << "\\t" << draw.y << "\\t" << draw.font << "\\t" << draw.size << "\\t"
                  << draw.color << "\\t" << draw.boundX << "\\t" << draw.boundY << "\\t"
                  << draw.boundWidth << "\\t" << draw.boundHeight << "\\t" << draw.text << "\\n";
    }}
}}\n"""
        )
    body += "}\n"
    (out / "render.cpp").write_text(body)
    subprocess.run(
        [
            "g++",
            "-std=c++17",
            "-I" + str(out),
            "-I" + str(root / "src"),
            str(out / "menu.cpp"),
            str(out / "render.cpp"),
            "-o",
            str(out / "render"),
        ],
        check=True,
    )
    output = subprocess.check_output([str(out / "render")], text=True)
frames = []
for line in output.splitlines():
    if line.startswith("FRAME\t"):
        frames.append([])
        continue
    values = line.split("\t", 9)
    frames[-1].append((list(map(int, values[:9])), values[9]))
svg = [
    '<svg xmlns="http://www.w3.org/2000/svg" width="960" height="280" viewBox="0 0 960 280">',
    '<rect width="960" height="280" fill="black"/>',
]
for frame, draws in enumerate(frames):
    offset = frame * 240
    for index, (values, text) in enumerate(draws):
        x, y, font, size, color, bx, by, bw, bh = values
        rgb = ((color >> 11) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31)
        clip = ""
        if bw and bh:
            name = f"clip-{frame}-{index}"
            svg.append(
                f'<clipPath id="{name}"><rect x="{bx + offset}" y="{by}" width="{bw}" height="{bh}"/></clipPath>'
            )
            clip = f' clip-path="url(#{name})"'
        svg.append(
            f'<text x="{x + offset}" y="{y}" font-family="DejaVu Sans Mono" font-size="{round(font * size / 0.6)}" '
            f'fill="rgb{rgb}"{clip}>{escape(text)}</text>'
        )
svg.append("</svg>")
args.output.parent.mkdir(parents=True, exist_ok=True)
source = args.output.with_suffix(".svg")
source.write_text("\n".join(svg))
subprocess.run(["rsvg-convert", "-z", "2", "-o", str(args.output), str(source)], check=True)
print(args.output)
