#include "Processor.h"
#include "LatticeSamples.h"
#include <filesystem>
#include <stdexcept>

namespace {
bool validWavSnapshot(const lattice::Pattern& m){
 if(m.numerator<1||m.numerator>16||(m.denominator!=4&&m.denominator!=8)
    ||!std::isfinite(m.bpm)||m.bpm<40||m.bpm>240||m.dispersion<0||m.dispersion>2
    ||(m.gridStep!=60&&m.gridStep!=120)||m.groups.empty()||m.groups.size()>32)return false;
 for(float value:{m.density,m.development,m.fill,m.space})
  if(!std::isfinite(value)||value<0||value>1)return false;
 int remaining=m.eighths();
 for(int group:m.groups){if(group<1||group>remaining)return false;remaining-=group;}
 if(remaining!=0)return false;
 int events=0;
 for(const auto& bar:m.bars)for(const auto& lane:bar){
  if(lane.empty()||lane.size()>size_t(m.barTicks()))return false;
  int position=0;
  for(const auto& cell:lane){
   if(cell.start!=position||cell.length<1||cell.length>m.barTicks()-position
      ||cell.velocity<0||cell.velocity>127)return false;
   position+=cell.length;
   if(cell.velocity>0&&++events>65536)return false;
  }
  if(position!=m.barTicks())return false;
 }
 return true;
}

// writer 只持有适配器；底层文件保留到最终 WAV 头写回后，才能检查所有写入错误。
class WavOutput final: public juce::OutputStream {
 juce::FileOutputStream& file;
 bool& failed;
public:
 WavOutput(juce::FileOutputStream& stream,bool& error):file(stream),failed(error){}
 void flush()override{file.flush();if(file.getStatus().failed())failed=true;}
 juce::int64 getPosition()override{return file.getPosition();}
 bool setPosition(juce::int64 position)override{const bool ok=file.setPosition(position);failed|=!ok;return ok;}
 bool write(const void* data,size_t size)override{const bool ok=file.write(data,size);failed|=!ok;return ok;}
};
}

LatticeProcessor::LatticeProcessor():AudioProcessor(BusesProperties().withOutput("Output",juce::AudioChannelSet::stereo(),true)){loadSamples();changed();}
LatticeProcessor::~LatticeProcessor(){
 cancelWavExport();
 std::lock_guard<std::mutex> lock(wavJobMutex);
 if(wavThread.joinable())wavThread.join();
}

LatticeProcessor::WavExportResult LatticeProcessor::getWavExportResult()const{
 std::lock_guard<std::mutex> lock(wavResultMutex);
 return wavResult;
}

void LatticeProcessor::cancelWavExport(){
 // 与最终替换串行：提交前取消保留旧目标，提交后取消不撤销成功结果。
 std::lock_guard<std::mutex> lock(wavResultMutex);
 if(wavResult.state==WavExportState::running)wavCancel.store(true,std::memory_order_release);
}

bool LatticeProcessor::startWavExport(const juce::File& destination){
 std::unique_lock<std::mutex> job(wavJobMutex,std::try_to_lock);
 if(!job.owns_lock()||isWavExporting())return false;
 if(wavThread.joinable())wavThread.join();
 wavProgress.store(0,std::memory_order_relaxed);
 try{
  const auto snapshot=[this]{juce::ScopedLock lock(modelLock);return pattern;}();
  const bool validPath=destination!=juce::File()&&destination.hasFileExtension("wav")
      &&destination.getParentDirectory().isDirectory()&&!destination.isDirectory()
      &&!destination.isSymbolicLink()&&destination.getParentDirectory().hasWriteAccess()
      &&(!destination.exists()||destination.hasWriteAccess());
  if(!validPath||!validWavSnapshot(snapshot)){
   std::lock_guard<std::mutex> lock(wavResultMutex);
   wavResult={WavExportState::failed,destination,snapshot.bpm,
       validPath?"WAV export refused: invalid pattern snapshot.":"WAV export refused: invalid or unwritable .wav destination."};
   return false;
  }
  {
   std::lock_guard<std::mutex> lock(wavResultMutex);
   wavCancel.store(false,std::memory_order_release);
   wavResult={WavExportState::running,destination,snapshot.bpm,
       "Panel pattern.bpm " + juce::String(snapshot.bpm,2) + "; not host tempo/automation. All 8 tracks; 16 bars + 3.5 s tail."};
   wavExporting.store(true,std::memory_order_release);
  }
  const bool replaceExisting=destination.existsAsFile();
  wavThread=std::thread([this,snapshot,destination,replaceExisting]{runWavExport(snapshot,destination,replaceExisting);});
  return true;
 }catch(const std::exception& error){
  std::lock_guard<std::mutex> lock(wavResultMutex);
  wavResult={WavExportState::failed,destination,0,"WAV export could not start: " + juce::String(error.what())};
 }catch(...){
  std::lock_guard<std::mutex> lock(wavResultMutex);
  wavResult={WavExportState::failed,destination,0,"WAV export could not start."};
 }
 wavExporting.store(false,std::memory_order_release);
 return false;
}

