"""Host checks of actual credential storage; no firmware build or network access."""

from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[2]
sdk = Path(os.environ.get("LILKA_SDK_REPO", str(root.parent / "sdk"))) / "lib/lilka/src"
with tempfile.TemporaryDirectory(prefix="keira-wifi-test-") as directory:
    out = Path(directory)
    shim = (
        (root / "tests/navigation/string.h")
        .read_text()
        .replace("public:", "public:\n    char operator[](size_t index) const { return value[index]; }", 1)
    )
    (out / "Arduino.h").write_text("#include <cstdint>\n" + shim)
    for signedness in ("-fsigned-char", "-funsigned-char"):
        for sanitized in (False, True):
            binary = out / "test"
            flags = (
                ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"] if sanitized else []
            )
            subprocess.run(
                [
                    "g++",
                    "-std=c++11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-sign-compare",
                    signedness,
                    *flags,
                    "-I" + str(out),
                    "-I" + str(root / "tests/wifi_management"),
                    "-I" + str(root / "src"),
                    "-I" + str(sdk),
                    str(sdk / "lilka/wifi_credentials.cpp"),
                    str(root / "tests/wifi_management/host.cpp"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})

    for name in ("keira", "lilka"):
        (out / name).mkdir()
    for name in ("WiFi.h", "esp_wifi.h", "keira/service.h", "keira/ksystem.h", "lilka/serial.h"):
        (out / name).write_text('#pragma once\n#include "service_hal.h"\n')
    for sanitized in (False, True):
        binary = out / "service-test"
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-fno-pie", "-no-pie"] if sanitized else []
        subprocess.run(
            [
                "g++",
                "-std=c++11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-sign-compare",
                *flags,
                "-I" + str(out),
                "-I" + str(root / "tests/wifi_management"),
                "-I" + str(root / "src"),
                "-I" + str(sdk),
                str(sdk / "lilka/wifi_credentials.cpp"),
                str(sdk / "lilka/wifi_connection.cpp"),
                str(sdk.parents[2] / "tests/wifi/scan_stub.cpp"),
                str(root / "src/services/network/network.cpp"),
                str(root / "tests/wifi_management/service.cpp"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True, env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})
