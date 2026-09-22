#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <cmath>

namespace ozo
{

//==============================================================================
// 实时频谱分析器（给界面用的，不参与音频处理）
//
// 三条硬约束：
//   1. 音频线程里绝不做内存分配 —— 所有缓冲在 prepare() 里一次性备好，
//      push() 只做累加和 FFT。
//   2. 用 std::atomic<float> 把结果交给界面线程，不设锁。
//      音频线程绝不能等界面线程，反过来界面线程读到一帧旧数据也无所谓。
//   3. 幅度做"快起慢落"：起 0.55 / 落 0.12。
//      纯瞬时值会抖得看不清，落得慢才有数字 EQ 那种拖尾感。
class SpectrumAnalyser
{
public:
    static constexpr int kNumBins   = 80;
    static constexpr int kFftOrder  = 11;                 // 2048 点
    static constexpr int kFftSize   = 1 << kFftOrder;

    void prepare (double sampleRate)
    {
        sr = sampleRate;

        fifoPos = 0;
        fifo.fill (0.0f);
        fftData.fill (0.0f);

        for (auto& b : bins)   b.store (0.0f);
        for (auto& p : peaks)  p.store (0.0f);
        energy.store (0.0f);

        // Hann 窗 —— 不加窗的话频谱泄露会把柱子的底抬起来，看着是"糊"的
        for (int i = 0; i < kFftSize; ++i)
            window[i] = 0.5f - 0.5f * std::cos (2.0f * pi * (float) i / (float) (kFftSize - 1));

        // 对数分箱：20 Hz .. 尽可能到 20 kHz。
        // 线性分箱会把 80 根柱子里的 70 根都堆在高频，低频全挤在一起，没法看。
        const double fMin  = 22.0;
        const double fMax  = juce::jmin (20000.0, sampleRate * 0.48);
        const double binHz = sampleRate / (double) kFftSize;
        const int    maxBin = kFftSize / 2;

        for (int i = 0; i < kNumBins; ++i)
        {
            const double f0 = fMin * std::pow (fMax / fMin,  (double) i       / (double) kNumBins);
            const double f1 = fMin * std::pow (fMax / fMin,  (double) (i + 1) / (double) kNumBins);

            int b0 = (int) std::floor (f0 / binHz);
            int b1 = (int) std::ceil  (f1 / binHz);

            b0 = juce::jlimit (1, maxBin - 2, b0);
            b1 = juce::jlimit (b0 + 1, maxBin - 1, b1);

            binStart[i] = b0;
            binEnd[i]   = b1;
        }

        ready = true;
    }

    void reset()
    {
        for (auto& b : bins)   b.store (0.0f);
        for (auto& p : peaks)  p.store (0.0f);
        energy.store (0.0f);
        fifoPos = 0;
        fifo.fill (0.0f);
    }

    //--------------------------------------------------------------------------
    // 音频线程调用。攒满一窗就做一次 FFT（48 kHz 下约 23 次/秒）。
    void push (const juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! ready)
            return;

        const int n    = buffer.getNumSamples();
        const int chns = buffer.getNumChannels();

        if (n <= 0 || chns <= 0)
            return;

        const auto* ptrs = buffer.getArrayOfReadPointers();
        const float invCh = 1.0f / (float) chns;

        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < chns; ++c)
                s += ptrs[c][i];

            fifo[(size_t) fifoPos] = s * invCh;

            if (++fifoPos >= kFftSize)
            {
                fifoPos = 0;
                analyse();
            }
        }
    }

    //--------------------------------------------------------------------------
    // 界面线程调用
    void readInto (float* dest, int num) const noexcept
    {
        const int n = juce::jmin (num, kNumBins);
        for (int i = 0; i < n; ++i)
            dest[i] = bins[(size_t) i].load();
    }

    // 峰值保持是在 analyse() 里算好的（顶上去、否则 ×0.94 慢慢掉）。
    // 之前只算不读，界面永远看不到。
    void readPeaksInto (float* dest, int num) const noexcept
    {
        const int n = juce::jmin (num, kNumBins);
        for (int i = 0; i < n; ++i)
            dest[i] = peaks[(size_t) i].load();
    }

    float getEnergy() const noexcept { return energy.load(); }

    // 每做完一次 FFT 就 +1。界面拿它判断"这一帧有没有新数据" ——
    // 宿主停止调用 processBlock 时它就不动了，界面才知道该让柱子落下来。
    uint32_t getTick() const noexcept { return tick.load(); }

    int getNumBins() const noexcept { return kNumBins; }

private:
    //--------------------------------------------------------------------------
    void analyse() noexcept
    {
        // fftData 必须是 2 * kFftSize：前半放输入，返回时整块变成交错的实部/虚部
        for (int i = 0; i < kFftSize; ++i)
            fftData[(size_t) i] = fifo[(size_t) i] * window[(size_t) i];

        for (int i = kFftSize; i < 2 * kFftSize; ++i)
            fftData[(size_t) i] = 0.0f;

        fft.performRealOnlyForwardTransform (fftData.data(), false);

        const float norm = 2.0f / (float) kFftSize;
        float sum = 0.0f;

        for (int i = 0; i < kNumBins; ++i)
        {
            // 一个对数箱里可能压着好几个 FFT bin，取最大而不是平均 ——
            // 平均会把窄带峰值抹平，柱状图就"趴"下去了。
            float mag = 0.0f;

            for (int k = binStart[i]; k < binEnd[i]; ++k)
            {
                const float re = fftData[(size_t) (2 * k)];
                const float im = fftData[(size_t) (2 * k + 1)];
                const float m  = std::sqrt (re * re + im * im) * norm;

                if (m > mag)
                    mag = m;
            }

            // -84 dB 底 .. 0 dB 顶，映射到 0..1
            const float db    = 20.0f * std::log10 (mag + 1.0e-7f);
            const float target = juce::jlimit (0.0f, 1.0f, (db + 84.0f) / 84.0f);

            // 快起慢落
            const float cur = bins[(size_t) i].load();
            const float co  = target > cur ? 0.55f : 0.12f;
            const float v   = cur + (target - cur) * co;

            bins[(size_t) i].store (v);

            // 峰值保持：比当前值高就直接顶上去，否则慢慢掉
            const float pk = peaks[(size_t) i].load();
            peaks[(size_t) i].store (v > pk ? v : pk * 0.94f);

            sum += v;
        }

        const float e = sum / (float) kNumBins;
        const float ce = energy.load();
        energy.store (ce + (e - ce) * (e > ce ? 0.5f : 0.12f));

        tick.fetch_add (1, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    static constexpr float pi = 3.14159265358979323846f;

    double sr = 48000.0;
    bool   ready = false;

    juce::dsp::FFT fft { kFftOrder };

    std::array<float, kFftSize>      fifo    {};
    std::array<float, kFftSize * 2>  fftData {};
    std::array<float, kFftSize>      window  {};
    int fifoPos = 0;

    std::array<int, kNumBins> binStart {};
    std::array<int, kNumBins> binEnd   {};

    std::array<std::atomic<float>, kNumBins> bins  {};
    std::array<std::atomic<float>, kNumBins> peaks {};
    std::atomic<float> energy { 0.0f };
    std::atomic<uint32_t> tick { 0 };
};

} // namespace ozo