void LatticeProcessor::runWavExport(const lattice::Pattern& snapshot,const juce::File& destination,bool replaceExisting){
 juce::File temporary;
 bool ownsTemporary=false;
 auto result=getWavExportResult();
 auto checkCancellation=[this]{if(wavCancel.load(std::memory_order_acquire))throw WavExportState::cancelled;};
 try{
  checkCancellation();
  // 构造、采样解码与销毁均在 worker；构造函数不启动任何导出任务。
  LatticeProcessor renderer;
  checkCancellation();
  for(const auto& drum:renderer.drumSamples)
   if(drum.audio.getNumChannels()!=2||drum.audio.getNumSamples()<=0||!std::isfinite(drum.sampleRate)||drum.sampleRate<=0)
    throw std::runtime_error("Sampler data could not be decoded.");
  constexpr double sampleRate=48000.0;
  constexpr int blockSize=4096;
  renderer.setNonRealtime(true);
  renderer.setRateAndBufferSizeDetails(sampleRate,blockSize);
  renderer.prepareToPlay(sampleRate,blockSize);
  // 不设置 preview/playhead：只发送一次性 note-on，句末不发 all-notes-off。
  std::vector<Event> events;
  for(int b=0;b<lattice::phraseBars;++b)for(int r=0;r<lattice::tracks;++r)
   for(const auto& cell:snapshot.bars[b][r])if(cell.velocity>0)
    events.push_back({b*snapshot.barTicks()+cell.start,r,cell.velocity});
  // 与 changed() 的排序一致，保留同 tick 下现有 voice/choke 行为。
  std::sort(events.begin(),events.end(),[](const Event& a,const Event& b){return a.tick<b.tick;});
  const double samplesPerTick=sampleRate*60.0/(snapshot.bpm*lattice::ppq);
  const auto phraseSamples=juce::int64(std::ceil(snapshot.barTicks()*lattice::phraseBars*samplesPerTick));
  const auto totalSamples=phraseSamples+juce::int64(std::llround(renderer.getTailLengthSeconds()*sampleRate));
  temporary=destination.getParentDirectory().getChildFile(".lattice-export-"+juce::Uuid().toString()+".wav");
  if(temporary.exists()||temporary.isSymbolicLink())throw std::runtime_error("Temporary filename is unavailable.");
  ownsTemporary=true;
  {
   juce::FileOutputStream output(temporary);
   if(!output.openedOk())throw std::runtime_error("Cannot open temporary WAV.");
   bool writeFailed=false;
   std::unique_ptr<juce::OutputStream> stream=std::make_unique<WavOutput>(output,writeFailed);
   juce::WavAudioFormat format;
   auto writer=format.createWriterFor(stream,juce::AudioFormatWriter::Options{}
       .withSampleRate(sampleRate).withNumChannels(2).withBitsPerSample(24));
   if(!writer)throw std::runtime_error("Cannot create 24-bit WAV writer.");
   juce::AudioBuffer<float> audio(2,blockSize);
   juce::MidiBuffer midi;
   size_t nextEvent=0;
   for(juce::int64 offset=0;offset<totalSamples;){
    checkCancellation();
    const int count=int(std::min<juce::int64>(blockSize,totalSamples-offset));
    audio.setSize(2,count,false,false,true);
    midi.clear();
    while(nextEvent<events.size()){
     const auto& event=events[nextEvent];
     const auto frame=juce::int64(std::floor(event.tick*samplesPerTick));
     if(frame>=offset+count)break;
     midi.addEvent(juce::MidiMessage::noteOn(10,lattice::notes[event.row],juce::uint8(event.velocity)),int(frame-offset));
     ++nextEvent;
    }
    renderer.processBlock(audio,midi);
    if(!writer->writeFromAudioSampleBuffer(audio,0,count)||writeFailed||output.getStatus().failed())
     throw std::runtime_error("WAV sample write failed.");
    offset+=count;
    wavProgress.store(float(double(offset)/double(totalSamples)*0.99),std::memory_order_relaxed);
   }
   checkCancellation();
   if(!writer->flush())throw std::runtime_error("WAV header flush failed.");
   writer.reset();
   output.flush();
   if(writeFailed||output.getStatus().failed())throw std::runtime_error("WAV final header/flush failed.");
  }
  renderer.releaseResources();
  result.message="Saved "+destination.getFileName()+" | 48 kHz stereo 24-bit | panel pattern.bpm "
      +juce::String(snapshot.bpm,2)+" (not host tempo) | 16 bars + 3.5 s, all 8 tracks.";
  {
   std::lock_guard<std::mutex> lock(wavResultMutex);
   checkCancellation();
   if(destination.isDirectory()||destination.isSymbolicLink()||(!replaceExisting&&destination.exists()))
    throw std::runtime_error("Destination changed during export; choose it again to confirm overwrite.");
   // 同目录 rename 原子提交；禁止 JUCE moveFileTo 的预删除/复制回退。
   std::error_code error;
   std::filesystem::rename(std::filesystem::u8path(temporary.getFullPathName().toStdString()),
                           std::filesystem::u8path(destination.getFullPathName().toStdString()),error);
   if(error)throw std::runtime_error("Atomic WAV replacement failed: "+error.message());
   ownsTemporary=false;
   result.state=WavExportState::succeeded;
   wavResult=result;
   wavProgress.store(1,std::memory_order_relaxed);
  }
 }catch(WavExportState){
  result.state=WavExportState::cancelled;
  result.message="WAV export cancelled. Original destination unchanged.";
 }catch(const std::exception& error){
  result.state=WavExportState::failed;
  result.message="WAV export failed: "+juce::String(error.what())+" Original destination unchanged.";
 }catch(...){
  result.state=WavExportState::failed;
  result.message="WAV export failed. Original destination unchanged.";
 }
 // 只清理本任务的单个临时文件，绝不递归删除或按通配符清理。
 if(ownsTemporary&&(temporary.isDirectory()||!temporary.deleteFile()))
  result.message+=" Temporary file could not be removed: "+temporary.getFullPathName();
 {
  std::lock_guard<std::mutex> lock(wavResultMutex);
  wavResult=std::move(result);
 }
 wavExporting.store(false,std::memory_order_release);
}

