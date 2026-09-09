#include "InputHost.h"
#include "ii/Dispatcher.h"
#include <cstdio>
using phz::parameters::Owner;
using phz::parameters::Kind;
struct TestLock {static inline int depth=0;TestLock(){++depth;}~TestLock(){--depth;}};
struct InputBackend:phz::ii::Backend {
 Owner* app;uint16_t slot;uint64_t type;bool change_on_recheck=false;int describes=0;
 InputBackend(Owner& o,uint16_t s,uint64_t t):app(&o),slot(s),type(t){}
 bool Describe(const phz::ii::Write& w,phz::ii::Descriptor& d)override {
  assert(TestLock::depth>0);
  if(w.slot!=slot||!phz::ii::DescribeSettings(*app,w.parameter,d))return false;
  d.instance=app;d.type=type;d.slot=slot;d.parameter=w.parameter;
  if(change_on_recheck&&++describes==2)++d.type;
  return true;
 }
 bool Apply(const phz::ii::Prepared& p)override{
  assert(TestLock::depth>0);return phz::ii::ApplySettings(*app,p.descriptor.parameter,p.value,p.descriptor.dependencies);
 }
};
template<AudioChannels C>void Send(InputApplet<C>& o,uint16_t id,uint16_t code,bool success){
 const uint16_t slot=C==MONO?0x101:0x301;
 InputBackend backend(o,slot,o.applet_id());phz::ii::Dispatcher<TestLock> engine;engine.SetActive(true);
 uint8_t frame[]={1,uint8_t(slot>>8),uint8_t(slot),uint8_t(id>>8),uint8_t(id),uint8_t(code>>8),uint8_t(code)};
 assert(engine.Capture(frame,7));engine.Service(backend);assert(engine.Inspect().counts.applied==unsigned(success));assert(TestLock::depth==0);
}
int32_t Read(const Owner& o,uint16_t id){int32_t v;assert(o.ReadParameter(id,v));return v;}
template<AudioChannels C>auto State(InputApplet<C>& a){std::array<uint64_t,4> d{};a.OnDataRequest(d);return d;}
template<AudioChannels C>void Preset(InputApplet<C>& a,int mode,int level,int source=0,int atten=60){
 std::array<uint64_t,4> d{};
 d[1]=uint8_t(mode)|(uint64_t(uint8_t(level))<<8)|(uint64_t(uint8_t(source))<<16)|(uint64_t(uint8_t(atten))<<24);
 a.OnDataReceive(d);
}
template<AudioChannels C>bool LegalMode(int m){
 return m==0||m==1||m==3||(C==STEREO&&m==2)
#ifdef AUDIO_INTERFACE
 ||m==4||m==5||m==7||(C==STEREO&&m==6)
#endif
 ;
}
template<AudioChannels C>void MixChecks(InputApplet<C>& a){
 const int mode=Read(a,1),level=Read(a,2),base=mode%4;const float scalar=level<-90?0.0f:dbToScalar(level);
 for(int ch=0;ch<2;++ch){
  assert(a.srcmix[ch].gains[0]==(mode<4?1.0f:0.0f));
  assert(a.srcmix[ch].gains[1]==(mode<4?0.0f:1.0f));
 }
 for(int ch=0;ch<C;++ch){
  assert(a.mixer[ch].gains[0]==((base==0||base==3||(base==2&&ch==0))?scalar:0));
  assert(a.mixer[ch].gains[1]==((base==1||base==3||(base==2&&ch==1))?scalar:0));
  assert(a.mixer[ch].gains[2]==1.0f); // existing audio chain passthrough remains unity
 }
}
template<AudioChannels C>void UnloadChecks(){
 InputApplet<C> a{};
 for(int pass=0;pass<20;++pass){
  a.Start();assert(a.attenuations[0].buffer&&a.attenuations[1].buffer);
  a.Controller();a.Unload();assert(!a.attenuations[0].buffer&&!a.attenuations[1].buffer);
  a.Unload();a.Controller(); // idempotent cleanup/no post-unload write
 }
 printf("Input %d repeat Start/Unload: both owned CV buffers released PASS\n",int(C));
}
void AllocationChecks(){
 AudioMixer<3> mixer;mixer.gain(0,1);mixer.Feed(0,1234);mixer.fail_allocate=true;mixer.update();assert(!mixer.transmitted);
 mixer.fail_allocate=false;mixer.Feed(0,1234);mixer.update();assert(mixer.transmitted);
 for(int sample:mixer.result.data)assert(sample==1234);
 puts("Actual mixer allocation exhaustion/recovery PASS");
}
template<AudioChannels C>void Parameters(){
 InputApplet<C> a{};a.Start();assert(a.Parameters().count==4);
 assert(a.applet_id()==0x000000049ddc3af4ULL);
 for(uint32_t code=0;code<=65535;++code)for(uint16_t id=1;id<=4;++id){
  const auto before=State(a);int32_t saved[4];for(int p=1;p<=4;++p)saved[p-1]=Read(a,p);
  bool valid=true;int expected=0;
  if(id==1){valid=LegalMode<C>(code);expected=code;}
  else if(id==3){valid=code<=57;if(valid)expected=code==0?0:code<=8?63+code:code<=24?87+code:code<=56?103+code:224;}
  else{const int lo=id==2?-91:-127,hi=id==2?90:127;expected=lo+int((int64_t(hi-lo)*code+32767)/65535);}
  Send(a,id,code,valid);
  if(valid)assert(Read(a,id)==expected);else assert(State(a)==before);
  for(int p=1;p<=4;++p)if(p!=id)assert(Read(a,p)==saved[p-1]);
  MixChecks(a);
 }
 const auto saved=State(a);
 for(int id:{0,5,65535})Send(a,id,0,false);
 for(int m=-128;m<=255;++m)if(!LegalMode<C>(m))assert(!a.SetParameter(1,m));
 assert(!a.SetParameter(2,-92)&&!a.SetParameter(2,91)&&!a.SetParameter(3,63)&&!a.SetParameter(4,-128)&&!a.SetParameter(4,128));
 assert(State(a)==saved);MixChecks(a);
 // Stale same-type transition and identity mismatch cannot reach a setter.
 const uint16_t slot=C==MONO?0x101:0x301;
 InputBackend backend(a,slot,a.applet_id());phz::ii::Dispatcher<TestLock> engine;engine.SetActive(true);
 uint8_t f[]={1,uint8_t(slot>>8),uint8_t(slot),0,2,0,0};
 engine.Capture(f,7);engine.Begin();engine.End(true);engine.Service(backend);assert(engine.Inspect().counts.applied==0&&State(a)==saved);
 backend.change_on_recheck=true;engine.Capture(f,7);engine.Service(backend);assert(engine.Inspect().counts.stale==1&&State(a)==saved);
 a.Unload();printf("Input %d all4 x65536 wire codes, domains/isolation/live mix/lifecycle PASS\n",int(C));
}
template<AudioChannels C>void PresetChecks(){
 InputApplet<C> a{};a.Start();
 // Every signed mode byte: valid unchanged, mono dual becomes equivalent left;
 // all other unavailable/corrupt choices become Left, never a table overrun.
 for(int m=-128;m<=127;++m)for(int level:{-128,-91,-90,0,90,127}){
  Preset(a,m,level,0x7f,-128);int expected=LegalMode<C>(m)?m:0;
#ifdef AUDIO_INTERFACE
  if(C==MONO&&m==6)expected=4;
#endif
  assert(Read(a,1)==expected&&Read(a,2)==constrain(level,-91,90)&&Read(a,3)==0&&Read(a,4)==-127);
  a.View();MixChecks(a);
 }
 for(int m=0;m<8;++m)if(LegalMode<C>(m))for(int level=-91;level<=90;++level){
  Preset(a,m,level,159,-60);const auto d=State(a);InputApplet<C> b{};b.OnDataReceive(d);assert(State(b)==d);
 }
 // Legacy bit widths/mix-to-mono migration and old CV source migration remain.
 for(int mode=0;mode<4;++mode)for(int mix=0;mix<2;++mix){
  std::array<uint64_t,4> d{};
  d[0]=uint64_t(mode)|(uint64_t(uint8_t(-6))<<4)|(uint64_t(mix)<<12)|(uint64_t(1|(60<<8))<<13);
  a.OnDataReceive(d);assert(Read(a,1)==((mix||mode==2)?3:mode));assert(Read(a,2)==-6&&Read(a,3)==64&&Read(a,4)==60);
 }
 // Reusing a save buffer must not preserve the legacy selector.
 std::array<uint64_t,4> reused{1,2,3,4};a.OnDataRequest(reused);assert(reused[0]==0);
 a.Unload();printf("Input %d old/current presets, corruption recovery and View bounds PASS\n",int(C));
}
template<AudioChannels C>void UiChecks(){
 InputApplet<C> a{};a.Start();a.editing=true;a.cursor=0;
 int previous=0;Preset(a,0,0);
 for(int m=1;m<8;++m)if(LegalMode<C>(m)){a.OnEncoderMove(1);assert(Read(a,1)==m);previous=m;MixChecks(a);}
 a.OnEncoderMove(1);assert(Read(a,1)==previous);
 for(int m=previous-1;m>=0;--m)if(LegalMode<C>(m)){a.OnEncoderMove(-1);assert(Read(a,1)==m);}
 a.OnEncoderMove(-1);assert(Read(a,1)==0);
 a.OnEncoderMove(100);assert(Read(a,1)==previous);a.OnEncoderMove(-100);assert(Read(a,1)==0);
 a.cursor=1;a.OnEncoderMove(-1);assert(Read(a,2)==-1);MixChecks(a);
 a.OnEncoderMove(-500);assert(Read(a,2)==-91);a.OnEncoderMove(500);assert(Read(a,2)==90);MixChecks(a);
 a.cursor=2;a.OnEncoderMove(1);assert(Read(a,3)==64);
 a.OnButtonPress();a.OnEncoderMove(-500);assert(Read(a,4)==-127);
 a.OnEncoderMove(500);assert(Read(a,4)==127);a.OnButtonPress();
 a.Unload();printf("Input %d native mode/gain/source/nested CV controls PASS\n",int(C));
}
template<AudioChannels C>void DspChecks(){
 InputApplet<C> a{};a.Start();
 for(int m=0;m<8;++m)if(LegalMode<C>(m))for(int lvl:{-91,-6,0,90}){
  Preset(a,m,lvl);MixChecks(a);
  for(int ch=0;ch<2;++ch){
   a.srcmix[ch].Feed(0,1000+ch*1000);a.srcmix[ch].Feed(1,-3000-ch*1000);a.srcmix[ch].update();
   const int expected=m<4?1000+ch*1000:-3000-ch*1000;
   for(int sample:a.srcmix[ch].result.data)assert(sample==expected);
  }
  // Run production final mixer on contrasting left/right + passthrough samples.
  // Scheduler/cable wiring is a host seam, not a hardware loopback measurement.
  for(int ch=0;ch<C;++ch){
   auto& mix=a.mixer[ch];mix.Feed(0,8192);mix.Feed(1,-4096);mix.Feed(2,1024);mix.update();
   const float gain=lvl<-90?0:dbToScalar(lvl);const int base=m%4;
   const float left=(base==0||base==3||(base==2&&ch==0))?0.25f*gain:0;
   const float right=(base==1||base==3||(base==2&&ch==1))?-0.125f*gain:0;
   const int expected=HostQ15(left+right+0.03125f);
   for(int sample:mix.result.data)assert(sample==expected);
  }
 }
 // Production controller drives BOTH VCA buffers using source/attenuation.
 for(int src:{0,64,96,128})for(int att:{-127,-60,0,60,127}){
  Preset(a,0,0,src,att);frame.adc[0]=HEMISPHERE_MAX_INPUT_CV/2;
  frame.dac[0]=HEMISPHERE_MAX_CV/2;frame.MIDIState.mapping[0].value=HEMISPHERE_MAX_CV/2;
  size_t at[2]={a.attenuations[0].write_ix,a.attenuations[1].write_ix};
  const int expected=float_to_q15(src?0.001f*Atten(att)*0.5f:1.0f);
  a.Controller();for(int ch=0;ch<2;++ch)assert(a.attenuations[ch].buffer[at[ch]]==expected);
 }
 assert(a.InputStream()==&a.passthru&&a.OutputStream()==&a.output);
 a.Unload();printf("Input %d actual source/final mixer DSP, passthrough/gain/CV controls PASS\n",int(C));
}
template<AudioChannels C>void Export(){
 InputApplet<C> a{};
 for(size_t i=0;i<a.Parameters().count;++i){
  const auto& e=a.Parameters().entries[i];
  printf("PARAM\t%s\t0x%016llx\t%u\t%s\t%u\t%d\t%d\n",C==MONO?"mono":"stereo",static_cast<unsigned long long>(a.applet_id()),e.id,e.key,unsigned(e.domain.kind),e.domain.minimum,e.domain.maximum);
  if(e.domain.kind==Kind::Enumeration)for(size_t o=0;o<e.domain.option_count;++o)
   printf("OPTION\t%s\t%u\t%u\t%d\n",C==MONO?"mono":"stereo",e.id,e.domain.options[o].code,e.domain.options[o].native);
 }
}
int main(int argc,char** argv){
 const std::string mode=argc>1?argv[1]:"";
 if(mode=="--unload"){UnloadChecks<MONO>();UnloadChecks<STEREO>();return 0;}
 if(mode=="--restore"){PresetChecks<MONO>();PresetChecks<STEREO>();return 0;}
 if(mode=="--allocation"){AllocationChecks();return 0;}
 assert(mode.empty());
 UnloadChecks<MONO>();UnloadChecks<STEREO>();AllocationChecks();
 Parameters<MONO>();Parameters<STEREO>();PresetChecks<MONO>();PresetChecks<STEREO>();
 UiChecks<MONO>();UiChecks<STEREO>();DspChecks<MONO>();DspChecks<STEREO>();Export<MONO>();Export<STEREO>();
}
