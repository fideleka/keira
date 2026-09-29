#pragma once

#include <Arduino.h>
#include <vector>

enum class RomSystem { NES, GameBoy, GameBoyColor };

RomSystem romSystemForPath(const String& path);
std::vector<String> readRecentRoms(RomSystem system);
void rememberRecentRom(const String& path);