void LatticeProcessor::loadSamples(){
 // Row order is part of the existing MIDI/state format: Low Tom precedes Hat.
 const char* names[]={"kick_wav","snare_wav","tom2_wav","hat_wav","open_wav","tom1_wav","ride_wav","crash_wav"};
 juce::WavAudioFormat format;
 for(int row=0;row<lattice::tracks;++row){
  int size=0;const auto* data=LatticeSamples::getNamedResource(names[row],size);
  if(data==nullptr||size<=0){jassertfalse;continue;}
  std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(new juce::MemoryInputStream(data,size,false),true));
  if(!reader||reader->lengthInSamples<=0||reader->lengthInSamples>std::numeric_limits<int>::max()||reader->sampleRate<=0){jassertfalse;continue;}
  auto& drum=drumSamples[row];drum.sampleRate=reader->sampleRate;
  drum.audio.setSize(2,int(reader->lengthInSamples));drum.audio.clear();
  if(!reader->read(&drum.audio,0,drum.audio.getNumSamples(),0,true,true)){drum.audio.setSize(0,0);jassertfalse;}
 }
}
juce::AudioProcessorEditor* LatticeProcessor::createEditor(){return makeLatticeEditor(*this);}
void LatticeProcessor::changed(){juce::ScopedLock model(modelLock);juce::ScopedLock lock(scheduleLock);pending.events.clear();pending.events.reserve(65536);pending.count=0;pending.ticks=pattern.barTicks()*lattice::phraseBars;pending.bpm=pattern.bpm;for(int b=0;b<lattice::phraseBars;++b)for(int r=0;r<lattice::tracks;++r)for(auto c:pattern.bars[b][r])if(c.velocity&&pending.count<int(pending.events.capacity())){pending.events.push_back({b*pattern.barTicks()+c.start,r,c.velocity});++pending.count;}std::sort(pending.events.begin(),pending.events.begin()+pending.count,[](const Event& a,const Event& b){return a.tick<b.tick;});version.fetch_add(1,std::memory_order_release);}
void LatticeProcessor::silence(){for(auto& v:voices){v.amplitude=0;v.age=100;}}
void LatticeProcessor::prepareToPlay(double sr,int){rate=sr;silence();internalTick=0;wasRunning=wasPreview=wasHost=false;applied=0;}
void LatticeProcessor::releaseResources(){silence();playTick.store(-1);}
void LatticeProcessor::trigger(int r,int vel){if(r<0||r>=lattice::tracks||vel<=0)return;if(r==3)for(auto& v:voices)if(v.row==4)v.amplitude=0;Voice* target=&voices[0];for(auto& v:voices)if(v.amplitude==0){target=&v;break;}else if(v.age>target->age)target=&v;*target={r,0,0,float(std::min(vel,127))/127.f};}
LatticeProcessor::StereoSample LatticeProcessor::sample(Voice& v){
 if(v.amplitude==0)return {};
 const auto& drum=drumSamples[v.row];const int length=drum.audio.getNumSamples();
 if(v.position>=length){v.amplitude=0;return {};}
 const int index=int(v.position);const float fraction=float(v.position-index);
 auto read=[&](int channel){const float a=drum.audio.getSample(channel,index);const float b=index+1<length?drum.audio.getSample(channel,index+1):0.f;return (a+(b-a)*fraction)*v.amplitude;};
 StereoSample out{read(0),read(1)};
 v.position+=drum.sampleRate/rate;v.age+=1.0/rate;return out;
}
void LatticeProcessor::processBlock(juce::AudioBuffer<float>& audio,juce::MidiBuffer& midi){juce::ScopedNoDenormals no;audio.clear();auto ver=version.load(std::memory_order_acquire);if(ver!=applied){juce::ScopedTryLock lock(scheduleLock);if(lock.isLocked()){active=pending;applied=ver;}}
 bool hostPlaying=false;double hostTick=0,hostBpm=0;bool hasHost=false;double bpm=active.bpm;
 if(auto* ph=getPlayHead())if(auto pos=ph->getPosition()){if(auto b=pos->getBpm())if(std::isfinite(*b)&&*b>0)hostBpm=std::clamp(*b,1.0,999.0);if(auto q=pos->getPpqPosition()){if(std::isfinite(*q)){hostTick=*q*480.0;hasHost=true;hostPlaying=pos->getIsPlaying();}}}
 const bool local=preview.load();const bool useHost=hasHost&&hostPlaying&&!local;if(useHost&&hostBpm>0)bpm=hostBpm;
 bool running=local||useHost;double step=bpm*480.0/(60.0*rate);displayedBpm.store(bpm);if(local&&!wasPreview)internalTick=0;if(!running&&wasRunning)silence();if(useHost&&wasHost&&std::abs(hostTick-expectedHostTick)>step*2)silence();if(useHost!=wasHost&&running)silence();double tick=useHost?hostTick:internalTick;
 auto it=midi.cbegin();auto finish=midi.cend();for(int i=0;i<audio.getNumSamples();++i){while(it!=finish&&(*it).samplePosition<=i){auto msg=(*it).getMessage();if(msg.isNoteOn()){for(int r=0;r<lattice::tracks;++r)if(msg.getNoteNumber()==lattice::notes[r])trigger(r,msg.getVelocity());}else if(msg.isAllNotesOff()||msg.isAllSoundOff())silence();++it;}
 if(running&&active.ticks>0){double wrapped=std::fmod(tick,active.ticks);if(wrapped<0)wrapped+=active.ticks;auto fire=[&](double a,double z){auto begin=active.events.begin(),end=begin+active.count;auto e=std::lower_bound(begin,end,a-0.000001,[](const Event& event,double value){return event.tick<value;});for(;e!=end&&e->tick<z-0.000001;++e)trigger(e->row,e->velocity);};fire(wrapped,std::min(wrapped+step,double(active.ticks)));if(wrapped+step>active.ticks)fire(0,wrapped+step-active.ticks);tick+=step;}
 float l=0,r=0;const float pan[8]={0,0,-0.15f,-0.25f,-0.2f,0.2f,0.3f,0.1f};for(auto& voice:voices){const auto s=sample(voice);l+=s.left*(1.f-pan[voice.row])*0.65f;r+=s.right*(1.f+pan[voice.row])*0.65f;}float tl=std::tanh(l),tr=std::tanh(r);if(audio.getNumChannels()>0)audio.setSample(0,i,tl);if(audio.getNumChannels()>1)audio.setSample(1,i,tr);}
 if(local)internalTick=tick;expectedHostTick=hostTick+audio.getNumSamples()*step;wasHost=useHost;wasPreview=local;wasRunning=running;playTick.store(running?int(std::fmod(std::max(0.0,tick),double(active.ticks))):-1);midi.clear();}
