#pragma once
#include <limits>

namespace wav_export_tests {
using State = LatticeProcessor::WavExportState;

// 仅在现有项目 build 下创建本次测试自己的目录，不调用 exportMidi() 的全局临时目录。
static juce::File buildDirectory()
{
    for (auto start : {juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory(),
                       juce::File::getCurrentWorkingDirectory()})
        for (auto dir = start; dir != dir.getParentDirectory(); dir = dir.getParentDirectory())
        {
            if (dir.getFileName() == "build" && dir.getChildFile("CMakeCache.txt").existsAsFile())
                return dir;
            for (const auto* name : {"LATTICE", "."})
            {
                const auto project = dir.getChildFile(name);
                if (project.getChildFile("tests/Tests.cpp").existsAsFile()
                    && project.getChildFile("build/CMakeCache.txt").existsAsFile())
                    return project.getChildFile("build");
            }
        }
    return {};
}

template <typename Condition>
static bool poll(Condition condition, int timeoutMs = 30000)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
    while (!condition())
    {
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) return false;
        juce::Thread::sleep(1);
    }
    return true;
}

static bool finished(LatticeProcessor& processor)
{
    const bool done = poll([&] { return !processor.isWavExporting(); });
    CHECK(done, "WAV 后台任务在超时前结束");
    if (!done)
    {
        processor.cancelWavExport();
        const bool cancelled = poll([&] { return !processor.isWavExporting(); }, 5000);
        CHECK(cancelled, "超时后取消也必须在限定时间内结束");
        // 避免损坏的 worker 让后续析构 join 永久阻塞测试进程。
        if (!cancelled) std::abort();
    }
    return done;
}

static bool noTemporaryFiles(const juce::File& dir)
{
    return dir.findChildFiles(juce::File::findFilesAndDirectories, false, ".lattice-export-*").isEmpty();
}

static void setPattern(LatticeProcessor& processor, const lattice::Pattern& pattern)
{
    { juce::ScopedLock lock(processor.modelLock); processor.pattern = pattern; }
    processor.changed();
}

static void prepare(LatticeProcessor& processor, double rate = 48000, int block = 257)
{
    processor.setRateAndBufferSizeDetails(rate, block);
    processor.prepareToPlay(rate, block);
}

static lattice::Pattern boundaryPattern(int numerator, int denominator, double bpm)
{
    lattice::Pattern pattern;
    pattern.meter(numerator, denominator);
    pattern.bpm = bpm;
    pattern.bars[0][0][0].velocity = 127;
    // 最后一 tick 的 Crash，不是最后一个默认网格单元的起点。
    pattern.bars.back()[7] = {{0, pattern.barTicks() - 1, 0}, {pattern.barTicks() - 1, 1, 127}};
    return pattern;
}

static juce::int64 phraseFrames(const lattice::Pattern& pattern)
{
    return juce::int64(std::ceil(pattern.barTicks() * lattice::phraseBars
                               * (48000.0 * 60.0 / (pattern.bpm * lattice::ppq))));
}

