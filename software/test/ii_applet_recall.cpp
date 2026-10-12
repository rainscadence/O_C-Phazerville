#include "ii/AppletRecall.h"
#include "ii/RecallRoutes.h"
#include <cassert>
#include <cstdio>
#include <map>
using namespace phz::ii;
struct InputMap {uint8_t source=0;int atten=60;uint8_t Source()const{return source;}bool SetSource(uint8_t s){source=s;return true;}};
int main() {
  constexpr uint64_t adsr = 8, atten = 56, wide = UINT64_C(0x1234567800000008);
  RecallOwnership<> live;
  assert(live.Select(0,adsr) && live.UsesDefaults(0));
  assert(live.Select(1,adsr) && !live.UsesDefaults(1) && live.UsesDefaults(0));
  assert(live.Select(2,adsr) && !live.UsesDefaults(2));
  assert(live.Remove(0) && !live.UsesDefaults(1));
  assert(live.Select(1,adsr) && !live.UsesDefaults(1));
  assert(live.Select(1,atten) && live.UsesDefaults(1));
  assert(live.Select(1,adsr) && !live.UsesDefaults(1)); // third instance still exists
  assert(live.Remove(2) && !live.UsesDefaults(1));
  assert(live.Select(1,atten) && live.Select(1,adsr) && live.UsesDefaults(1));
  assert(live.Select(0,wide) && live.UsesDefaults(0)); // full identity, no low-word alias
  assert(!live.Select(4,adsr) && !live.Select(1,0) && !live.Remove(4));
  live.RestorePreset({adsr,adsr,atten,wide});
  for (size_t i=0;i<4;++i) assert(!live.UsesDefaults(i));
  assert(live.Select(1,adsr) && !live.UsesDefaults(1));

  // Every pair of quadrants, both arrival orders; first-loaded != lowest slot.
  for (size_t first=0;first<4;++first) for(size_t second=0;second<4;++second) {
    if(first==second)continue;
    RecallOwnership<> state;
    assert(state.Select(first,adsr) && state.Select(second,adsr));
    assert(state.UsesDefaults(first) && !state.UsesDefaults(second));
    assert(state.Remove(first) && !state.UsesDefaults(second));
    assert(state.Select(second,atten) && state.Select(second,adsr));
    assert(state.UsesDefaults(second));
  }
  using Bank=RecallBank<2,4>;
  Bank bank0(0),bank1(1); Bank::Profile a;
  a.count=2;a.bindings[0]={1,RecallKind::Input,2};a.bindings[1]={1,RecallKind::Output,1};
  assert(bank0.Remember(atten,a) && bank0.Dirty());
  auto revision=bank0.Revision();
  assert(bank0.Remember(atten,a) && bank0.Revision()==revision); // idempotent edit
  assert(!bank0.Saved(revision,false) && bank0.Dirty());
  assert(bank0.Saved(revision,true) && !bank0.Dirty());
  a.bindings[1].channel=32;
  assert(bank0.Remember(adsr,a));
  assert(!bank0.Saved(revision,true) && bank0.Dirty());
  assert(bank0.Find(atten)->bindings[1].channel==1);
  assert(bank0.Find(adsr)->bindings[1].channel==32 && !bank1.Find(adsr));
  assert(bank1.Remember(wide,a) && !bank1.Find(adsr) && bank1.Bank()==1);
  assert(!bank0.Remember(wide,a) && !bank0.Find(wide)); // full means reject, no eviction
  auto before=*bank0.Find(adsr);
  a.count=5;assert(!bank0.Remember(adsr,a));a=before;
  a.bindings[1].channel=33;assert(!bank0.Remember(adsr,a));a=before;
  a.bindings[1].channel=2;assert(!bank0.Remember(adsr,a));a=before; // input/output conflict
  a.bindings[1]={1,RecallKind::Input,2};assert(!bank0.Remember(adsr,a)); // duplicate field
  a.bindings[1]={2,RecallKind::Input,2};assert(a.Valid()); // input fanout within first instance
  assert(bank0.Find(adsr)->bindings[1].channel==32);
  std::map<uint16_t,uint64_t> disk;
  assert(bank0.Export([&](uint16_t key,uint64_t value){disk[key]=value;return true;}));
  assert(bank0.Dirty()); // serializing alone does not prove durable save
  Bank rebooted(0);
  auto read=[&](uint16_t key,uint64_t& value){auto it=disk.find(key);if(it==disk.end())return false;value=it->second;return true;};
  assert(rebooted.Import(read) && !rebooted.Dirty());
  assert(rebooted.Find(atten)->bindings[1].channel==1 && rebooted.Find(adsr)->bindings[1].channel==32);
  assert(!bank1.Import(read) && bank1.Find(wide)); // wrong bank must not erase existing state
  const auto good=disk;
  for(const auto& record:good) {
    disk=good;disk.erase(record.first);
    assert(!rebooted.Import(read));
    assert(rebooted.Find(atten)->bindings[1].channel==1);
  }
  disk=good;disk[7]=disk[1];assert(!rebooted.Import(read)); // duplicate applet identity
  disk=good;disk[2]=5;assert(!rebooted.Import(read)); // field count exceeds capacity
  disk=good;disk[0]|=UINT64_C(1)<<63;assert(!rebooted.Import(read));
  for(size_t fail=0;fail<good.size();++fail){size_t writes=0;assert(!bank0.Export([&](uint16_t,uint64_t){return writes++!=fail;}));assert(bank0.Dirty());}
  // Exhaustive wire-sized field/channel codec domains and all reserved bits.
  for (uint32_t field=1;field<=65535;++field) for(auto kind:{RecallKind::Input,RecallKind::Output}) {
    RecallBinding b{uint16_t(field),kind,uint8_t(field%33)},out{};
    const auto encoded=EncodeRecallBinding(b);
    assert(DecodeRecallBinding(encoded,out));
    assert(out.field==b.field && out.kind==b.kind && out.channel==b.channel);
  }
  RecallBinding sentinel{7,RecallKind::Output,32};
  for(unsigned bit=32;bit<64;++bit){auto out=sentinel;assert(!DecodeRecallBinding(EncodeRecallBinding(sentinel)|(UINT64_C(1)<<bit),out));assert(out.field==7&&out.channel==32);}
  for(unsigned channel=33;channel<64;++channel){auto out=sentinel;assert(!DecodeRecallBinding(UINT64_C(1)<<24 | UINT64_C(1)<<22 | uint64_t(channel)<<16 | 1,out));assert(out.field==7&&out.channel==32);}
  for(auto bad:{UINT64_C(0),UINT64_C(0x02000001),UINT64_C(0x01c00001)}){auto out=sentinel;assert(!DecodeRecallBinding(bad,out));assert(out.field==7);}
  {
    DirectionalMappings routes;
    using D=DirectionalMappings::Direction;
    assert(routes.SetDirection(0,D::Output,0) && routes.SetDirection(31,D::Output,0));
    InputMap maps[4]{{0xc1,45},{0x80,37},{0xc2,25},{0x40,11}};
    auto input=[&](uint16_t field)->InputMap*{if(field<=2&&field)return &maps[field-1];if(field>=0x100&&field<=0x101)return &maps[2+field-0x100];return nullptr;};
    RecallProfile<16> profile,empty;
    assert(routes.RouteOutput(3,1,8));
    assert(CaptureRecall(input,routes,3,profile));
    assert(profile.count==6 && profile.bindings[4].channel==1);
    assert(ApplyRecall(empty,input,routes,3,8));
    assert(!maps[0].source && !maps[2].source);
    assert(maps[1].source==0x80 && maps[3].source==0x40); // MIDI and physical inputs retained
    assert(maps[0].atten==45 && maps[2].atten==25);
    assert(routes.OutputMappingFor(3)==0 && !routes.Get(0).valid);
    assert(ApplyRecall(profile,input,routes,3,8));
    assert(maps[0].source==0xc1 && maps[2].source==0xc2 && routes.OutputMappingFor(3)==1);
    // Another active quadrant owns I32. Recall must not steal it.
    assert(routes.RouteOutput(1,32,8));
    profile.bindings[4].channel=32;
    assert(!ApplyRecall(profile,input,routes,3,8));
    assert(routes.OutputMappingFor(1)==32 && routes.OutputMappingFor(3)==0);
    // Input requests cannot turn an output into an input.
    profile.bindings[0].channel=1;
    assert(!ApplyRecall(profile,input,routes,3,8) && !maps[0].source);
    assert(routes.Get(0).direction==D::Output);
    RecallProfile<2> undersized;
    assert(!CaptureRecall(input,routes,3,undersized));
    profile={};profile.count=1;profile.bindings[0]={1,RecallKind::Output,9};
    assert(!ApplyRecall(profile,input,routes,3,8,[](size_t)->size_t{return 1;}));
    assert(routes.Get(8).direction==D::Input && routes.OutputMappingFor(3)==0);
    assert(ApplyRecall(profile,input,routes,3,8,[](size_t)->size_t{return 0;}));
    assert(routes.Get(8).direction==D::Output && routes.OutputMappingFor(3)==9);
  }
  puts("PASS: ii recall ownership, all quadrant orders, duplicate/no-promotion policy, preset isolation, bank isolation, full IDs, capacity, dirty revisions and binding codec");
}
