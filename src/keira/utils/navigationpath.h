#pragma once

#include <Arduino.h>

// Lexical canonicalization only: navigation must not resolve or touch the filesystem.
inline String canonicalNavigationPath(const String& path) {
    if (!path.startsWith("/")) return "";
    String result;
    int from = 1;
    while (from < path.length()) {
        int end = path.indexOf('/', from);
        if (end < 0) end = path.length();
        String part = path.substring(from, end);
        if (part == "..") {
            int slash = result.lastIndexOf('/');
            result = slash >= 0 ? result.substring(0, slash) : String();
        } else if (!part.isEmpty() && part != ".") {
            result += "/";
            result += part;
        }
        from = end + 1;
    }
    return result.isEmpty() ? String("/") : result;
}
