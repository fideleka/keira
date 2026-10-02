// Uses the common controller/menu boundary plus actual recentroms.cpp and
// launcher menu methods. Only Preferences storage and fopen routing are mocked.
#include <cstdio>
#include <set>
std::map<std::string, String> savedPreferences;
std::set<std::string> availableFiles;
bool namespacePresent = false;
bool nvsLocked = false;
int begins = 0, firstUseFailures = 0;
void lockNvs() { assert(!nvsLocked); nvsLocked = true; }
void unlockNvs() { assert(nvsLocked); nvsLocked = false; }
#define NVS_LOCK lockNvs()
#define NVS_UNLOCK unlockNvs()
class Preferences {
public:
    bool begin(const char* name, bool readOnly) {
        assert(nvsLocked && std::string(name) == "keira");
        ++begins;
        if (readOnly && !namespacePresent) { ++firstUseFailures; return false; }
        namespacePresent = true;
        return true;
    }
    void end() { assert(nvsLocked); }
    size_t getString(const char* key, char* value, size_t capacity) {
        assert(nvsLocked && capacity == 4000);
        auto found = savedPreferences.find(key);
        if (found == savedPreferences.end() || found->second.length() + 1 > capacity) return 0;
        std::strcpy(value, found->second.c_str());
        return found->second.length() + 1;
    }
    size_t putString(const char* key, const String& value) {
        assert(nvsLocked && value.length() <= 3999);
        savedPreferences[key] = value;
        return value.length();
    }
};
FILE* fixtureOpen(const char* path, const char*) {
    if (!availableFiles.count(path)) return nullptr;
    return std::tmpfile();
}
// REAL_HISTORY

