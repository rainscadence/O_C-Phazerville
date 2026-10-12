#include "LadderHost.h"
#include "ii/Dispatcher.h"
#include <cstdio>
#include <climits>
using phz::parameters::Owner;
using phz::parameters::Kind;
constexpr int Lo[12]={-12288,0,0,0,0,-127,0,-127,0,-127,0,-127};
constexpr int Hi[12]={12288,180,400,50,0,127,0,127,0,127,0,127};
bool IsSource(int id){return id>=5&&id%2;}
int Source(int code){return !code?0:code<=8?code+63:code<=24?code+87:code<=56?code+103:code==57?224:code+134;}
bool LegalSource(int s){return s==0||(s>=64&&s<=71)||(s>=96&&s<=111)||(s>=128&&s<=159)||(s>=192&&s<=223)||s==224;}
int RestoredSource(int s){
 if(s>0&&s<64)return s<=8?s+63:s<=16?s+87:128+std::min(s-17,31);
 if(s>=224)return 224;
 return LegalSource(s)?s:0;
}
int Read(const Owner& a,int id){int32_t value;assert(a.ReadParameter(id,value));return value;}
struct Lock {static inline int depth=0;Lock(){++depth;}~Lock(){--depth;}};
template<AudioChannels C>struct Backend:phz::ii::Backend {
 LadderApplet<C>& a;bool wrong_type=false;int descriptions=0;uint16_t slot;
 Backend(LadderApplet<C>& a,uint16_t slot):a(a),slot(slot){}
 bool Describe(const phz::ii::Write& w,phz::ii::Descriptor& d)override{
  assert(Lock::depth);
  if(w.slot!=slot||!phz::ii::DescribeSettings(a,w.parameter,d))return false;
  d.instance=&a;d.type=a.applet_id();d.slot=w.slot;d.parameter=w.parameter;
  if(wrong_type&&++descriptions==2)++d.type;
  return true;
 }
 bool Apply(const phz::ii::Prepared& p)override{
  assert(Lock::depth);
  return phz::ii::ApplySettings(a,p.descriptor.parameter,p.value,p.descriptor.dependencies);
 }
};
template<AudioChannels C>auto Save(LadderApplet<C>& a){
 std::array<uint64_t,4> d{{0,0,0x123456789abcdef0ULL,0xfedcba9876543210ULL}};
 a.OnDataRequest(d);assert(d[2]==0x123456789abcdef0ULL&&d[3]==0xfedcba9876543210ULL);return d;
}
template<AudioChannels C>void Send(LadderApplet<C>& a,int id,uint16_t code,bool accepted,uint16_t slot){
 Backend<C> b(a,C==MONO?0x101:0x301);phz::ii::Dispatcher<Lock> d;d.SetActive(true);
 uint8_t f[]={1,uint8_t(slot>>8),uint8_t(slot),uint8_t(id>>8),uint8_t(id),uint8_t(code>>8),uint8_t(code)};
 assert(d.Capture(f,7));d.Service(b);assert(d.Inspect().counts.applied==unsigned(accepted)&&!Lock::depth);
}
template<AudioChannels C>void Restore(){
 LadderApplet<C> a{};a.Start();std::array<uint64_t,4> d{};
 d[0]=uint64_t(uint16_t(32767))|(uint64_t(65535)<<16)|(uint64_t(65535)<<32)|(uint64_t(65535)<<48);
 d[1]=0x807f807f807f807fULL;a.OnDataReceive(d);
 printf("Ladder restore: pitch=%d resonance=%d drive=%d passband=%d source=%d attenuation=%d\n",a.pitch,a.res,a.gain,a.pb_gain,a.pitch_cv.Source(),a.pitch_cv.attenuversion);
 assert(a.pitch==12288&&a.res==0&&a.gain==0&&a.pb_gain==0);
 assert(a.pitch_cv.Source()==0&&a.pitch_cv.attenuversion==-127);
 // Sweep each 16-bit scalar independently; other values stay at valid defaults.
 for(int field=0;field<4;++field)for(uint32_t raw=0;raw<65536;++raw){
  std::array<int,4> v{{1536,0,100,50}};v[field]=raw<32768?raw:int(raw)-65536;
  d[0]=0;for(int p=0;p<4;++p)d[0]|=uint64_t(uint16_t(v[p]))<<(p*16);
  d[1]=0;a.OnDataReceive(d);
  int actual[]={a.pitch,a.res,a.gain,a.pb_gain};
  for(int p=0;p<4;++p)assert(actual[p]==constrain(v[p],Lo[p],Hi[p]));
 }
 // Independent source x attenuation combinations for every mapping.
 for(int map=0;map<4;++map)for(int s=0;s<256;++s)for(int raw=0;raw<256;++raw){
  d[0]=uint64_t(1536)|(uint64_t(100)<<32)|(uint64_t(50)<<48);
  d[1]=uint64_t(s|(raw<<8))<<(16*map);a.OnDataReceive(d);
  CVInputMap* maps[]={&a.pitch_cv,&a.res_cv,&a.gain_cv,&a.pitch_cv2};
  for(int p=0;p<4;++p){
   assert(maps[p]->Source()==(p==map?RestoredSource(s):0));
   assert(maps[p]->attenuversion==(p==map?constrain(raw<128?raw:raw-256,-127,127):0));
  }
  a.Controller();a.View();
 }
 d[0]=uint64_t(uint16_t(-1536))|(uint64_t(180)<<16)|(uint64_t(400)<<32)|(uint64_t(50)<<48);
 d[1]=0x3c9fff80c0604040ULL;a.OnDataReceive(d);auto saved=Save(a);
 assert(saved[0]==d[0]&&saved[1]==d[1]);LadderApplet<C> b{};b.OnDataReceive(saved);assert(Save(b)==saved);
 puts("Ladder independent scalar/map-byte restore sweeps and valid layout PASS");
}
template<AudioChannels C>void Wire(){
 LadderApplet<C> a{};a.Start();assert(a.Parameters().count==12);
 for(uint32_t code=0;code<65536;++code)for(int id=1;id<=12;++id){
  auto before=Save(a);int values[12];for(int p=1;p<=12;++p)values[p-1]=Read(a,p);
  bool accepted=!IsSource(id)||code<90;
  int expected=IsSource(id)?(accepted?Source(code):0):Lo[id-1]+(int64_t(Hi[id-1]-Lo[id-1])*code+32767)/65535;
  Send(a,id,code,accepted,C==MONO?0x101:0x301);
  if(accepted)assert(Read(a,id)==expected);else assert(Save(a)==before);
  for(int p=1;p<=12;++p)if(p!=id)assert(Read(a,p)==values[p-1]);
 }
 const auto before=Save(a);
 for(int id:{0,13,65535})Send(a,id,0,false,C==MONO?0x101:0x301);
 for(uint16_t slot:{uint16_t(1),uint16_t(C==MONO?0x301:0x101),uint16_t(0x306)})Send(a,1,0,false,slot);
 assert(Save(a)==before);
 Backend<C> b(a,C==MONO?0x101:0x301);phz::ii::Dispatcher<Lock> d;d.SetActive(true);
 uint8_t f[]={1,uint8_t(b.slot>>8),1,0,1,0,0};d.Capture(f,7);d.Begin();d.End(true);d.Service(b);
 assert(!d.Inspect().counts.applied&&Save(a)==before);
 b.wrong_type=true;d.Capture(f,7);d.Service(b);assert(d.Inspect().counts.stale==1&&Save(a)==before);
 for(int id=1;id<=12;++id){assert(!a.SetParameter(id,INT_MIN)&&!a.SetParameter(id,INT_MAX));}
 assert(Save(a)==before);
 if constexpr(C==MONO){Backend<C> right(a,0x201);phz::ii::Dispatcher<Lock> r;r.SetActive(true);f[1]=2;r.Capture(f,7);r.Service(right);assert(r.Inspect().counts.applied==1);}
 puts("Ladder twelve x65536 wire words, isolation, domain rejection, slots and lifecycle PASS");
}
template<AudioChannels C>void Ui(){
 LadderApplet<C> a{};a.Start();a.editing=true;a.cursor=0;a.OnEncoderMove(1);assert(Read(a,1)==1552);
 a.OnEncoderMove(INT_MAX);assert(Read(a,1)==12288);a.OnEncoderMove(INT_MIN);assert(Read(a,1)==-12288);
 for(auto pair:std::array<std::array<int,2>,3>{{{{3,2}},{{5,3}},{{7,4}}}}){
  a.cursor=pair[0];a.OnEncoderMove(INT_MAX);assert(Read(a,pair[1])==Hi[pair[1]-1]);
  a.OnEncoderMove(INT_MIN);assert(Read(a,pair[1])==Lo[pair[1]-1]);
 }
 for(auto pair:std::array<std::array<int,2>,4>{{{{1,5}},{{2,7}},{{4,9}},{{6,11}}}}){
  a.editing=true;a.cursor=pair[0];a.OnEncoderMove(1);assert(Read(a,pair[1])==64);
  a.OnButtonPress();a.OnEncoderMove(INT_MAX);assert(Read(a,pair[1]+1)==127);
  a.OnEncoderMove(INT_MIN);assert(Read(a,pair[1]+1)==-127);a.OnButtonPress();
 }
 puts("Ladder actual UI pitch step, scalar boundaries and four nested CV editors PASS");
}
bool Near(float a,float b){return std::abs(a-b)<=0.0001f*std::max(1.0f,std::abs(b));}
template<AudioChannels C>void Effects(){
 LadderApplet<C> a{};a.Start();assert(a.patch_count==2*C);
 assert(a.InputStream()==&a.input&&a.OutputStream()==&a.output);
 for(int i=0;i<C;++i){
  auto in=a.patch[i*2],out=a.patch[i*2+1];
  assert(in.from==&a.input&&in.from_ch==i&&in.to==&a.filters[i]&&in.to_ch==0);
  assert(out.from==&a.filters[i]&&out.from_ch==0&&out.to==&a.output&&out.to_ch==i);
 }
 for(int id=1;id<=12;++id){
  for(int n=0;n<3;++n){
   int v=IsSource(id)?(n==0?0:n==1?64:128):n==0?Lo[id-1]:n==1?(Lo[id-1]+Hi[id-1])/2:Hi[id-1];
   assert(a.SetParameter(id,v));a.Controller();
   float hz=PitchToRatio(a.pitch+a.pitch_cv.In()+a.pitch_cv2.In())*C3;
   float r=constrain(0.01f*a.res+a.res_cv.InF(),0.0f,1.8f);
   float drive=0.01f*a.gain+a.gain_cv.InF(),pbg=0.01f*a.pb_gain;
   float host=std::min(drive,4.0f),over=host>1?1+(host-1)*(1-pbg):std::max(host,0.0f);
   AudioFilterLadder reference;reference.frequency(hz);
   for(auto& filter:a.filters){
    assert(Near(filter.Fbase,hz)&&Near(filter.alpha,reference.alpha)&&Near(filter.K,4*r));
    assert(Near(filter.pbg,pbg)&&Near(filter.host_overdrive,host)&&Near(filter.overdrive,over));
   }
  }
 }
 // Four modulation assignments, independent signed amounts and simultaneous pitch inputs.
 for(auto& m:frame.MIDIState.mapping)m.value=ONE_OCTAVE;
 frame.adc[0]=ONE_OCTAVE;frame.dac[0]=-ONE_OCTAVE;
 for(int map:{5,7,9,11})for(int source:{64,96,128})for(int atten:{-127,-60,0,60,127}){
  a.SetParameter(map,source);a.SetParameter(map+1,atten);a.Controller();
  float hz=PitchToRatio(a.pitch+a.pitch_cv.In()+a.pitch_cv2.In())*C3;
  float r=constrain(0.01f*a.res+a.res_cv.InF(),0.0f,1.8f);
  float host=std::min(0.01f*a.gain+a.gain_cv.InF(),4.0f);
  float pbg=0.01f*a.pb_gain;
  float over=host>1?1+(host-1)*(1-pbg):std::max(host,0.0f);
  for(auto& filter:a.filters){
   assert(Near(filter.Fbase,hz)&&std::isfinite(filter.alpha));
   assert(Near(filter.K,4*r)&&Near(filter.pbg,pbg));
   assert(Near(filter.host_overdrive,host)&&Near(filter.overdrive,over));
  }
 }
 // Execute the pinned filter's real linear fallback on modest signals. Its
 // normal CMSIS FIR path is deliberately unavailable in this host fixture.
 LadderApplet<C> b{};b.Start();b.SetParameter(1,1536);b.SetParameter(2,20);b.SetParameter(3,100);b.Controller();
 for(auto& f:b.filters){assert(!f.polyOn);f.Feed(0,2048);f.update();assert(f.transmitted);bool any=false;for(int x:f.result.data)any|=x!=0;assert(any);}
 if constexpr(C==STEREO)assert(std::equal(std::begin(b.filters[0].result.data),std::end(b.filters[0].result.data),std::begin(b.filters[1].result.data)));
 for(auto& f:b.filters){f.fail_allocate=true;f.transmitted=false;f.update();assert(!f.transmitted);}
 puts("Ladder actual controller/filter setters, coupled passband/drive, cable wiring and linear-fallback audio PASS; default FIR/target timing not covered");
}
template<AudioChannels C>void Export(){
 LadderApplet<C> a{};const char* mode=C==MONO?"mono":"stereo";
 for(size_t i=0;i<a.Parameters().count;++i){const auto& e=a.Parameters().entries[i];
  printf("PARAM\t%s\t0x%016llx\t%u\t%s\t%u\t%d\t%d\n",mode,(unsigned long long)a.applet_id(),e.id,e.key,unsigned(e.domain.kind),e.domain.minimum,e.domain.maximum);
  if(e.domain.kind==Kind::Enumeration)for(size_t j=0;j<e.domain.option_count;++j)printf("OPTION\t%s\t%u\t%u\t%d\n",mode,e.id,e.domain.options[j].code,e.domain.options[j].native);
 }
}
int main(int argc,char** argv){
 for(unsigned slot=0;slot<32;++slot) {
  phz::ii::virtual_inputs.Reset();LadderApplet<MONO> a{};
  std::array<uint64_t,4> data{};
  for(unsigned map=0;map<4;++map) data[1] |= uint64_t(0xc000 | (192+slot)) << (16*map);
  a.OnDataReceive(data);const auto saved=Save(a);
  assert(phz::ii::virtual_inputs.SetRange(slot,phz::ii::VirtualInputs::Range::Off));
  a.OnDataReceive(saved);assert(Save(a)==saved);
 }
 phz::ii::virtual_inputs.Reset();
 if(argc==2&&std::string(argv[1])=="--ui-overflow"){LadderApplet<MONO> a{};a.editing=true;a.cursor=0;a.OnEncoderMove(INT_MAX);assert(a.pitch==12288);return 0;}
 if(argc==2&&std::string(argv[1])=="--restore"){Restore<MONO>();return 0;}
 assert(argc==1);Restore<MONO>();Restore<STEREO>();Wire<MONO>();Wire<STEREO>();Ui<MONO>();Ui<STEREO>();Effects<MONO>();Effects<STEREO>();Export<MONO>();Export<STEREO>();
}
