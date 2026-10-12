#include "ii/RecallRoutes.h"
#include <map>
#include <string>
#include <memory>
#include <cassert>
#include <cstdio>
#define FLASHMEM
static uint32_t now=10000;
uint32_t millis(){return now;}
namespace HS {
struct CVInputMap {uint8_t source=0;int atten=60;uint8_t Source()const{return source;}bool SetSource(uint8_t s){source=s;return true;}static size_t CountIiBindings(size_t){return 0;}};
struct HemisphereApplet {CVInputMap private_input;bool edit=false;bool EditMode()const{return edit;}bool IsEditingInputMap()const{return edit;}CVInputMap* IiRecallInput(uint16_t field){return field==0x100?&private_input:nullptr;}};
CVInputMap cvmap[8];uint64_t appletIds[]={56,8,99,100};constexpr int HEMISPHERE_AVAILABLE_APPLETS=4,MESSAGE_POPUP=0;
struct DigitalInputMap {CVInputMap input;CVInputMap* IiInput(){return &input;}} trigmap[8];
}
using HemisphereApplet=HS::HemisphereApplet;
namespace OC {namespace CORE {bool app_isr_enabled=true;}
struct Settings {struct Address {bool enabled()const{return true;}}ii_address;}global_settings;}
namespace phz {namespace ii {DirectionalMappings virtual_inputs;struct InterruptLock{};struct Transition{explicit Transition(bool){}void Complete(){}};}}
using Records=std::map<uint16_t,uint64_t>;
struct FS {std::map<std::string,Records> files;bool exists(const char* f){return files.count(f);}}SD;
bool SDcard_Ready=false;
namespace PhzConfig {
FS myfs;Records current;bool fail=false;
struct ScopedContext {Records before;ScopedContext(){before.swap(current);}~ScopedContext(){before.swap(current);}};
void setValue(uint16_t k,uint64_t v){current[k]=v;}
bool getValue(uint16_t k,uint64_t& v){auto i=current.find(k);if(i==current.end())return false;v=i->second;return true;}
bool save_config(const char* file,FS& fs=myfs){if(fail)return false;fs.files[file]=current;return true;}
bool load_config(const char* file,FS& fs){if(!fs.exists(file))return false;current=fs.files[file];return true;}
}
class AppQuadrants {
 public:
 using IiRecallBank=phz::ii::RecallBank<128,16>;using IiRecallProfile=IiRecallBank::Profile;
 std::unique_ptr<IiRecallBank> ii_recall_bank;phz::ii::RecallOwnership<> ii_recall_owners;
 IiRecallProfile ii_recall_baseline[4];
 bool ii_recall_loaded=false,ii_recall_sd=false,ii_recall_restoring=false,isEditing=false,output_pending=false;
 uint32_t ii_recall_changed=0,ii_recall_polled=0,ii_recall_retry=0;
 HemisphereApplet objects[5];HemisphereApplet* active_applet[4]{&objects[0],&objects[1],&objects[2],&objects[3]};
 int active_applet_index[4]{0,2,2,3};int popups=0;
 void PokePopup(int,const char*){++popups;}
 void RecallLoad(uint8_t);bool RecallSave();void RecallTick();void RecallCapture(int,bool=false);void RecallBaseline();
 bool RecallSnapshot(int,IiRecallProfile&);void RecallSwitch(int,HemisphereApplet*);
 HS::CVInputMap* RecallInput(int,HemisphereApplet*,uint16_t);
};
#include "apps/QuadrantsRecall.h"
int main(){
 using namespace phz::ii;using D=DirectionalMappings::Direction;
 AppQuadrants a;
 PhzConfig::current[123]=456;
 a.RecallLoad(0);assert(a.ii_recall_loaded && PhzConfig::current.at(123)==456);
 assert(virtual_inputs.SetDirection(0,D::Output,0));
 assert(virtual_inputs.SetDirection(31,D::Output,0));
 assert(virtual_inputs.RouteOutput(1,1,8));
 a.RecallCapture(0);assert(a.ii_recall_bank->Find(56)->bindings[0].channel==1);
 assert(a.RecallSave() && !a.ii_recall_bank->Dirty());
 assert(OC::CORE::app_isr_enabled && PhzConfig::current.at(123)==456);
 a.active_applet_index[0]=1;a.RecallSwitch(0,a.active_applet[0]); // ADSR no defaults yet
 assert(virtual_inputs.OutputMappingFor(1)==0);
 assert(virtual_inputs.RouteOutput(1,32,8));a.RecallCapture(0);
 assert(a.ii_recall_bank->Find(8)->bindings[0].channel==32);
 // Duplicate in quadrant B, with cached private ii source, must start unmapped.
 a.active_applet_index[1]=1;a.objects[1].private_input.source=0xc2;
 a.RecallSwitch(1,a.active_applet[1]);
 assert(virtual_inputs.OutputMappingFor(1)==32 && virtual_inputs.OutputMappingFor(3)==0);
 assert(a.objects[1].private_input.source==0);
 a.active_applet_index[0]=0;a.RecallSwitch(0,a.active_applet[0]);
 assert(virtual_inputs.OutputMappingFor(1)==1 && virtual_inputs.OutputMappingFor(3)==0);
 a.active_applet_index[1]=2;a.RecallSwitch(1,a.active_applet[1]);
 a.active_applet_index[1]=1;a.RecallSwitch(1,a.active_applet[1]);
 assert(virtual_inputs.OutputMappingFor(3)==32);
 // Preset override baseline prevents learning. Only subsequently edited field is merged.
 virtual_inputs.RouteOutput(3,0,8);a.RecallBaseline();a.RecallCapture(1);
 assert(a.ii_recall_bank->Find(8)->bindings[0].channel==32);
 a.objects[1].private_input.source=0xc3;a.RecallCapture(1);
 const auto* profile=a.ii_recall_bank->Find(8);
 assert(profile->count==2 && profile->bindings[0].channel==32);
 assert(profile->bindings[1].field==0x100 && profile->bindings[1].channel==4);
 // Pending editor changes are not recorded; aborted switch resets the baseline.
 a.isEditing=true;a.objects[1].private_input.source=0xc4;a.RecallCapture(1);
 assert(a.ii_recall_bank->Find(8)->bindings[1].channel==4);a.isEditing=false;
 PhzConfig::fail=true;assert(!a.RecallSave() && a.ii_recall_bank->Dirty());
 assert(PhzConfig::current.at(123)==456 && OC::CORE::app_isr_enabled);
 PhzConfig::fail=false;assert(a.RecallSave());
 a.RecallLoad(1);assert(!a.ii_recall_bank->Find(8));
 a.RecallLoad(0);assert(a.ii_recall_bank->Find(8)->bindings[0].channel==32);
 PhzConfig::myfs.files["II_002.CFG"][0]=0;
 a.RecallLoad(2);assert(!a.ii_recall_loaded); // corrupt data must not be overwritten
 assert(a.RecallSave() && PhzConfig::myfs.files["II_002.CFG"].at(0)==0);
 puts("PASS: production recall adapter, switching, duplicate/private unmapping, preset delta merge, isolated context, failed save and bank reload");
}
