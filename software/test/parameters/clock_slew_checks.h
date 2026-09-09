#pragma once
#include "applets/ClockDivider.h"
#include "applets/Slew.h"
void ClockSlewChecks() {
  ClockDivider clock{};clock.Start();Slew slew{};slew.Start();
  for(uint32_t code=0;code<=65535;++code) {
    for(uint16_t id=1;id<=4;++id) {
      Send(clock,id,code);int minimum=(id&1)?-64:-24;
      assert(Read(clock,id)==minimum+int((int64_t(64-minimum)*code+32767)/65535));
    }
    for(uint16_t id=1;id<=2;++id) { Send(slew,id,code);assert(Read(slew,id)==int((200LL*code+32767)/65535)); }
  }
  // Native updates do not reset running clock phase or bypass the primary base.
  clock.divmult[1].clock_count=7;clock.divmult[1].next_clock=1234;
  assert(clock.SetParameter(2,-24));assert(clock.divmult[1].clock_count==7&&clock.divmult[1].next_clock==1234);
  assert(clock.SetParameter(1,3));clock.modulation[0]=2;clock.Controller();
  assert(Read(clock,1)==3&&clock.divmult[0].steps==5);
  clock.modulation[0]=0;assert(clock.SetParameter(1,2)&&clock.SetParameter(2,1));clock.Reset();
  OC::CORE::ticks=100;clock.clocks[0]=true;clock.Controller();assert(clock.pulses[0]==1);
  OC::CORE::ticks=200;clock.Controller();assert(clock.pulses[0]==1);
  OC::CORE::ticks=300;clock.Controller();assert(clock.pulses[0]==2);
  // Every pair of primary values round-trips without contaminating cascades.
  for(int a=-64;a<=64;++a)for(int b=-64;b<=64;++b) {
    assert(clock.SetParameter(1,a)&&clock.SetParameter(3,b)&&clock.SetParameter(2,-24)&&clock.SetParameter(4,64));
    ClockDivider restored{};restored.OnDataReceive(clock.OnDataRequest());
    for(uint16_t id=1;id<=4;++id)assert(Read(clock,id)==Read(restored,id));
  }
  for(int a=-24;a<=64;++a)for(int b=-24;b<=64;++b) {
    assert(clock.SetParameter(2,a)&&clock.SetParameter(4,b));ClockDivider restored{};restored.OnDataReceive(clock.OnDataRequest());
    for(uint16_t id=1;id<=4;++id)assert(Read(clock,id)==Read(restored,id));
  }
  // Valid legacy bytes retain the old +32 meaning. Lost spill data is not guessed.
  for(int a=-32;a<=64;++a)for(int b=-24;b<=64;++b) {
    uint64_t old=uint64_t(a+32)|(uint64_t(2+32)<<8)|(uint64_t(b+32)<<16)|(uint64_t(1+32)<<24);
    ClockDivider restored{};restored.OnDataReceive(old);
    assert(Read(restored,1)==a&&Read(restored,2)==b&&Read(restored,3)==2&&Read(restored,4)==1);
  }
  ClockDivider lost{};lost.OnDataReceive(0xffffffffffffffe0ull);
  for(uint16_t id=1;id<=4;++id)assert(Read(lost,id)==64); // same known-corrupt legacy behavior
  const auto saved=clock.OnDataRequest();
  clock.OnDataReceive((saved&0xffffffffull)|(0x43440201ull<<32));assert(clock.OnDataRequest()==saved);
  clock.OnDataReceive((saved&~0xffull)|0xff);assert(clock.OnDataRequest()==saved);
  ClockDivider uiClock{};uiClock.Start();uiClock.editing=true;uiClock.OnEncoderMove(-1000);
  assert(Read(uiClock,1)==-64);uiClock.editing=false;uiClock.OnEncoderMove(1);uiClock.editing=true;uiClock.OnEncoderMove(-1000);assert(Read(uiClock,2)==-24);
  Slew uiSlew{};uiSlew.Start();OC::CORE::ticks=123456;
  assert(uiSlew.SetParameter(1,100));uiSlew.View();assert(uiSlew.displayedMs==Proportion(100,200,64000)/17);
  uiSlew.OnButtonPress();uiSlew.OnEncoderMove(-1000);assert(Read(uiSlew,1)==100&&Read(uiSlew,2)==0);
  uiSlew.View();assert(uiSlew.displayedMs==0);
  OC::CORE::ticks+=20000;uiSlew.displayedMs=-1;uiSlew.View();assert(uiSlew.displayedMs==-1);
  // Isolated setting writes, rejected values and all coefficient preset pairs.
  for(int rise=0;rise<=200;++rise)for(int fall=0;fall<=200;++fall) {
    assert(slew.SetParameter(1,rise)&&slew.SetParameter(2,fall));Slew restored{};restored.OnDataReceive(slew.OnDataRequest());
    assert(Read(restored,1)==rise&&Read(restored,2)==fall);
  }
  slew.OnDataReceive(0xffff);assert(Read(slew,1)==200&&Read(slew,2)==200);
  assert(!slew.SetParameter(1,-1)&&!slew.SetParameter(2,201)&&Read(slew,1)==200&&Read(slew,2)==200);
  for(Owner* owner : {static_cast<Owner*>(&clock),static_cast<Owner*>(&slew)}) {
    for(size_t i=0;i<owner->Parameters().count;++i) {
      int32_t before[4];for(size_t j=0;j<owner->Parameters().count;++j)before[j]=Read(*owner,owner->Parameters().entries[j].id);
      Send(*owner,owner->Parameters().entries[i].id,0);
      for(size_t j=0;j<owner->Parameters().count;++j)if(i!=j)assert(before[j]==Read(*owner,owner->Parameters().entries[j].id));
    }
  }
  for(int coefficient=0;coefficient<=200;++coefficient)for(int target : {-HEMISPHERE_MAX_INPUT_CV,-2,0,2,HEMISPHERE_MAX_INPUT_CV}) {
    Slew app{};app.Start();assert(app.SetParameter(1,coefficient)&&app.SetParameter(2,coefficient));
    app.inputs[0]=app.inputs[1]=target;
    int previous[2]{};
    for(int tick=0;tick<32;++tick) {
      ++OC::CORE::ticks;app.Controller();
      for(int ch=0;ch<2;++ch) {
        assert(app.outputs[ch]>=std::min(0,target)&&app.outputs[ch]<=std::max(0,target));
        assert(target>=0?app.outputs[ch]>=previous[ch]:app.outputs[ch]<=previous[ch]);previous[ch]=app.outputs[ch];
        if(coefficient==0)assert(app.outputs[ch]==target);
      }
    }
    app.gates[0]=app.gates[1]=true;app.Controller();assert(app.outputs[0]==target&&app.outputs[1]==target);
  }
  puts("ClockDivider/Slew: all6 x65536 codes, native effects, phase, legacy/versioned presets, isolated updates and signed/controller boundaries PASS");
}
