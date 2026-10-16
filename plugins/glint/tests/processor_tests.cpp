#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
float value(OzoGlintProcessor& p,const juce::String& id){auto* v=p.getAPVTS().getRawParameterValue(id);require(v!=nullptr,"Missing parameter");return v->load();}
void set(OzoGlintProcessor& p,const juce::String& id,float v){auto* q=p.getAPVTS().getParameter(id);require(q!=nullptr,"Missing parameter");q->setValueNotifyingHost(q->convertTo0to1(v));}
juce::MemoryBlock binary(const juce::XmlElement& x){juce::MemoryBlock b;juce::AudioProcessor::copyXmlToBinary(x,b);return b;}
void tick(OzoGlintProcessor& p){juce::AudioBuffer<float>b(2,128);b.clear();b.setSample(0,0,.5f);juce::MidiBuffer m;p.processBlock(b,m);require(b.getMagnitude(0,128)==0,"Instrument emitted audio");}
}
int main(int argc,char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        OzoGlintProcessor p;p.prepareToPlay(48000,128);
        require((p.getName()=="ozo NIGHTSHIFT"||p.getName()=="ozo GLINT")&&p.acceptsMidi()&&p.producesMidi(),"Wrong MIDI identity");
        require(p.getTotalNumInputChannels()==0&&p.getTotalNumOutputChannels()==2,"Wrong audio buses");
        require(p.getParameters().size()==60,"Expected four global plus seven by eight lane parameters");
        set(p,"amount",.61f);set(p,"root",9);set(p,"scale",2);set(p,"outputMode",1);
        for(std::size_t lane=0;lane<7;++lane)
        {
            set(p,OzoGlintProcessor::laneParameterID(lane,"enabled"),1);
            set(p,OzoGlintProcessor::laneParameterID(lane,"first"),float(lane));
            set(p,OzoGlintProcessor::laneParameterID(lane,"repeat"),float(lane+2));
            set(p,OzoGlintProcessor::laneParameterID(lane,"feedback"),.7f+.02f*float(lane));
        }
        tick(p);juce::MemoryBlock state;p.getStateInformation(state);
        auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),int(state.getSize()));
        require(xml&&xml->getIntAttribute("version")==6,"Expected v6 state");
        set(p,"amount",0);set(p,"lane6_feedback",0);p.setStateInformation(state.getData(),int(state.getSize()));
        require(std::abs(value(p,"amount")-.61f)<.001f&&std::abs(value(p,"lane6_feedback")-.82f)<.001f,"Lane state roundtrip failed");
        const auto text=p.exportSettingsText();require(text.contains("lane6_feedback"),"Copy settings omitted lane data");
        set(p,"lane6_feedback",0);require(p.importSettingsText(text),"Paste settings rejected own document");
        require(std::abs(value(p,"lane6_feedback")-.82f)<.001f,"Paste did not restore lane");
        require(!p.importSettingsText("bad json and XML")&&std::abs(value(p,"lane6_feedback")-.82f)<.001f,"Invalid paste changed state");
        for(auto id:{"amount","root","scale","lane0_feedback","lane6_first","lane4_enabled","lane5_cycle"})
            for(auto bad:{"nan","inf","-1","99","0oops"})
            {
                auto invalid=std::make_unique<juce::XmlElement>(*xml);
                for(auto* child:invalid->getChildByName("PARAMETERS")->getChildIterator())if(child->getStringAttribute("id")==id)child->setAttribute("value",bad);
                auto bytes=binary(*invalid);p.setStateInformation(bytes.getData(),int(bytes.getSize()));
                require(std::abs(value(p,"amount")-.61f)<.001f&&value(p,"root")==9&&std::abs(value(p,"lane6_feedback")-.82f)<.001f,"Invalid state partially applied");
            }
        for(int version=1;version<=5;++version)
        {
            juce::XmlElement old("OZO_MIDI_DELAY_STATE");old.setAttribute("version",version);old.setAttribute("seed","1870294833");auto* params=old.createNewChildElement("PARAMETERS");
            auto add=[&](const char* id,double v){auto* x=params->createNewChildElement("PARAM");x->setAttribute("id",id);x->setAttribute("value",v);};
            add("amount",.47);add("outputMode",1);
            if(version==5){add("root",7);add("scale",1);}
            else{add("character",2);if(version>=2){add("direction",2);add("rate",4);}if(version>=3){add("octaves",3);add("rhythm",1);}if(version>=4)add("shine",1);}
            auto b=binary(old);p.setStateInformation(b.getData(),int(b.getSize()));
            require(std::abs(value(p,"amount")-.47f)<.001f&&value(p,"outputMode")==1,"Old global settings lost");
            require(value(p,"root")==float(version==5?7:0)&&value(p,"scale")==float(version==5?1:0),"Old key migration incorrect");
            require(value(p,"lane0_enabled")==0&&value(p,"lane3_enabled")==1&&std::abs(value(p,"lane3_feedback")-.84f)<.001f,"Old state failed default lanes");
        }
        std::cout<<"PASS 60 parameters, v6 state, legacy migration and copy/paste validation\n";
        set(p,"amount",.3f);set(p,"root",0);set(p,"scale",1);set(p,"outputMode",0);tick(p);
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());require(editor!=nullptr,"Missing editor");
        auto* night=dynamic_cast<OzoGlintEditor*>(editor.get());
        require(night&&night->hasAirportResources(),"Embedded airport layers missing");
        std::vector<juce::Component*> controls;
        const auto collect=[&](auto&& self,juce::Component& parent)->void
        {
            for(auto* child:parent.getChildren())
            {
                if(p.getAPVTS().getParameter(child->getName())!=nullptr)controls.push_back(child);
                self(self,*child);
            }
        };
        collect(collect,*editor);
        int sliders=0,choices=0,toggles=0;
        juce::StringArray ids;
        for(auto* child:controls)
        {
            const auto id=child->getName();auto* parameter=p.getAPVTS().getParameter(id);
            if(!ids.contains(id))ids.add(id);
            require(child->getParentComponent()->getLocalBounds().contains(child->getBounds()),"Clipped parameter control");
            if(auto* s=dynamic_cast<juce::Slider*>(child))
            {
                ++sliders;
                const bool detailReadout=bool(s->getProperties().getWithDefault("detailReadout",false));
                const bool runwayControl=bool(s->getProperties().getWithDefault("runwayControl",false));
                auto& style=dynamic_cast<NightShiftLookAndFeel&>(s->getLookAndFeel());
                const auto sliderLayout=style.getSliderLayout(*s);
                if(detailReadout)
                {
                    require(s->getTextBoxPosition()==juce::Slider::TextBoxBelow&&!sliderLayout.textBoxBounds.isEmpty(),
                            "Detail knob is missing its permanent value");
                    require(sliderLayout.sliderBounds.getBottom()<sliderLayout.textBoxBounds.getY(),
                            "Detail value overlaps the knob ring");
                }
                else if(!runwayControl)
                {
                    require(s->getTextBoxPosition()==juce::Slider::NoTextBox&&sliderLayout.textBoxBounds.isEmpty(),
                            "Main-panel knob still has a permanent readout");
                    require(bool(s->onDragStart)&&bool(s->onDragEnd),"Main-panel knob lacks drag tooltip callbacks");
                }
                const auto target=parameter->convertFrom0to1(.55f);
                s->setValue(target,juce::sendNotificationSync);
                require(std::abs(value(p,id)-target)<.011f,"Slider attachment mismatch");
                set(p,id,parameter->convertFrom0to1(.22f));
                require(std::abs(float(s->getValue())-value(p,id))<.011f,"Host automation did not update slider");
                if(id.endsWith("_first")||id.endsWith("_repeat")||id.endsWith("_gate")||id.endsWith("_cycle"))
                {
                    const int steps=id.endsWith("_cycle")?4:10;
                    require(s->getMinimum()==0&&s->getMaximum()==steps-1&&s->getInterval()==1,"Timing knob lost discrete choices");
                    auto* choiceParam=dynamic_cast<juce::AudioParameterChoice*>(parameter);
                    require(choiceParam!=nullptr,"Timing parameter type changed");
                    for(int choice=0;choice<steps;++choice)
                    {
                        s->setValue(choice,juce::sendNotificationSync);
                        require(value(p,id)==float(choice),"Timing knob choice mismatched");
                        require(s->getTextFromValue(choice)==choiceParam->choices[choice],"Timing knob text lost beat value");
                    }
                }
            }
            if(auto* c=dynamic_cast<juce::ComboBox*>(child))
            {
                ++choices;c->setSelectedItemIndex(0,juce::sendNotificationSync);
                require(value(p,id)==0,"Combo attachment mismatch");
                set(p,id,float(c->getNumItems()-1));
                require(c->getSelectedItemIndex()==c->getNumItems()-1,"Host automation did not update choice");
            }
            if(auto* b=dynamic_cast<juce::ToggleButton*>(child))
            {
                ++toggles;b->setToggleState(true,juce::sendNotificationSync);require(value(p,id)==1,"Lane enable attachment mismatch");
                set(p,id,0);require(!b->getToggleState(),"Host automation did not update lane switch");
            }
        }
        require(sliders==85&&choices==3&&toggles==14&&ids.size()==60,"Missing main or complete-detail parameter controls");
        const auto find=[&](const juce::String& id)->juce::Component*
        {
            for(auto* c:controls)if(c->getComponentID()==id)return c;
            return nullptr;
        };
        int previousY=-1;
        for(auto lane:OzoGlintEditor::displayLaneOrder)
        {
            auto* first=find(OzoGlintProcessor::laneParameterID(lane,"first"));
            require(first&&first->getY()>previousY,"Display order is not high-to-low engine lane mapping");
            previousY=first->getY();
            auto* runway=dynamic_cast<juce::Slider*>(find(OzoGlintProcessor::laneParameterID(lane,"first")+"_runway"));
            require(runway!=nullptr,"Missing runway interaction");
            runway->setValue(4,juce::sendNotificationSync);
            require(dynamic_cast<juce::Slider*>(first)->getValue()==4,"Runway and First knob not linked");
            require(std::abs(runway->valueToProportionOfLength(4)-.125)<.0001,"Runway position is not proportional to actual beats");
        }
        require(find("lane1_first")->getTitle().containsIgnoreCase("diatonic"),"Third role is incorrectly labelled fixed semitones");
        const auto findComponent=[&](auto&& self,juce::Component& parent,const juce::String& id)->juce::Component*
        {
            if(parent.getComponentID()==id)return &parent;
            for(auto* child:parent.getChildren())if(auto* found=self(self,*child,id))return found;
            return nullptr;
        };
        auto* laneSwitch=dynamic_cast<juce::ToggleButton*>(find("lane3_enabled"));
        auto* firstChoice=dynamic_cast<juce::Slider*>(find("lane3_first"));
        require(laneSwitch&&firstChoice&&bool(laneSwitch->onClick),"Missing lane dimming controls");
        laneSwitch->setToggleState(true,juce::sendNotificationSync);laneSwitch->onClick();
        const auto activeText=firstChoice->findColour(juce::Slider::textBoxTextColourId);
        laneSwitch->setToggleState(false,juce::sendNotificationSync);laneSwitch->onClick();
        require(firstChoice->isEnabled()&&firstChoice->findColour(juce::Slider::textBoxTextColourId).getBrightness()<activeText.getBrightness(),
                "Off lane timing knob did not dim or became inoperable");
        laneSwitch->setToggleState(true,juce::sendNotificationSync);laneSwitch->onClick();
        require(firstChoice->findColour(juce::Slider::textBoxTextColourId)==activeText,"On lane timing knob did not regain brightness");
        auto* brand=findComponent(findComponent,*editor,"nightshiftBrand");
        auto* tooltip=dynamic_cast<juce::Label*>(findComponent(findComponent,*editor,"knobValueTooltip"));
        auto* master=dynamic_cast<juce::Slider*>(find("amount"));
        require(brand&&master&&brand->getParentComponent()!=master->getParentComponent(),"Brand still belongs to fading panel");
        master->getParentComponent()->setAlpha(.35f);
        require(brand->getAlpha()==1.0f&&brand->getParentComponent()->getAlpha()==1.0f,"Brand fades with controls");
        master->onDragStart();
        require(tooltip&&tooltip->isVisible()&&tooltip->getText().endsWith("%"),"Drag tooltip did not show percentage");
        master->onDragEnd();
        require(!tooltip->isVisible(),"Percent tooltip remains after drag ends");
        firstChoice->onDragStart();
        require(tooltip->isVisible()&&tooltip->getText()==firstChoice->getTextFromValue(firstChoice->getValue()),
                "Timing knob tooltip did not show its beat value");
        firstChoice->onDragEnd();
        require(!tooltip->isVisible(),"Timing tooltip remains after drag ends");
        for(std::size_t lane=0;lane<7;++lane)
        {
            auto* button=dynamic_cast<juce::TextButton*>(findComponent(findComponent,*editor,"details_lane"+juce::String(int(lane))));
            require(button!=nullptr&&bool(button->onClick),"Missing lane details entry");
            require(button->getButtonText().isEmpty()&&button->getWidth()==276&&button->getHeight()==80,
                    "Lane details is still a small labelled button");
            auto* panel=button->getParentComponent();
            require(panel->getComponentAt(button->getX()+272,button->getY()+65)==button,
                    "Panel background does not open lane details");
            for(const auto* field:{"enabled","first","repeat","feedback","level"})
            {
                auto* control=find(OzoGlintProcessor::laneParameterID(lane,field));
                auto* hit=panel->getComponentAt(control->getBounds().getCentre());
                require(hit==control||control->isParentOf(hit),"Panel details intercepts a parameter control");
            }
            auto* enable=dynamic_cast<juce::ToggleButton*>(find(OzoGlintProcessor::laneParameterID(lane,"enabled")));
            require(enable&&enable->getWidth()==enable->getHeight()&&enable->getButtonText().isEmpty(),
                    "Lane enable is not a square text-free lamp");
            for(const auto* field:{"first","repeat","feedback","level"})
            {
                auto* knob=dynamic_cast<juce::Slider*>(find(OzoGlintProcessor::laneParameterID(lane,field)));
                auto& style=dynamic_cast<NightShiftLookAndFeel&>(knob->getLookAndFeel());
                const auto layout=style.getSliderLayout(*knob);
                const auto dial=layout.sliderBounds.translated(knob->getX(),knob->getY());
                const int titleBottom=button->getY()+19;
                const int topGap=dial.getY()-titleBottom;
                const int bottomGap=button->getBottom()-dial.getBottom();
                require(topGap>=8&&bottomGap>=8&&std::abs(topGap-bottomGap)<=1,
                        "Knob tick ring is not centred below its parameter label");
            }
            button->onClick();
            int previousCentre=-1;
            for(const auto* field:{"first","repeat","feedback","level","gate","motion","cycle"})
            {
                const auto suffix=(juce::String(field)=="first"||juce::String(field)=="repeat"||juce::String(field)=="feedback"||juce::String(field)=="level")?"_detail":"";
                auto* knob=find(OzoGlintProcessor::laneParameterID(lane,field)+suffix);
                require(knob!=nullptr,"Missing detail knob");
                const int centre=knob->getBounds().getCentreX();
                require(previousCentre<0||centre-previousCentre==130,"Detail knobs are not evenly spaced");
                previousCentre=centre;
            }
            for(const auto* field:{"enabled","first","repeat","feedback","level"})
            {
                const auto id=OzoGlintProcessor::laneParameterID(lane,field);
                auto* control=find(id+"_detail");
                require(control&&control->isVisible(),"Full details omitted a main lane option");
                require(control->getParentComponent()!=find(id)->getParentComponent(),"Detail option was not in the detail panel");
                set(p,id,p.getAPVTS().getParameter(id)->convertFrom0to1(.45f));
                if(auto* s=dynamic_cast<juce::Slider*>(control))
                    require(std::abs(s->getValue()-value(p,id))<.01,"Detail slider not synced");
                if(auto* c=dynamic_cast<juce::ComboBox*>(control))
                    require(c->getSelectedItemIndex()==int(value(p,id)),"Detail timing not synced");
            }
            for(std::size_t other=0;other<7;++other)
            {
                auto* gate=find(OzoGlintProcessor::laneParameterID(other,"gate"));
                require(gate&&gate->isVisible()==(other==lane),"Detail panel shows wrong lane parameters");
                for(const auto* field:{"first","repeat","feedback","level"})
                {
                    auto* main=find(OzoGlintProcessor::laneParameterID(other,field));
                    require(main&&main->isVisible()&&main->getParentComponent()==find("amount")->getParentComponent(),
                            "A core lane parameter is still hidden inside details");
                }
            }
            auto* close=dynamic_cast<juce::TextButton*>(findComponent(findComponent,*editor,"closeDetails"));
            require(close&&bool(close->onClick),"Missing detail close control");
            close->onClick();
            require(!close->getParentComponent()->isVisible(),"Detail panel did not close");
        }
        auto* presetBar=dynamic_cast<ozo::PresetBar*>(findComponent(findComponent,*editor,"presetBar"));
        require(presetBar&&presetBar->getNumEntries()>=6,"Missing PRISM-style preset bar");
        require(bool(presetBar->onSelect)&&bool(presetBar->onSave)&&bool(presetBar->onDelete),"Preset callbacks not connected");
        presetBar->onSelect(0);
        require(p.getPresetIndex()==0&&presetBar->getIndex()==0,"Factory selection did not reach processor");
        presetBar->onSelect(1);
        require(p.getPresetIndex()==1&&presetBar->getIndex()==1,"Preset switching did not update selection");
        set(p,"amount",.17f);
        require(p.getPresetIndex()==-1,"Manual parameter edit did not clear preset selection");
        require(p.deleteUserPreset(0).failed(),"Factory preset must not be deletable");
        auto* runway=dynamic_cast<juce::Slider*>(find("lane0_first_runway"));
        require(runway&&!runway->getSliderSnapsToMousePosition(),"Runway click unexpectedly jumps First");
        runway->setValue(4,juce::sendNotificationSync);
        const auto mouse=juce::Desktop::getInstance().getMainMouseSource();
        const auto now=juce::Time::getCurrentTime();
        const juce::Point<float> downPos(900,20);
        const auto event=[&](juce::Point<float> pos,int clicks,bool dragged)
        {
            return juce::MouseEvent(mouse,pos,juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                                    1,0,0,0,0,runway,runway,now,downPos,now,clicks,dragged);
        };
        runway->mouseDown(event(downPos,1,false));
        runway->mouseUp(event(downPos,1,false));
        runway->mouseDown(event(downPos,2,false));
        runway->mouseDoubleClick(event(downPos,2,false));
        runway->mouseUp(event(downPos,2,false));
        require(value(p,"lane0_first")==4,"Opening runway details changed First");
        runway->mouseDown(event(downPos,1,false));
        runway->mouseDrag(event({870.75f,20},1,true));
        runway->mouseUp(event({870.75f,20},1,true));
        require(value(p,"lane0_first")==3,"Dragging runway did not move First by one beat choice");
        juce::MemoryBlock complete;p.getStateInformation(complete);
        const auto full=p.exportSettingsText();
        for(const auto& id:ids)set(p,id,p.getAPVTS().getParameter(id)->convertFrom0to1(0));
        require(p.importSettingsText(full)&&p.exportSettingsText()==full,"Hidden lane settings did not roundtrip");
        editor->setSize(1254,706);
        editor->setSize(2006,1129);
        require(night->hasAirportResources(),"Resources lost during resize");
        editor.reset();p.setStateInformation(state.getData(),int(state.getSize()));
        set(p,"amount",.3f);set(p,"root",0);set(p,"scale",1);set(p,"outputMode",0);editor.reset(p.createEditor());
        if(argc==2)
        {
            juce::File file{juce::String(argv[1])};require(!file.exists(),"Preview already exists");file.getParentDirectory().createDirectory();auto stream=file.createOutputStream();require(stream&&stream->openedOk(),"Cannot write preview");
            require(juce::PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(),true,1.5f),*stream),"PNG encode failed");
        }
        std::cout<<"PASS seven-lane editor bounds and parameter bindings\n";editor.reset();p.releaseResources();return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
