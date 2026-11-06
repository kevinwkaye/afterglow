#pragma once

#include "DspUtil.h"

// Spots the start of each new note or chord for Auto Layer Catch.
// A fast envelope racing ahead of a slow one is an attack. Whether something is loud
// enough to count is judged against how loud you've been playing, never a fixed level,
// so soft playing and quiet sources catch as well as loud ones.
namespace ag
{
class Detector
{
public:
    void prepare (float sampleRate)
    {
        fs = sampleRate;
        setBass (bass);
        lp.setCutoff (7000.0f, fs);
        fastAtt = std::exp (-1.0f / (0.0006f * fs));
        fastRel = std::exp (-1.0f / (0.018f * fs));
        slowAtt = std::exp (-1.0f / (0.040f * fs));
        slowRel = std::exp (-1.0f / (0.35f * fs));
        playAtt = std::exp (-1.0f / (0.3f * fs));
        playRel = std::exp (-1.0f / (6.0f * fs));
        lockSamples = (int) (0.085f * fs);
        reset();
    }

    // bass: keep the low strings in the detector
    void setBass (bool b)
    {
        bass = b;
        hp.setCutoff (b ? 28.0f : 70.0f, fs);
    }

    void reset()
    {
        fast = slow = play = 0.0f;
        hp.reset(); lp.reset();
        sinceOnset = 1 << 30;
        armed = true;
    }

    // Returns the attack's strength (0..1) on the sample an attack is found, else 0
    float process (float x)
    {
        const float lowCut = x - hp.process (x);
        const float a = std::fabs (lp.process (lowCut));

        fast = a > fast ? fastAtt * fast + (1.0f - fastAtt) * a : fastRel * fast + (1.0f - fastRel) * a;
        slow = fast > slow ? slowAtt * slow + (1.0f - slowAtt) * fast : slowRel * slow + (1.0f - slowRel) * fast;
        play = fast > play ? playAtt * play + (1.0f - playAtt) * fast : playRel * play + (1.0f - playRel) * fast;

        if (sinceOnset < (1 << 30)) ++sinceOnset;
        if (! armed && sinceOnset > lockSamples && fast < slow * 1.15f)
            armed = true;

        const float threshold = gate();
        if (armed && fast > threshold && fast > slow * 1.8f + 1.0e-6f)
        {
            armed = false;
            sinceOnset = 0;
            return clampf (fast / (threshold * 8.0f), 0.1f, 1.0f);
        }
        return 0.0f;
    }

    // 23 dB under how loud you've been playing, never lower than about -94 dBFS
    float gate() const { return std::max (2.0e-5f, play * 0.07f); }
    float level() const { return fast; }
    float playingLevel() const { return play; }
    bool sounding() const { return fast > gate() * 1.5f; }

private:
    float fs = 48000.0f;
    float fastAtt = 0, fastRel = 0, slowAtt = 0, slowRel = 0, playAtt = 0, playRel = 0;
    float fast = 0, slow = 0, play = 0;
    OnePole hp, lp;
    int sinceOnset = 1 << 30, lockSamples = 4000;
    bool armed = true, bass = false;
};
} // namespace ag
