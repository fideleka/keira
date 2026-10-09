"""Generate exact, editable wiring sheets; no network or third-party packages."""

from pathlib import Path
import html
import math
import re

ROOT = Path(__file__).resolve().parent
BLUE = "#1760a0"
GREEN = "#08796b"
ORANGE = "#a95b08"
INK = "#253448"


class Sheet:
    def __init__(self, name, title, subtitle, height=1080):
        self.name = name
        self.height = height
        self.items = [
            f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1500 {height}">',
            "<style>text{font-family:DejaVu Sans,sans-serif;fill:#253448}"
            ".wire{fill:none;stroke-width:3;stroke-linejoin:round}</style>",
            f'<rect width="1500" height="{height}" fill="#ffffff"/>',
        ]
        self.text(40, 48, title, 28, bold=True)
        self.text(40, 82, subtitle, 17)

    def text(self, x, y, value, size=18, color=INK, bold=False, anchor="start"):
        weight = ' font-weight="bold"' if bold else ""
        self.items.append(
            f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}"'
            f' style="fill:{color}"{weight}>{html.escape(str(value))}</text>'
        )

    def box(self, x, y, width, height, fill="#f1f5f9", stroke="#b7c6d6"):
        self.items.append(
            f'<rect x="{x}" y="{y}" width="{width}" height="{height}"'
            f' rx="6" fill="{fill}" stroke="{stroke}" stroke-width="2"/>'
        )

    def line(self, points, color=BLUE, dashed=False):
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        coordinates = " ".join(f"{x},{y}" for x, y in points)
        self.items.append(f'<polyline class="wire" points="{coordinates}" stroke="{color}"{dash}/>')

    def dot(self, x, y, color=BLUE, radius=5):
        self.items.append(f'<circle cx="{x}" cy="{y}" r="{radius}" fill="{color}"/>')

    def ground(self, x, y):
        self.line([(x, y), (x, y + 12)], INK)
        for offset, half_width in [(12, 14), (18, 9), (24, 4)]:
            self.line([(x - half_width, y + offset), (x + half_width, y + offset)], INK)

    def resistor(self, x, y, label, vertical=False):
        width, height = (18, 58) if vertical else (80, 22)
        self.box(x, y, width, height, "#ffffff", INK)
        self.text(x + width + 14 if vertical else x, y + 27 if vertical else y - 10, label, 16)

    def capacitor(self, x, y, label):
        self.line([(x, y), (x, y + 18)], INK)
        self.line([(x - 14, y + 18), (x + 14, y + 18)], INK)
        self.line([(x - 14, y + 28), (x + 14, y + 28)], INK)
        self.line([(x, y + 28), (x, y + 70)], INK)
        self.text(x + 23, y + 35, label, 16)

    def save(self):
        self.items.append("</svg>")
        svg = "\n".join(self.items)
        (ROOT / f"{self.name}.svg").write_text(svg + "\n")
        return svg