// 独立 Processor，仅喂一次 MIDI；不同于导出 worker 的 4096 帧块，不启用 preview。
static void compareReference(const juce::File& file, const lattice::Pattern& snapshot, bool boundary)
{
    juce::WavAudioFormat format;
    auto stream = file.createInputStream();
    CHECK(stream != nullptr, "导出目标可打开读取");
    if (!stream) return;
    std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(stream.release(), true));
    CHECK(reader != nullptr, "真实导出文件可由 WAV reader 读回");
    if (!reader) return;
    const auto phrase = phraseFrames(snapshot);
    CHECK(reader->sampleRate == 48000 && reader->bitsPerSample == 24 && reader->numChannels == 2
          && !reader->usesFloatingPointData, "WAV 为 48kHz / 24-bit PCM / stereo");
    CHECK(reader->lengthInSamples == phrase + 168000, "帧数 = ceil(16 bar tick samples) + 168000");
    if (reader->lengthInSamples != phrase + 168000 || reader->numChannels != 2) return;

    struct Hit { int tick, row, velocity; };
    std::vector<Hit> hits;
    for (int bar = 0; bar < lattice::phraseBars; ++bar)
        for (int row = 0; row < lattice::tracks; ++row)
            for (const auto& cell : snapshot.bars[bar][row])
                if (cell.velocity > 0) hits.push_back({bar * snapshot.barTicks() + cell.start, row, cell.velocity});
    // 同 tick 的 voice/choke 顺序遵循现有 schedule 契约。
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.tick < b.tick; });
    juce::MidiBuffer timeline;
    for (const auto& hit : hits)
        timeline.addEvent(juce::MidiMessage::noteOn(10, lattice::notes[hit.row], juce::uint8(hit.velocity)),
                          int(std::floor(hit.tick * (48000.0 * 60.0 / (snapshot.bpm * lattice::ppq)))));

    LatticeProcessor reference;
    reference.setNonRealtime(true);
    prepare(reference);
    juce::AudioBuffer<float> expected(2, 257), actual(2, 257);
    juce::MidiBuffer midi;
    double maxError = 0;
    float firstPeak = 0, tailPeak = 0, gapPeak = 0, peak = 0;
    bool readable = true, finite = true;
    const auto lastHit = juce::int64(std::floor((snapshot.barTicks() * lattice::phraseBars - 1)
                                               * (48000.0 * 60.0 / (snapshot.bpm * lattice::ppq))));
    for (juce::int64 offset = 0; offset < reader->lengthInSamples;)
    {
        const int count = int(std::min<juce::int64>(257, reader->lengthInSamples - offset));
        expected.setSize(2, count, false, false, true);
        actual.setSize(2, count, false, false, true);
        midi.clear();
        midi.addEvents(timeline, int(offset), count, -int(offset));
        reference.processBlock(expected, midi);
        if (!reader->read(&actual, 0, count, offset, true, true)) { readable = false; break; }
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < count; ++i)
            {
                const float a = actual.getSample(channel, i), e = expected.getSample(channel, i);
                finite &= std::isfinite(a) && std::isfinite(e);
                maxError = std::max(maxError, std::abs(double(a) - e));
                peak = std::max(peak, std::abs(a));
                const auto frame = offset + i;
                if (frame < 4096) firstPeak = std::max(firstPeak, std::abs(a));
                if (frame >= phrase + 4800 && frame < phrase + 24000) tailPeak = std::max(tailPeak, std::abs(a));
                if (frame >= lastHit - 4096 && frame < lastHit) gapPeak = std::max(gapPeak, std::abs(a));
            }
        offset += count;
    }
    reference.releaseResources();
    recordPeak(peak);
    std::printf("        WAV 全帧参考最大误差 = %.9g，帧数 = %lld\n", maxError, (long long)reader->lengthInSamples);
    CHECK(readable && finite && peak > 0.001f, "完整 16 小节与尾音全部读回，音频有限且非静音");
    CHECK(maxError < 1e-6, "全部双声道样本与独立一次性 MIDI 渲染差 < 1e-6（含尾段，不循环）");
    if (boundary)
    {
        CHECK(firstPeak > 0.001f, "tick 0 首击未丢失");
        CHECK(gapPeak < 1e-6f, "最后 tick 之前保持静音，Crash 没有提前触发");
        CHECK(tailPeak > 0.001f, "最后 tick Crash 在句末之后仍有真实尾音，未被截断");
    }
}

static void renderCases(const juce::File& dir)
{
    section("WAV: 格式、完整音频参考、边界与快照");
    const std::array<lattice::Pattern, 4> cases {{lattice::Pattern{}, boundaryPattern(7, 8, 40),
                                               boundaryPattern(5, 4, 240), boundaryPattern(4, 4, 123.45)}};
    for (size_t i = 0; i < cases.size(); ++i)
    {
        const auto& snapshot = cases[i];
        std::printf("        case %zu: %d/%d %.2f bpm%s\n", i, snapshot.numerator, snapshot.denominator,
                    snapshot.bpm, i == 0 ? " 默认完整 16bar" : " 首击 + 最后 tick Crash");
        LatticeProcessor processor;
        setPattern(processor, snapshot);
        const auto file = dir.getChildFile("render-" + juce::String(int(i)) + ".wav");
        const bool started = processor.startWavExport(file);
        CHECK(started, "合法快照启动真实后台 WAV 导出");
        if (!started) continue;
        if (i == 0)
        {
            CHECK(processor.isWavExporting(), "快照修改发生在后台导出尚未完成时");
            auto changed = snapshot;
            changed.clear();
            changed.bpm = 240;
            setPattern(processor, changed);
        }
        if (!finished(processor)) return;
        const auto result = processor.getWavExportResult();
        CHECK(result.state == State::succeeded && result.file == file && result.bpm == snapshot.bpm,
              "导出成功，结果保留发起时路径与 BPM（后改 pattern 不影响快照）");
        CHECK(processor.getWavExportProgress() == 1.0f, "成功后进度为 1");
        CHECK(noTemporaryFiles(dir), "成功后不残留导出临时文件");
        if (result.state == State::succeeded) compareReference(file, snapshot, i != 0);
    }
}

static bool activeWriter(LatticeProcessor& processor, const juce::File& dir)
{
    const bool observed = poll([&] {
        return !processor.isWavExporting()
            || (processor.getWavExportProgress() > 0 && !noTemporaryFiles(dir));
    });
    const bool active = observed && processor.isWavExporting() && processor.getWavExportProgress() > 0
                        && processor.getWavExportProgress() < 1 && !noTemporaryFiles(dir);
    CHECK(active, "通过条件轮询确认 worker 已真实写入临时 WAV 且尚未完成");
    return active;
}

