#!/usr/bin/env python3
"""Execute GPIO Manager's production constructor/PWM guards against driver-owned pins."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/apps/gpiomanager/gpiomanager.cpp').read_text()
def function(signature):
    start=source.index(signature);opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
production='\n'.join(function(signature) for signature in [
    'GPIOManagerApp::GPIOManagerApp()', 'bool GPIOManagerApp::pinReserved(',
    'void GPIOManagerApp::updatePWM(', 'void GPIOManagerApp::startPWM(', 'void GPIOManagerApp::stopPWM('])
prelude=r'''
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
using std::min;
#define PIN_COUNT 6
#define INPUT 0
#define OUTPUT 1
#define LOW 0
#define PIN_MODE_PWM 5
#define PWM_DEFAULT_FREQ 2000
#define LEDC_TIMER_RESOLUTION 10
constexpr int K_BTN_BACK=1;
std::vector<int> writes;
void pinMode(int p,int){writes.push_back(p);}void digitalWrite(int p,int){writes.push_back(p);}
void ledcAttachPin(int p,int){writes.push_back(p);}void ledcDetachPin(int p){writes.push_back(p);}
unsigned ledcSetup(int channel,unsigned freq,int){writes.push_back(100+channel);return freq;}
void ledcWrite(int channel,unsigned){writes.push_back(100+channel);}
struct DriverManager {bool claimed=true;bool extensionPinOwned(int pin){return claimed&&(pin==12||pin==13||pin==14||pin==21);}};
DriverManager manager;struct {DriverManager*devices=&manager;}ksystem;
namespace lilka {
enum Button {A,B,C,D};
struct {bool isPWMChannelReserved(int){return false;}}brightness;
struct {template<class...T>void log(const char*,T...){}void err(const char*){}}serial;
}
struct App {App(const char*){}};
struct Menu{void setTitle(const char*){}void addActivationButton(int){}};
struct GPIOManagerApp:App {
 GPIOManagerApp();
 uint8_t pinNo[6]={12,13,14,21,47,48},pinM[6]={},pinData[6]={},pwmDutyCycle[6]={};
 unsigned pwmFrequency[6]={};struct Config{int channel,timer;}ledcConfigs[6]={{0,0},{2,1},{4,2},{6,3},{1,0},{3,1}};
 Menu menu;
 bool isValidPin(uint8_t i)const{return i<6;}
 bool pinReserved(uint8_t)const;
 void updatePWM(uint8_t);void startPWM(uint8_t,uint8_t);void stopPWM(uint8_t);
};
'''
checks=r'''
int main(){
 GPIOManagerApp app;assert((writes==std::vector<int>{47,48}));writes.clear();
 for(unsigned i=0;i<4;++i){app.startPWM(i,50);app.stopPWM(i);app.pinM[i]=PIN_MODE_PWM;app.updatePWM(i);}
 assert(writes.empty());
 app.startPWM(4,50);assert(!writes.empty());assert(writes[0]==47);writes.clear();
 manager.claimed=false;GPIOManagerApp free;assert(writes.size()==6);
 puts("PASS: GPIO constructor/PWM guards preserve driver pins; free pins retain normal behavior");
}
'''
with tempfile.TemporaryDirectory(prefix='keira-gpio-guard-') as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(prelude+production+checks)
    subprocess.run(['g++','-std=c++11','-fsanitize=address,undefined','-fno-pie','-no-pie',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