def circuit():
    s = Sheet(
        "rtc-circuit",
        "RTC modification • complete connection diagram",
        "3.3 V only • BCLK envelope selector • firmware-coordinated prototype, not yet bench-qualified",
    )
    s.box(40, 110, 1420, 440)
    s.text(65, 146, "A. RTC branch — audio wiring stays direct", 22, bold=True)
    s.box(70, 178, 270, 270, "#eaf3fc")
    s.text(95, 210, "Lilka / amplifier J5", 20, bold=True)
    s.box(535, 178, 440, 315)
    s.text(565, 210, "U1  TS5A23157DGS", 21, bold=True)
    s.box(1160, 178, 265, 315, "#e9f7f2")
    s.text(1185, 210, "DS3231 module", 20, bold=True)
    for y, label, com, nc, rtc in [
        (260, "1  LRCK / GPIO1", "10 COM1", "9 NC1", "C / SCL"),
        (335, "3  DIN / GPIO2", "6 COM2", "7 NC2", "D / SDA"),
    ]:
        s.text(95, y - 12, label, 18)
        s.line([(340, y), (535, y)])
        s.text(552, y - 12, com, 17)
        s.line([(680, y), (835, y)], GREEN)
        s.text(885, y - 12, nc, 17, anchor="middle")
        s.line([(975, y), (1160, y)], GREEN)
        s.text(1185, y - 12, rtc, 18)
        s.text(745, y + 29, "LOW connects NC", 15, GREEN, anchor="middle")
    s.text(95, 399, "2  BCK / GPIO42", 18)
    s.text(95, 425, "→ detector below", 16, BLUE)
    s.text(565, 401, "2 NO1 + 4 NO2: leave OPEN", 18, ORANGE)
    s.text(565, 433, "1 IN1 + 5 IN2: SELECT", 18, ORANGE)
    s.text(565, 467, "8 V+ = 3.3 V; 3 GND = ground", 17)
    s.text(1185, 385, "+ = 3.3 V", 18)
    s.text(1185, 416, "− = GND", 18)
    s.text(1185, 449, "NC: leave OPEN", 18, ORANGE)
    s.text(
        65,
        528,
        "RTC-side pull-ups only: D → 4.7k → 3.3 V; C → 4.7k → 3.3 V. Omit added resistors if module pull-ups are confirmed.",
        17,
    )
    s.box(40, 570, 1420, 305)
    s.text(65, 607, "B. Detector — a branch from BCK; never interrupt the amplifier clock wire", 22, bold=True)
    s.text(65, 659, "J5 pad 2", 17)
    s.text(65, 683, "BCK", 17)
    s.line([(160, 690), (205, 690)])
    s.resistor(205, 679, "R1 1kΩ")
    s.line([(285, 690), (325, 690)])
    # Standard diode symbol: anode at left, cathode bar at right.
    s.items.append('<path d="M325 675 L325 705 L350 690 Z" fill="none" stroke="#253448" stroke-width="3"/>')
    s.line([(351, 671), (351, 709)], INK)
    s.line([(351, 690), (735, 690)])
    s.text(315, 652, "D1 BAT43", 17)
    s.text(355, 717, "band → ENV", 15, ORANGE)
    s.text(575, 676, "ENV", 18, BLUE, bold=True)
    s.dot(510, 690)
    s.line([(510, 690), (510, 740)])
    s.resistor(501, 740, "R2 47kΩ", vertical=True)
    s.line([(510, 798), (510, 824)], INK)
    s.ground(510, 824)
    s.dot(660, 690)
    s.line([(660, 690), (660, 736)])
    s.capacitor(660, 736, "")
    s.text(660, 866, "Cenv 100nF", 16, anchor="middle")
    s.line([(660, 806), (660, 824)], INK)
    s.ground(660, 824)
    s.box(735, 647, 305, 144, "#e9f7f2")
    s.text(758, 674, "U2 SN74LVC1G17", 20, bold=True)
    s.text(758, 703, "2 A → non-inverting → 4 Y", 17)
    s.text(758, 735, "5 VCC = 3.3 V; 3 GND", 17)
    s.text(758, 767, "1 NC: leave OPEN", 17, ORANGE)
    s.line([(1040, 690), (1380, 690)], ORANGE)
    s.text(1150, 650, "SELECT", 18, ORANGE, bold=True)
    s.text(1300, 675, "to U1 pins 1 + 5", 17, ORANGE, anchor="middle")
    s.dot(1140, 690, ORANGE)
    s.line([(1140, 690), (1140, 740)], ORANGE)
    s.resistor(1131, 740, "R3 100kΩ", vertical=True)
    s.line([(1140, 798), (1140, 824)], INK)
    s.ground(1140, 824)
    s.box(40, 895, 1420, 143, "#fff6e9")
    s.text(
        65,
        927,
        "Power: J5 pad 7 VIN = +3V3; J5 pad 6 = GND. Every ground symbol uses this same system ground.",
        19,
        bold=True,
    )
    s.text(
        65,
        958,
        "Cbuf 100nF: U2 pins 5 ↔ 3.  Csw 100nF: U1 pins 8 ↔ 3.  Crtc 1µF: module + ↔ −. Place close to each device.",
        18,
    )
    s.text(
        65,
        989,
        "BCK HIGH / clocking → SELECT HIGH → RTC isolated. BCK LOW, after settling → SELECT LOW → RTC connected.",
        18,
    )
    s.text(
        65,
        1020,
        "Do not connect SELECT to GPIO46/SLEEP. Provisional waits: 2ms HIGH before audio; 20ms LOW before I²C.",
        18,
        ORANGE,
    )
    return s.save()


