"""Render actual SDK menus using production Devices/Display construction blocks.

Recording HAL, approximate host font, no firmware build; not a device screenshot.
"""

from pathlib import Path
import subprocess
import tempfile
import argparse
from html import escape

root = Path(__file__).resolve().parents[2]
sdk = root.parent / "sdk"
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=Path("/tmp/keira-device-menu.png"))
args = parser.parse_args()
device_source = (root / "src/apps/devices/devices.cpp").read_text()
display_source = (root / "src/apps/displaysettings/displaysettings.cpp").read_text()
def construction(source, marker, end):
    start = source.index(marker)
    return source[start:source.index(end,start)]
list_block = construction(device_source, "lilka::Menu menu(K_S_DEVICES)", "menu.setCursor(cursor)")
page_block = construction(device_source, "lilka::Menu menu(info.name)", "menu.setCursor(cursor)")
display_block = construction(display_source, "lilka::Menu menu(K_S_LAUNCHER_DISPLAY)", "while (!menu.isFinished())")
display_updates = construction(display_source, "        menu.setItem(", "        // Observe adjustment keys")
blocks = [("Devices",list_block,"menu"), ("Device",page_block,"menu"),
          ("Device scrolled",page_block+"menu.setCursor(8);menu.update();", "menu"),
          ("Display",display_block+display_updates,"menu")]

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
    mock = mock.replace("using String = std::string;", "class String : public std::string { public: using std::string::string; String(const std::string& s):std::string(s){} String(int n):std::string(std::to_string(n)){} };")
    (out / "mock_graphics.h").write_text(mock)
    (out / "ui.h").write_text('#include "mock_graphics.h"\nnamespace lilka {\n' + menu + "\n}\n")
    (out / "menu.cpp").write_text((sdk / "lib/lilka/src/lilka/menu.cpp").read_text())
    body = """#include "ui.h"
#include <iostream>
#include "keira/localizations/lang_en.h"
#define K_BTN_BACK lilka::Button::B
#include "driver_api.h"
struct DeviceManager {
 enum Mode {Internal,External,Both};
 struct Info {const char* name="ILI9341 display"; bool active=true,enabled=true;unsigned settingCount=7;};
 struct SettingInfo {const char*label;const char*choice;int value;unsigned kind;};
 unsigned count(){return 1;}void info(unsigned,Info&){}
 void settingInfo(unsigned,unsigned f,SettingInfo&r){
  const char* labels[]={"Clock pin","MOSI pin","Reset pin","DC pin","SPI speed","Orientation","Refresh limit"};
  const char* choices[]={"GPIO12","GPIO13","GPIO14","GPIO21","20 MHz","Automatic","30 FPS"};
  r={labels[f],choices[f],1,KD_SETTING_CHOICE};
 }
 Mode displayMode(){return Both;}
};
DeviceManager deviceManager;
struct {DeviceManager*devices=&deviceManager;}ksystem;
namespace lilka {
struct {bool isEnabled(){return true;}int getBrightness(){return 70;}}brightness;
struct {bool isAvailable(){return true;}unsigned getTimeoutSeconds(){return 0;}unsigned getDimTimeoutSeconds(){return 0;}}displaySettings;
}
String timeoutText(unsigned){return K_S_DISPLAY_NEVER;}
int main() {
    DeviceManager*manager=&deviceManager;unsigned index=0;DeviceManager::Info info;manager->info(index,info);

"""
    for title, construction, variable in blocks:
        body += (
            "{\n"
            + construction
            + f"""lilka::Canvas canvas(280, 216);
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
            "-I" + str(root / "drivers"),
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
    '<svg xmlns="http://www.w3.org/2000/svg" width="1120" height="216" viewBox="0 0 1120 216">',
    '<rect width="1120" height="216" fill="black"/>',
]
for frame, draws in enumerate(frames):
    offset = frame * 280
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
