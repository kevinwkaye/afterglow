#pragma once

#include "DspUtil.h"

// The built-in FX loop: reverb, delay, chorus and shimmer. Each returns its wet
// signal only; the engine does the mixing and the Pre / Loop / Post routing.
namespace ag
{
enum FxType { FxVerb = 0, FxDelay, FxChorus, FxShimmer };

// Schroeder allpass with a fixed integer delay
struct Allpass
{
    DelayLine d;
    int len = 100;
    float g = 0.7f;
    void init (int samples, float gain) { len = samples; g = gain; d.init (nextPow2 (samples + 4)); }
    void clear() { d.clear(); }
    float process (float x)
    {
        const float delayed = d.at (len);
        const float y = -g * x + delayed;
        d.push (x + g * y);
        return y;
    }
};

// Octave-up pitch shifter: two crossfading read heads sweeping through a short buffer
struct OctaveUp
{
    DelayLine d;
    float window = 2400.0f, phase = 0.0f;
    void init (float fs)
    {
        window = 0.05f * fs;
        d.init (nextPow2 ((int) window * 2 + 16));
        phase = 0.0f;
    }
    void clear() { d.clear(); phase = 0.0f; }
    float process (float x)
    {
        d.push (x);
        phase += 1.0f / window;
        if (phase >= 1.0f) phase -= 1.0f;
        float out = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            float p = phase + 0.5f * (float) k;
            if (p >= 1.0f) p -= 1.0f;
            const float delay = window * (1.0f - p) + 4.0f;
            out += d.tap (delay) * std::sin (kPi * p);
        }
        return out * 0.7071f;
    }
};

// Eight-line feedback delay network: wide, smooth, gently moving. With shimmer on,
// an octave-up copy of the tail feeds back in so it climbs.
class Reverb
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const float ms[8] = { 31.7f, 37.9f, 43.1f, 47.3f, 53.9f, 61.1f, 67.7f, 79.3f };
        for (int i = 0; i < 8; ++i)
        {
            len[i] = ms[i] * 0.0013f * fs; // a touch larger than a hall
            line[i].init (nextPow2 ((int) len[i] + 64));
            damp[i].reset();
            lfo[i] = 0.13f * (float) i;
            lfoRate[i] = (0.09f + 0.037f * (float) i) / fs;
        }
        const float sc = fs / 44100.0f;
        const int apL[4] = { 142, 107, 379, 277 }, apR[4] = { 149, 113, 367, 293 };
        for (int i = 0; i < 4; ++i)
        {
            inL[i].init ((int) ((float) apL[i] * sc), 0.7f);
            inR[i].init ((int) ((float) apR[i] * sc), 0.7f);
        }
        shifter.init (fs);
        shimHp.setCutoff (350.0f, fs);
        shimLp.setCutoff (7000.0f, fs);
        clear();
    }

    void clear()
    {
        for (auto& l : line) l.clear();
        for (auto& f : damp) f.reset();
        for (auto& a : inL) a.clear();
        for (auto& a : inR) a.clear();
        shifter.clear();
        shimHp.reset(); shimLp.reset();
        shimFeed = 0.0f;
    }

    void set (float amount, bool shimmerOn)
    {
        shimmer = shimmerOn;
        const float rt60 = shimmer ? 3.5f + 12.0f * amount : 1.4f + 11.0f * amount * amount + 1.5f * amount;
        for (int i = 0; i < 8; ++i)
        {
            gain[i] = std::pow (10.0f, -3.0f * len[i] / (fs * rt60));
            damp[i].setCutoff (shimmer ? 9000.0f : 6500.0f - 2500.0f * amount, fs);
        }
        shimAmount = shimmer ? 0.28f + 0.18f * amount : 0.0f;
    }

    Stereo process (Stereo in)
    {
        float l = in.l, r = in.r;
        for (auto& a : inL) l = a.process (l);
        for (auto& a : inR) r = a.process (r);

        float y[8];
        for (int i = 0; i < 8; ++i)
        {
            lfo[i] += lfoRate[i];
            if (lfo[i] >= 1.0f) lfo[i] -= 1.0f;
            const float mod = (6.0f * fs / 48000.0f) * std::sin (kTwoPi * lfo[i]);
            y[i] = damp[i].process (line[i].tap (len[i] + mod));
        }

        // Fast Walsh-Hadamard mix, energy preserving
        for (int h = 1; h < 8; h <<= 1)
            for (int i = 0; i < 8; i += h << 1)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = y[j], b = y[j + h];
                    y[j] = a + b;
                    y[j + h] = a - b;
                }
        const float norm = 0.35355339f;

        const float shim = shimFeed;
        for (int i = 0; i < 8; ++i)
        {
            const float inject = (i & 1) ? r : l;
            line[i].push (softClip (y[i] * norm * gain[i] + 0.5f * inject + shim * 0.5f));
        }

        Stereo out;
        out.l = (line[0].at (1) + line[2].at (1) + line[4].at (1) + line[6].at (1)) * 0.45f;
        out.r = (line[1].at (1) + line[3].at (1) + line[5].at (1) + line[7].at (1)) * 0.45f;

        if (shimmer)
        {
            const float mono = 0.5f * (out.l + out.r);
            const float up = shifter.process (mono);
            const float hp = up - shimHp.process (up);
            shimFeed = shimLp.process (hp) * shimAmount;
        }
        else
        {
            shimFeed = 0.0f;
        }
        return out;
    }

