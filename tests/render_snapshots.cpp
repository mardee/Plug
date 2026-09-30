#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI guiInit;

    juce::File outputDir = juce::File::getCurrentWorkingDirectory().getChildFile ("outputs");
    outputDir.createDirectory();

    auto saveSnapshot = [&] (juce::AudioProcessorEditor* editor, const juce::String& filename)
    {
        juce::Image img = editor->createComponentSnapshot (editor->getLocalBounds());
        juce::File outFile = outputDir.getChildFile (filename);
        outFile.deleteFile();
        juce::FileOutputStream stream (outFile);
        juce::PNGImageFormat().writeImageToStream (img, stream);
        std::cout << "Saved: " << outFile.getFullPathName() << "\n";
    };

    // 1. 常规模式 0%
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        if (auto* p = processor.getAPVTS().getParameter (ozo::ParamID::drive))
            p->setValueNotifyingHost (0.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        saveSnapshot (editor.get(), "prism_normal_0.png");
    }

    // 2. 常规模式 50%
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        saveSnapshot (editor.get(), "prism_normal_50.png");
    }

    // 3. 常规模式 100%（触发 WILD 开关浮现）
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        if (auto* p = processor.getAPVTS().getParameter (ozo::ParamID::drive))
            p->setValueNotifyingHost (1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        saveSnapshot (editor.get(), "prism_normal_100.png");
    }

    // 4. 狂野模式（静音无音频）
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        if (auto* p = processor.getAPVTS().getParameter (ozo::ParamID::wild))
            p->setValueNotifyingHost (1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        saveSnapshot (editor.get(), "prism_wild_idle.png");
    }

    // 5. 狂野模式（有音频激励）
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        if (auto* p = processor.getAPVTS().getParameter (ozo::ParamID::wild))
            p->setValueNotifyingHost (1.0f);

        // 模拟音频块处理产生频谱和能量
        juce::AudioBuffer<float> buf (2, 512);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = buf.getWritePointer (ch);
            for (int i = 0; i < 512; ++i)
                d[i] = std::sin ((float) i * 0.15f) * 0.7f;
        }
        juce::MidiBuffer midi;
        processor.processBlock (buf, midi);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        // 让 timerCallback / pushVisualState 运行并渲染
        saveSnapshot (editor.get(), "prism_wild_active.png");
    }

    // 6. EXPERT 侧边小窗展开形态（浅色模式）
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        if (auto* e = dynamic_cast<ozo::EZampEditor*> (editor.get()))
        {
            e->setViewMode (true);
            // 推进动画至满档
            for (int f = 0; f < 30; ++f)
                e->timerCallback();
        }
        saveSnapshot (editor.get(), "prism_expert_drawer_light.png");
    }

    // 7. EXPERT 侧边小窗展开形态（深色狂野模式）
    {
        ozo::EZampProcessor processor;
        processor.prepareToPlay (44100.0, 512);
        if (auto* p = processor.getAPVTS().getParameter (ozo::ParamID::wild))
            p->setValueNotifyingHost (1.0f);

        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        editor->setSize (960, 490);
        if (auto* e = dynamic_cast<ozo::EZampEditor*> (editor.get()))
        {
            e->setViewMode (true);
            for (int f = 0; f < 30; ++f)
                e->timerCallback();
        }
        saveSnapshot (editor.get(), "prism_expert_drawer_dark.png");
    }

    return 0;
}
