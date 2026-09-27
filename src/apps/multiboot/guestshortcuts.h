#pragma once

#include <Arduino.h>
#include <vector>

// Recent SD multiboot images remain independently launchable in Applications.
std::vector<String> readGuestShortcuts();
void rememberGuestShortcut(const String& path, const std::vector<String>& previous);