private:
    float fs = 48000.0f;
    DelayLine line[8];
    float len[8] = {}, gain[8] = {}, lfo[8] = {}, lfoRate[8] = {};
    OnePole damp[8];
    Allpass inL[4], inR[4];
    OctaveUp shifter;
    OnePole shimHp, shimLp;
    float shimFeed = 0.0f, shimAmount = 0.0f;
    bool shimmer = false;
};

// Ping-pong delay with a darkening, gently saturating feedback path
class Delay
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const int size = nextPow2 ((int) (1.4f * fs));
        dl.init (size);
        dr.init (size);
        time.setTime (0.25f, fs);
        lpL.setCutoff (4200.0f, fs); lpR.setCutoff (4200.0f, fs);
        hpL.setCutoff (110.0f, fs); hpR.setCutoff (110.0f, fs);
        clear();
    }

    void clear()
    {
        dl.clear(); dr.clear();
        lpL.reset(); lpR.reset(); hpL.reset(); hpR.reset();
        time.primed = false;
    }

    void set (float amount)
    {
        targetTime = 0.09f * std::pow (1.15f / 0.09f, amount) * fs;
        feedback = 0.25f + 0.55f * amount;
    }

    Stereo process (Stereo in)
    {
        const float t = time.next (targetTime);
        float yl = dl.tap (t), yr = dr.tap (t);
        yl = lpL.process (yl); yl -= hpL.process (yl);
        yr = lpR.process (yr); yr -= hpR.process (yr);
        // left feeds right and right feeds left: echoes bounce across the stereo field
        dl.push (softClip (0.5f * (in.l + in.r) + yr * feedback));
        dr.push (softClip (yl * feedback));
        return { yl, yr };
    }

private:
    float fs = 48000.0f, targetTime = 10000.0f, feedback = 0.5f;
    DelayLine dl, dr;
    Smoother time;
    OnePole lpL, lpR, hpL, hpR;
};

// Three-voice stereo chorus
class Chorus
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        const int size = nextPow2 ((int) (0.06f * fs));
        dl.init (size);
        dr.init (size);
        clear();
    }

    void clear()
    {
        dl.clear(); dr.clear();
        for (int i = 0; i < 3; ++i) phase[i] = (float) i / 3.0f;
    }

    void set (float amount)
    {
        depth = (1.5f + 5.0f * amount) * 0.001f * fs;
        rate = 0.25f + 0.9f * amount;
    }

    Stereo process (Stereo in)
    {
        dl.push (in.l);
        dr.push (in.r);
        Stereo out;
        for (int i = 0; i < 3; ++i)
        {
            phase[i] += rate * (1.0f + 0.13f * (float) i) / fs;
            if (phase[i] >= 1.0f) phase[i] -= 1.0f;
            const float base = (12.0f + 4.0f * (float) i) * 0.001f * fs;
            out.l += dl.tap (base + depth * (0.5f + 0.5f * std::sin (kTwoPi * phase[i])));
            out.r += dr.tap (base + depth * (0.5f + 0.5f * std::sin (kTwoPi * (phase[i] + 0.25f))));
        }
        return out * 0.45f;
    }

private:
    float fs = 48000.0f, depth = 100.0f, rate = 0.5f;
    float phase[3] = {};
    DelayLine dl, dr;
};

// One effect slot holding all four; only the chosen one runs
class FxSlot
{
public:
    void prepare (float fs)
    {
        verb.prepare (fs);
        delay.prepare (fs);
        chorus.prepare (fs);
    }

    void clear()
    {
        verb.clear();
        delay.clear();
        chorus.clear();
    }

    void set (int type, float amount)
    {
        current = type;
        switch (type)
        {
            case FxDelay:  delay.set (amount); break;
            case FxChorus: chorus.set (amount); break;
            case FxShimmer: verb.set (amount, true); break;
            default: verb.set (amount, false); break;
        }
    }

    Stereo process (Stereo in)
    {
        switch (current)
        {
            case FxDelay:  return delay.process (in);
            case FxChorus: return chorus.process (in);
            default:       return verb.process (in);
        }
    }

private:
    Reverb verb;
    Delay delay;
    Chorus chorus;
    int current = FxVerb;
};
} // namespace ag
