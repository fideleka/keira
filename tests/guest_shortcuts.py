"""Production guest-shortcut persistence: expanded capacity and NVS byte boundaries."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MOCK = r'''
#pragma once
#include <Arduino.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
inline std::map<std::string,std::string> saved;
inline std::set<std::string> files;
class Preferences {
 std::string space;
public:
 bool begin(const char* name,bool){space=name;return true;}
 void end(){}
 size_t getString(const char* key,char* data,size_t capacity){
   assert(capacity==4000);auto value=saved[space+":"+key];
   if(value.size()+1>capacity)return 0;
   std::strcpy(data,value.c_str());return value.size()+1;
 }
 size_t putString(const char* key,const String& value){
   assert(value.length()<=3999);saved[space+":"+key]=value.c_str();return value.length();
 }
};
inline FILE* fixtureOpen(const char* path,const char*){
 return files.count(path)?std::tmpfile():nullptr;
}
namespace lilka {struct Files {String getSDRoot(){return "/sd";}};inline Files fileutils;}
'''
TEST = r'''
#include "mock.h"
#define fopen fixtureOpen
#include "guestshortcuts.cpp"
#undef fopen
String path(int index,size_t length=24){
 std::string p="/sd/"+std::to_string(index)+"-";
 p+=std::string(length-p.size()-4,'x');p+=".bin";files.insert(p);return p;
}
int main(){
 assert(readGuestShortcuts().empty());
 std::vector<String> history;
 for(int i=0;i<25;++i){rememberGuestShortcut(path(i),history);history=readGuestShortcuts();}
 assert(history.size()==20&&history.front()==path(24)&&history.back()==path(5));
 rememberGuestShortcut(path(10),history);history=readGuestShortcuts();
 assert(history.size()==20&&history.front()==path(10));
 assert(std::count(history.begin(),history.end(),path(10))==1);
 // Existing four-entry histories survive; the legacy single slot is migrated.
 saved.clear();std::string four;
 for(int i=0;i<4;++i){if(i)four+='\n';four+=path(i).c_str();}
 saved["keira:guest_fw"]=four;saved["lilka:multiboot_path"]=path(4).c_str();
 history=readGuestShortcuts();assert(history.size()==5&&history[0]==path(0)&&history[4]==path(4));
 assert(saved["lilka:multiboot_path"]==path(4).c_str());
 saved["keira:guest_fw"]="/missing.bin\nrelative.bin\n/sd/not.txt\n"+four+"\n"+path(0).c_str();
 assert(readGuestShortcuts().size()==5);
 // Exactly 3999 payload bytes fit. An over-budget candidate is skipped,
 // allowing a later shorter entry to remain, without dropping the newest app.
 saved.clear();history.clear();
 for(int i=0;i<20;++i)history.push_back(path(100+i,i==19?180:200));
 rememberGuestShortcut(history[0],history);
 assert(saved["keira:guest_fw"].size()==3999&&readGuestShortcuts()==history);
 const String newest=path(200,181);rememberGuestShortcut(newest,history);
 auto expanded=readGuestShortcuts();assert(expanded.size()==20&&expanded.front()==newest);
 assert(expanded.back()==history.back());assert(saved["keira:guest_fw"].size()<=3999);
 assert(std::find(expanded.begin(),expanded.end(),history[18])==expanded.end());
 files.erase(newest.c_str());assert(readGuestShortcuts().size()==19);
 puts("Guest shortcuts: 20 entries, recency/dedup, migration, missing files and 3999-byte boundary PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-guests-') as directory:
    tmp = Path(directory)
    string = (ROOT / 'tests/navigation/string.h').read_text().replace('};', '''
    bool startsWith(const String& s) const {return startsWith(s.c_str());}
    friend String operator+(const String& a,const String& b){return a.value+b.value;}
};''')
    (tmp / 'Arduino.h').write_text(string)
    for name in ('Preferences.h', 'lilka.h'):
        (tmp / name).write_text('#pragma once\n#include "mock.h"\n')
    (tmp / 'mock.h').write_text(MOCK)
    (tmp / 'test.cpp').write_text(TEST)
    for flags in ([], ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']):
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                        *flags, '-I'+str(tmp), '-I'+str(ROOT / 'src/apps/multiboot'),
                        str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
        subprocess.run([str(tmp / 'test')], check=True)
