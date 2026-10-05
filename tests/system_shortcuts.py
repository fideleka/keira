#!/usr/bin/env python3
"""Host-test actual GB/NES/tracker output functions and player's live gain expression."""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--sdk", type=Path, default=ROOT.parent / "sdk-system-shortcuts")
args = parser.parse_args()

def function(path, signature):
    text = path.read_text()
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]

sync = function(ROOT / "src/apps/soundsettings/sound.cpp", "bool SoundConfigApp::syncSystemVolume()")
gb = function(ROOT / "src/apps/gameboy/gameboyapp.cpp", "void GameBoyApp::writeAudio()")
nes = function(ROOT / "src/apps/nes/osd.cpp", "void do_audio_frame()")
tracker = function(ROOT / "src/apps/liltracker/i2s_sink.cpp", "size_t I2SSink::write(")
adjust = function(args.sdk / "lib/lilka/src/lilka/audio.cpp", "void Audio::adjustVolume(")
player = (ROOT / "src/keira/ksound/audioplayer.cpp").read_text()
player = player[player.index("void AudioPlayer::audioTaskFunc("):player.index("void AudioPlayer::stopInternal()")]
live_gain = player[player.index("const float outputGain ="):player.index("if (!self->generator->loop())")]
assert "lilka::audio.getVolume()" in live_gain
assert player.index(live_gain) < player.index("self->generator->loop()")
assert "volumeLevel" not in gb
prelude = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <vector>
#include "lilka/system_shortcuts.h"
#include "apps/nes/preferences.h"
using esp_err_t = int;
constexpr int ESP_OK = 0, portMAX_DELAY = -1;
#define pdMS_TO_TICKS(ms) (ms)
namespace lilka {
struct Audio {
 static int level;
 static int getVolume() { return level; }
 static void adjustVolume(void*, size_t, int, uint32_t);
};
int Audio::level = 50;
Audio audio;
}
std::vector<int16_t> output;
int writeLimit = 0;
namespace esp_i2s {
constexpr int I2S_NUM_0 = 0;
int i2s_write(int, const void* buffer, size_t size, size_t* written, int) {
 *written = writeLimit ? static_cast<size_t>(writeLimit) : size;
 const int16_t* samples = static_cast<const int16_t*>(buffer);
 output.insert(output.end(), samples, samples + *written / sizeof(int16_t));
 return ESP_OK;
}
}
using esp_i2s::i2s_write;
constexpr int HW_AUDIO_SAMPLERATE = 480, NES_REFRESH_RATE = 60, DEFAULT_FRAGSIZE = 4;
int16_t nesSamples[16];
int16_t* audio_frame = nesSamples;
void audio_callback(void* data, int count) {
 for(int i=0;i<count;++i) static_cast<int16_t*>(data)[i] = 1000;
}
size_t gbcore_render_audio(void*, int16_t* data) {
 for(int i=0;i<16;++i) data[i] = 1000;
 return 8;
}
struct GameBoyApp {
 int16_t buffer[16];
 int16_t* audioFrame = buffer;
 void* core = nullptr;
 bool audioReady = true;
 void stopAudio() { audioReady = false; }
 void writeAudio();
};
struct I2SSink { size_t write(const int16_t*, size_t); };
struct SoundConfigApp {
 int volumeLevel=50, observedSystemVolume=50;
 bool syncSystemVolume();
};
struct Output {float gain=0; int updates=0; void SetGain(float g) {gain=g; ++updates;}};
struct Player {
 Output sink;
 Output* output = &sink;
 float gain = 1;
 float appliedGain = -1;
 void sample() {
 Player* self = this;
'''
checks = r'''
 }
};
void checkSamples(int expected, size_t count) {
 assert(output.size() == count);
 for(auto sample : output) assert(sample == expected);
 output.clear();
}
int main() {
 SoundConfigApp settings; settings.volumeLevel=65;
 assert(!settings.syncSystemVolume() && settings.volumeLevel==65); // Preserve unsaved local edits.
 lilka::Audio::level=40;
 assert(settings.syncSystemVolume() && settings.volumeLevel==40); // New global change wins.
 assert(!settings.syncSystemVolume());
 lilka::Audio::level=50;
 GameBoyApp gb;
 gb.writeAudio(); checkSamples(500, 16);
 lilka::Audio::level=0; gb.writeAudio(); checkSamples(0,16);
 lilka::Audio::level=100; gb.writeAudio(); checkSamples(1000,16);
 lilka::Audio::level=50; do_audio_frame(); checkSamples(500,16);
 lilka::Audio::level=0; do_audio_frame(); checkSamples(0,16);
 lilka::Audio::level=100; do_audio_frame(); checkSamples(1000,16);
 I2SSink tracker; int16_t source[300];
 for(auto& sample:source) sample=1000;
 lilka::Audio::level=50; assert(tracker.write(source,300)==300); checkSamples(500,300);
 for(auto sample:source) assert(sample==1000); // Const input stays untouched.
 lilka::Audio::level=0; assert(tracker.write(source,300)==300); checkSamples(0,300);
 writeLimit=2; assert(tracker.write(source,300)==1); checkSamples(0,1); writeLimit=0;
 Player player; lilka::Audio::level=50; player.sample(); assert(player.sink.gain==0.5f);
 player.sample(); assert(player.sink.updates==1); // Unchanged master avoids redundant SetGain.
 lilka::Audio::level=0; player.sample(); assert(player.sink.gain==0);
 player.gain=2; lilka::Audio::level=100; player.sample(); assert(player.sink.gain==2);
 // Global suppression feeds the existing per-axis emulator filter, not its timers.
 lilka::detail::SystemShortcuts shortcut; nesmenu::DirectionFilter x, y;
 shortcut.scan(1<<8,100,true);
 auto result=shortcut.scan((1<<8)|1,120,true);
 assert(result.suppressed&1);
 assert(y.update(false, !(result.suppressed&1),120,true,500)==0);
 result=shortcut.scan(1,700,true); assert(result.suppressed&1);
 assert(y.update(false, !(result.suppressed&1),700,true,500)==0);
 shortcut.scan(0,720,true);
 result=shortcut.scan(1,740,true); assert(!(result.suppressed&1));
 assert(y.update(false,true,740,true,500)==1);
 assert(y.update(false,true,741,true,500)==0);
 assert(y.update(false,true,1240,true,500)==1);
 assert(x.update(true,false,740,true,200)==-1);
 assert(x.update(true,false,940,true,200)==-1);
 puts("Actual GB/NES/tracker live PCM, player master gain, emulator filter integration PASS");
}
'''
# DirectionFilter API is the production per-axis update method.
header=(ROOT / "src/apps/nes/preferences.h").read_text()
assert "class DirectionFilter" in header
with tempfile.TemporaryDirectory(prefix="keira-shortcuts-") as directory:
    tmp=Path(directory)
    (tmp / "test.cpp").write_text(prelude + live_gain + checks[:checks.index("void checkSamples")]
                                 + adjust.replace("void Audio::", "void lilka::Audio::") + gb + nes + tracker + sync + checks[checks.index("void checkSamples"):])
    for flags in ([], ["-fsanitize=address,undefined", "-fno-pie", "-no-pie"]):
        subprocess.run(["g++", "-std=c++11", "-Wall", "-Wextra", "-Wno-sign-compare", *flags,
                        "-I"+str(ROOT/"src"), "-I"+str(args.sdk/"lib/lilka/src"),
                        str(tmp/"test.cpp"), "-o", str(tmp/"regression")], check=True)
        subprocess.run([str(tmp/"regression")], check=True)
