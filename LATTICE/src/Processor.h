#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Model.h"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
class LatticeProcessor final: public juce::AudioProcessor {
public:
 LatticeProcessor();
 ~LatticeProcessor() override;
 enum class WavExportState { idle, running, succeeded, failed, cancelled };
 struct WavExportResult {
  WavExportState state=WavExportState::idle;
  juce::File file;
  double bpm=0;
  juce::String message;
 };
 // 非音频线程调用；已有目标的覆盖授权由调用方确认。拒绝重复任务时保留当前结果。
 bool startWavExport(const juce::File&);
 void cancelWavExport(); // 请求取消；任务完成前仍返回 exporting=true。
 bool isWavExporting()const{return wavExporting.load(std::memory_order_acquire);}
 float getWavExportProgress()const{return wavProgress.load(std::memory_order_relaxed);}
 WavExportResult getWavExportResult()const;
 lattice::Pattern pattern;juce::CriticalSection modelLock;
 std::atomic<bool> preview{false};std::atomic<int> playTick{-1};std::atomic<double> displayedBpm{120};
 void changed();juce::File exportMidi();
 const juce::String getName() const override{return "ozo LATTICE";}
 void prepareToPlay(double,int)override;void releaseResources()override;void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&)override;
 bool isBusesLayoutSupported(const BusesLayout& l)const override{return l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()&&l.getMainInputChannelSet().isDisabled();}
 bool acceptsMidi()const override{return true;}bool producesMidi()const override{return false;}bool isMidiEffect()const override{return false;}double getTailLengthSeconds()const override{return 3.5;}
 bool hasEditor()const override{return true;}juce::AudioProcessorEditor* createEditor()override;
 int getNumPrograms()override{return 1;}int getCurrentProgram()override{return 0;}void setCurrentProgram(int)override{}const juce::String getProgramName(int)override{return "Lattice";}void changeProgramName(int,const juce::String&)override{}
 void getStateInformation(juce::MemoryBlock&)override;void setStateInformation(const void*,int)override;
private:
 std::thread wavThread;
 std::mutex wavJobMutex;
 mutable std::mutex wavResultMutex;
 std::atomic<bool> wavExporting{false},wavCancel{false};
 std::atomic<float> wavProgress{0};
 WavExportResult wavResult;
 void runWavExport(const lattice::Pattern&,const juce::File&,bool replaceExisting);
 struct Event{int tick=0,row=0,velocity=0;};
 struct Schedule{std::vector<Event> events;int count=0,ticks=30720;double bpm=100;Schedule(){events.reserve(65536);}};
 Schedule pending,active;juce::CriticalSection scheduleLock;std::atomic<unsigned> version{1};unsigned applied=0;
 struct DrumSample{juce::AudioBuffer<float> audio;double sampleRate=48000;};
 std::array<DrumSample,lattice::tracks> drumSamples;
 struct Voice{int row=0;double age=100,position=0;float amplitude=0;};std::array<Voice,48> voices;
 struct StereoSample{float left=0,right=0;};
 double rate=48000,internalTick=0,expectedHostTick=0;bool wasRunning=false,wasPreview=false,wasHost=false;
 void loadSamples();void trigger(int,int);StereoSample sample(Voice&);void silence();
};
juce::AudioProcessorEditor* makeLatticeEditor(LatticeProcessor&);
