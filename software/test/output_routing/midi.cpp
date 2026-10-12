#include "routing/AutoMidi.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Sink {
  struct Event { bool on; int ch,note,ports; };
  std::vector<Event> events;
  int held[16][128][4]{};
  int cc_count=0,last_cc=-1,last_val=-1;
  void Note(bool on, uint8_t ch,uint8_t note,uint8_t ports) {
    events.push_back({on,ch,note,ports});
    for(int p=0;p<4;++p)if(ports&(1<<p)) {
      held[ch][note][p]+=on?1:-1;
      assert(held[ch][note][p]>=0 && held[ch][note][p]<=1);
    }
  }
  void CC(uint8_t ch,uint8_t cc,uint8_t value,uint8_t ports) {
    assert(ch<16&&cc<128&&value<128&&ports);++cc_count;last_cc=cc;last_val=value;
  }
  void Empty() {for(auto& c:held)for(auto& n:c)for(int p:n)assert(p==0);}
};
int main() {
  using M=phz::routing::AutoMidi<8>;
  M m;M::Config c[8];M::Value v[8];Sink sink;
  for(int p=0;p<4;++p) {
    c[2*p]={M::Kind::Pitch,uint8_t(p),0,true};
    c[2*p+1]={M::Kind::Gate,uint8_t(p),0,true};
    v[2*p].code=60+p;
  }
  auto tick=[&](uint8_t ports=3){m.Tick(c,v,ports,2,sink);};
  tick();assert(sink.events.empty());
  for(int p=0;p<4;++p)v[2*p+1].gate=true;
  tick();assert(sink.events.size()==4); // includes quadrants3 and4
  c[4].enabled=false;tick();assert(!sink.events.back().on&&sink.events.back().note==62);
  c[4].enabled=true;tick();assert(sink.events.size()==5); // no replay while high
  v[5].gate=false;tick();v[5].gate=true;tick();assert(sink.events.back().on);
  v[0].code=127;tick();assert(!sink.events[sink.events.size()-2].on&&sink.events.back().note==127);
  // Reconfiguration releases exact old channel/note, then waits for fresh gate.
  c[0].channel=c[1].channel=15;tick();assert(!sink.events.back().on&&sink.events.back().ch==0);
  v[1].gate=false;tick();v[1].gate=true;tick();assert(sink.events.back().ch==15);
  tick(0);sink.Empty(); // old ports still receive cleanup
  tick(3);sink.Empty();
  for(auto& cfg:c)cfg.enabled=false;tick();sink.Empty();
  m.Stop(sink);sink.Empty();

  // Duplicate automatic owners share note-on and release only last owner.
  for(int i=0;i<8;++i){c[i]={};v[i]={};}
  for(int p=0;p<2;++p){c[2*p]={M::Kind::Pitch,1,0,true};c[2*p+1]={M::Kind::Gate,1,0,true};v[2*p].code=127;}
  tick();v[1].gate=v[3].gate=true;size_t count=sink.events.size();tick();
  assert(sink.events.size()==count+1);
  c[0].enabled=false;tick();assert(sink.events.size()==count+1);
  c[2].enabled=false;tick();assert(sink.events.size()==count+2);sink.Empty();
  m.Stop(sink);

  // Pulse mode retains bounded duration; configured disabled Gate stays silent.
  for(auto& cfg:c)cfg={};
  c[0]={M::Kind::Pitch,0,0,true};v[0].code=0;tick();assert(sink.events.back().on);
  tick();tick();sink.Empty();count=sink.events.size();tick();assert(sink.events.size()==count);
  c[1]={M::Kind::Gate,0,0,false};v[0].code=64;tick();sink.Empty();
  // Zero CC is sent initially; changing CC identity resends same value.
  c[0]={M::Kind::CC,0,32,true};v[0].code=0;tick();assert(sink.last_cc==32&&sink.last_val==0);
  const int cc=sink.cc_count;tick();assert(sink.cc_count==cc);
  c[0].controller=33;tick();assert(sink.cc_count==cc+1&&sink.last_cc==33);
  c[0].enabled=false;tick();assert(sink.cc_count==cc+1);
  m.Stop(sink);

  // Deterministic randomized routing/notes/ports never double-own or underflow.
  uint32_t rng=117;
  for(int step=0;step<20000;++step) {
    rng=rng*1664525u+1013904223u;int i=(rng>>16)%8;
    c[i].enabled=(rng&1);c[i].channel=(rng>>2)%4;
    c[i].kind=(i&1)?M::Kind::Gate:M::Kind::Pitch;
    v[i].code=(rng>>8)%128;v[i].gate=(rng&32);
    tick((rng>>24)&15);
    if(step%31==0)m.Stop(sink);
  }
  m.Stop(sink);sink.Empty();
  puts("PASS: automatic MIDI ownership, all four pairs, route/port cleanup, retrigger policy, CC identity, 20000 transitions");
}
