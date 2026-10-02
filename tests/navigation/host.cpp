#include <Arduino.h>
#include "keira/utils/navigationpath.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <vector>

using menu_icon_t = int;
const int app_img = 1, nes_img = 2, app_group_img = 3;
constexpr int K_BTN_BACK = 1, K_BTN_EXIT = 1;
#define K_S_MENU_BACK "Localized back"
#define K_S_LAUNCHER_NES_FOLDER "Localized NES"
#define K_S_LAUNCHER_GB_FOLDER "Localized GB"
#define K_S_LAUNCHER_GBC_FOLDER "Localized GBC"
struct Event { String title; int cursor; int button; std::function<void(const std::vector<String>&, int)> check; };
std::vector<Event> events;
size_t nextEvent = 0;
int millis();
void vTaskDelay(int) {}
#define MENU_HEIGHT 5
#define LILKA_UI_UPDATE_DELAY_MS 10
#define portTICK_PERIOD_MS 1
namespace lilka {
enum Button { A, B, C, D, SELECT, START, UP, DOWN, LEFT, RIGHT, COUNT };
struct Press { bool justPressed = false; };
struct State { Press a, b, c, d, select, start, up, down, left, right; };
using _StateButtons = Press[10];
struct Controller { State state; State getState() { return state; } } controller;
namespace colors { constexpr int White = 0, Candy_pink = 1, Black = 2; }
struct MenuItem { String title; const int* icon = nullptr; int color = 0; String postfix; std::function<void(void*)> callback; void* callbackData = nullptr; };
class Menu {
    String title;
    std::vector<MenuItem> items;
    bool done = false;
    int cursor = 0, scroll = 0, lastCursorMove = 0;
    Button button = Button::COUNT;
    std::vector<Button> activationButtons{Button::A};
public:
    explicit Menu(const String& t) : title(t) {}
    void clearItems() { items.clear(); }
    void setTitle(const String& t) { title = t; }
    int getScroll() const { return scroll; }
    void addActivationButton(int b) { activationButtons.push_back(static_cast<Button>(b)); }
    void addItem(const String& t, const int* = nullptr, int = 0, const String& = "", void(*)(void*) = nullptr, void* = nullptr) { MenuItem item; item.title = t; items.push_back(item); }
    void getItem(int i, MenuItem* item) { *item = items.at(i); }
    void setItem(int i, const String& t, const int*, int, const String&) { items.at(i).title = t; }
    void setCursor(int16_t i);
    void sdkUpdate();
    int getCursor() const { return cursor; }
    int getButton() const { return static_cast<int>(button); }
    bool isFinished() { bool result = done; done = false; return result; }
    void update() {
        assert(nextEvent < events.size());
        const auto& event = events.at(nextEvent++);
        assert(title == event.title);
        std::vector<String> titles;
        for (const auto& item : items) titles.push_back(item.title);
        if (event.check) event.check(titles, cursor);
        setCursor(event.cursor);
        controller.state = {};
        reinterpret_cast<_StateButtons*>(&controller.state)[0][event.button].justPressed = true;
        sdkUpdate();
        controller.state = {};
    }
    void draw(void*) {}
};
struct FileUtils {
    String getHumanFriendlySize(long) { return "size"; }
    String joinPath(const String& a, const String& b) { String s = a; s += "/"; s += b; return s; }
    String getParentDirectory(const String& p) { int i = p.lastIndexOf('/'); return i <= 0 ? String("/") : p.substring(0, i); }
} fileutils;
// SDK_METHODS
struct Serial { template<class... T> void log(T...) {} } serial;
}
// PRODUCTION_ITEMS