def adapters():
    s = Sheet(
        "rtc-adapters",
        "Adapter placement • top/component-side views",
        "Two separate physical adapters • verify package fit and every lead-to-header connection before adding power",
        1040,
    )
    s.box(40, 115, 680, 570)
    s.text(65, 153, "U1: TS5A23157DGS • 0.5mm side", 23, bold=True)
    s.text(65, 184, "Pin-1 dot toward the adapter’s square pad 1", 18)
    s.box(245, 220, 260, 380, "#34475b", "#34475b")
    s.dot(270, 245, "#ffffff", 7)
    left = [(1, "IN1 / SELECT"), (2, "NO1 / OPEN"), (3, "GND"), (4, "NO2 / OPEN"), (5, "IN2 / SELECT")]
    right = [(10, "COM1 / LRCK"), (9, "NC1 / RTC C"), (8, "V+ / 3.3 V"), (7, "NC2 / RTC D"), (6, "COM2 / DIN")]
    for i, ((lp, ll), (rp, rl)) in enumerate(zip(left, right)):
        y = 270 + i * 70
        s.line([(215, y), (245, y)], INK)
        s.line([(505, y), (535, y)], INK)
        s.box(187, y - 14, 28, 28, "#d9e5d9", GREEN)
        s.box(535, y - 14, 28, 28, "#d9e5d9", GREEN)
        s.text(177, y + 6, f"{lp}  {ll}", 16, anchor="end")
        s.text(573, y + 6, f"{rp}  {rl}", 16)
    s.text(65, 647, "Numbers are IC pins and matching adapter header numbers.", 17)
    s.box(745, 115, 715, 570)
    s.text(770, 153, "U2: SN74LVC1G17DBVR • 0.95mm side", 22, bold=True)
    s.text(770, 184, "Your SOT2310 carrier, oriented exactly as in your photo", 18)
    for i in range(5):
        x = 870 + 105 * i
        for y, number in [(270, 10 - i), (540, 1 + i)]:
            s.box(x - 16, y - 16, 32, 32, "#d9e5d9", GREEN)
            s.text(x, y - 30 if y == 270 else y + 48, str(number), 21, anchor="middle", bold=True)
    s.box(935, 335, 290, 140, "#34475b", "#34475b")
    s.dot(955, 453, "#ffffff", 7)
    for x, adapter, chip, label in [(975, 9, 5, "3.3 V"), (1185, 7, 4, "SELECT")]:
        s.line([(x, 286), (x, 335)], ORANGE if chip == 4 else BLUE)
        s.text(x, 321, f"IC {chip}", 17, anchor="middle")
        s.text(x, 216, label, 17, anchor="middle")
    for x, adapter, chip, label in [(975, 2, 1, "NC/open"), (1080, 3, 2, "ENV"), (1185, 4, 3, "GND")]:
        s.line([(x, 475), (x, 524)], BLUE)
        s.text(x, 509, f"IC {chip}", 17, anchor="middle")
        s.text(x, 625, label, 17, anchor="middle")
    s.text(770, 657, "3-leg side faces down; middle three positions are used.", 17)
    s.box(40, 710, 1420, 278, "#fff6e9")
    s.text(65, 749, "Buffer mapping: IC pin → carrier pad", 23, bold=True)
    s.text(
        65, 790, "1 NC → 2 (leave open)     2 A → 3 (ENV)     3 GND → 4     4 Y → 7 (SELECT)     5 VCC → 9 (3.3 V)", 20
    )
    s.text(
        65,
        832,
        "Carrier pads 1, 5, 6, 8, 10 stay unused. Dry-fit first: 0.95mm pitch alone does not guarantee row-spacing fit.",
        18,
    )
    s.text(
        65,
        874,
        "Never mount the switch and buffer on opposite faces of ONE adapter: the shared header nets would short them.",
        18,
        ORANGE,
        bold=True,
    )
    s.text(
        65,
        916,
        "Cbuf sits close to buffer IC pins 5 and 3 (carrier pads 9 and 4); Csw close to switch pins 8 and 3.",
        18,
    )
    s.text(65, 955, "Read the actual pin-1 mark and carrier numbering. Views from the underside are mirrored.", 18)
    return s.save()


def parse_pcb(path):
    tokens = re.findall(r'"(?:\\.|[^"\\])*"|[^\s()]+|[()]', path.read_text())
    stack = []
    root = None
    for token in tokens:
        if token == "(":
            node = []
            if stack:
                stack[-1].append(node)
            else:
                root = node
            stack.append(node)
        elif token == ")":
            stack.pop()
        else:
            stack[-1].append(token.strip('"'))
    return root


def children(node, key):
    return [item for item in node if isinstance(item, list) and item and item[0] == key]


def child(node, key):
    return children(node, key)[0]


