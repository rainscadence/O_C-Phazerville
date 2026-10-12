#include "ii/HostAction.h"
#include <cassert>
#include <cstdio>
using namespace phz::ii;
int main() {
  uint8_t frame[8]{};
  HostAction result{HostActionKind::LoadPreset, 999, 777};
  auto reject = [&](const uint8_t* bytes, size_t size) {
    result = {HostActionKind::LoadPreset, 999, 777};
    assert(!DecodeHostAction(bytes, size, result));
    assert(result.kind == HostActionKind::LoadPreset && result.target == 999 && result.applet == 777);
  };
  frame[0]=7;frame[2]=1;frame[4]=56;
  reject(nullptr,7);
  for(size_t n=0;n<=8;++n)if(n!=7)reject(frame,n);
  for(unsigned id=0;id<=65535;++id) {
    frame[3]=id>>8;frame[4]=id;
    for(unsigned slot=1;slot<=4;++slot) {
      frame[2]=slot;
      if(!id)reject(frame,7);
      else {
        assert(DecodeHostAction(frame,7,result));
        assert(result.kind==HostActionKind::SelectApplet&&result.target==slot&&result.applet==id);
      }
    }
  }
  frame[3]=0;frame[4]=56;
  for(unsigned slot=0;slot<=65535;++slot) {
    frame[1]=slot>>8;frame[2]=slot;
    if(slot<1||slot>4)reject(frame,7);
  }
  frame[0]=8;frame[3]=frame[4]=0;
  for(unsigned preset=0;preset<=65535;++preset) {
    frame[1]=preset>>8;frame[2]=preset;
    if(preset>31)reject(frame,7);
    else {assert(DecodeHostAction(frame,7,result));assert(result.target==preset&&result.applet==0&&result.kind==HostActionKind::LoadPreset);}
  }
  frame[1]=frame[2]=0;
  for(unsigned r=1;r<=65535;++r) {
    frame[3]=r>>8;frame[4]=r;reject(frame,7);
    frame[3]=frame[4]=0;frame[5]=r>>8;frame[6]=r;reject(frame,7);
    frame[0]=7;frame[2]=1;frame[4]=56;reject(frame,7);
    frame[0]=8;frame[2]=frame[4]=frame[5]=frame[6]=0;
  }
  for(unsigned command=0;command<256;++command) {
    frame[0]=command;if(command!=7&&command!=8)reject(frame,7);
  }
  puts("PASS: host action decoder, full word domains, exact framing and unchanged output on rejection");
}