juce::File LatticeProcessor::exportMidi(){lattice::Pattern m;{juce::ScopedLock lock(modelLock);m=pattern;}juce::MidiMessageSequence seq;auto tempo=juce::MidiMessage::tempoMetaEvent(int(60000000.0/m.bpm));seq.addEvent(tempo);seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(m.numerator,m.denominator));for(int b=0;b<lattice::phraseBars;++b)for(int r=0;r<lattice::tracks;++r)for(auto c:m.bars[b][r])if(c.velocity){auto on=juce::MidiMessage::noteOn(10,lattice::notes[r],juce::uint8(c.velocity));on.setTimeStamp(b*m.barTicks()+c.start);seq.addEvent(on);auto off=juce::MidiMessage::noteOff(10,lattice::notes[r]);off.setTimeStamp(b*m.barTicks()+c.start+std::max(1,std::min(60,c.length-1)));seq.addEvent(off);}auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(m.barTicks()*lattice::phraseBars);seq.addEvent(end);seq.updateMatchedPairs();juce::MidiFile file;file.setTicksPerQuarterNote(480);file.addTrack(seq);juce::StringArray groups;for(int n:m.groups)groups.add(juce::String(n));auto folder=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ozo-LATTICE");if(!folder.createDirectory())return {};auto f=folder.getChildFile("LATTICE_"+juce::String(m.numerator)+"-"+juce::String(m.denominator)+"_"+groups.joinIntoString("+")+"_"+juce::String(m.bpm,0)+"bpm_"+juce::Uuid().toString().substring(0,8)+".mid");juce::FileOutputStream stream(f);if(!stream.openedOk()||!file.writeTo(stream))return {};stream.flush();return f;}
void LatticeProcessor::getStateInformation(juce::MemoryBlock& dest){juce::ScopedLock lock(modelLock);juce::XmlElement xml("LATTICE");xml.setAttribute("version",2);xml.setAttribute("n",pattern.numerator);xml.setAttribute("d",pattern.denominator);xml.setAttribute("bpm",pattern.bpm);xml.setAttribute("seed",pattern.seed);xml.setAttribute("density",double(pattern.density));xml.setAttribute("ghost",pattern.ghost);xml.setAttribute("accents",pattern.accents);xml.setAttribute("development",double(pattern.development));xml.setAttribute("fill",double(pattern.fill));xml.setAttribute("space",double(pattern.space));xml.setAttribute("dispersion",pattern.dispersion);xml.setAttribute("gridStep",pattern.gridStep);juce::StringArray gs;for(auto n:pattern.groups)gs.add(juce::String(n));xml.setAttribute("groups",gs.joinIntoString("+"));juce::StringArray bl;for(bool b:pattern.barLocked)bl.add(juce::String(b?1:0));xml.setAttribute("barlock",bl.joinIntoString(","));for(int b=0;b<lattice::phraseBars;++b)for(int r=0;r<lattice::tracks;++r){auto* lane=xml.createNewChildElement("lane");lane->setAttribute("b",b);lane->setAttribute("r",r);lane->setAttribute("lock",pattern.locked[r]);for(auto c:pattern.bars[b][r]){auto* x=lane->createNewChildElement("c");x->setAttribute("t",c.start);x->setAttribute("l",c.length);x->setAttribute("v",c.velocity);}}xml.setAttribute("backbeat",pattern.backbeat);copyXmlToBinary(xml,dest);}
void LatticeProcessor::setStateInformation(const void* data,int size){if(size<=0||size>8000000)return;auto xml=getXmlFromBinary(data,size);if(!xml||!xml->hasTagName("LATTICE"))return;int version=xml->getIntAttribute("version",1);if(version!=1&&version!=2)return;lattice::Pattern m;int n=xml->getIntAttribute("n"),d=xml->getIntAttribute("d");if(n<1||n>16||(d!=4&&d!=8))return;m.meter(n,d);m.bpm=xml->getDoubleAttribute("bpm",100);m.seed=xml->getIntAttribute("seed",1729);m.density=float(xml->getDoubleAttribute("density",.55));m.ghost=xml->getBoolAttribute("ghost",true);m.accents=xml->getBoolAttribute("accents",true);if(!std::isfinite(m.bpm)||m.bpm<40||m.bpm>240||!std::isfinite(m.density)||m.density<0||m.density>1)return;m.backbeat=xml->getBoolAttribute("backbeat",false);m.groups.clear();auto gs=juce::StringArray::fromTokens(xml->getStringAttribute("groups"),"+","");if(gs.size()>32)return;for(auto s:gs){if(!s.containsOnly("0123456789")||s.length()>2||s.getIntValue()<1||s.getIntValue()>32)return;m.groups.push_back(s.getIntValue());}if(!m.valid())return;
 m.dispersion=1;
 if(version>=2){if(xml->hasAttribute("dispersion")){auto s=xml->getStringAttribute("dispersion");if(s.length()!=1||!s.containsOnly("012"))return;m.dispersion=s.getIntValue();}m.development=float(xml->getDoubleAttribute("development",0));m.fill=float(xml->getDoubleAttribute("fill",0));m.space=float(xml->getDoubleAttribute("space",0));if(!std::isfinite(m.development)||m.development<0||m.development>1)return;if(!std::isfinite(m.fill)||m.fill<0||m.fill>1)return;if(!std::isfinite(m.space)||m.space<0||m.space>1)return;int g=xml->getIntAttribute("gridStep",60);if(g!=60&&g!=120)return;m.gridStep=g;auto bl=juce::StringArray::fromTokens(xml->getStringAttribute("barlock"),",","");if(bl.size()>lattice::phraseBars)return;for(int b=0;b<(int)bl.size()&&b<lattice::phraseBars;++b)m.barLocked[b]=bl[b].getIntValue()!=0;}
 else{m.gridStep=120;}
 int barsN=version>=2?lattice::phraseBars:4;int trkN=version>=2?lattice::tracks:7;std::vector<std::vector<bool>> seen(barsN,std::vector<bool>(trkN,false));
 for(auto* lane:xml->getChildIterator()){int b=lane->getIntAttribute("b",-1),r=lane->getIntAttribute("r",-1);if(!lane->hasTagName("lane")||b<0||b>=barsN||r<0||r>=trkN||seen[b][r])return;seen[b][r]=true;if(r<trkN)m.locked[r]=lane->getBoolAttribute("lock");auto& l=m.bars[b][r];l.clear();int pos=0;for(auto* c:lane->getChildIterator()){int t=c->getIntAttribute("t",-1),len=c->getIntAttribute("l",0),v=c->getIntAttribute("v",-1);if(!c->hasTagName("c")||t!=pos||len<1||len>m.barTicks()-pos||(v!=0&&v!=40&&v!=80&&v!=127)||l.size()>=512)return;l.push_back({t,len,v});pos+=len;}if(pos!=m.barTicks())return;}
 for(auto& row:seen)for(bool s:row)if(!s)return;{juce::ScopedLock lock(modelLock);pattern=std::move(m);}changed();}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new LatticeProcessor();}
