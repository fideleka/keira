#pragma once
#include <string>
class String {
    std::string value;
public:
    String() = default;
    String(const char* s) : value(s) {}
    String(std::string s) : value(std::move(s)) {}
    const char* c_str() const { return value.c_str(); }
    int length() const { return value.size(); }
    bool isEmpty() const { return value.empty(); }
    bool startsWith(const char* s) const { return value.rfind(s, 0) == 0; }
    int indexOf(char c, int from = 0) const {
        auto pos = value.find(c, from);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    int lastIndexOf(char c) const {
        auto pos = value.rfind(c);
        return pos == std::string::npos ? -1 : static_cast<int>(pos);
    }
    String substring(int from, int to) const { return value.substr(from, to - from); }
    String substring(int from) const { return value.substr(from); }
    String& operator+=(const String& other) { value += other.value; return *this; }
    friend bool operator==(const String& a, const String& b) { return a.value == b.value; }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
};
