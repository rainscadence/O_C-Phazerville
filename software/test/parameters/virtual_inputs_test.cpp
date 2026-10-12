#include "VcaHost.h"
#define private public
#include "audio_applets/VCAApplet.h"
#undef private
#include "ii/Dispatcher.h"
#include "ii/VirtualInputPreset.h"
#include "ii/DirectionalPreset.h"
#include <map>
#include <cstring>
#include <cstdio>
#include <cmath>
using namespace phz::ii;
using Range = VirtualInputs::Range;

struct Lock { static inline int depth=0; Lock(){++depth;} ~Lock(){--depth;} };
struct Sink : Backend {
  bool Describe(const Write&,Descriptor&) override { assert(false); return false; }
  bool Apply(const Prepared&) override { assert(false); return false; }
  bool ApplyVirtualCV(uint16_t channel,uint16_t code) override {
    assert(Lock::depth); return virtual_inputs.Write(channel,code);
  }
};
void BankTests() {
  VirtualInputs bank;
  for (unsigned range=1;range<=3;++range) {
    assert(bank.SetRange(0,Range(range))); assert(bank.Value(0)==0);
    const int lo=range==2?-7680:0, hi=range==3?15360:7680;
    int previous=lo;
    for(uint32_t code=0;code<65536;++code) {
      assert(bank.Write(1,uint16_t(code)));
      // Independent quotient/remainder rounding oracle.
      const int64_t product=int64_t(hi-lo)*code;
      const int expected=lo+product/65535+((product%65535)*2>=65535);
      assert(bank.Value(0)==expected && expected>=previous);
      assert(bank.Get(0).code==code && bank.Get(0).received);
      previous=expected;
    }
    assert(bank.Value(0)==hi); bank.ClearValues(); assert(bank.Value(0)==0);
  }
  for(size_t i=0;i<32;++i){assert(bank.SetRange(i,Range::Bipolar5));assert(bank.Write(i+1,1000+i));}
  const auto before=bank.Get(0);
  for(uint16_t ch : {uint16_t(0),uint16_t(33),uint16_t(65535)})assert(!bank.Write(ch,99));
  for(size_t i : {size_t(32),SIZE_MAX}) {
    assert(!bank.SetRange(i,Range::Off)); assert(!bank.RestoreRange(i,1));
    assert(bank.Value(i)==0 && bank.Get(i).range==Range::Off);
  }
  for(unsigned r : {4u,255u})assert(!bank.SetRange(0,Range(r)));
  for(uint64_t r : {uint64_t(4),uint64_t(256),UINT64_MAX})assert(!bank.RestoreRange(0,r));
  assert(bank.Get(0).code==before.code && bank.Value(0)==before.value);
  assert(bank.SetRange(0,Range::Bipolar5) && bank.Get(0).received);
  assert(bank.SetRange(0,Range::Off) && !bank.Write(1,65535) && bank.Value(0)==0);
  assert(bank.Get(1).code==1001);
  bank.ClearValues();for(size_t i=0;i<32;++i)assert(!bank.Get(i).received && bank.Value(i)==0);
  assert(bank.Get(0).range==Range::Off && bank.Get(1).range==Range::Bipolar5);
  bank.Reset();for(size_t i=0;i<32;++i)assert(bank.Get(i).range==Range::Unipolar5);
  puts("PASS: all 196608 range/code pairs, bounds, isolation, clear/reset, invalid records");
}
void PresetTests() {
  {
    virtual_inputs.Reset();
    const auto baseline=CVInputMap::CountIiBindings(31);
    {
      CVInputMap a;
      assert(a.SetSource(223));
      assert(CVInputMap::IiUsageMask() & (uint32_t(1)<<31));
      assert(CVInputMap::CountIiBindings(31)==baseline+1);
      CVInputMap b(a), c;
      c=a; const CVInputMap& alias=c; c=alias;
      assert(CVInputMap::CountIiBindings(31)==baseline+3);
      { CVInputMap temporary(b); assert(CVInputMap::CountIiBindings(31)==baseline+4); }
      assert(CVInputMap::CountIiBindings(31)==baseline+3);
      assert(b.SetSource(222));
      assert((CVInputMap::IiUsageMask() & (uint32_t(3)<<30)) == (uint32_t(3)<<30));
      assert(CVInputMap::CountIiBindings(31)==baseline+2);
      CVInputMap* dynamic=new CVInputMap(a);
      assert(CVInputMap::CountIiBindings(31)==baseline+3);
      delete dynamic;
      assert(CVInputMap::CountIiBindings(31)==baseline+2);
      // Removing a middle list node must not detach its surviving neighbors.
      auto* middle=new CVInputMap(a); auto* head=new CVInputMap(a);
      delete middle;
      assert(CVInputMap::CountIiBindings(31)==baseline+3);
      delete head;
      assert(CVInputMap::CountIiBindings(31)==baseline+2);
      const auto attenuation=c.attenuversion;
      auto cleared=CVInputMap::VisitIiBindings(31,[](CVInputMap& m){assert(m.SetSource(0));});
      assert(cleared==baseline+2 && CVInputMap::CountIiBindings(31)==0);
      assert(!(CVInputMap::IiUsageMask() & (uint32_t(1)<<31)));
      assert(CVInputMap::IiUsageMask() & (uint32_t(1)<<30));
      assert(a.Source()==0 && c.Source()==0 && c.attenuversion==attenuation && b.Source()==222);
    }
    assert(CVInputMap::CountIiBindings(31)==0);
    assert(CVInputMap::CountIiBindings(32)==0);
    struct FutureApplet {CVInputMap maps[3];};
    { FutureApplet next;for(auto& m:next.maps)assert(m.SetSource(223));assert(CVInputMap::CountIiBindings(31)==3); }
    assert(CVInputMap::CountIiBindings(31)==0);
    puts("PASS: automatic private/shared binding inventory; copy/assignment/temporary/array/heap lifetimes and selective clearing");
  }
  {
    // Central guard applies equally to shared maps and private applet maps.
    using D=DirectionalMappings::Direction;
    virtual_inputs.Reset();
    CVInputMap m;
    for(unsigned i=0;i<32;++i) {
      assert(virtual_inputs.SetDirection(i,i%2?D::Output:D::Off,0));
      assert(m.SetSource(64)); const auto before=m.Pack();
      assert(!m.SetSource(192+i) && m.Pack()==before);
      assert(m.RestoreSource(192+i)==(i%2==0));
      if(i%2==0) assert(m.Source()==192+i);
    }
    assert(m.SetSource(159));m.ChangeSource(1);assert(m.Source()==224);
    m.ChangeSource(-1);assert(m.Source()==159);
    for(unsigned i=0;i<32;++i) {
      assert(virtual_inputs.SetDirection(i,D::Input,0));
      assert(m.SetSource(159));m.ChangeSource(1);assert(m.Source()==192+i);
      assert(m.SetSource(224));m.ChangeSource(-1);assert(m.Source()==192+i);
      assert(virtual_inputs.SetDirection(i,D::Off,0));
    }
    // Restore still preserves disabled legacy references; reads remain zero.
    m.Unpack(0x3cc0);assert(m.Source()==192 && m.RawIn()==0);
    m.ChangeSource(0);assert(m.Source()==192);
    m.ChangeSource(INT32_MAX);assert(m.Source()==224);
    m.ChangeSource(INT32_MIN);assert(m.Source()==159);
    virtual_inputs.Reset();
    puts("PASS: shared/private source selection rejects Off/Output; skips empty bank both ways; preserves legacy restore");
  }
  // Exercise the production staging helper with the actual CVInputMap codec.
  {
    std::map<uint16_t,uint64_t> saved;
    auto read=[&](uint16_t k,uint64_t& v){auto i=saved.find(k);if(i==saved.end())return false;v=i->second;return true;};
    DirectionalMappings source, staged;
    CVInputMap current[16], result[16];
    for (auto& m : current) assert(m.SetSource(64));
    for (auto& m : result) assert(m.SetSource(96));
    using D=DirectionalMappings::Direction;
    assert(source.SetDirection(31,D::Output,0));
    assert(source.AssignOutput(31,8,8) && source.Publish(31,8,3840));
    for(size_t i=0;i<32;++i) assert(PresetMappingRecord(source,i,saved[kVirtualRangeKey+i]));
    assert(saved[kVirtualRangeKey]==1 && saved[kVirtualRangeKey+31]>3);
    assert(!StageDirectionalPreset(read,20,8,current,result,staged));
    saved[kIiUsageKey]=IiUsageRecord(0);
    assert(StageDirectionalPreset(read,20,8,current,result,staged));
    saved[kIiUsageKey]=IiUsageRecord(uint32_t(1)<<31);
    assert(!StageDirectionalPreset(read,20,8,current,result,staged));
    saved[kIiUsageKey]=IiUsageRecord(0);
    assert(staged.Get(31).producer==8 && !staged.Get(31).valid);
    LiveMappings live; live=staged;
    assert(live.DirectionalMappings::Get(31).producer==8);
    uint16_t reply=123; assert(!live.ReadVoltage(32,reply) && reply==123);
    // All 256 source bytes: only the bound output is an incompatible ii source.
    for(unsigned s=0;s<256;++s) assert(ValidPresetCvSource(staged,s)==(s!=223));
    // Conflicts in every packed field/page reject without altering destinations.
    for(size_t slot=0;slot<16;++slot) {
      for(unsigned page=0;page<4;++page) saved[20+page]=UINT64_C(0x3c403c403c403c40);
      const size_t shift=(slot%4)*16;
      saved[20+slot/4]=(saved[20+slot/4]&~(UINT64_C(0xffff)<<shift))|(UINT64_C(0x3cdf)<<shift);
      assert(!StageDirectionalPreset(read,20,8,current,result,staged));
      for(auto& m:result) assert(m.Source()==64);
      assert(staged.Get(31).producer==8);
    }
    saved.erase(20); // no first page: retained current bindings must still validate
    assert(current[15].SetSource(223));
    assert(!StageDirectionalPreset(read,20,8,current,result,staged));
    assert(source.SetDirection(0,D::Off,0));
    assert(PresetMappingRecord(source,0,saved[kVirtualRangeKey]) && saved[kVirtualRangeKey]==0);
    assert(current[15].SetSource(192)); // legacy Off bindings remain legal
    assert(StageDirectionalPreset(read,20,8,current,result,staged));
    assert(result[15].Source()==192 && staged.Get(0).direction==D::Off);
    // Compare the staging reader with the real variadic production packer.
    CVInputMap packed[4];
    for(unsigned j=0;j<4;++j) { assert(packed[j].SetSource(64+j)); packed[j].attenuversion=-60+int(j)*40; }
    saved[20]=PackPackables(packed[0],packed[1],packed[2],packed[3]);
    saved.erase(21); // subsequent pages preserve current values
    assert(StageDirectionalPreset(read,20,8,current,result,staged));
    for(unsigned j=0;j<4;++j) assert(result[j].Pack()==packed[j].Pack());
    assert(result[15].Source()==192);
    saved[kVirtualRangeKey+31]=UINT64_MAX;
    assert(!StageDirectionalPreset(read,20,8,current,result,staged));
    assert(staged.Get(31).producer==8 && result[15].Source()==192);
    puts("PASS: production directional preset staging, legacy records, every CV conflict position, retained bindings, no persisted live outputs");
  }
  std::map<uint16_t,uint64_t> records;
  for(size_t i=0;i<32;++i)records[kVirtualRangeKey+i]=i%4;
  auto reader=[&](uint16_t key,uint64_t& v){auto it=records.find(key);if(it==records.end())return false;v=it->second;return true;};
  VirtualInputs bank;assert(LoadVirtualRanges(reader,bank));
  for(size_t i=0;i<32;++i)assert(unsigned(bank.Get(i).range)==i%4 && !bank.Get(i).received);
  bank.Write(2,555);records[kVirtualRangeKey+31]=256;
  assert(!LoadVirtualRanges(reader,bank));assert(bank.Get(1).code==555);
  records.clear();assert(LoadVirtualRanges(reader,bank));
  for(size_t i=0;i<32;++i)assert(bank.Get(i).range==Range::Unipolar5 && !bank.Get(i).received);
  // Existing and newly allocated input sources survive the actual preset codec.
  CVInputMap map;
  for(int source=192;source<=223;++source) {
    assert(map.SetSource(source));
    assert(std::strlen(map.InputName()) <= 3); // fixed-width applet label cell
  }
  for(int source : {0,64,96,128,159,192,223,224}) {
    assert(map.SetSource(source));map.attenuversion=-60;
    CVInputMap restored;restored.Unpack(map.Pack());
    assert(restored.Source()==source && restored.attenuversion==-60);
  }
  map.SetSource(192);assert(std::strcmp(map.InputName(),"I 1")==0);
  map.SetSource(223);assert(std::strcmp(map.InputName(),"I32")==0);
  map.SetSource(200);assert(std::strcmp(map.InputName(),"I 9")==0);
  map.SetSource(201);assert(std::strcmp(map.InputName(),"I10")==0);
  // Source selector reaches all ii channels, without changing existing MIDI.
  map.SetSource(159);map.ChangeSource(1);assert(map.Source()==192);
  for(int s=193;s<=223;++s){map.ChangeSource(1);assert(map.Source()==s);}
  map.ChangeSource(1);assert(map.Source()==224);
  puts("PASS: staged ranges, malformed/legacy presets, MIDI/ii mapping round trips, source navigation");
}
void FriendlyValueTests() {
  uint8_t packet[8]={3,0,1,0,0,0x10,0,0};
  for(unsigned command=0;command<256;++command) {
    packet[0]=command; Write out{9,8,7};
    assert(DecodeVirtualSignal(packet,7,out)==(command>=2 && command<=4));
    if(command<2 || command>4) assert(out.slot==9 && out.parameter==8 && out.value==7);
  }
  for(unsigned command=2;command<=4;++command) {
    packet[0]=command;
    for(size_t length=0;length<=8;++length) {
      Write out{9,8,7}; assert(DecodeVirtualSignal(packet,length,out)==(length==7));
      if(length!=7) assert(out.slot==9 && out.parameter==8 && out.value==7);
    }
    Write out{9,8,7}; assert(!DecodeVirtualSignal(nullptr,7,out));
    packet[4]=1; assert(!DecodeVirtualSignal(packet,7,out)); packet[4]=0;
    packet[2]=33; assert(!DecodeVirtualSignal(packet,7,out)); packet[2]=1;
    assert(out.slot==9 && out.parameter==8 && out.value==7);
  }
  VirtualInputs bank;
  for (unsigned range=1;range<=3;++range) {
    bank.SetRange(0,Range(range));
    const int lo=range==2?-7680:0, hi=range==3?15360:7680;
    for(int bits=0;bits<65536;++bits) {
      const int signedValue=bits<32768?bits:bits-65536;
      const int expected=std::max(lo,std::min(hi,int(std::round(signedValue*15.0/16.0))));
      assert(bank.WriteVoltage(1,bits));
      assert(bank.Value(0)==expected && bank.Get(0).received);
      assert(bank.Get(0).code==std::round(double(expected-lo)*65535/(hi-lo)));
    }
    for(int position=0;position<=16384;++position) {
      assert(bank.WriteNormalized(1,position));
      assert(bank.Value(0)==lo+std::round(double(hi-lo)*position/16384));
      assert(bank.Get(0).code==std::round(double(position)*65535/16384));
    }
    const auto before=bank.Get(0);
    for(int invalid=16385;invalid<65536;++invalid) assert(!bank.WriteNormalized(1,invalid));
    assert(bank.Value(0)==before.value && bank.Get(0).code==before.code);
  }
  bank.SetRange(0,Range::Bipolar5);
  assert(bank.WriteVoltage(1,4096) && bank.Value(0)==3840);
  assert(bank.WriteVoltage(1,uint16_t(-4096)) && bank.Value(0)==-3840);
  assert(bank.WriteVoltage(1,0) && bank.Value(0)==0);
  assert(bank.WriteNormalized(1,8192) && bank.Value(0)==0);
  for(auto channel : {0,33,65535}) {
    assert(!bank.WriteVoltage(channel,4096)); assert(!bank.WriteNormalized(channel,8192));
  }
  bank.SetRange(0,Range::Off);
  assert(!bank.WriteVoltage(1,4096) && !bank.WriteNormalized(1,8192));
  assert(!bank.Get(0).received && bank.Value(0)==0);
  puts("PASS: exhaustive signed/normalized values, clipping, signs, zero, Off, diagnostic codes");
}
void PacketTests() {
  Dispatcher<Lock> engine;Sink sink;engine.SetActive(true);virtual_inputs.Reset();
  auto send=[&](uint16_t channel,uint16_t code,uint16_t reserved=0,uint8_t command=2){
    uint8_t bytes[]={command,uint8_t(channel>>8),uint8_t(channel),uint8_t(reserved>>8),uint8_t(reserved),uint8_t(code>>8),uint8_t(code)};
    assert(engine.Capture(bytes,7));engine.Service(sink);
  };
  send(1,65535);assert(virtual_inputs.Value(0)==7680);
  for(auto ch : {0,33,65535})send(ch,0);
  send(1,0,1);send(1,0,0,3);assert(virtual_inputs.Value(0)==7680);
  uint8_t bytes[]={2,0,1,0,0,0,0};
  for(size_t n : {size_t(0),size_t(6),size_t(8)})assert(!engine.Capture(bytes,n));
  assert(engine.Capture(bytes,7));engine.Begin();engine.End(true);
  engine.Service(sink);assert(virtual_inputs.Value(0)==7680); // queue invalidated
  engine.SetActive(false);assert(!engine.Capture(bytes,7));engine.SetActive(true);
  virtual_inputs.ClearValues();
  // Fan-out through actual input mapper, and unchanged MIDI source.
  CVInputMap first,second,midi;
  first.SetSource(192);second.SetSource(192);second.attenuversion=-60;midi.SetMidiMap(0);
  frame.MIDIState.mapping[0].value=1234;
  send(1,32768);
  assert(first.In()==3840 && second.In()==-3840 && midi.In()==1234);
  VcaApplet<MONO> app;app.Start();
  assert(app.SetParameterNative(2,192));
  const auto base=app.level;
  auto control=[&](){size_t i=app.cv_stream.write_ix;app.Controller();return app.cv_stream.buffer[i];};
  send(1,0);assert(control()==0);
  send(1,32768);const auto halfway=control();
  send(1,65535);assert(control()>halfway && halfway>0 && app.level==base);
  std::array<uint64_t,4> saved{};app.OnDataRequest(saved);
  app.level_cv.SetMidiMap(0);app.OnDataReceive(saved);
  assert(app.level_cv.Source()==192);
  app.Unload();
  assert(Lock::depth==0);
  puts("PASS: real dispatcher -> virtual CV -> shared mapper -> VCA controller; fan-out and MIDI isolation");
}
void SourceGroupTests() {
  virtual_inputs.Reset();
  for(unsigned i=0;i<32;++i) virtual_inputs.SetRange(i,Range::Off);
  CVInputMap map;map.attenuversion=-17;
  assert(map.SetSource(0));map.ChangeSourceGroup(1);assert(map.source_type()==CVInputMap::TYPE_ADC);
  map.ChangeSourceGroup(1);assert(map.source_type()==CVInputMap::TYPE_DAC&&map.index()==0);
  map.ChangeSourceGroup(1);assert(map.source_type()==CVInputMap::TYPE_DAC&&map.index()==DAC_CHANNEL_COUNT);
  map.ChangeSourceGroup(1);assert(map.source_type()==CVInputMap::TYPE_MIDI);
  map.ChangeSourceGroup(1);assert(map.source_type()==CVInputMap::TYPE_INTERNAL);
  virtual_inputs.SetRange(7,Range::Bipolar5);
  map.ChangeSourceGroup(-1);assert(map.Source()==(192|7));
  map.ChangeSourceGroup(-1);assert(map.source_type()==CVInputMap::TYPE_MIDI);
  map.ChangeSourceGroup(-1);assert(map.source_type()==CVInputMap::TYPE_DAC&&map.index()>=DAC_CHANNEL_COUNT);
  map.ChangeSourceGroup(-1);assert(map.source_type()==CVInputMap::TYPE_DAC&&map.index()==DAC_CHANNEL_COUNT-1);
  map.ChangeSourceGroup(-1);assert(map.source_type()==CVInputMap::TYPE_ADC);
  map.ChangeSourceGroup(-1);assert(map.Source()==0);map.ChangeSourceGroup(-1);assert(map.Source()==0);
  assert(map.attenuversion==-17);
  virtual_inputs.SetRange(7,Range::Off);
  virtual_inputs.SetDirection(7,DirectionalMappings::Direction::Output,0);
  assert(map.SetSource(128));map.ChangeSourceGroup(1);
  assert(map.source_type()==CVInputMap::TYPE_INTERNAL);
  puts("PASS: CV source category navigation, physical/virtual split, empty ii skip and attenuation preservation");
}
int main(){BankTests();PresetTests();PacketTests();FriendlyValueTests();SourceGroupTests();}