enum class RomSystem { NES, GameBoy, GameBoyColor };
std::map<RomSystem, std::vector<String>> histories;
std::vector<String> readRecentRoms(RomSystem s) { return histories[s]; }
struct KeiraThread { const char* getName() { return "Game"; } };
using TaskHandle_t = int;
TaskHandle_t xTaskGetCurrentTaskHandle() { return 1; }
std::function<void()> gameOnResume;
bool suspended = false;
void vTaskSuspend(TaskHandle_t) {
    suspended = true;
    // Represents the app running and writing last-played only before resume.
    gameOnResume();
    suspended = false;
}
#define KMTX_LOCK(x) ((void)0)
#define KMTX_UNLOCK(x) ((void)0)
#define K_TMG_DBG if (false)
class ThreadManager {
public:
    std::vector<KeiraThread*> threadsToRun;
    void spawn(KeiraThread* thread, bool autoSuspend = true);
};
ThreadManager manager;
std::vector<String> launches;
bool failLaunch = false;
void launch(const String& path, bool nes) {
    RomSystem system = nes ? RomSystem::NES : (path.c_str()[path.length()-1] == 'c' ? RomSystem::GameBoyColor : RomSystem::GameBoy);
    launches.push_back(path);
    gameOnResume = [=]() {
        assert(suspended);
        if (!failLaunch) {
            auto& history = histories[system];
            history.erase(std::remove(history.begin(), history.end(), path), history.end());
            history.insert(history.begin(), path);
        }
    };
    KeiraThread game;
    manager.spawn(&game);
    manager.threadsToRun.clear();
}
#define K_FT_NES_HANDLER(p) launch(p, true)
#define K_FT_GB_HANDLER(p) launch(p, false)
class LauncherApp {
public:
    void* canvas = nullptr;
    void queueDraw() {}
    void showMenu(const String&, ITEM_LIST&, bool = true, LauncherMenuKind = LauncherMenuKind::Static);
    void refreshRecentRomItems(ITEM_LIST&, LauncherMenuKind);
    void refreshRecentRomFolders(ITEM_LIST&);
};
enum { FT_NONE, FT_NES_ROM, FT_GB_ROM, FT_BIN, FT_LUA_SCRIPT, FT_JS_SCRIPT, FT_SOUND, FT_LT, FT_SO, FT_IMAGE, FT_DIR, FT_OTHER };
struct FMEntry { int type = FT_DIR; char path[256] = {}; char name[256] = {}; bool selected = false; long st_size = 0; const int* icon = nullptr; int color = 0; };
enum { FM_MODE_VIEW, FM_MODE_SELECT, FM_MODE_RELOAD };
#define FM_DBG if (false)
#define FM_RELOAD_BUTTON 2
#define FM_EXIT_BUTTON 1
#define FM_SELECT_BUTTON 3
#define FM_INFO_BUTTON 4
#define FM_OKAY_BUTTON 0
#define FM_OPTIONS_MENU_BUTTON 5
#define K_S_FMANAGER_ARE_YOU_SURE_ALERT "sure"
#define K_S_FMANAGER_SELECTED_ENTRIES_EXIT_FMT "exit"
#define K_FT_BIN_HANDLER(p) ((void)0)
#define K_FT_LUA_SCRIPT_HANDLER(p) ((void)0)
#define K_FT_JS_SCRIPT_HANDLER(p) ((void)0)
#define K_FT_SOUND_HANDLER(p) ((void)0)
#define K_FT_LT_HANDLER(p) ((void)0)
#define K_FT_SO_HANDLER(p) ((void)0)
#define K_FT_IMAGE_HANDLER(p) ((void)0)
#define FT_DEFAULT_DIR_HANDLER currentPath = path;
#define FT_DEFAULT_OTHER_HANDLER fileInfoShowAlert();
template<class... T> String StringFormat(T...) { return ""; }
#define PROGRESS_FILE_LIST_NO_DRAW_COUNT 10
#define PROGRESS_FRAME_TIME 30
#define FT_DIR_ICON (&app_group_img)
#define FT_DIR_COLOR 0
#define FM_SELECTED_FOLDER_ICON (&app_group_img)
#define FM_SELECTED_FILE_ICON (&app_group_img)
#define ENTRY_NOT_FOUND_INDEX 65535
#define FM_MODE_RESET changeMode(FM_MODE_VIEW)
#define K_S_ERROR "Error"
#define K_S_CANT_OPEN_DIR_FMT "Can't open"
#define K_S_FMANAGER_SORTING "Sorting"
#define K_S_FMANAGER_ALMOST_DONE "Almost done"
#define LILKA_MENU_CLBK_CAST(x) nullptr
#define LILKA_MENU_CLBK_DATA_CAST(x) nullptr
int millis() { return 0; }
struct Canvas { void fillScreen(int) {} };
struct Progress {
    void setMessage(const String&) {}
    void setProgress(int) {}
    void draw(Canvas*) {}
};
class FileManagerApp {
public:
    String currentPath = "/", initalPath = "/", parentReturnPath;
    std::vector<FMEntry> currentDirEntries, selectedDirEntries;
    FMEntry currentEntry;
    lilka::Menu fileListMenu{"Files"};
    int mode = FM_MODE_VIEW;
    bool exitChildDialogs = false;
    Canvas canvasStorage;
    Canvas* canvas = &canvasStorage;
    Progress dirLoadProgress;
    int progress = 0, lastProgress = -1, lastFrameTime = 0;
    void queueDraw() {}
    void alert(const String&, const String&) {}
    bool fileListMenuLoadDir();
    static int getDirEntryIndex(const std::vector<FMEntry>&, const FMEntry&) { return ENTRY_NOT_FOUND_INDEX; }
    bool navigateToParent();
    int parentReturnCursor() const;
    void onFileListMenuItem();
    void openCurrentEntry();
    static FMEntry pathToEntry(const String& path) {
        FMEntry e;
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) e.type = FT_OTHER;
        String parent = lilka::fileutils.getParentDirectory(path);
        std::strcpy(e.path, parent.c_str());
        std::strcpy(e.name, path.substring(path.lastIndexOf('/') + 1).c_str());
        return e;
    }
    bool isCurrentDirSelected() { return fileListMenu.getCursor() == static_cast<int>(currentDirEntries.size()) || std::strcmp(currentEntry.name, ".") == 0; }
    void changeMode(int m) { mode = m; }
    bool confirm(const String&, const String&) { return true; }
    void selectCurrentEntry() {}
    void deselectCurrentEntry() {}
    void fileInfoShowAlert() {}
    void fileSelectionOptionsMenuShow() {}
    void fileOptionsMenuShow() {}
};
// PRODUCTION_METHODS

