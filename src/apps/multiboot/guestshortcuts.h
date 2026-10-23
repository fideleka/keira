#pragma once

#include <Arduino.h>
#include <vector>

// Keep up to 20 recent SD multiboot images independently launchable in Applications.
// The saved list is also bounded by the NVS string capacity.
std::vector<String> readGuestShortcuts();
void rememberGuestShortcut(const String& path, const std::vector<String>& previous);
