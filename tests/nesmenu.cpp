#include "apps/nes/preferences.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <initializer_list>
using namespace nesmenu;
namespace {
int renameCount = 0;
int failAt = 0;
bool failRollback = false;
int failingRename(const char* from, const char* to) {
    if (++renameCount == failAt || (failRollback && renameCount == 3)) return -1;
    return rename(from, to);
}
void put(const char* path, const char* data) {
    FILE* file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, strlen(data), file) == strlen(data));
    int closed = fclose(file);
    assert(!closed);
}
void parser() {
    Preferences p;
    auto parse = [&](const char* text) { return parseConfig(text, strlen(text), p); };
    assert(parse("version=1\n") == ConfigResult::Ok);
    assert(!p.precisionMode && p.directionDelayXMs == 250 && p.directionDelayYMs == 250 && p.turboA && p.turboB);
    assert(
        parse("# hello\r\nversion=1\r\nprecision_mode=1\ndirection_delay_ms=1000\nturbo_a=0\nturbo_b=0") ==
        ConfigResult::Ok
    );
    assert(p.precisionMode && p.directionDelayXMs == 1000 && p.directionDelayYMs == 1000 && !p.turboA && !p.turboB);
    for (auto text : {
        "version=1\ndirection_delay_ms=700\ndirection_delay_x_ms=200\ndirection_delay_y_ms=500",
        "direction_delay_y_ms=500\ndirection_delay_x_ms=200\ndirection_delay_ms=700\nversion=1",
        "version=1\ndirection_delay_y_ms=500\ndirection_delay_ms=700\ndirection_delay_x_ms=200",
        "version=1\ndirection_delay_x_ms=200\ndirection_delay_ms=700\ndirection_delay_y_ms=500",
        "version=1\ndirection_delay_ms=700\ndirection_delay_y_ms=500\ndirection_delay_x_ms=200",
        "version=1\ndirection_delay_x_ms=200\ndirection_delay_y_ms=500\ndirection_delay_ms=700"}) {
        assert(parse(text) == ConfigResult::Ok);
        assert(p.directionDelayXMs == 200 && p.directionDelayYMs == 500);
    }
    assert(parse("version=1\ndirection_delay_x_ms=200") == ConfigResult::Ok);
    assert(p.directionDelayXMs == 200 && p.directionDelayYMs == 250);
    assert(parse("version=1\ndirection_delay_y_ms=500") == ConfigResult::Ok);
    assert(p.directionDelayXMs == 250 && p.directionDelayYMs == 500);
    assert(parse("version=1\ndirection_delay_ms=700\ndirection_delay_y_ms=500") == ConfigResult::Ok);
    assert(p.directionDelayXMs == 700 && p.directionDelayYMs == 500);
    for (auto invalid :
         {"",
          "version=1\nprecision_mode=2",
          "version=1\nturbo_a=-1",
          "version=1\nx=1",
          "version=1\ndirection_delay_ms=49",
          "version=1\ndirection_delay_ms=1001",
          "version=1\ndirection_delay_ms=99999999999999999999999",
          "version=1\nversion=1",
          "version=1\n=1",
          "version=1\ndirection_delay_x_ms=49",
          "version=1\ndirection_delay_x_ms=1001",
          "version=1\ndirection_delay_y_ms=49",
          "version=1\ndirection_delay_y_ms=1001",
          "version=1\ndirection_delay_y_ms=500\ndirection_delay_y_ms=500",
          "version=1\ndirection_delay_x_ms=200\ndirection_delay_x_ms=200",
          "version=1\ndirection_delay_ms=200\ndirection_delay_ms=200",
          "version=1\ndirection_delay_x_ms=200x",
          "version=1\ndirection_delay_y_ms=99999999999999999"}) {
        assert(parse(invalid) == ConfigResult::Malformed);
        assert(!p.precisionMode && p.directionDelayXMs == 250 && p.directionDelayYMs == 250 && p.turboA);
    }
    assert(parse("version=2\nprecision_mode=1") == ConfigResult::Unsupported);
    assert(parse("version=99999999999999999999") == ConfigResult::Unsupported);
    char large[MAX_CONFIG_BYTES + 1] = {};
    assert(parseConfig(large, sizeof(large), p) == ConfigResult::Malformed);
    const char nul[] = "version=1\n\0x=1";
    assert(parseConfig(nul, sizeof(nul) - 1, p) == ConfigResult::Malformed);
    char path[MAX_PATH_BYTES];
    assert(configPath("/sd/dir.with.dots/Magic Jewelry.nes", path, sizeof(path)));
    assert(!strcmp(path, "/sd/dir.with.dots/Magic Jewelry.conf"));
    assert(configPath("/sd/Game.v1.NES", path, sizeof(path)) && !strcmp(path, "/sd/Game.v1.conf"));
    assert(configPath("/sd/Game", path, sizeof(path)) && !strcmp(path, "/sd/Game.conf"));
    assert(configPath("/sd/.hidden", path, sizeof(path)) && !strcmp(path, "/sd/.hidden.conf"));
    assert(!configPath("game.nes", path, 9));
}
void timing() {
    Preferences p;
    DirectionFilter f;
    assert(f.update(true, false, 0, p.precisionMode, p.directionDelayXMs) == -1);
    assert(f.update(true, false, 1, p.precisionMode, p.directionDelayXMs) == -1); // default bypass
    p.precisionMode = true;
    assert(f.update(true, false, 10, p.precisionMode, p.directionDelayXMs) == -1);
    assert(f.update(true, false, 11, p.precisionMode, p.directionDelayXMs) == 0);
    assert(f.update(true, false, 259, p.precisionMode, p.directionDelayXMs) == 0);
    assert(f.update(true, false, 260, p.precisionMode, p.directionDelayXMs) == -1);
    assert(f.update(true, false, 1000, p.precisionMode, p.directionDelayXMs) == -1); // sustained, no repeated edges
    assert(f.update(false, true, 1001, p.precisionMode, p.directionDelayXMs) == 1); // reversal resets
    assert(f.update(false, true, 1002, p.precisionMode, p.directionDelayXMs) == 0);
    assert(f.update(false, false, 1003, p.precisionMode, p.directionDelayXMs) == 0);
    assert(f.update(false, true, 1004, p.precisionMode, p.directionDelayXMs) == 1); // release resets
    assert(f.update(true, true, 1005, p.precisionMode, p.directionDelayXMs) == 0); // conflicting directions neutral
    assert(f.update(true, false, 1006, p.precisionMode, p.directionDelayXMs) == -1);
    f.reset(); // modal entry/resume/reset
    assert(f.update(true, false, 1007, p.precisionMode, p.directionDelayXMs) == -1);
    f.reset();
    assert(f.update(false, true, UINT32_MAX - 100, p.precisionMode, p.directionDelayXMs) == 1);
    assert(f.update(false, true, 148, p.precisionMode, p.directionDelayXMs) == 0);
    assert(f.update(false, true, 149, p.precisionMode, p.directionDelayXMs) == 1); // wrap-safe subtraction
    DirectionFilter x, y;
    p.directionDelayXMs = 200;
    p.directionDelayYMs = 500;
    auto sample = [&](bool negative, bool positive, uint32_t now, int wantX, int wantY) {
        assert(x.update(negative, positive, now, p.precisionMode, p.directionDelayXMs) == wantX);
        assert(y.update(negative, positive, now, p.precisionMode, p.directionDelayYMs) == wantY);
    };
    sample(true, false, 0, -1, -1);
    sample(true, false, 199, 0, 0);
    sample(true, false, 200, -1, 0);
    sample(true, false, 499, -1, 0);
    sample(true, false, 500, -1, -1);
    sample(false, true, 501, 1, 1); // reversal starts both independently
    sample(false, true, 701, 1, 0);
    sample(true, true, 702, 0, 0);
    sample(false, true, 703, 1, 1);
    x.reset(); // modal boundary
    y.reset();
    sample(false, true, UINT32_MAX - 100, 1, 1);
    sample(false, true, 99, 1, 0);
    sample(false, true, 398, 1, 0);
    sample(false, true, 399, 1, 1);
    p.precisionMode = false;
    sample(true, false, 400, -1, -1);
    sample(true, false, 401, -1, -1);
    ReleaseGate gate;
    assert(gate.blocked(true)); // chord held
    assert(gate.blocked(true)); // one chord button or menu A still held
    assert(!gate.blocked(false));
    gate.arm();
    assert(gate.blocked(true));
    assert(!gate.blocked(false));
}
void files() {
    char directory[] = "/tmp/keira-nesmenu-test-XXXXXX";
    assert(mkdtemp(directory));
    char path[MAX_PATH_BYTES], temp[MAX_PATH_BYTES + 8], backup[MAX_PATH_BYTES + 8];
    snprintf(path, sizeof(path), "%s/Game.conf", directory);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    Preferences p, loaded;
    assert(loadConfig(path, loaded) == ConfigResult::Missing);
    assert(saveConfig(path, p) == ConfigResult::Ok);
    p.precisionMode = true;
    p.directionDelayXMs = 200;
    p.directionDelayYMs = 500;
    FileOperations fail{failingRename, remove};
    for (int failure : {1, 2}) {
        renameCount = 0;
        failAt = failure;
        assert(saveConfig(path, p, &fail) == ConfigResult::IoError);
        assert(loadConfig(path, loaded) == ConfigResult::Ok && !loaded.precisionMode && loaded.directionDelayXMs == 250);
        assert(access(temp, F_OK) && access(backup, F_OK));
    }
    renameCount = 0;
    failAt = 2;
    failRollback = true;
    assert(saveConfig(path, p, &fail) == ConfigResult::IoError);
    assert(loadConfig(path, loaded) == ConfigResult::Missing);
    assert(loadConfig(backup, loaded) == ConfigResult::Ok && !loaded.precisionMode);
    assert(saveConfig(path, p) == ConfigResult::IoError); // unresolved backup not overwritten
    assert(!rename(backup, path));
    failRollback = false;
    assert(saveConfig(path, p) == ConfigResult::Ok);
    assert(
        loadConfig(path, loaded) == ConfigResult::Ok && loaded.precisionMode && loaded.directionDelayXMs == 200 &&
        loaded.directionDelayYMs == 500
    );
    FILE* canonical = fopen(path, "rb");
    assert(canonical);
    char saved[MAX_CONFIG_BYTES + 1] = {};
    assert(fread(saved, 1, MAX_CONFIG_BYTES, canonical) > 0);
    assert(!fclose(canonical));
    assert(strstr(saved, "direction_delay_x_ms=200\ndirection_delay_y_ms=500\n"));
    assert(!strstr(saved, "direction_delay_ms="));
    put(path, "version=1\ndirection_delay_ms=700");
    assert(loadConfig(path, loaded) == ConfigResult::Ok);
    assert(loaded.directionDelayXMs == 700 && loaded.directionDelayYMs == 700);
    assert(saveConfig(path, loaded) == ConfigResult::Ok);
    assert(loadConfig(path, loaded) == ConfigResult::Ok);
    assert(loaded.directionDelayXMs == 700 && loaded.directionDelayYMs == 700);
    for (bool horizontal : {true, false}) {
        Preferences invalid;
        (horizontal ? invalid.directionDelayXMs : invalid.directionDelayYMs) = 1001;
        assert(saveConfig(path, invalid) == ConfigResult::Malformed);
        assert(loadConfig(path, loaded) == ConfigResult::Ok && loaded.directionDelayYMs == 700);
    }
    put(backup, "version=1\n");
    assert(saveConfig(path, p) == ConfigResult::IoError); // unresolved backup protected
    assert(!remove(backup));
    put(path, "version=2\n");
    assert(saveConfig(path, p) == ConfigResult::Unsupported);
    assert(loadConfig(path, loaded) == ConfigResult::Unsupported);
    put(path, "version=1\nbogus");
    assert(saveConfig(path, p) == ConfigResult::Malformed);
    put(path, "version=1\n");
    assert(!symlink("/dev/full", temp)); // fprintf/flush error preserves prior file
    assert(saveConfig(path, p) == ConfigResult::IoError);
    assert(loadConfig(path, loaded) == ConfigResult::Ok && !loaded.precisionMode);
    assert(saveConfig("/nonexistent-keira-test/Game.conf", p) == ConfigResult::IoError);
    assert(!remove(path));
    assert(!rmdir(directory));
}
} // namespace
int main() {
    parser();
    timing();
    files();
    puts("NES menu preferences: parser/path/version/timing/reversal/release gate and I/O failure tests PASS");
}