int main(int argc, char** argv) {
    assert(argc == 2);
    LauncherApp app;
    for (auto kind : {LauncherMenuKind::NES, LauncherMenuKind::GameBoy, LauncherMenuKind::GameBoyColor}) {
        RomSystem system = kind == LauncherMenuKind::NES ? RomSystem::NES : kind == LauncherMenuKind::GameBoy ? RomSystem::GameBoy : RomSystem::GameBoyColor;
        const char* ext = system == RomSystem::NES ? ".nes" : system == RomSystem::GameBoy ? ".gb" : ".gbc";
        String a = "/sd/first/one"; a += ext;
        String b = "/sd/other/two"; b += ext;
        histories[system] = {a, b};
        ITEM_LIST list;
        events = {{"Whatever translation", 1, 0, [&](const auto& titles, int cursor) { assert(cursor == 0); assert(titles.size() == 3); }},
                  {"Whatever translation", 0, 0, [&](const auto& titles, int cursor) { assert(histories[system].front() == b); assert(cursor == 0); assert(titles[0] == b.substring(b.lastIndexOf('/')+1)); }},
                  {"Whatever translation", 2, 0, [&](const auto& titles, int cursor) { assert(titles.size() == 3); assert(cursor == 0); }}};
        nextEvent = 0;
        app.showMenu("Whatever translation", list, true, kind);
        assert(launches[launches.size()-1] == b && launches[launches.size()-2] == b);
        assert(histories[system].size() == 2);
    }
    ITEM_LIST apps = {ITEM::APP("Ordinary", [] {})};
    app.refreshRecentRomFolders(apps);
    auto copy = apps;
    for (int i = 0; i < 30; ++i) app.refreshRecentRomFolders(apps);
    assert(apps.size() == 4 && apps.back().name == "Ordinary");
    assert(copy[0].submenu[0].name == apps[0].submenu[0].name);
    // Nested execution, parent reappears with refreshed paths and no duplicate folders.
    auto chosen = histories[RomSystem::NES][1];
    events = {{"Apps translated", 0, 0, nullptr}, {K_S_LAUNCHER_NES_FOLDER, 1, 0, nullptr},
              {K_S_LAUNCHER_NES_FOLDER, 0, 1, [&](const auto& titles, int cursor) { assert(cursor == 0); assert(titles[0] == chosen.substring(chosen.lastIndexOf('/')+1)); }},
              {"Apps translated", 0, 1, [&](const auto& titles, int cursor) { assert(titles.size() == 5); assert(cursor == 0); assert(apps[0].submenu[0].name == chosen.substring(chosen.lastIndexOf('/')+1)); }}};
    nextEvent = 0;
    app.showMenu("Apps translated", apps, true, LauncherMenuKind::Applications);
    // Removed history/failure does not create a bogus recency entry.
    failLaunch = true;
    auto before = histories[RomSystem::NES];
    events = {{"Failed", 1, 0, nullptr}, {"Failed", 0, 1, nullptr}};
    nextEvent = 0;
    ITEM_LIST failed;
    app.showMenu("Failed", failed, true, LauncherMenuKind::NES);
    assert(histories[RomSystem::NES] == before);
    histories[RomSystem::NES].clear();
    app.refreshRecentRomFolders(apps);
    assert(apps.size() == 3 && copy[0].submenu[0].name.length() > 0);

    assert(canonicalNavigationPath("/sd//a/./b/../c/") == "/sd/a/c");
    assert(canonicalNavigationPath("/../../") == "/");
    assert(canonicalNavigationPath("relative").isEmpty());
    FileManagerApp fm;
    fm.fileListMenu.addActivationButton(FM_EXIT_BUTTON);
    fm.fileListMenu.addActivationButton(FM_RELOAD_BUTTON);
    fm.currentPath = "/sd/games/target/";
    assert(fm.navigateToParent());
    assert(fm.currentPath == "/sd/games" && fm.parentReturnPath == "/sd/games/target");
    for (const char* name : {".hidden", "a", "b", "c", "d", "e", "f", "target", "z"}) {
        auto e = FileManagerApp::pathToEntry(lilka::fileutils.joinPath(fm.currentPath, name));
        fm.currentDirEntries.push_back(e);
        fm.fileListMenu.addItem(name);
    }
    assert(fm.parentReturnCursor() == 7);
    fm.currentDirEntries[7].type = FT_OTHER;
    assert(fm.parentReturnCursor() == 0);
    fm.currentDirEntries[7].type = FT_DIR;
    std::strcpy(fm.currentDirEntries[7].path, "/sd/sibling");
    assert(fm.parentReturnCursor() == 0); // Same basename, different directory.
    std::strcpy(fm.currentDirEntries[7].path, "/sd/games/");
    assert(fm.parentReturnCursor() == 7);
    fm.currentDirEntries.pop_back();
    fm.currentDirEntries.pop_back();
    assert(fm.parentReturnCursor() == 0); // Missing/renamed/filtered child.
    // Execute actual B and explicit Back-entry handlers independently.
    for (int button : {FM_EXIT_BUTTON, FM_OKAY_BUTTON}) {
        fm.currentPath = "/sd/games/target";
        fm.currentDirEntries.clear();
        fm.fileListMenu.clearItems();
        fm.fileListMenu.addItem("Back");
        events = {{"Files", 0, button, nullptr}}; nextEvent = 0;
        fm.fileListMenu.update(); fm.fileListMenu.isFinished();
        fm.onFileListMenuItem();
        assert(fm.currentPath == "/sd/games" && fm.parentReturnPath == "/sd/games/target");
        assert(fm.navigateToParent() && fm.parentReturnPath == "/sd/games");
        assert(fm.navigateToParent() && fm.currentPath == "/");
        assert(!fm.navigateToParent());
    }
    fm.currentPath = "/sd"; fm.initalPath = "/sd/";
    assert(!fm.navigateToParent());
    // Execute the real directory loader against a temporary fixture: sorting,
    // hidden entries, one-shot cursor restoration, actual menu rebuild and Back.
    std::filesystem::path fixture = std::filesystem::path(argv[1]) / "files";
    std::filesystem::create_directory(fixture);
    for (const char* name : {".hidden", "a", "b", "c", "d", "e", "f", "target", "z"})
        std::filesystem::create_directory(fixture / name);
    std::ofstream(fixture / "ordinary.txt") << "file";
    fm.initalPath = "/";
    fm.currentPath = (fixture / "target").string().c_str();
    assert(fm.navigateToParent());
    assert(fm.fileListMenuLoadDir());
    assert(fm.fileListMenu.getCursor() == 7);
    assert(std::strcmp(fm.currentDirEntries[7].name, "target") == 0);
    assert(fm.parentReturnPath.isEmpty());
    events = {{fixture.string().c_str(), 7, FM_RELOAD_BUTTON, nullptr}}; nextEvent = 0;
    fm.fileListMenu.update();
    assert(fm.fileListMenu.getScroll() <= 7 && fm.fileListMenu.getScroll() + 4 >= 7);
    fm.fileListMenu.isFinished();
    fm.onFileListMenuItem();
    assert(fm.fileListMenuLoadDir());
    assert(fm.fileListMenu.getCursor() == 0); // Ordinary refresh behavior unchanged.
    fm.parentReturnPath = (fixture / "missing").string().c_str();
    assert(fm.fileListMenuLoadDir() && fm.fileListMenu.getCursor() == 0);
    // Ordinary directory entry opens its own listing at the default first entry.
    events = {{fixture.string().c_str(), 7, FM_OKAY_BUTTON, nullptr}}; nextEvent = 0;
    fm.fileListMenu.update(); fm.fileListMenu.isFinished();
    fm.onFileListMenuItem();
    assert(fm.currentPath == String((fixture / "target").string()));
    assert(fm.fileListMenuLoadDir() && fm.fileListMenu.getCursor() == 0);
    // Parent navigation also works while selecting files, without clearing selections.
    fm.mode = FM_MODE_SELECT;
    fm.selectedDirEntries.push_back(FileManagerApp::pathToEntry((fixture / "ordinary.txt").string().c_str()));
    events = {{(fixture / "target").string().c_str(), 0, FM_EXIT_BUTTON, nullptr}}; nextEvent = 0;
    fm.fileListMenu.update(); fm.fileListMenu.isFinished();
    fm.onFileListMenuItem();
    assert(fm.fileListMenuLoadDir() && fm.fileListMenu.getCursor() == 7);
    assert(fm.selectedDirEntries.size() == 1);
    std::cout << "production menu/navigation regression passed\n";
}
