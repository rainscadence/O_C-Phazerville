#include "VcaHost.h"
#include "util/clkdivmult.h"
constexpr int DIGITAL_INPUT_COUNT=4, GATE_THRESHOLD=15<<7;
inline const uint8_t *METRO_L_ICON=nullptr,*METRO_R_ICON=nullptr,*DIGITAL_INPUT_ICONS=nullptr;
struct {bool auto_reset=false;} clock_m;
bool EuclideanFilter(int,int,int,uint32_t){return true;}
#include "CVInputMap.h"
#include "ii/RecallRoutes.h"
#include "ii/DirectionalPreset.h"
#include <map>
#include <cstring>
#include <cstdio>
int main(){
 using namespace phz::ii;
 using D=DirectionalMappings::Direction;
 virtual_inputs.Reset();
 const auto baseline=CVInputMap::CountIiBindings(0);
 {
  DigitalInputMap gate{};
  gate.Unpack(0xc0 | (1<<8));
  assert(gate.source_type()==DigitalInputMap::TYPE_II);
  assert(!strcmp(gate.InputName(),"I 1"));
  assert(CVInputMap::CountIiBindings(0)==baseline+1);
  assert(!gate.Gate() && !gate.Clock());
  assert(virtual_inputs.WriteVoltage(1,8192));
  assert(gate.Gate() && gate.Clock() && !gate.Clock());
  assert(virtual_inputs.WriteVoltage(1,0));
  assert(!gate.Gate() && !gate.Clock());
  assert(virtual_inputs.WriteVoltage(1,8192));
  assert(gate.Clock());
  auto saved=gate.Pack(); DigitalInputMap copy{};copy.Unpack(saved);
  assert(copy.Pack()==saved && copy.Gate());
  assert(CVInputMap::CountIiBindings(0)==baseline+2);
  CVInputMap::VisitIiBindings(0,[](CVInputMap& m){m.SetSource(0);});
  assert(!gate.enabled() && !copy.enabled());
  assert(virtual_inputs.SetDirection(0,D::Output,CVInputMap::CountIiBindings(0)));
  gate.SetMidiMap(31); gate.ChangeSource(1);
  assert(gate.source_type()==DigitalInputMap::TYPE_II && gate.index()==1);
  gate.Unpack(0xdf | (1<<8));
  assert(gate.index()==31 && !strcmp(gate.InputName(),"I32"));
  gate.ChangeSource(1);assert(gate.source_type()==DigitalInputMap::TYPE_INTERNAL);
  gate.ChangeSource(-1);assert(gate.index()==31);
  virtual_inputs.ClearValues();assert(!gate.Gate());
  gate.Clear();
  RecallProfile<16> profile;profile.count=1;profile.bindings[0]={3,RecallKind::Input,2};
  auto input=[&](uint16_t field)->CVInputMap*{return field==3?gate.IiInput():nullptr;};
  assert(ApplyRecall(profile,input,virtual_inputs,1,8,CVInputMap::CountIiBindings));
  assert(gate.source_type()==DigitalInputMap::TYPE_II && gate.index()==1);
  RecallProfile<16> snapshot;assert(CaptureRecall(input,virtual_inputs,1,snapshot));
  assert(snapshot.bindings[0].field==3 && snapshot.bindings[0].channel==2);
  // Existing MIDI and physical sources retain their serialized byte meanings.
  gate.SetMidiMap(7);assert((gate.Pack()&255)==0xa7);
  frame.MIDIState.mapping[7].value=7680;assert(gate.Gate());
  gate.SetGateInput(2);assert((gate.Pack()&255)==0x42);
  frame.gate_high[2]=true;assert(gate.Gate());
  for(unsigned i=0;i<32;++i){
   virtual_inputs.SetDirection(i,D::Input,0);
   gate.Unpack((0xc0+i)|(1<<8));
   assert((gate.Pack()&255)==0xc0+i);
   virtual_inputs.SetRange(i,VirtualInputs::Range::Bipolar5);
   virtual_inputs.WriteVoltage(i+1,uint16_t(-8192));assert(!gate.Gate());
   virtual_inputs.WriteVoltage(i+1,2048);assert(!gate.Gate()); // exactly 1.25V
   virtual_inputs.WriteVoltage(i+1,2049);assert(gate.Gate());
  }
  DigitalInputMap retained[8]{};
  retained[7].Unpack(0xdf|(1<<8));
  DirectionalMappings staged;
  staged.SetDirection(31,D::Output,0);
  std::map<uint16_t,uint64_t> records;
  auto read=[&](uint16_t key,uint64_t& value){auto it=records.find(key);if(it==records.end())return false;value=it->second;return true;};
  assert(!ValidateDigitalPreset(read,10,20,retained,staged));
  records[10]=records[11]=records[12]=records[13]=0;
  assert(ValidateDigitalPreset(read,10,20,retained,staged));
  records[13]=uint64_t(0xdf)<<32;
  assert(!ValidateDigitalPreset(read,10,20,retained,staged));
  records.clear();records[20]=0;
  assert(ValidateDigitalPreset(read,10,20,retained,staged));
  gate.Unpack(0xc1|(2<<8));
  for(int i=0;i<4;++i){
   virtual_inputs.WriteVoltage(2,0);assert(!gate.Clock());
   virtual_inputs.WriteVoltage(2,8192);assert(gate.Clock()==(i%2==0));
  }
  auto clone=gate;assert(clone.Pack()==gate.Pack());
  gate.Clear();clone.Clear();
 }
 assert(CVInputMap::CountIiBindings(0)==baseline);
 puts("PASS: ii digital gate/edge, 32 slots, direction inventory, recall, packing, MIDI/physical regression");
}