static void previewAndCancel(const juce::File& dir)
{
    section("WAV: 在线 preview、重复启动与取消保留目标");
    auto pattern = boundaryPattern(16, 4, 40);
    // 慢速长句为观察运行中任务留出空间，preview 对照包含持续发声。
    for (auto& bar : pattern.bars) for (auto& cell : bar[3]) cell.velocity = 80;
    LatticeProcessor processor, control;
    setPattern(processor, pattern);
    setPattern(control, pattern);
    prepare(processor, 44100, 256);
    prepare(control, 44100, 256);
    processor.preview.store(true);
    control.preview.store(true);
    juce::AudioBuffer<float> actual(2, 256), expected(2, 256);
    juce::MidiBuffer midi, controlMidi;
    double maxError = 0;
    float peak = 0;
    bool finite = true, sameTick = true;
    auto compareBlock = [&] {
        processor.processBlock(actual, midi);
        control.processBlock(expected, controlMidi);
        sameTick &= processor.playTick.load() == control.playTick.load();
        for (int ch = 0; ch < 2; ++ch) for (int sample = 0; sample < 256; ++sample)
        {
            const float a = actual.getSample(ch, sample), b = expected.getSample(ch, sample);
            finite &= std::isfinite(a) && std::isfinite(b);
            maxError = std::max(maxError, std::abs(double(a) - b));
            peak = std::max(peak, std::abs(a));
        }
    };
    for (int i = 0; i < 16; ++i) compareBlock();
    const auto target = dir.getChildFile("existing.wav");
    const char fixture[] = "LATTICE 自建目标 fixture：取消不能改写\0\x01\x7f";
    const bool created = target.replaceWithData(fixture, sizeof(fixture));
    CHECK(created, "仅在专用测试目录创建已有目标 fixture");
    if (!created) return;
    const bool started = processor.startWavExport(target);
    CHECK(started, "preview 播放期间可启动 WAV 导出");
    if (!started) return;
    const bool active = activeWriter(processor, dir);
    const auto second = dir.getChildFile("duplicate.wav");
    const auto before = processor.getWavExportResult();
    const bool duplicate = processor.startWavExport(second);
    CHECK(!duplicate, "同一 Processor 运行中拒绝重复启动");
    const auto after = processor.getWavExportResult();
    CHECK(after.state == before.state && after.file == before.file && after.bpm == before.bpm
          && after.message == before.message && !second.exists(), "重复启动不修改原任务结果且不创建第二目标");
    int concurrentBlocks = 0;
    for (int i = 0; i < 128; ++i)
    {
        if (processor.isWavExporting()) ++concurrentBlocks;
        compareBlock();
    }
    CHECK(active && concurrentBlocks > 0, "真实导出进行期间执行在线 processBlock 对照");
    processor.cancelWavExport();
    if (!finished(processor)) return;
    for (int i = 0; i < 16; ++i) compareBlock();
    CHECK(finite && maxError < 1e-7 && peak > 0.001f && sameTick && processor.preview.load(),
          "导出启动/写入/取消前后 preview 与独立在线对照音频及 playTick 一致，未中断");
    CHECK(processor.getWavExportResult().state == State::cancelled, "运行中取消返回 cancelled");
    juce::MemoryBlock bytes;
    CHECK(target.loadFileAsData(bytes) && bytes.getSize() == sizeof(fixture)
          && std::memcmp(bytes.getData(), fixture, sizeof(fixture)) == 0, "取消后已有目标逐字节不变");
    CHECK(noTemporaryFiles(dir), "取消完成后不残留临时文件");
    processor.releaseResources();
    control.releaseResources();
}

