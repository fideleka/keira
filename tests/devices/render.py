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
parser.add_argument("--language", choices=("en", "uk"), default="en")
parser.add_argument("--long-values", action="store_true", help="Render the longest driver choices")
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
          ("Device scrolled",page_block+"menu.setCursor(10);menu.update();", "menu"),
          ("SPI test",page_block+"menu.setCursor(11);menu.update();", "menu"),
          ("Display",display_block+display_updates,"menu")]

gpio_source = (root / "src/apps/gpiomanager/gpiomanager.cpp").read_text()
gpio_block = construction(gpio_source, "            for (int i = 0; i < PIN_COUNT; i++)", "            menu.setCursor(lastPos)")
blocks += [("GPIO protected", 'lilka::Menu menu("GPIO");'+gpio_block, "menu"),
           ("GPIO free pins", 'lilka::Menu menu("GPIO");'+gpio_block+'menu.setCursor(5);menu.update();', "menu")]

# Render the actual root Settings labels, retaining nested submenu/lambda boundaries.
import re
launcher = (root / "src/apps/launcher/launcher.cpp").read_text()
start = launcher.index("K_S_SETTINGS,", launcher.index("item_t LauncherApp::buildMainMenu"))
start = launcher.index("{", start)
depth = 0
labels = []
for match in re.finditer(r'"(?:\\.|[^"\\])*"|\{|\}|ITEM::(?:MENU|SUBMENU|APP)\s*\(\s*(K_S_[A-Z_]+)', launcher[start:]):
    token = match.group(0)
    if token == "{":
        depth += 1
    elif token == "}":
        depth -= 1
        if depth == 0:
            break
    elif match.group(1) and depth == 1:
        labels.append(match.group(1))
assert labels.count("K_S_DEVICES") == 1
assert launcher.count("ITEM::MENU(K_S_DEVICES") == 1
assert "ITEM::APP(K_S_DEVICES" not in launcher
settings_block = "lilka::Menu menu(K_S_SETTINGS);" + "".join("menu.addItem(" + label + ");" for label in labels)
blocks.insert(0, ("Settings · Devices", settings_block + "menu.setCursor(" + str(labels.index("K_S_DEVICES")) + ");menu.update();", "menu"))

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
    # Approximate font metrics count UTF-8 glyphs, not encoded bytes.
    mock = mock.replace("return std::strlen(text) * font;", "size_t glyphs=0;for(const unsigned char* p=reinterpret_cast<const unsigned char*>(text);*p;++p)glyphs+=(*p&0xc0)!=0x80;return glyphs*font;")
    mock = mock.replace("*width = text.length() * font * size;", "size_t glyphs=0;for(unsigned char c:text)glyphs+=(c&0xc0)!=0x80;*width=glyphs*font*size;")
    (out / "mock_graphics.h").write_text(mock)
    (out / "ui.h").write_text('#include "mock_graphics.h"\nnamespace lilka {\n' + menu + "\n}\n")
    (out / "menu.cpp").write_text((sdk / "lib/lilka/src/lilka/menu.cpp").read_text())
    body = """#include "ui.h"
#include <iostream>
#include "keira/localizations/lang_en.h"
#define K_BTN_BACK lilka::Button::B
#include "driver_api.h"
#define PIN_COUNT 6
#define INPUT 0
#define OUTPUT 1
#define HIGH 1
#define PIN_INPUT INPUT
#define PIN_OUTPUT OUTPUT
#define PIN_MODE_PWM 5
const menu_icon_t input_img={},output_img={};
uint8_t pinNo[6]={12,13,14,21,47,48},pinM[6]={},pinData[6]={},pwmDutyCycle[6]={};
unsigned pwmFrequency[6]={};
bool pinReserved(unsigned i){return i<5;} // Configured GPIO47 BLK is owned too.

struct DeviceManager {
 enum Mode {Internal,External,Both};
 struct Info {const char* name="ILI9341 display"; bool active=true,enabled=true;unsigned settingCount=10;};
 struct SettingInfo {const char*label;const char*choice;int value;unsigned kind;};
 unsigned count(){return 1;}void info(unsigned,Info&){}
 void settingInfo(unsigned,unsigned f,SettingInfo&r){
  const char* labels[]={"Clock pin","MOSI pin","Reset pin","DC pin","Backlight pin","SPI speed","Orientation","Refresh limit","Panel drive","SPI test"};
  const char* choices[]={"GPIO12","GPIO13","GPIO14","GPIO21","GPIO47","10 MHz","Automatic","30 FPS","79Hz default","Off"};
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
    if args.long_values:
        body = body.replace('"10 MHz","Automatic","30 FPS","79Hz default","Off"',
                            '"10 MHz","Automatic, flipped","30 FPS","79Hz default","Fixed image + writes"')
    body = body.replace('localizations/lang_en.h', 'localizations/lang_' + args.language + '.h')
    localization = device_source[device_source.index('namespace {'):device_source.index('DevicesApp::DevicesApp')]
    body = body.replace('int main() {', '#include <cstring>\n' + localization + '\nint main() {')
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
            "-I" + str(root / "drivers"), "-I" + str(sdk / "lib/lilka/src"),
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
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{len(frames)*280}" height="216" viewBox="0 0 {len(frames)*280} 216">',
    f'<rect width="{len(frames)*280}" height="216" fill="black"/>',
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