bool failNextLaunch = false;
bool removeOnReturn = false;
void launch(const String& path, bool nes) {
    assert((romSystemForPath(path) == RomSystem::NES) == nes);
    // Child's successful exit precedes launcher resume/refresh; use actual writer.
    if (!failNextLaunch) rememberRecentRom(path);
    if (removeOnReturn) availableFiles.erase(path.c_str());
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
// LAUNCHER_METHODS
int millis() { return 0; }

int main() {
    LauncherApp launcher;
    ITEM_LIST apps{ITEM::APP("Ordinary", nullptr)};
    // First-use absent namespace takes Preferences.begin's failure branch.
    launcher.refreshRecentRomFolders(apps);
    assert(apps.size() == 1 && firstUseFailures == 3 && !nvsLocked);
    namespacePresent = true;
    for (const char* key : {"recent_nes", "recent_gb", "recent_gbc"}) savedPreferences[key] = "";
    launcher.refreshRecentRomFolders(apps);
    assert(apps.size() == 1);
    savedPreferences["recent_nes"] = "/sd/missing.nes\nrelative.nes\n/sd/wrong.gb\n/sd/file.txt";
    assert(readRecentRoms(RomSystem::NES).empty());
    savedPreferences["recent_nes"] = std::string(4000, 'x').c_str();
    assert(readRecentRoms(RomSystem::NES).empty()); // Oversized persisted value rejected, not erased.
    assert(savedPreferences["recent_nes"].length() == 4000);
    // 20 independently valid entries in each history, all 60 titles owned.
    for (const auto& info : {std::make_pair("recent_nes", ".nes"),
                             std::make_pair("recent_gb", ".gb"),
                             std::make_pair("recent_gbc", ".gbc")}) {
        String saved;
        for (int i = 0; i < 20; ++i) {
            std::string path = "/sd/folder/" + std::to_string(i) + info.second;
            availableFiles.insert(path);
            if (i) saved += '\n';
            saved += String(path);
        }
        savedPreferences[info.first] = saved;
    }
    launcher.refreshRecentRomFolders(apps);
    assert(apps.size() == 4);
    for (int i = 0; i < 3; ++i) assert(apps[i].submenu.size() == 20);
    ITEM_LIST ownedCopy = apps;
    // Play non-top game via actual nested Applications -> emulator menu, then
    // inspect still-open menu immediately after real history writer/read path.
    const char* folders[] = {K_S_LAUNCHER_NES_FOLDER, K_S_LAUNCHER_GB_FOLDER, K_S_LAUNCHER_GBC_FOLDER};
    const char* extensions[] = {".nes", ".gb", ".gbc"};
    for (int system = 0; system < 3; ++system) {
        String played = String(std::string("7") + extensions[system]);
        events = {{"Applications", system, 0, nullptr},
                  {folders[system], 7, 0, nullptr},
                  {folders[system], 0, 1, [&](const auto& titles, int cursor) {
                      assert(cursor == 0 && titles.size() == 21 && titles.front() == played);
                  }},
                  {"Applications", 0, 1, [&](const auto& titles, int) { assert(titles.size() == 5); }}};
        nextEvent = 0;
        launcher.showMenu("Applications", apps, true, LauncherMenuKind::Applications);
        assert(apps[system].submenu.front().name == played);
    }
    assert(ownedCopy[0].submenu[7].name == "7.nes");
    auto repeated = savedPreferences["recent_nes"];
    for (int i = 0; i < 10; ++i) rememberRecentRom("/sd/folder/7.nes");
    assert(savedPreferences["recent_nes"] == repeated && readRecentRoms(RomSystem::NES).size() == 20);
    failNextLaunch = true;
    events = {{"Failed", 1, 0, nullptr}, {"Failed", 0, 1, nullptr}};
    nextEvent = 0;
    ITEM_LIST failed;
    launcher.showMenu("Failed", failed, true, LauncherMenuKind::NES);
    assert(savedPreferences["recent_nes"] == repeated);
    failNextLaunch = false;
    removeOnReturn = true;
    events = {{"Removed", 0, 0, nullptr}, {"Removed", 0, 1, [&](const auto& titles, int cursor) {
        assert(cursor == 0 && titles.size() == 20 && titles.front() == "0.nes");
    }}};
    nextEvent = 0;
    launcher.showMenu("Removed", failed, true, LauncherMenuKind::NES);
    removeOnReturn = false;
    assert(readRecentRoms(RomSystem::NES).size() == 19);

    // Writer bounds saved data to 3999 bytes and 20 rows even at max path length.
    std::string longPath = "/sd/" + std::string(247, 'x') + ".nes";
    assert(longPath.size() == 255);
    availableFiles.insert(longPath);
    std::string tooLong = "/sd/" + std::string(248, 'x') + ".nes";
    availableFiles.insert(tooLong);
    savedPreferences["recent_nes"] = tooLong.c_str();
    assert(readRecentRoms(RomSystem::NES).empty());
    rememberRecentRom(longPath.c_str());
    assert(readRecentRoms(RomSystem::NES).front() == String(longPath));
    auto before = savedPreferences["recent_nes"];
    rememberRecentRom("/sd/missing.nes");
    assert(savedPreferences["recent_nes"] == before);
    String crowded;
    for (int i = 0; i < 20; ++i) {
        std::string path = "/sd/" + std::string(183, 'a' + i) + std::to_string(i) + ".nes";
        if (path.size() > 255) path.erase(4, path.size() - 255);
        availableFiles.insert(path);
        if (i) crowded += '\n';
        crowded += String(path);
    }
    assert(crowded.length() <= 3999);
    savedPreferences["recent_nes"] = crowded;
    rememberRecentRom(longPath.c_str());
    assert(savedPreferences["recent_nes"].length() <= 3999);
    assert(readRecentRoms(RomSystem::NES).size() <= 20);
    // Duplicates, missing files and cross-system extensions are filtered by actual parser.
    availableFiles.insert("/sd/duplicate.nes");
    savedPreferences["recent_nes"] = "/sd/duplicate.nes\n/sd/duplicate.nes\n/sd/missing.nes\n/sd/wrong.gb";
    assert(readRecentRoms(RomSystem::NES).size() == 1);
    launcher.refreshRecentRomFolders(apps);
    assert(apps[0].submenu.size() == 1 && ownedCopy[0].submenu.size() == 20);
    assert(!nvsLocked && begins > 10);
    std::cout << "actual NVS/history/menu pipeline passed\n";
}
