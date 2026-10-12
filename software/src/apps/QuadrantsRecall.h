#pragma once
// Included after AppQuadrants. All file operations run from UI/main-loop hooks,
// never the Wire callback or clock-synchronized preset callback.

FLASHMEM __attribute__((noinline)) HS::CVInputMap* AppQuadrants::RecallInput(int slot, HemisphereApplet* applet, uint16_t field) {
  if(slot<0 || slot>=4 || !applet)return nullptr;
  if(field==1 || field==2)return &HS::cvmap[slot*2+field-1];
  if(field==3 || field==4)return HS::trigmap[slot*2+field-3].IiInput();
  return applet->IiRecallInput(field);
}
FLASHMEM __attribute__((noinline)) bool AppQuadrants::RecallSnapshot(int slot, IiRecallProfile& out) {
  if(slot<0 || slot>=4 || !active_applet[slot])return false;
  phz::ii::InterruptLock lock;
  return phz::ii::CaptureRecall([&](uint16_t field){return RecallInput(slot,active_applet[slot],field);},
      phz::ii::virtual_inputs,slot*2+1,out);
}
FLASHMEM __attribute__((noinline)) void AppQuadrants::RecallBaseline() {
  std::array<uint64_t,4> ids{};
  for(int slot=0;slot<4;++slot){
    const int index=active_applet_index[slot];
    if(active_applet[slot] && index>=0 && index<HS::HEMISPHERE_AVAILABLE_APPLETS)ids[slot]=HS::appletIds[index];
    ii_recall_baseline[slot]={};
    RecallSnapshot(slot,ii_recall_baseline[slot]);
  }
  ii_recall_owners.RestorePreset(ids);
}
FLASHMEM __attribute__((noinline)) void AppQuadrants::RecallCapture(int slot, bool confirmed) {
  if(!ii_recall_loaded || ii_recall_restoring || !OC::global_settings.ii_address.enabled() ||
     slot<0 || slot>=4 || !active_applet[slot] || (!confirmed && (isEditing || output_pending ||
     active_applet[slot]->EditMode() || active_applet[slot]->IsEditingInputMap())))return;
  IiRecallProfile current;
  if(!RecallSnapshot(slot,current))return;
  const auto id=ii_recall_owners.Applet(slot);
  if(!id)return;
  bool duplicate=false;
  for(int other=0;other<4;++other)if(other!=slot && ii_recall_owners.Applet(other)==id)duplicate=true;
  const auto& baseline=ii_recall_baseline[slot];
  const auto* saved=ii_recall_bank->Find(id);
  IiRecallProfile changed=saved ? *saved : IiRecallProfile{};
  bool edited=false;
  // Merge ONLY changed fields; a preset override or a rejected/conflicting
  // recall must not become a new default as a side effect of a different edit.
  for(size_t i=0;i<current.count;++i){
    const auto& value=current.bindings[i];
    const phz::ii::RecallBinding* before=nullptr;
    for(size_t j=0;j<baseline.count;++j)
      if(baseline.bindings[j].field==value.field && baseline.bindings[j].kind==value.kind)before=&baseline.bindings[j];
    if(!before || before->channel==value.channel)continue;
    edited=true;size_t j=0;
    while(j<changed.count && (changed.bindings[j].field!=value.field || changed.bindings[j].kind!=value.kind))++j;
    if(j==changed.bindings.size()){PokePopup(HS::MESSAGE_POPUP,"ii recall full");return;}
    changed.bindings[j]=value;if(j==changed.count)++changed.count;
  }
  if(edited && (!duplicate || ii_recall_owners.UsesDefaults(slot))){
    const auto revision=ii_recall_bank->Revision();
    if(!ii_recall_bank->Remember(id,changed)){PokePopup(HS::MESSAGE_POPUP,"ii recall conflict");return;}
    if(revision!=ii_recall_bank->Revision())ii_recall_changed=millis();
  }
  ii_recall_baseline[slot]=current;
}
FLASHMEM __attribute__((noinline)) void AppQuadrants::RecallSwitch(int slot, HemisphereApplet* old) {
  if(!ii_recall_loaded || !OC::global_settings.ii_address.enabled())return;
  const uint64_t id=HS::appletIds[active_applet_index[slot]];
  if(ii_recall_owners.Applet(slot)==id)return;
  const IiRecallProfile empty{};
  bool conflict=false;
  {phz::ii::InterruptLock lock;
    // Cached private mappings are detached too, not just the shared CV pair.
    if(old)phz::ii::ApplyRecall(empty,[&](uint16_t field){return RecallInput(slot,old,field);},phz::ii::virtual_inputs,slot*2+1,8);
    ii_recall_owners.Select(slot,id);
    const auto* profile=ii_recall_owners.UsesDefaults(slot) ? ii_recall_bank->Find(id) : nullptr;
    conflict=!phz::ii::ApplyRecall(profile ? *profile : empty,
      [&](uint16_t field){return RecallInput(slot,active_applet[slot],field);},phz::ii::virtual_inputs,slot*2+1,8,HS::CVInputMap::CountIiBindings);
  }
  RecallSnapshot(slot,ii_recall_baseline[slot]);
  if(conflict)PokePopup(HS::MESSAGE_POPUP,"ii recall conflict");
}
FLASHMEM __attribute__((noinline)) bool AppQuadrants::RecallSave() {
  if(!ii_recall_loaded || !ii_recall_bank || !ii_recall_bank->Dirty())return true;
  phz::ii::Transition transition(false);
  struct Pause {bool previous=OC::CORE::app_isr_enabled;
    Pause(){OC::CORE::app_isr_enabled=false;}~Pause(){OC::CORE::app_isr_enabled=previous;}} pause;
  const uint64_t revision=ii_recall_bank->Revision();
  char filename[16];snprintf(filename,sizeof(filename),"II_%03u.CFG",unsigned(ii_recall_bank->Bank()));
  PhzConfig::ScopedContext context;
  const bool encoded=ii_recall_bank->Export([](uint16_t key,uint64_t value){PhzConfig::setValue(key,value);return true;});
  const bool saved=encoded && (ii_recall_sd ? PhzConfig::save_config(filename,SD) : PhzConfig::save_config(filename));
  ii_recall_bank->Saved(revision,saved);
  // Save failure changes no live configuration; retain dirty edits for retry.
  transition.Complete();return saved;
}
FLASHMEM __attribute__((noinline)) void AppQuadrants::RecallLoad(uint8_t bank) {
  ii_recall_loaded=false;
  std::unique_ptr<IiRecallBank> next(new(std::nothrow) IiRecallBank(bank));
  std::unique_ptr<IiRecallBank> scratch(new(std::nothrow) IiRecallBank(bank));
  if(!next || !scratch){PokePopup(HS::MESSAGE_POPUP,"ii recall memory");return;}
  char filename[16],backup[20];snprintf(filename,sizeof(filename),"II_%03u.CFG",unsigned(bank));
  snprintf(backup,sizeof(backup),"%s.bak",filename);
  ii_recall_sd=SDcard_Ready;
  FS& fs=ii_recall_sd ? static_cast<FS&>(SD) : static_cast<FS&>(PhzConfig::myfs);
  const bool exists=fs.exists(filename) || fs.exists(backup);
  bool valid=!exists;
  {PhzConfig::ScopedContext context;
    if(exists && PhzConfig::load_config(filename,fs))
      valid=next->Import([](uint16_t key,uint64_t& value){return PhzConfig::getValue(key,value);},*scratch);
  }
  if(!valid){PokePopup(HS::MESSAGE_POPUP,"ii recall invalid");return;}
  ii_recall_bank=std::move(next);ii_recall_loaded=true;
  RecallBaseline();
}
FLASHMEM __attribute__((noinline)) void AppQuadrants::RecallTick() {
  if(!ii_recall_loaded || ii_recall_restoring)return;
  const uint32_t now=millis();
  if(uint32_t(now-ii_recall_polled)<100)return;
  ii_recall_polled=now;
  for(int slot=0;slot<4;++slot)RecallCapture(slot);
  if(!isEditing && !output_pending && ii_recall_bank->Dirty() &&
     uint32_t(now-ii_recall_changed)>=2000 && uint32_t(now-ii_recall_retry)>=5000){
    ii_recall_retry=now;RecallSave();
  }
}
