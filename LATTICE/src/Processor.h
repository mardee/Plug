#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Model.h"
#include <atomic>
#include <vector>
class LatticeProcessor final: public juce::AudioProcessor {
public:
 LatticeProcessor();
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
 struct Event{int tick=0,row=0,velocity=0;};
 struct Schedule{std::vector<Event> events;int count=0,ticks=30720;double bpm=100;Schedule(){events.reserve(65536);}};
 Schedule pending,active;juce::CriticalSection scheduleLock;std::atomic<unsigned> version{1};unsigned applied=0;
 struct Voice{int row=0;double age=100,phase=0,phase2=0;float amplitude=0,previousNoise=0;};std::array<Voice,48> voices;
 double rate=48000,internalTick=0,expectedHostTick=0;bool wasRunning=false,wasPreview=false,wasHost=false;
 uint32_t noiseState=712367u;
 void trigger(int,int);float sample(Voice&);void silence();
};
juce::AudioProcessorEditor* makeLatticeEditor(LatticeProcessor&);