def board(pcb):
    root = parse_pcb(pcb)
    s = Sheet(
        "rtc-board-points",
        "Lilka board • five connection points on amplifier J5",
        "PCB-derived rear/amp-side view • board top (extension connector) stays at the top • not a photograph",
        1000,
    )
    scale = 7.0

    def position(x, y):
        # Rear view: horizontal reflection of KiCad's top-view board coordinates.
        return 60 + (160.02 - x) * scale, 190 + (y - 43.18) * scale

    polygon = next(item for item in children(root, "gr_poly") if child(item, "layer")[1] == "Edge.Cuts")
    vertices = [(float(p[1]), float(p[2])) for p in child(polygon, "pts")[1:]]
    points = " ".join(f"{x},{y}" for x, y in [position(*p) for p in vertices])
    s.items.append(f'<polygon points="{points}" fill="#edf4ee" stroke="#72937b" stroke-width="3"/>')
    j5 = {}
    for footprint in children(root, "footprint"):
        refs = [p[2] for p in children(footprint, "property") if p[1] == "Reference"]
        if not refs:
            continue
        ref = refs[0]
        at = child(footprint, "at")
        ox, oy = float(at[1]), float(at[2])
        angle = math.radians(float(at[3]) if len(at) > 3 else 0)
        for pad in children(footprint, "pad"):
            # Show through-hole anchor pads only, not top-only hidden SMD pads.
            if pad[2] != "thru_hole":
                continue
            xy = child(pad, "at")
            px, py = float(xy[1]), float(xy[2])
            world = (ox + px * math.cos(angle) + py * math.sin(angle), oy - px * math.sin(angle) + py * math.cos(angle))
            x, y = position(*world)
            s.dot(x, y, "#b7c9bb", 4)
            if ref == "J5":
                j5[pad[1]] = (x, y, child(pad, "net")[2])
        if ref in ["J2", "J1", "J3", "J4", "J6"]:
            x, y = position(ox, oy)
            s.text(x, y - 20, ref, 17, anchor="middle")
    s.text(510, 153, "Extension header: no wires added", 19, GREEN, anchor="middle")
    s.text(510, 579, "Mirrored rear view — J5 is toward the right here", 17, anchor="middle")
    # Enlarged row map next to the actual highlighted pad location.
    for number in ["1", "2", "3", "6", "7"]:
        x, y, net = j5[number]
        s.dot(x, y, BLUE, 7)
    x, y, _ = j5["2"]
    s.line([(x + 12, y), (1010, y), (1010, 185)], BLUE, dashed=True)
    s.box(1040, 120, 420, 600)
    s.text(1065, 157, "J5 signal row • enlarged", 23, bold=True)
    rows = [
        ("1", "LRCK / GPIO1", "→ U1 pin 10 COM1"),
        ("2", "BCK / GPIO42", "→ R1 1k detector input"),
        ("3", "DIN / GPIO2", "→ U1 pin 6 COM2"),
        ("4", "GAIN", "Leave untouched"),
        ("5", "SD / SLEEP", "Leave untouched / GPIO46"),
        ("6", "GND", "Common system ground"),
        ("7", "VIN = +3V3", "Power to all three devices"),
    ]
    expected = {"1": "LRCK", "2": "BCK", "3": "DIN", "6": "GND", "7": "+3V3"}
    for i, (number, label, destination) in enumerate(rows):
        y = 213 + i * 67
        color = BLUE if number in expected else "#8a949e"
        s.dot(1080, y, color, 10)
        s.text(1105, y + 6, f"{number}  {label}", 19, color, bold=True)
        s.text(1105, y + 30, destination, 16)
        if number in expected:
            assert j5[number][2] == expected[number], (number, j5[number])
    s.text(1065, 697, "1 is above 7 in this rear view.", 17)
    s.box(40, 750, 1420, 194, "#fff6e9")
    s.text(
        65,
        789,
        "Solder to the existing J5 signal-row joints; amplifier stays connected directly as before.",
        21,
        bold=True,
    )
    s.text(
        65,
        828,
        "Confirm the pad by its amplifier label and continuity to the matching J5 net. Actual installed-module access varies.",
        18,
    )
    s.text(
        65,
        866,
        "Speaker OUT− is NOT ground. Do not use battery B+/B−, USB 5V, or the amplifier SD/SLEEP pad for this circuit.",
        18,
        ORANGE,
    )
    s.text(
        65,
        905,
        "Source: Lilka hardware/v2/main.kicad_pcb. Your board is marked 2.3; verify labels/net continuity before soldering.",
        18,
    )
    return s.save()


def main():
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pcb", type=Path, required=True, help="Lilka main.kicad_pcb")
    args = parser.parse_args()
    svgs = [circuit(), adapters(), board(args.pcb)]
    style = "body{margin:0;background:#e7edf3;font-family:sans-serif}main{max-width:1500px;margin:20px auto}svg{width:100%;height:auto;display:block;margin-bottom:24px}"
    page = '<!doctype html>\n<meta charset="utf-8">\n<meta name="viewport" content="width=device-width,initial-scale=1">\n<title>Lilka RTC wiring sheets</title>\n'
    page += f"<style>{style}</style>\n<main>\n" + "\n".join(svgs) + "\n</main>\n"
    (ROOT / "rtc-wiring.html").write_text(page)


if __name__ == "__main__":
    main()
