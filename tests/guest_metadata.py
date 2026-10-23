"""Actual metadata reader, real ArduinoJson, bounded icon input and launcher wiring."""
from pathlib import Path
import json
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
JSON = ROOT / '.pio/libdeps/v2/ArduinoJson/src'
SOURCE = ROOT / 'src/apps/multiboot'
HARNESS = r'''
#include "appmetadata.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
using menu_icon_t=const uint16_t[576];
const menu_icon_t app_img={};
namespace lilka {namespace colors {constexpr uint16_t White=0xffff;}}
// ACTUAL_ITEMS
struct MultiBootApp{};
std::vector<String> fixtureGuests;
std::vector<String> readGuestShortcuts(){return fixtureGuests;}
class LauncherApp {
public:
 std::vector<GuestAppMetadata> guestMetadata_;
 String launched;
 template<class T>void runApp(const String&path){launched=path;}
 ITEM_LIST buildMenu();
};
ITEM_LIST LauncherApp::buildMenu(){
 ITEM_LIST appsItems;
 // ACTUAL_GUEST_MENU
}
int main(int argc,char**argv){
 assert(argc==5);GuestAppMetadata m;
 readGuestAppMetadata(argv[1],m);
 assert(m.name==argv[2]);assert(m.hasIcon==(argv[3][0]=='1'));
 if(m.hasIcon){FILE*f=fopen(argv[4],"rb");assert(f);unsigned char p[1152];
   assert(fread(p,1,sizeof(p),f)==sizeof(p));fclose(f);
   for(unsigned i=0;i<576;++i)assert(m.pixels[i]==(p[2*i]|(unsigned(p[2*i+1])<<8)));
 }
 fixtureGuests={argv[1]};LauncherApp launcher;auto items=launcher.buildMenu();
 assert(items.size()==1&&items[0].name==m.name);
 if(m.hasIcon){assert(items[0].icon==&launcher.guestMetadata_[0].pixels);
   assert(!memcmp(*items[0].icon,m.pixels,sizeof(m.pixels)));}
 else assert(items[0].icon==&app_img);
 items[0].callback();assert(launcher.launched==argv[1]);
 readGuestAppMetadata("/missing/unavailable.bin",m);
 assert(m.name=="unavailable"&&!m.hasIcon); // No stale prior icon/name.
}
'''
with tempfile.TemporaryDirectory(prefix='keira-metadata-') as directory:
    tmp = Path(directory)
    string = (ROOT / 'tests/navigation/string.h').read_text().replace('};', '''
    void remove(int at) {value.erase(at);}
    bool equalsIgnoreCase(const char* other) const {String a=*this,b=other;a.toLowerCase();b.toLowerCase();return a==b;}
    friend String operator+(const String& a,const String& b){return a.value+b.value;}
};''')
    (tmp / 'Arduino.h').write_text(string)
    header = (ROOT/'src/apps/launcher/launcher.h').read_text()
    items = header[header.index('enum class LauncherMenuKind'):header.index('class LauncherApp')]
    source = (ROOT/'src/apps/launcher/launcher.cpp').read_text()
    start = source.index('    const std::vector<String> guests = readGuestShortcuts();')
    stop = source.index('    return appsItems;', start) + len('    return appsItems;')
    (tmp / 'test.cpp').write_text(HARNESS.replace('// ACTUAL_ITEMS', items)
                                .replace('// ACTUAL_GUEST_MENU', source[start:stop]))
    fixture = tmp / 'app'
    fixture.mkdir()
    icon = fixture / 'icon.rgb565'
    raw = bytes((i % 256 for i in range(1152)))
    icon.write_bytes(raw)
    metadata = fixture / 'app.json'
    base = {'schema': 1, 'name': 'Custom app', 'firmware': 'doom.bin', 'icon': 'icon.rgb565'}
    for flags in ([], ['-fsanitize=address,undefined', '-fno-pie', '-no-pie']):
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-sign-compare',
                        *flags, '-I'+str(tmp), '-I'+str(SOURCE), '-I'+str(JSON),
                        str(SOURCE/'appmetadata.cpp'), str(tmp/'test.cpp'), '-o', str(tmp/'test')], check=True)

        def check(name='Doom', valid=False):
            subprocess.run([str(tmp/'test'), str(fixture/'doom.bin'), name, str(int(valid)), str(icon)], check=True)

        metadata.unlink(missing_ok=True)
        check()
        for content in ('broken JSON', '[]', '{}', json.dumps({**base, 'schema': 2}),
                        json.dumps({**base, 'firmware': 'other.bin'}), ' ' * 1025):
            metadata.write_text(content)
            check()
        for invalid in (None, '', 'x'*49, 'bad\nname'):
            metadata.write_text(json.dumps({**base, 'name': invalid}))
            check('Doom', True)
        for invalid in ('../icon.rgb565', '/icon.rgb565', 'dir\\icon.rgb565', '..', 'C:icon.rgb565', None):
            metadata.write_text(json.dumps({**base, 'icon': invalid}))
            check('Custom app', False)
        metadata.write_text(json.dumps(base))
        for payload in (b'', raw[:-1], raw+b'X'):
            icon.write_bytes(payload)
            check('Custom app', False)
        icon.write_bytes(raw)
        check('Custom app', True)
        metadata.write_text(json.dumps({**base, 'name': 'Відео'}))
        check('Відео', True)
        for repo in ('keira', 'doom_port', 'lasertank-lilka', 'lilka-obd-scanner', 'lilplayer', 'lilka-video-player'):
            path = ROOT.parent / repo
            info = json.loads((path/'app.json').read_text())
            subprocess.run([str(tmp/'test'), str(path/info['firmware']), info['name'], '1',
                            str(path/'icon.rgb565')], check=True)
source = (ROOT/'src/apps/launcher/launcher.cpp').read_text()
assert 'readGuestAppMetadata(path, guestMetadata_[guestIndex++])' in source
assert 'metadata.hasIcon ? &metadata.pixels : nullptr' in source
assert source.index('guestMetadata_.resize(guests.size())') < source.index('metadata.hasIcon ?')
print('Guest metadata: actual parser, bounds/fallback, UTF-8 and all six shipped icons PASS (normal + ASan/UBSan)')