static void rejectionAndDestruction(const juce::File& dir)
{
    section("WAV: 无效路径、非法快照与析构取消 join");
    LatticeProcessor processor;
    const auto directoryTarget = dir.getChildFile("directory.wav");
    CHECK(directoryTarget.createDirectory().wasOk(), "创建属于测试的目录型目标 fixture");
    for (const auto& file : {dir.getChildFile("wrong.mid"), dir.getChildFile("missing/target.wav"), directoryTarget})
    {
        CHECK(!processor.startWavExport(file), "无效目标路径同步拒绝");
        CHECK(!processor.isWavExporting() && processor.getWavExportResult().state == State::failed,
              "路径拒绝后没有后台任务且报告 failed");
    }
    CHECK(!dir.getChildFile("wrong.mid").exists() && !dir.getChildFile("missing").exists()
          && directoryTarget.isDirectory(), "无效路径拒绝不创建文件或删除目录");
    const lattice::Pattern valid;
    for (int kind = 0; kind < 3; ++kind)
    {
        auto invalid = valid;
        if (kind == 0) invalid.bpm = std::numeric_limits<double>::quiet_NaN();
        if (kind == 1) invalid.groups = {1};
        if (kind == 2) invalid.bars[15][7].back().length -= 1;
        setPattern(processor, invalid);
        const auto file = dir.getChildFile("invalid-" + juce::String(kind) + ".wav");
        CHECK(!processor.startWavExport(file), "非法 BPM / 分组 / 非连续完整 lane 快照同步拒绝");
        CHECK(!processor.isWavExporting() && processor.getWavExportResult().state == State::failed
              && !file.exists() && noTemporaryFiles(dir), "非法快照不创建目标或临时文件");
    }
    const auto target = dir.getChildFile("destroyed.wav");
    auto doomed = std::make_unique<LatticeProcessor>();
    setPattern(*doomed, boundaryPattern(16, 4, 40));
    const bool started = doomed->startWavExport(target);
    CHECK(started, "析构测试启动真实后台导出");
    if (started) activeWriter(*doomed, dir);
    // 故意不先 cancel；另起销毁线程，仅为使析构内部 join 也受条件轮询超时保护。
    std::atomic<bool> destroyed{false};
    std::thread destroyer([owned = std::move(doomed), &destroyed]() mutable {
        owned.reset();
        destroyed.store(true, std::memory_order_release);
    });
    const bool joined = poll([&] { return destroyed.load(std::memory_order_acquire); }, 5000);
    CHECK(joined, "析构取消并 join 在超时前返回");
    if (!joined) std::abort(); // 不 detach，避免后台线程继续访问已离开作用域的数据。
    destroyer.join();
    CHECK(!target.exists() && noTemporaryFiles(dir), "析构返回时 worker 已取消并 join，未提交目标且无临时文件");
}

static void editorLayout()
{
    section("WAV: 编辑器按钮只读布局检查，不触发文件对话框");
    LatticeProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    CHECK(editor != nullptr, "创建真实 Processor 编辑器");
    if (!editor) return;
    juce::Button* wav = nullptr;
    juce::Button* mid = nullptr;
    int wavCount = 0, midCount = 0;
    std::function<void(juce::Component&)> visit = [&](juce::Component& component) {
        if (auto* button = dynamic_cast<juce::Button*>(&component))
        {
            if (button->getComponentID() == "save-wav") { wav = button; ++wavCount; }
            if (button->getButtonText() == "SAVE .MID") { mid = button; ++midCount; }
        }
        for (auto* child : component.getChildren()) visit(*child);
    };
    visit(*editor);
    CHECK(wavCount == 1 && midCount == 1, "按 componentID save-wav 找到唯一按钮及 SAVE .MID");
    if (!wav || !mid) return;
    const auto wavBounds = editor->getLocalArea(wav, wav->getLocalBounds());
    const auto midBounds = editor->getLocalArea(mid, mid->getLocalBounds());
    CHECK(wav->isVisible() && wav->isEnabled() && !wavBounds.isEmpty() && !midBounds.isEmpty()
          && editor->getLocalBounds().contains(wavBounds) && editor->getLocalBounds().contains(midBounds),
          "两个保存按钮布局非空且在编辑器内，WAV 按钮可用");
    CHECK(wavBounds.getX() >= midBounds.getRight() && !wavBounds.intersects(midBounds),
          "SAVE WAV 位于 SAVE .MID 右侧且不重叠");
}
} // namespace wav_export_tests

static void runWavExportTests()
{
    using namespace wav_export_tests;
    g_usedProcessor = true;
    const auto build = buildDirectory();
    CHECK(build != juce::File() && build.isDirectory(), "找到现有 build，禁止回退到个人或全局临时目录");
    if (build == juce::File() || !build.isDirectory()) return;
    const auto dir = build.getChildFile("wav-tests-" + juce::Uuid().toString());
    CHECK(!dir.exists() && !dir.isSymbolicLink(), "本次测试目录为未使用的 UUID 路径");
    if (dir.exists() || dir.isSymbolicLink()) return;
    const bool created = dir.createDirectory().wasOk();
    CHECK(created, "在 build 下创建唯一 WAV 测试目录");
    if (!created) return;
    std::printf("        测试目录：%s\n", dir.getFullPathName().toRawUTF8());
    renderCases(dir);
    previewAndCancel(dir);
    rejectionAndDestruction(dir);
    editorLayout();
    CHECK(noTemporaryFiles(dir), "全部任务销毁后无残留导出临时文件");
    // 仅删除上面亲自创建、从未用于个人文件的 UUID 子目录。
    CHECK(dir.deleteRecursively(), "清理本次测试专属目录");
}
