#include <juce_audio_processors_headless/juce_audio_processors_headless.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr double sampleRate=48000.0;
constexpr int block=128;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Event { long long sample; juce::MidiMessage message; };
class Clock final:public juce::AudioPlayHead
{
public:
    long long sample=0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;p.setBpm(120);p.setTimeInSamples(sample);p.setPpqPosition(double(sample)/24000.0);p.setIsPlaying(true);return p;
    }
};
void set(juce::AudioPluginInstance& p,const juce::String& name,float normalised)
{
    for(auto* parameter:p.getParameters())if(parameter->getName(128)==name){parameter->setValueNotifyingHost(normalised);return;}
    throw std::runtime_error("Missing parameter: "+name.toStdString());
}
bool same(const juce::MidiMessage& a,const juce::MidiMessage& b)
{
    return a.getRawDataSize()==b.getRawDataSize()&&std::equal(a.getRawData(),a.getRawData()+a.getRawDataSize(),b.getRawData());
}
class Host
{
public:
    explicit Host(const juce::String& path)
    {
        formats.addFormat(std::make_unique<juce::VST3PluginFormatHeadless>());
        juce::OwnedArray<juce::PluginDescription> found;formats.getFormat(0)->findAllTypesForFile(found,path);
        require(!found.isEmpty()&&(found[0]->name=="ozo NIGHTSHIFT"||found[0]->name=="ozo GLINT")&&found[0]->isInstrument,"NIGHTSHIFT instrument not found");
        juce::String error;plugin=formats.createPluginInstance(*found[0],sampleRate,block,error);
        if(!plugin)throw std::runtime_error(error.toStdString());
        require(plugin->acceptsMidi()&&plugin->producesMidi(),"MIDI I/O missing");
        require(plugin->getTotalNumInputChannels()==0&&plugin->getTotalNumOutputChannels()==2,"Wrong silent instrument bus layout");
        for(auto* parameter:plugin->getParameters())
            require(parameter->getName(128)!="Character"&&parameter->getName(128)!="Shine"&&parameter->getName(128)!="Rate","Retired mode parameter still exposed");
        plugin->setPlayHead(&clock);plugin->prepareToPlay(sampleRate,block);
    }
    ~Host(){plugin->setPlayHead(nullptr);plugin->releaseResources();}
    std::vector<Event> render(const std::vector<Event>& input,long long length)
    {
        std::vector<Event> output;std::size_t cursor=0;
        juce::AudioBuffer<float> audio(2,block);juce::MidiBuffer midi;
        for(long long at=0;at<length;at+=block)
        {
            const int count=int(std::min<long long>(block,length-at));audio.setSize(2,count,false,false,true);audio.clear();audio.setSample(0,0,.5f);midi.clear();
            while(cursor<input.size()&&input[cursor].sample<at+count)
            {require(input[cursor].sample>=at,"Unsorted fixture");midi.addEvent(input[cursor].message,int(input[cursor].sample-at));++cursor;}
            clock.sample=at;plugin->processBlock(audio,midi);
            require(audio.getMagnitude(0,count)==0.0f,"MIDI instrument emitted audio");
            for(const auto event:midi){require(event.samplePosition>=0&&event.samplePosition<count,"Invalid event offset");output.push_back({at+event.samplePosition,event.getMessage()});}
        }
        return output;
    }
    juce::AudioPluginFormatManager formats;Clock clock;std::unique_ptr<juce::AudioPluginInstance> plugin;
};
void balanced(const std::vector<Event>& events)
{
    std::array<int,2048> notes{};int ons=0;
    for(const auto& event:events)
    {
        const auto& m=event.message;if(!m.isNoteOnOrOff())continue;
        auto& n=notes[(m.getChannel()-1)*128+m.getNoteNumber()];
        if(m.isNoteOn()){require(n==0,"Overlapping generated key");++n;++ons;}
        else {require(n>0,"Unowned generated Off");--n;}
    }
    require(ons>0,"No sparkles generated");require(std::all_of(notes.begin(),notes.end(),[](int n){return n==0;}),"Unfinished generated notes");
}
std::vector<Event> melody(int pitch)
{
    std::vector<Event> result;
    for(int i=0;i<24;++i)
    {
        result.push_back({i*48000+31,juce::MidiMessage::noteOn(1,pitch,juce::uint8(100))});
        result.push_back({i*48000+4800,juce::MidiMessage::noteOff(1,pitch)});
    }
    return result;
}
}
int main(int argc,char** argv)
{
    if(argc!=2&&argc!=3){std::cerr<<"Usage: ozoGlintHostTests plugin.vst3 [demo-output-directory]\n";return 2;}
    try
    {
        {
            Host h{juce::String(argv[1])};set(*h.plugin,"Amount",0);
            const std::vector<Event> input{{17,juce::MidiMessage::noteOn(1,60,juce::uint8(83))},{123,juce::MidiMessage::controllerEvent(1,64,127)},{333,juce::MidiMessage::noteOff(1,60)},{601,juce::MidiMessage::controllerEvent(1,64,0)}};
            auto out=h.render(input,1024);require(out.size()==input.size(),"Dry MIDI event count changed");
            for(std::size_t i=0;i<input.size();++i)require(out[i].sample==input[i].sample&&same(out[i].message,input[i].message),"Original melody changed");
            std::cout<<"PASS VST3 identity, silent output and exact original MIDI\n";
        }
        for(int mode=0;mode<3;++mode)
        {
            Host h{juce::String(argv[1])};set(*h.plugin,"Output",1);set(*h.plugin,"Amount",.3f);
            set(*h.plugin,"Key Root",mode==2?9.0f/11.0f:0.0f);set(*h.plugin,"Scale",mode*.5f);
            int source=mode==2?71:62;auto out=h.render(melody(source),52*48000);balanced(out);
            bool third=false,fifth=false,octave=false;
            for(auto& e:out)if(e.message.isNoteOn())
            {
                int n=e.message.getNoteNumber(),pc=n%12;
                require(e.message.getVelocity()<=48,"Echoes must immediately retreat below half of source velocity");
                if(mode==0)require(pc==source%12,"Unset key added guessed harmony");
                if(mode==1){require(pc==2||pc==5||pc==9||pc==11||pc==7,"D in C major must use only its seven lane relations");third|=pc==5;fifth|=pc==9;}
                if(mode==2){require(pc==11||pc==2||pc==5||pc==7||pc==4,"B in A minor must use only its seven lane relations");third|=pc==2;fifth|=pc==5;}
                octave|=pc==source%12&&n>=source+12;
            }
            require(octave,"No octave light");if(mode)require(third&&fifth,"Diatonic third/fifth never appeared");
            std::cout<<"PASS VST3 harmony mode "<<mode<<", "<<out.size()/2<<" finite light events\n";
        }
        {
            Host h{juce::String(argv[1])};set(*h.plugin,"Amount",.6f);set(*h.plugin,"Key Root",7.0f/11.0f);set(*h.plugin,"Scale",.5f);
            h.render({},128);juce::MemoryBlock state;h.plugin->getStateInformation(state);
            set(*h.plugin,"Amount",0);h.plugin->setStateInformation(state.getData(),int(state.getSize()));
            auto out=h.render(melody(67),52*48000);require(out.size()>48,"State restore did not restore sparkles");
            std::cout<<"PASS VST3 current state restore\n";
        }
        {
            Host h{juce::String(argv[1])};set(*h.plugin,"Amount",.3f);set(*h.plugin,"Output",1);
            set(*h.plugin,"Key Root",0);set(*h.plugin,"Scale",.5f);
            auto out=h.render({{0,juce::MidiMessage::noteOn(1,74,juce::uint8(100))},
                               {240,juce::MidiMessage::noteOff(1,74)}},26*48000);
            balanced(out);
            std::array<int,3> counts{};std::array<long long,3> last{};
            for(const auto& e:out)if(e.message.isNoteOn())
            {
                const int n=e.message.getNoteNumber();int lane=n==77?0:n==81?1:n==86?2:-1;
                require(lane>=0,"Default isolated source emitted an unexpected lane");
                ++counts[std::size_t(lane)];last[std::size_t(lane)]=e.sample;
            }
            for(int i=0;i<3;++i){require(counts[std::size_t(i)]>3,"Lane still ends after only three replies");require(last[std::size_t(i)]>3*48000,"Lane tail still ends prematurely");}
            std::cout<<"PASS VST3 sustained lanes: third="<<counts[0]<<" fifth="<<counts[1]<<" octave="<<counts[2]
                     <<"; last On seconds="<<last[0]/48000.0<<", "<<last[1]/48000.0<<", "<<last[2]/48000.0<<'\n';
        }
        {
            Host h{juce::String(argv[1])};set(*h.plugin,"Amount",.3f);set(*h.plugin,"Output",1);
            for(int lane=1;lane<=7;++lane)set(*h.plugin,"L"+juce::String(lane)+" Enable",lane==4?1.0f:0.0f);
            set(*h.plugin,"L4 First",4.0f/9);set(*h.plugin,"L4 Repeat",6.0f/9);
            set(*h.plugin,"L4 Gate",2.0f/9);set(*h.plugin,"L4 Feedback",.8f/.98f);
            set(*h.plugin,"L4 Level",.4f);set(*h.plugin,"L4 Motion",0);
            auto out=h.render({{0,juce::MidiMessage::noteOn(1,60,juce::uint8(100))},
                               {240,juce::MidiMessage::noteOff(1,60)}},52*48000);
            balanced(out);int count=0;const int velocities[]{40,32,26};
            for(const auto& event:out)if(event.message.isNoteOn())
            {
                require(event.message.getNoteNumber()==72,"Manual enable/role mapping failed");
                if(count<3){require(event.sample==12000+count*24000,"Manual First/Repeat mismatch");require(event.message.getVelocity()==velocities[count],"Manual feedback/level mapping mismatch");}
                ++count;
            }
            require(count>=3,"Manual lane did not repeat");
            std::cout<<"PASS VST3 independent First, Repeat, Feedback, Level and lane switches\n";
        }
        if(argc==3)
        {
            const juce::File folder{juce::String(argv[2])};
            require(!folder.exists(),"Refusing to overwrite demonstration directory");
            require(folder.createDirectory().wasOk(),"Cannot create demo directory");
            const int notes[]{60,62,64,67,69,67,64,62};std::vector<Event> input;
            for(int i=0;i<8;++i){input.push_back({i*48000,juce::MidiMessage::noteOn(1,notes[i],juce::uint8(88))});input.push_back({i*48000+16000,juce::MidiMessage::noteOff(1,notes[i])});}
            Host h{juce::String(argv[1])};set(*h.plugin,"Amount",.45f);set(*h.plugin,"Key Root",0);set(*h.plugin,"Scale",.5f);
            const auto output=h.render(input,36*48000);
            auto save=[&](const std::vector<Event>& events,const char* name)
            {
                juce::MidiMessageSequence track;track.addEvent(juce::MidiMessage::tempoMetaEvent(500000));
                for(auto e:events){e.message.setTimeStamp(double(e.sample)*960.0/24000.0);track.addEvent(e.message);}
                track.updateMatchedPairs();juce::MidiFile midi;midi.setTicksPerQuarterNote(960);midi.addTrack(track);
                auto stream=folder.getChildFile(name).createOutputStream();require(stream&&stream->openedOk()&&midi.writeTo(*stream),"Cannot write MIDI demo");
            };
            save(input,"01-original-C-major.mid");save(output,"02-glint-C-major.mid");
            std::cout<<"DEMO "<<folder.getFullPathName()<<'\n';
        }
        return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
