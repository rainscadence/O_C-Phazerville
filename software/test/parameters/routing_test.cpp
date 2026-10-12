#include "RoutingHost.h"
#include "ii/Dispatcher.h"
#include <cstdio>
#include <climits>
using phz::parameters::Owner;
using phz::parameters::Kind;
struct Lock {static inline int depth=0;Lock(){++depth;}~Lock(){--depth;}};
struct Backend:phz::ii::Backend {
 RoutingHostBase& app;bool wrong_type=false;int descriptions=0;
 Backend(RoutingHostBase& a):app(a){}
 bool Describe(const phz::ii::Write& w,phz::ii::Descriptor& d)override{
  assert(Lock::depth>0);
  if(w.slot!=0x301||!phz::ii::DescribeSettings(app,w.parameter,d))return false;
  d.instance=&app;d.type=app.applet_id();d.slot=w.slot;d.parameter=w.parameter;
  if(wrong_type&&++descriptions==2)++d.type;
  return true;
 }
 bool Apply(const phz::ii::Prepared& p)override{
  assert(Lock::depth>0);
  return phz::ii::ApplySettings(app,p.descriptor.parameter,p.value,p.descriptor.dependencies);
 }
};
void Send(RoutingHostBase& app,int id,uint16_t code,bool accepted,uint16_t slot=0x301){
 Backend backend(app);phz::ii::Dispatcher<Lock> d;d.SetActive(true);
 uint8_t f[]={1,uint8_t(slot>>8),uint8_t(slot),uint8_t(id>>8),uint8_t(id),uint8_t(code>>8),uint8_t(code)};
 assert(d.Capture(f,7));d.Service(backend);assert(d.Inspect().counts.applied==unsigned(accepted));assert(!Lock::depth);
}
int Read(const Owner& app,int id){int32_t value;assert(app.ReadParameter(id,value));return value;}
int CvCode(int code){return !code?0:code<=8?code+63:code<=24?code+87:code<=56?code+103:code==57?224:code+134;}
bool LegalSource(int s){return s==0||(s>=64&&s<=71)||(s>=96&&s<=111)||(s>=128&&s<=159)||(s>=192&&s<=223)||s==224;}
int RestoredSource(int s){
 if(s>0&&s<64)return s<=8?s+63:s<=16?s+87:128+std::min(s-17,31);
 if(s>=224)return 224;
 return LegalSource(s)?s:0;
}
void Matrix(MidSideApplet& a){
 float g=dbToScalar(a.gain);
 assert(a.mixers[0].gains[0]==g&&a.mixers[0].gains[1]==g);
 assert(a.mixers[1].gains[0]==g&&a.mixers[1].gains[1]==-g);
}
void Restore(){
 CrosspanApplet a{};a.Start();
 a.OnDataReceive(uint8_t(127)|(uint64_t(0x7f)<<8)|(uint64_t(uint8_t(-128))<<16));
 assert(a.crosspan==100&&a.crosspan_cv.Source()==0&&a.crosspan_cv.attenuversion==-127);
 for(int raw=0;raw<256;++raw)for(int source=0;source<256;++source)for(int shape=0;shape<2;++shape){
  int value=raw<128?raw:raw-256;
  a.OnDataReceive(uint64_t(raw)|(uint64_t(source)<<8)|(uint64_t(raw)<<16)|(uint64_t(shape)<<24)|0xfffffffe00000000ULL);
  assert(a.crosspan==constrain(value,0,100)&&a.crosspan_cv.Source()==RestoredSource(source));
  assert(a.crosspan_cv.attenuversion==constrain(value,-127,127)&&int(a.xfade_shape)==shape);
  auto saved=a.OnDataRequest();CrosspanApplet b{};b.OnDataReceive(saved);assert(b.OnDataRequest()==saved);
  a.Controller();a.View(); // no invalid CV indexing, including bad DAC bytes
 }
 // Every legal amount and curve preserves the original packed byte layout.
 for(int amount=0;amount<=100;++amount)for(int shape=0;shape<2;++shape){
  uint64_t saved=uint64_t(amount)|(uint64_t(159)<<8)|(uint64_t(uint8_t(-60))<<16)|(uint64_t(shape)<<24);
  a.OnDataReceive(saved);assert(a.OnDataRequest()==saved);
 }
 a.Unload();
 MidSideApplet b{};b.Start();
 for(int raw=0;raw<256;++raw){
  b.OnDataReceive(raw);int gain=constrain(raw<128?raw:raw-256,-90,90);
  assert(b.gain==gain&&b.OnDataRequest()==uint8_t(gain));Matrix(b);
 }
 puts("Routing restores: exhaustive Crosspan value/source/curve bytes, legacy mapping, valid layout and MidSide gain clamp PASS");
}
template<class App>void Parameters(int count){
 App a{};a.Start();assert(a.Parameters().count==size_t(count));
 for(uint32_t code=0;code<65536;++code)for(int id=1;id<=count;++id){
  const auto before=a.OnDataRequest();int values[4];for(int p=1;p<=count;++p)values[p-1]=Read(a,p);
  bool accepted=true;int expected=0;
  if constexpr(std::is_same_v<App,CrosspanApplet>){
   if(id==2){accepted=code<90;if(accepted)expected=CvCode(code);}
   else if(id==4){accepted=code<2;expected=code;}
   else{int lo=id==1?0:-127,hi=id==1?100:127;expected=lo+(int64_t(hi-lo)*code+32767)/65535;}
  }else expected=-90+(int64_t(180)*code+32767)/65535;
  Send(a,id,code,accepted);
  if(accepted)assert(Read(a,id)==expected);else assert(a.OnDataRequest()==before);
  for(int p=1;p<=count;++p)if(p!=id)assert(Read(a,p)==values[p-1]);
  if constexpr(std::is_same_v<App,MidSideApplet>)Matrix(a);
 }
 const auto saved=a.OnDataRequest();
 for(int id:{0,count+1,65535})Send(a,id,0,false);
 for(uint16_t slot:{uint16_t(1),uint16_t(0x101),uint16_t(0x201),uint16_t(0x306)})Send(a,1,0,false,slot);
 assert(a.OnDataRequest()==saved);
 Backend backend(a);phz::ii::Dispatcher<Lock> d;d.SetActive(true);uint8_t f[]={1,3,1,0,1,0,0};
 d.Capture(f,7);d.Begin();d.End(true);d.Service(backend);assert(d.Inspect().counts.applied==0&&a.OnDataRequest()==saved);
 backend.wrong_type=true;d.Capture(f,7);d.Service(backend);assert(d.Inspect().counts.stale==1&&a.OnDataRequest()==saved);
 assert(!a.SetParameter(1,INT_MIN)&&!a.SetParameter(1,INT_MAX));
 if constexpr(std::is_same_v<App,CrosspanApplet>){
  assert(!a.SetParameter(2,63)&&!a.SetParameter(3,-128)&&!a.SetParameter(3,128)&&!a.SetParameter(4,-1)&&!a.SetParameter(4,2));
 }
 assert(a.OnDataRequest()==saved);a.Unload();
 printf("%s all%d x65536 wire words, native domains, isolation, stereo-only destination and stale lifecycle PASS\n",a.applet_name(),count);
}
void Ui(){
 CrosspanApplet a{};a.Start();assert(a.xfade_shape==CrosspanApplet::EQUAL_POWER);
 a.editing=true;a.cursor=0;a.OnEncoderMove(INT_MAX);assert(Read(a,1)==100);a.OnEncoderMove(INT_MIN);assert(Read(a,1)==0);
 a.cursor=1;a.OnEncoderMove(1);assert(Read(a,2)==64);a.OnButtonPress();
 a.OnEncoderMove(-500);assert(Read(a,3)==-127);a.OnEncoderMove(500);assert(Read(a,3)==127);a.OnButtonPress();
 a.editing=true;a.cursor=2;a.OnEncoderMove(-1);assert(Read(a,4)==0);
 a.OnEncoderMove(INT_MAX);assert(Read(a,4)==1);a.OnEncoderMove(1);assert(Read(a,4)==1);a.OnEncoderMove(INT_MIN);assert(Read(a,4)==0);
 a.Unload();
 MidSideApplet b{};b.Start();assert(Read(b,1)==-6);b.OnEncoderMove(1);assert(Read(b,1)==-5);Matrix(b);
 b.OnEncoderMove(INT_MAX);assert(Read(b,1)==90);Matrix(b);b.OnEncoderMove(INT_MIN);assert(Read(b,1)==-90);Matrix(b);
 puts("Actual Crosspan nested CV/curve UI and MidSide signed matrix updates PASS");
}
bool Cable(RoutingHostBase& a,AudioStream& from,int f,AudioStream& to,int t){
 int found=0;for(size_t i=0;i<a.patch_count;++i){auto& c=a.patch[i];if(c.from==&from&&c.from_ch==f&&c.to==&to&&c.to_ch==t)++found;}return found==1;
}
void CrosspanDsp(){
 CrosspanApplet a{};a.Start();assert(a.patch_count==14);
 for(int from=0;from<2;++from)for(int to=0;to<2;++to){
  assert(Cable(a,a.input,from,a.vcas[from][to],0));
  assert(Cable(a,a.attenuations[from==to?0:1],0,a.vcas[from][to],1));
  assert(Cable(a,a.vcas[from][to],0,a.mixers[to],from));
 }
 for(int to=0;to<2;++to)assert(Cable(a,a.mixers[to],0,a.output,to));
 assert(a.InputStream()==&a.input&&a.OutputStream()==&a.output);
 for(int curve=0;curve<2;++curve)for(int amount=0;amount<=100;++amount){
  assert(a.SetParameter(1,amount)&&a.SetParameter(2,0)&&a.SetParameter(4,curve));
  auto at=a.attenuations[0].write_ix;a.Controller();float t=amount*0.01f;
  float out,in;
  if(curve==1){out=1-t;in=t;}else EqualPowerFade(out,in,t);
  int16_t straight=a.attenuations[0].buffer[at],cross=a.attenuations[1].buffer[at];
  assert(straight==float_to_q15(out)&&cross==float_to_q15(in));
  // Preserve the existing approximation, not an invented exact-power law.
  if(curve==0&&amount==50)assert(std::abs(out-0.7033548f)<0.000001f&&out==in);
  for(int from=0;from<2;++from)for(int to=0;to<2;++to){
   auto& v=a.vcas[from][to];v.Feed(0,from? -4096:8192);v.Feed(1,from==to?straight:cross);v.update();
   auto cv=from==to?straight:cross;int expected=HostQ15((from?-0.125f:0.25f)*cv/32768.0f);
   for(int sample:v.result.data)assert(sample==expected);
   a.mixers[to].Feed(from,expected);
  }
  for(int to=0;to<2;++to){
   auto& mix=a.mixers[to];mix.update();int expected=HostQ15(
    (HostQ15(0.25f*(to?cross:straight)/32768.0f)+HostQ15(-0.125f*(to?straight:cross)/32768.0f))/32768.0f);
   for(int sample:mix.result.data)assert(sample==expected);
  }
 }
 // Controller CV changes use the existing source ranges and nonlinear Atten().
 for(int source:{0,64,96,128})for(int atten:{-127,-60,0,60,127})for(int sign:{-1,1}){
  frame.adc[0]=sign*HEMISPHERE_MAX_INPUT_CV/2;frame.dac[0]=sign*HEMISPHERE_MAX_CV/2;frame.MIDIState.mapping[0].value=sign*HEMISPHERE_MAX_CV/2;
  a.SetParameter(1,50);a.SetParameter(2,source);a.SetParameter(3,atten);a.SetParameter(4,1);
  auto at=a.attenuations[0].write_ix;a.Controller();
  float t=constrain(0.5f+(source?sign*0.5f*0.001f*Atten(atten):0),0.0f,1.0f);
  assert(a.attenuations[0].buffer[at]==float_to_q15(1-t)&&a.attenuations[1].buffer[at]==float_to_q15(t));
 }
 a.Unload();a.Unload();assert(!a.attenuations[0].buffer&&!a.attenuations[1].buffer);a.Controller();
 puts("Crosspan real cable declarations, controller coefficients, VCA/mixer processing, signed CV and cleanup PASS (host scheduling/CMSIS seams)");
}
void MidSideDsp(){
 MidSideApplet a{};a.Start();assert(a.patch_count==6);
 for(int to=0;to<2;++to){assert(Cable(a,a.mixers[to],0,a.output,to));for(int from=0;from<2;++from)assert(Cable(a,a.input,from,a.mixers[to],from));}
 assert(a.InputStream()==&a.input&&a.OutputStream()==&a.output);
 for(int gain=-90;gain<=90;++gain){assert(a.SetParameter(1,gain));Matrix(a);
  for(auto input:std::array<std::array<int16_t,2>,4>{{{{8192,8192}},{{8192,-8192}},{{8192,0}},{{0,-4096}}}})
   for(int to=0;to<2;++to){auto& mix=a.mixers[to];mix.Feed(0,input[0]);mix.Feed(1,input[1]);mix.update();
    float scalar=dbToScalar(gain);int expected=HostQ15(input[0]/32768.0f*scalar+input[1]/32768.0f*(to?-scalar:scalar));
    for(int sample:mix.result.data)assert(sample==expected);
   }
 }
 puts("MidSide actual sum/difference matrices: every gain, correlated/opposite/one-sided signals and saturation PASS");
}
void Export(RoutingHostBase& a){
 for(size_t i=0;i<a.Parameters().count;++i){const auto& e=a.Parameters().entries[i];
  printf("PARAM\t%s\t0x%016llx\t%u\t%s\t%u\t%d\t%d\n",a.applet_name(),static_cast<unsigned long long>(a.applet_id()),e.id,e.key,unsigned(e.domain.kind),e.domain.minimum,e.domain.maximum);
  if(e.domain.kind==Kind::Enumeration)for(size_t j=0;j<e.domain.option_count;++j)printf("OPTION\t%s\t%u\t%u\t%d\n",a.applet_name(),e.id,e.domain.options[j].code,e.domain.options[j].native);
 }
}
int main(int argc,char** argv){
 for(unsigned slot=0;slot<32;++slot) {
  phz::ii::virtual_inputs.Reset();CrosspanApplet a{};
  a.OnDataReceive(uint64_t(50)|(uint64_t(192+slot)<<8)|(uint64_t(196)<<16));
  const auto saved=a.OnDataRequest();
  assert(phz::ii::virtual_inputs.SetRange(slot,phz::ii::VirtualInputs::Range::Off));
  a.OnDataReceive(saved);assert(a.OnDataRequest()==saved);
 }
 phz::ii::virtual_inputs.Reset();
 if(argc==2&&std::string(argv[1])=="--restore"){Restore();return 0;}
 if(argc==2&&std::string(argv[1])=="--crosspan"){Restore();Parameters<CrosspanApplet>(4);CrosspanDsp();return 0;}
 assert(argc==1);
 Restore();Parameters<CrosspanApplet>(4);Parameters<MidSideApplet>(1);Ui();CrosspanDsp();MidSideDsp();
 CrosspanApplet a{};MidSideApplet b{};Export(a);Export(b);
}
