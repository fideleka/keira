"""Fixed-size measurement windows: cadence, long gaps, reset and concurrent writers."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 t=Path(d);(t/'freertos').mkdir()
 (t/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <mutex>
static std::mutex hostMutex;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) hostMutex.lock()
#define portEXIT_CRITICAL(p) hostMutex.unlock()
''')
 (t/'esp_timer.h').write_text('#pragma once\n')
 (t/'test.cpp').write_text('''#define KEIRA_MEASURE 1
#include "keira/measurement.h"
#include <cassert>
#include <thread>
#include <vector>
int main(){measurement::Window w;measurement::Snapshot s;
 w.poll(1000);w.poll(17000);w.poll(57001);w.poll(157002);w.changed();w.submitted(17000);w.take(s);
 assert(s.firstPollUs==1000&&s.lastPollUs==157002&&s.firstSubmitUs==17000);
 assert(s.polls==4&&s.pollGap.count==3&&s.pollGap.maxUs==100001);
 assert(s.gapOver33==2&&s.gapOver50==1&&s.gapOver100==1&&s.changes==1&&s.submissions==1);
 w.poll(257003);w.take(s);assert(s.polls==1&&s.pollGap.count==1&&s.pollGap.maxUs==100001&&!s.changes);
 std::vector<std::thread> workers;
 for(unsigned j=0;j<4;++j)workers.emplace_back([&]{for(unsigned i=0;i<10000;++i){w.record(measurement::Audio,100);w.submitted(17000);}});
 for(auto&x:workers)x.join();w.take(s);
 assert(s.phase[measurement::Audio].count==40000&&s.phase[measurement::Audio].totalUs==4000000&&s.submissions==40000);
 w.take(s);assert(!s.polls&&!s.submissions&&!s.phase[measurement::Audio].count);
}
''')
 subprocess.run(['g++','-std=c++11','-pthread','-fsanitize=address,undefined','-I'+str(t),'-I'+str(ROOT/'src'),str(t/'test.cpp'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True)
print('PASS: cadence, long-gap bins across windows, reset, 64-bit totals and concurrent phase writers')
