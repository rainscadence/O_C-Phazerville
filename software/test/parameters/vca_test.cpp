#include "VcaHost.h"
// Test-only observation access; all calculations/setters below are production.
#define private public
#include "audio_applets/VCAApplet.h"
#undef private
#include "ii/Dispatcher.h"
#include <cstdio>
using phz::parameters::Owner;
using phz::parameters::Kind;
struct TestLock {static inline int depth=0;TestLock(){++depth;}~TestLock(){--depth;}};
struct AudioBackend:phz::ii::Backend {
 Owner* app;uint16_t slot;bool change_on_recheck=false;int describes=0;
 AudioBackend(Owner& o,uint16_t s):app(&o),slot(s){}
 bool Describe(const phz::ii::Write& w,phz::ii::Descriptor& d)override {
  assert(TestLock::depth>0);
  if(w.slot!=slot||!phz::ii::DescribeSettings(*app,w.parameter,d))return false;
  d.instance=app;d.type=0x15a1c1;d.slot=slot;d.parameter=w.parameter;
  if(change_on_recheck&&++describes==2)++d.type;
  return true;
 }
 bool Apply(const phz::ii::Prepared& p)override{
  assert(TestLock::depth>0);return phz::ii::ApplySettings(*app,p.descriptor.parameter,p.value,p.descriptor.dependencies);
 }
};
void Send(Owner& o,uint16_t slot,uint16_t id,uint16_t code,bool success=true){
 AudioBackend backend(o,slot);phz::ii::Dispatcher<TestLock> engine;engine.SetActive(true);
 uint8_t frame[]={1,uint8_t(slot>>8),uint8_t(slot),uint8_t(id>>8),uint8_t(id),uint8_t(code>>8),uint8_t(code)};
 assert(engine.Capture(frame,7));engine.Service(backend);assert(engine.Inspect().counts.applied==unsigned(success));assert(TestLock::depth==0);
}
int32_t Read(const Owner& o,uint16_t id){int32_t v;assert(o.ReadParameter(id,v));return v;}
template<AudioChannels C>auto State(VcaApplet<C>& a){std::array<uint64_t,4> d{};a.OnDataRequest(d);return d;}
template<AudioChannels C>void Preset(VcaApplet<C>& a,int l,int b,int s,int ls=0,int ss=0,int la=60,int sa=60,bool r=true,bool inv=false){
 std::array<uint64_t,4> d{};
 d[0]=uint8_t(l)|(uint64_t(uint8_t(b))<<8)|(uint64_t(uint8_t(s))<<16)|(uint64_t(r)<<62)|(uint64_t(inv)<<63);
 d[1]=uint8_t(ls)|(uint64_t(uint8_t(la))<<8)|(uint64_t(uint8_t(ss))<<16)|(uint64_t(uint8_t(sa))<<24);
 a.OnDataReceive(d);
}
template<AudioChannels C>int Control(VcaApplet<C>& a){
 const size_t i=a.cv_stream.write_ix;a.Controller();return a.cv_stream.buffer[i];
}
template<AudioChannels C>void ShapeRegression(){
 VcaApplet<C> app{};app.Start();app.level_cv.SetInput(0);app.shape_cv.SetInput(1);
 frame.adc[0]=HEMISPHERE_MAX_INPUT_CV/2;frame.adc[1]=0;
 const int linear=Control(app);
 frame.adc[1]=HEMISPHERE_MAX_INPUT_CV;const int curved=Control(app);
 printf("VCA %d shape CV: linear=%d full-shape=%d\n",int(C),linear,curved);fflush(stdout);
 assert(curved<linear/2);assert(app.shape==0);
 app.Unload();
}
void AllocationChecks(){
 AudioVCA vca;vca.Feed(0,16384);vca.Feed(1,16384);vca.fail_allocate=true;vca.update();
 assert(!vca.transmitted);vca.fail_allocate=false;vca.Feed(0,16384);vca.Feed(1,16384);vca.update();assert(vca.transmitted);
 InterpolatingStream<> cv;cv.Acquire();for(int i=0;i<8;++i)cv.Push(100);
 cv.fail_allocate=true;const float pos=cv.read_ix;cv.update();assert(!cv.transmitted&&cv.read_ix==pos);
 cv.fail_allocate=false;cv.update();assert(cv.transmitted);cv.Release();
 puts("Actual VCA/interpolator allocation exhaustion and recovery PASS");
}
void MapChecks(){
 CVInputMap map{};
 for(int source=0;source<=255;++source){
  const bool valid=source==0||(source>=64&&source<72)||(source>=96&&source<112)||(source>=128&&source<160)||(source>=192&&source<224)||source==224;
  const auto old=map.Pack();
  assert(map.SetSource(source)==valid);if(!valid)assert(map.Pack()==old);else assert(map.Source()==source);
  for(int atten=-127;atten<=127;++atten){
   map.attenuversion=atten;const auto bits=map.Pack();CVInputMap back{};back.Unpack(bits);
   assert(back.Source()==map.Source()&&back.attenuversion==atten);
  }
 }
 // Restored invalid type must not make source cycling read outside ordered_types.
 map.Unpack(0xa0);map.ChangeSource(1);assert(map.Source()==64);
 puts("Actual CV mapping: native source rejection/isolation, negative attenuation packing PASS");
}
template<AudioChannels C>void Parameters(){
 VcaApplet<C> app{};app.Start();const uint16_t slot=C==MONO?0x101:0x301;
 assert(app.Parameters().count==9);
 for(uint32_t code=0;code<=65535;++code){
  for(uint16_t id=1;id<=9;++id){
   const bool enumeration=id==2||id==6;const auto before=State(app);
   int32_t expected=0;
   if(enumeration){
    if(code<=89){expected=code==0?0:code<=8?63+code:code<=24?87+code:code<=56?103+code:code==57?224:134+code;}
   }else{
    phz::parameters::Domain d;assert(app.DescribeParameter(id,d));
    expected=d.kind==Kind::Boolean?int(code>=32768):d.minimum+int((int64_t(d.maximum-d.minimum)*code+32767)/65535);
   }
   int32_t saved[9];for(uint16_t p=1;p<=9;++p)saved[p-1]=Read(app,p);
   const bool valid=!enumeration||code<=89;
   Send(app,slot,id,code,valid);
   if(valid)assert(Read(app,id)==expected);else assert(State(app)==before);
   for(uint16_t p=1;p<=9;++p)if(p!=id)assert(Read(app,p)==saved[p-1]);
  }
 }
 for(int id=0;id<=10;id+=10)Send(app,slot,id,0,false);
 auto saved=State(app);assert(!app.SetParameter(1,91));assert(!app.SetParameter(5,-1));assert(!app.SetParameter(3,-128));assert(!app.SetParameter(8,2));assert(State(app)==saved);
 // Same-type load/topology transitions invalidate pending/prepared work.
 AudioBackend backend(app,slot);phz::ii::Dispatcher<TestLock> engine;engine.SetActive(true);
 uint8_t f[]={1,uint8_t(slot>>8),uint8_t(slot),0,1,0,0};
 engine.Capture(f,7);engine.Begin();engine.End(true);engine.Service(backend);assert(engine.Inspect().counts.applied==0);
 backend.change_on_recheck=true;engine.Capture(f,7);engine.Service(backend);assert(engine.Inspect().counts.stale==1&&State(app)==saved);
 // Real setter updates every actual AudioVCA, not just saved/UI fields.
 Preset(app,-6,-12,0,64,0,60,60,false,false);
 for(int setting : {-91,-90,-12,0,12,90}){
  assert(app.SetParameter(1,setting));assert(app.SetParameter(4,setting));
  const float expected=setting<-90?0.0f:dbToScalar(setting);
  for(auto& v:app.vcas)assert(v.level_==expected&&v.bias_==expected);
 }
 for(int r=0;r<2;++r){assert(app.SetParameter(8,r));for(auto& v:app.vcas)assert(v.rectify_==bool(r));}
 // Genuine DSP multiply/rectify/saturation, positive and negative inputs.
 for(int l:{-91,-6,0,90})for(int b:{-91,-12,0})for(int r:{0,1})for(int signal:{-16384,16384})for(int cv:{-16384,16384}){
  Preset(app,l,b,0,0,0,60,60,r);
  for(auto& v:app.vcas){
   v.Feed(0,signal);v.Feed(1,cv);v.update();assert(v.transmitted);
   float mod=(b<-90?0.0f:dbToScalar(b))+(l<-90?0.0f:dbToScalar(l))*cv/32768.0f;
   if(r&&mod<0)mod=0;
   const int expected=HostQ15(mod*signal/32768.0f);
   for(int sample:v.result.data)assert(sample==expected);
  }
 }
 // Existing preset representation round-trips all legal byte-native settings.
 for(int value=-91;value<=90;++value){Preset(app,value,90-(value+91),50,71,159,-127,127);auto d=State(app);VcaApplet<C> other{};other.OnDataReceive(d);assert(State(other)==d);}
 for(int s=0;s<=100;++s){Preset(app,-6,-12,s,224,0);auto d=State(app);VcaApplet<C> other{};other.OnDataReceive(d);assert(State(other)==d);}
 // Malformed preset values cannot reach frame arrays or unsupported domains.
 Preset(app,127,-128,127,0x7f,0xff,-128,-128);
 assert(Read(app,1)==90&&Read(app,4)==-91&&Read(app,5)==100);
 assert(Read(app,2)==0&&Read(app,6)==224&&Read(app,3)==-127&&Read(app,7)==-127);
 // Local edits and nested map editor use same native parameter semantics.
 Preset(app,0,-91,50);app.editing=true;app.cursor=0;app.OnEncoderMove(-1);assert(Read(app,1)==-1);
 for(auto& v:app.vcas)assert(v.level_==dbToScalar(-1));
 app.cursor=1;app.OnEncoderMove(1);assert(Read(app,2)==64);app.OnButtonPress();app.OnEncoderMove(-200);assert(Read(app,3)==-127);
 app.OnButtonPress();app.editing=true;app.cursor=4;app.OnEncoderMove(1);assert(Read(app,6)==64);app.OnButtonPress();app.OnEncoderMove(200);assert(Read(app,7)==127);
 app.OnButtonPress();app.editing=true;app.cursor=5;app.OnEncoderMove(1);assert(Read(app,8)==0);
 app.cursor=6;app.OnEncoderMove(1);assert(Read(app,9)==1);
 app.Unload();
 printf("VCA %d all9 x65536 codes, isolation, DSP/stereo effects, native UI/presets/lifecycle PASS\n",int(C));
}
template<AudioChannels C>void ControllerChecks(){
 VcaApplet<C> a{};a.Start();Preset(a,0,-91,0,64,65);
 for(int shape=0;shape<=100;++shape)for(int att:{-127,-60,0,60,127})for(int cv:{-HEMISPHERE_MAX_INPUT_CV,0,HEMISPHERE_MAX_INPUT_CV/2,HEMISPHERE_MAX_INPUT_CV}){
  a.shape=shape;a.shape_cv.attenuversion=att;
  for(int input:{-HEMISPHERE_MAX_INPUT_CV,0,HEMISPHERE_MAX_INPUT_CV/2,HEMISPHERE_MAX_INPUT_CV}){
   frame.adc[0]=input;frame.adc[1]=cv;a.invert=false;
   const int out=Control(a);a.invert=true;const int inverted=Control(a);
   assert(std::abs(out+inverted)<=1);assert(a.shape==shape);
  }
 }
 // Selection and attenuation actually affect the response.
 Preset(a,0,-91,50,64,65,60,60);frame.adc[0]=HEMISPHERE_MAX_INPUT_CV/2;frame.adc[1]=HEMISPHERE_MAX_INPUT_CV/2;
 const int full=Control(a);a.shape_cv.attenuversion=0;const int half=Control(a);a.shape_cv.attenuversion=-60;const int linear=Control(a);
 assert(full<half&&half<linear&&linear==16384);
 a.shape_cv.SetSource(96);frame.dac[0]=HEMISPHERE_MAX_CV/2;a.shape_cv.attenuversion=60;assert(Control(a)==full);
 a.shape_cv.SetSource(128);frame.MIDIState.mapping[0].value=HEMISPHERE_MAX_CV/2;assert(Control(a)==full);
 a.level_cv.attenuversion=127;a.shape=100;assert(Control(a)==32767); // no exponent overflow at amplified CV
 a.Unload();printf("VCA %d actual shape/source/attenuation/invert controller effects PASS\n",int(C));
}
template<AudioChannels C>void Export(){
 VcaApplet<C> a{};
 for(size_t i=0;i<a.Parameters().count;++i){
  const auto&e=a.Parameters().entries[i];
  printf("PARAM\t%s\t0x000000000015a1c1\t%u\t%s\t%u\t%d\t%d\n",C==MONO?"mono":"stereo",e.id,e.key,unsigned(e.domain.kind),e.domain.minimum,e.domain.maximum);
 }
}
int main(int argc,char** argv){
 for (unsigned slot=0;slot<32;++slot) {
  phz::ii::virtual_inputs.Reset();
  VcaApplet<MONO> a{};
  Preset(a,-6,-12,50,192+slot,192+slot,-60,60);
  const auto saved=State(a);
  assert(phz::ii::virtual_inputs.SetRange(slot,phz::ii::VirtualInputs::Range::Off));
  a.OnDataReceive(saved);
  assert(State(a)==saved); // legacy disabled input binding must not disappear
 }
 phz::ii::virtual_inputs.Reset();
 if(argc>1&&std::string(argv[1])=="--negative"){
  VcaApplet<MONO> a{};a.Start();Preset(a,0,-91,1,64,0,-120);
  frame.adc[0]=HEMISPHERE_MAX_INPUT_CV;
  const float input=a.level_cv.InF(1.0f);assert(input < -1);
  const int expected=float_to_q15(a.varexp(0.3f,input));
  const int actual=Control(a);
  printf("Negative CV preservation: input=%f actual=%d expected=%d\n",input,actual,expected);fflush(stdout);
  assert(actual==expected);a.Unload();return 0;
 }
 if(argc>1&&std::string(argv[1])=="--allocation"){AllocationChecks();return 0;}
 if(argc>1&&std::string(argv[1])=="--shape"){ShapeRegression<MONO>();ShapeRegression<STEREO>();return 0;}
 ShapeRegression<MONO>();ShapeRegression<STEREO>();AllocationChecks();MapChecks();
 Parameters<MONO>();Parameters<STEREO>();ControllerChecks<MONO>();ControllerChecks<STEREO>();Export<MONO>();Export<STEREO>();
 for(const auto& o:phz::parameters::CvSourceOptions)printf("OPTION\t%u\t%d\n",o.code,o.native);
}
