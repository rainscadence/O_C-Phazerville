#include "AppletHost.h"
#include "applets/Voltage.h"
#include "applets/AttenuateOffset.h"
#include "ii/Dispatcher.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
using namespace phz::parameters;
struct Lock { Lock() {} };
struct Backend : phz::ii::Backend {
  Owner* owner;
  explicit Backend(Owner& o) : owner(&o) {}
  bool Describe(const phz::ii::Write& w,phz::ii::Descriptor& d) override {
    if (w.slot != 1 || !phz::ii::DescribeSettings(*owner,w.parameter,d)) return false;
    d.instance=owner; d.type=uint64_t{1}<<50; d.slot=w.slot; d.parameter=w.parameter;
    return true;
  }
  bool Apply(const phz::ii::Prepared& p) override {
    return phz::ii::ApplySettings(*owner,p.descriptor.parameter,p.value,p.descriptor.dependencies);
  }
};
void Send(Owner& app,uint16_t id,uint16_t code,bool expected=true) {
  Backend backend(app); phz::ii::Dispatcher<Lock> engine; engine.SetActive(true);
  uint8_t frame[]={1,0,1,uint8_t(id>>8),uint8_t(id),uint8_t(code>>8),uint8_t(code)};
  assert(engine.Capture(frame,7)); engine.Service(backend);
  assert(engine.Inspect().counts.applied == unsigned(expected));
}
int32_t Read(const Owner& app,uint16_t id) { int32_t v; assert(app.ReadParameter(id,v)); return v; }
#include "clock_slew_checks.h"
// Not mentioned anywhere in the dispatcher or transport: automatic support is
// a consequence of declaring settings through the same Owner interface.
class FutureApplet : public Owner {
 public:
  int amount=0, derived=0; bool enabled=true; int mode=0; uint64_t generation=0;
  bool available=true, malformed_boolean=false;
  static bool BooleanSnapshot(const Owner& base,Domain& d) {
    if(static_cast<const FutureApplet&>(base).malformed_boolean)d.maximum=2;
    return true;
  }
  static bool Snapshot(const Owner& base,Domain& d) {
    const auto& app=static_cast<const FutureApplet&>(base);
    d.maximum=app.mode ? 20 : 10; d.dependencies=app.generation;
    return app.available;
  }
  static int32_t Get(const Owner& base) { return static_cast<const FutureApplet&>(base).amount; }
  static bool Set(Owner& base,int32_t v,uint64_t token) {
    auto& app=static_cast<FutureApplet&>(base);
    if (token != app.generation || v==7) return false; // no partial mutation
    app.amount=v; app.derived=v*2; return true;
  }
  Schema Parameters() const override {
    static constexpr Entry entries[]={
      {1,"amount","A renamed UI label","native",{Kind::Integer,0,20},Get,Set,Snapshot},
      {2,"retired","Removed control","native",{Kind::Integer,0,1},nullptr,nullptr},
      []() constexpr { auto e=Field<FutureApplet,&FutureApplet::enabled>::Make(8,"enabled","On","boolean",0,1); e.describe=BooleanSnapshot;return e; }(),
    };
    static_assert(Valid(entries)); return {entries,3};
  }
};
void FutureTests() {
  FutureApplet app; Send(app,8,0); assert(!app.enabled); Send(app,8,65535); assert(app.enabled);
  Send(app,2,1,false); Send(app,9,1,false); Send(app,1,65535); assert(app.amount==10&&app.derived==20);
  app.mode=1; ++app.generation; Send(app,1,65535); assert(app.amount==20&&app.derived==40);
  assert(!app.SetParameter(1,3,0) && app.amount==20); // stale dependency
  assert(!app.SetParameter(1,7,app.generation) && app.amount==20&&app.derived==40);
  app.available=false; Send(app,1,0,false); assert(app.amount==20);
  app.malformed_boolean=true;Send(app,8,0,false);assert(app.enabled);
  assert(app.Parameters().entries[2].id==8); // ID not table index
  puts("Future applet: inherited routing, appended IDs, tombstone, custom atomic setter/dependencies PASS");
}
void DeclarationTests() {
  constexpr Entry a[]={ {1,"x","X","native",{Kind::Integer,0,10},nullptr,nullptr} };
  static_assert(Valid(a));
  constexpr Entry zero[]={ {0,"x","X","native",{Kind::Integer,0,10},nullptr,nullptr} };
  static_assert(!Valid(zero));
  constexpr Entry duplicate[]={a[0],a[0]}; static_assert(!Valid(duplicate));
  constexpr Option options[]={{0,30},{1,40}};
  Domain d{Kind::Enumeration,0,0,options,2};
  assert(NativeAllowed(d,30)&&NativeAllowed(d,40)&&!NativeAllowed(d,0));
  d.options=nullptr; assert(!NativeAllowed(d,30));
  d.options=options; d.option_count=257; assert(!NativeAllowed(d,30));
  d.option_count=0; assert(!NativeAllowed(d,30));
  d.kind=Kind::Boolean;
  for(int v=-2;v<4;++v)assert(NativeAllowed(d,v)==(v==0||v==1));
  d.kind=Kind::Integer;d.minimum=10;d.maximum=0;assert(!NativeAllowed(d,5));
  d.kind=Kind(255);assert(!NativeAllowed(d,0));
  puts("Declaration validation and native-domain rejection PASS");
}
void RealAppletTests() {
  Voltage v{};v.Start();AttenuateOffset a{};a.Start();
  for (int octaveMax : {4,6}) for (int octaveZero : {0,3}) {
    HS::octave_max=octaveMax;OC::DAC::kOctaveZero=octaveZero;
    for(uint32_t code=0;code<=65535;++code) {
      Send(v,1,code);Send(v,2,code);Send(v,3,code);Send(v,4,code);
      for(uint16_t id=1;id<=5;++id)Send(a,id,code);
      const int minimum=HEMISPHERE_MIN_CV/128, maximum=HEMISPHERE_MAX_CV/128;
      const int expected=minimum+int((int64_t(maximum-minimum)*code+32767)/65535);
      assert(Read(v,1)==expected&&Read(v,3)==expected);
      assert(Read(v,2)==int(code>=32768)&&Read(v,4)==int(code>=32768));
      const int offsetMax=HEMISPHERE_MAX_CV/128;
      const int offsetExpected=-offsetMax+int((int64_t(2*offsetMax)*code+32767)/65535);
      assert(Read(a,1)==offsetExpected&&Read(a,3)==offsetExpected);
      const int levelExpected=-126+int((252LL*code+32767)/65535);
      assert(Read(a,2)==levelExpected&&Read(a,4)==levelExpected);
      assert(Read(a,5)==int(code>=32768));
    }
  }
  HS::octave_max=6;OC::DAC::kOctaveZero=3;
  Send(v,1,65535);Send(v,2,0); v.gates[0]=false;v.Controller();assert(v.outputs[0]==HEMISPHERE_MAX_CV);
  v.gates[0]=true;v.Controller();assert(v.outputs[0]==0);
  Send(v,2,65535);v.Controller();assert(v.outputs[0]==HEMISPHERE_MAX_CV);
  Send(a,1,32767);Send(a,3,32767); // symmetric offset domain ->0
  assert(a.SetParameter(2,63)&&a.SetParameter(4,63));
  a.inputs[0]=256;a.inputs[1]=128;Send(a,5,0);a.Controller();assert(a.outputs[0]==256&&a.outputs[1]==128);
  Send(a,5,65535);a.Controller();assert(a.outputs[1]==384);
  int32_t old=Read(v,1); v.editing=true;v.OnEncoderMove(-1);assert(Read(v,1)==old-1);
  auto blob=v.OnDataRequest();Voltage restored{};restored.OnDataReceive(blob);assert(Read(restored,1)==Read(v,1));
  // A prepared range from one hardware setting cannot be reused after changing it.
  phz::ii::Descriptor before;assert(phz::ii::DescribeSettings(v,1,before));
  HS::octave_max=4;assert(!phz::ii::ApplySettings(v,1,before.maximum,before.dependencies));
  Send(v,500,0,false);Send(a,0,0,false);
  // One absolute update may not alter other settings. Exercise both endpoints
  // and a midpoint separately; no assumptions about private field layout.
  for(Owner* owner : {static_cast<Owner*>(&v),static_cast<Owner*>(&a)}) {
    for(size_t selected=0;selected<owner->Parameters().count;++selected) {
      int32_t snapshot[5];
      for(uint16_t code : {uint16_t(0),uint16_t(32768),uint16_t(65535)}) {
        for(size_t j=0;j<owner->Parameters().count;++j)snapshot[j]=Read(*owner,owner->Parameters().entries[j].id);
        Send(*owner,owner->Parameters().entries[selected].id,code);
        for(size_t j=0;j<owner->Parameters().count;++j)if(j!=selected)assert(Read(*owner,owner->Parameters().entries[j].id)==snapshot[j]);
      }
    }
  }
  AttenuateOffset restoredAtten{};restoredAtten.OnDataReceive(a.OnDataRequest());
  for(uint16_t id=1;id<=5;++id)assert(Read(restoredAtten,id)==Read(a,id));
  puts("Real Voltage/AttenOff: all9 parameters x65536 codes x4 range profiles, Controller effects, isolated writes, UI/preset compatibility PASS");
}
void Export(const char* type,unsigned id,const Owner& app) {
  for(size_t i=0;i<app.Parameters().count;++i) {
    const auto& e=app.Parameters().entries[i];Domain d;assert(app.DescribeParameter(e.id,d));
    printf("PARAM\t%s\t%u\t%u\t%s\t%u\t%d\t%d\n",type,id,e.id,e.key,unsigned(d.kind),d.minimum,d.maximum);
  }
}
int main(){DeclarationTests();FutureTests();RealAppletTests();ClockSlewChecks();Voltage v{};AttenuateOffset a{};Export("Voltage",43,v);Export("AttenuateOffset",56,a);ClockDivider c{};Slew s{};Export("ClockDivider",6,c);Export("Slew",19,s);}
