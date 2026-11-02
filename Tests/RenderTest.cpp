// Offline render test for the Afterglow engine (no JUCE needed).
// Plays a guitar-like phrase through every catch, hold, piano and effect mode with
// the brass pedal scripted, then checks for NaNs, runaway levels, clicks and silence.
// Pass a folder to also write WAV files.
//
// Usage: afterglow_render_test [output_folder]

#include "AfterglowEngine.h"
#include <cstdio>
#include <functional>
#include <string>

using namespace ag;

static constexpr float kFs = 48000.0f;

// Plucked notes and a chord: note k starts at 0.2 + 0.9k seconds
static void makeInput (std::vector<float>& l, std::vector<float>& r, int n, float lowest = 220.0f, int notes = 6)
{
    l.assign ((size_t) n, 0.0f);
    r.assign ((size_t) n, 0.0f);
    const float ratios[6] = { 1.0f, 1.26f, 1.498f, 2.0f, 1.498f, 1.26f };
    for (int k = 0; k < notes; ++k)
    {
        const int s0 = (int) ((0.2f + (float) k * 0.9f) * kFs);
        const bool chord = k % 6 == 3;
        for (int i = 0; i < (int) (1.6f * kFs) && s0 + i < n; ++i)
        {
            const float t = (float) i / kFs;
            const float e = std::exp (-t * 2.5f) * std::min (1.0f, t * 500.0f);
            float v = 0.0f;
            for (int c = 0; c < (chord ? 3 : 1); ++c)
            {
                const float f = lowest * ratios[k % 6] * (c == 0 ? 1.0f : (c == 1 ? 1.26f : 1.498f)) * (k >= 6 ? 1.12f : 1.0f);
                v += 0.22f * e * (std::sin (kTwoPi * f * t) + 0.45f * std::sin (kTwoPi * 2 * f * t) + 0.25f * std::sin (kTwoPi * 3 * f * t));
            }
            l[(size_t) (s0 + i)] += v;
            r[(size_t) (s0 + i)] += v;
        }
    }
}

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r)
{
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), dataBytes = n * 4, sr = (uint32_t) kFs;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + dataBytes); std::fwrite ("WAVE", 1, 4, f);
    std::fwrite ("fmt ", 1, 4, f); u32 (16); u16 (1); u16 (2); u32 (sr); u32 (sr * 4); u16 (4); u16 (16);
    std::fwrite ("data", 1, 4, f); u32 (dataBytes);
    for (uint32_t i = 0; i < n; ++i)
    {
        int16_t a = (int16_t) (clampf (l[i], -1, 1) * 32767.0f);
        int16_t b = (int16_t) (clampf (r[i], -1, 1) * 32767.0f);
        std::fwrite (&a, 2, 1, f); std::fwrite (&b, 2, 1, f);
    }
    std::fclose (f);
}

struct Result
{
    float peak = 0, rms = 0, maxJump = 0, side = 0;
    bool nan = false;
    std::vector<float> l, r;
    float rmsBetween (float t0, float t1) const
    {
        double s = 0; int c = 0;
        for (int i = (int) (t0 * kFs); i < (int) (t1 * kFs) && i < (int) l.size(); ++i, ++c)
            s += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
        return c > 0 ? (float) std::sqrt (s / (2.0 * c)) : 0.0f;
    }
};

using Script = std::function<void (float t, EngineParams&, AfterglowEngine&)>;

static Result render (const char* name, EngineParams p, const Script& script, const std::string& outDir,
                      float seconds = 7.0f, float lowest = 220.0f, int notes = 6, std::vector<float>* customInput = nullptr)
{
    static AfterglowEngine eng; // large: keep it off the stack
    eng.prepare (kFs);
    const int n = (int) (seconds * kFs);
    Result res;
    if (customInput) { res.l = *customInput; res.r = *customInput; res.l.resize ((size_t) n); res.r.resize ((size_t) n); }
    else makeInput (res.l, res.r, n, lowest, notes);
    auto& l = res.l;
    auto& r = res.r;

    const int block = 128;
    for (int s = 0; s < n; s += block)
    {
        const int len = std::min (block, n - s);
        if (script) script ((float) s / kFs, p, eng);
        eng.process (l.data() + s, r.data() + s, len, p);
    }

    double sum = 0, side = 0;
    for (int i = 0; i < n; ++i)
    {
        const float a = l[(size_t) i], b = r[(size_t) i];
        if (! std::isfinite (a) || ! std::isfinite (b)) res.nan = true;
        res.peak = std::max (res.peak, std::max (std::fabs (a), std::fabs (b)));
        sum += (double) a * a + (double) b * b;
        side += (double) (a - b) * (a - b);
        if (i > 1)
        {
            const float d2 = std::max (std::fabs (a - 2 * l[(size_t) i - 1] + l[(size_t) i - 2]),
                                       std::fabs (b - 2 * r[(size_t) i - 1] + r[(size_t) i - 2]));
            res.maxJump = std::max (res.maxJump, d2);
        }
    }
    res.rms = (float) std::sqrt (sum / (2.0 * n));
    res.side = (float) std::sqrt (side / n);
    std::printf ("%-26s peak %.3f  rms %.4f  side %.4f  jump %.3f  layers caught %d%s\n", name, res.peak, res.rms, res.side, res.maxJump,
                 eng.uiCaptures.load(), res.nan ? "  NaN!" : "");
    if (! outDir.empty()) writeWav (outDir + "/" + name + ".wav", l, r);
    return res;
}

// pedal held between t0 and t1
static Script holdPedal (float t0, float t1, float depth = 1.0f)
{
    return [=] (float t, EngineParams& q, AfterglowEngine&) { q.pedal = (t >= t0 && t < t1) ? depth : 0.0f; };
}

int main (int argc, char** argv)
{
    const std::string out = argc > 1 ? argv[1] : "";
    int failures = 0;
    auto expect = [&] (bool ok, const char* what, const char* name)
    {
        if (! ok) { std::printf ("  FAIL %s: %s\n", name, what); ++failures; }
    };
    auto check = [&] (const char* name, const Result& r, bool expectSound = true)
    {
        expect (! r.nan, "NaN in output", name);
        expect (r.peak <= 1.0f, "output over full scale", name);
        if (expectSound) expect (r.rms > 0.004f, "no effect heard", name);
        expect (r.maxJump < 0.35f, "click (sudden jump)", name);
    };

    EngineParams base;
    base.killDry = true; // effect only, so silence means the engine did nothing
    base.fxMix = 0.0f;
    base.rise = 0.0f;
    base.decay = 0.5f;
    base.tone = 1.0f;

    {
        // Level: a steady tone, caught and held, should come back at about the same level
        std::vector<float> sine ((size_t) (4.0f * kFs));
        for (size_t i = 0; i < sine.size(); ++i) sine[i] = 0.3f * std::sin (kTwoPi * 330.0f * (float) i / kFs) * std::min (1.0f, (float) i / 2000.0f) * (i < (size_t) (1.2f * kFs) ? 1.0f : 0.0f);
        auto p = base; p.layers = 1; p.catchMode = CatchSingle;
        auto r = render ("unity-level", p, holdPedal (0.9f, 3.0f), out, 4.0f, 220.0f, 0, &sine);
        check ("unity-level", r);
        const float held = r.rmsBetween (2.0f, 2.9f);
        std::printf ("  held level %.3f vs input %.3f (ratio %.2f)\n", held, 0.3f * 0.7071f, held / (0.3f * 0.7071f));
        expect (held > 0.15f && held < 0.3f, "held layer not near unity", "unity-level");
        expect (r.rmsBetween (3.6f, 4.0f) < 0.5f * held, "did not fade on release", "unity-level");
    }

    {
        auto p = base; p.layers = 4;
        auto r = render ("all-all", p, holdPedal (0.1f, 5.6f), out);
        check ("all-all", r);
        expect (r.side > 0.002f, "layers not spread in stereo", "all-all");
        expect (r.rmsBetween (6.6f, 7.0f) < 0.3f * r.rmsBetween (5.0f, 5.5f), "layers did not decay after release", "all-all");
    }
    { auto p = base; p.layers = 4; p.holdMode = HoldNone; check ("all-none", render ("all-none", p, holdPedal (0.1f, 6.5f), out)); }
    { auto p = base; p.layers = 4; p.holdMode = HoldFirst; p.decay = 0.4f; check ("all-first", render ("all-first", p, holdPedal (0.1f, 6.5f), out)); }
    {
        auto p = base; p.layers = 2; p.catchMode = CatchGroup;
        int caught = 0;
        auto r = render ("group-of-2", p, [&] (float t, EngineParams& q, AfterglowEngine& e)
        {
            q.pedal = t > 0.1f && t < 6.5f ? 1.0f : 0.0f;
            caught = e.uiCaptures.load();
        }, out);
        check ("group-of-2", r);
        expect (caught == 2, "group did not close after two layers", "group-of-2");
    }
    {
        // Single: tap the pedal on two notes
        auto p = base; p.layers = 3; p.catchMode = CatchSingle; p.decay = 0.6f;
        check ("single-taps", render ("single-taps", p, [] (float t, EngineParams& q, AfterglowEngine&)
        {
            q.pedal = (t > 0.5f && t < 2.0f) || (t > 2.4f && t < 5.0f) ? 1.0f : 0.0f;
        }, out));
    }
    {
        // Infinite: many notes, layers fold together but never stop
        auto p = base; p.layers = kInfinite; p.decay = 0.7f;
        int active = 0;
        auto r = render ("infinite-layers", p, [&] (float t, EngineParams& q, AfterglowEngine& e)
        {
            q.pedal = t > 0.1f ? 1.0f : 0.0f;
            active = e.uiActive.load();
        }, out, 14.0f, 196.0f, 14);
        check ("infinite-layers", r);
        expect (active <= AfterglowEngine::kSlots && active >= 8, "infinite mode lost its layers", "infinite-layers");
    }
    {
        auto p = base; p.layers = 3; p.decay = 1.0f;
        auto r = render ("decay-infinite", p, holdPedal (0.1f, 3.0f), out);
        check ("decay-infinite", r);
        expect (r.rmsBetween (6.0f, 7.0f) > 0.5f * r.rmsBetween (3.5f, 4.0f), "infinite decay faded", "decay-infinite");
    }
    {
        // Double tap clears everything at once
        auto p = base; p.layers = 4; p.decay = 1.0f;
        auto r = render ("double-tap-clear", p, [] (float t, EngineParams& q, AfterglowEngine&)
        {
            q.pedal = (t > 0.1f && t < 3.0f) || (t > 3.2f && t < 3.4f) || (t > 3.55f && t < 3.7f) ? 1.0f : 0.0f;
        }, out, 6.0f, 220.0f, 4);
        check ("double-tap-clear", r);
        expect (r.rmsBetween (4.0f, 6.0f) < 0.002f, "double tap did not clear", "double-tap-clear");
    }
    {
        // Real piano: release damps the strings quickly
        auto p = base; p.holdMode = HoldNone; p.tone = 0.0f; p.layers = 10; p.decay = 0.8f;
        auto r = render ("real-piano", p, holdPedal (0.1f, 3.5f), out);
        check ("real-piano", r);
        expect (r.rmsBetween (3.8f, 4.1f) < 0.25f * r.rmsBetween (3.0f, 3.4f), "dampers did not fall", "real-piano");
    }
    { auto p = base; p.holdMode = HoldNone; p.tone = 0.1f; p.catchMode = CatchGroup; p.layers = 3; check ("piano-group", render ("piano-group", p, holdPedal (0.1f, 5.0f), out)); }
    {
        // Sostenuto: hold the chord, later notes that share its partials ring it again
        auto p = base; p.holdMode = HoldNone; p.tone = 0.0f; p.catchMode = CatchSingle; p.layers = 6; p.decay = 0.7f;
        check ("sostenuto", render ("sostenuto", p, holdPedal (3.0f, 6.5f), out));
    }
    {
        // Footswitch Latch catches without the pedal
        auto p = base; p.layers = 4; p.fsOn = true; p.fsMode = FsLatch;
        check ("latch", render ("latch", p, nullptr, out));
    }
    {
        // Half pedal swells: depth moves the held level smoothly
        auto p = base; p.layers = 4; p.rise = 0.2f;
        check ("half-pedal", render ("half-pedal", p, [] (float t, EngineParams& q, AfterglowEngine&)
        {
            q.pedal = t > 0.1f ? 0.55f + 0.45f * std::sin (t * 3.0f) : 0.0f;
        }, out));
    }
    { auto p = base; p.layers = 5; p.resonance = 0.9f; check ("resonance-up", render ("resonance-up", p, holdPedal (0.1f, 6.0f), out)); }
    { auto p = base; p.layers = 5; p.resonance = -0.9f; check ("resonance-down", render ("resonance-down", p, holdPedal (0.1f, 6.0f), out)); }
    { auto p = base; p.layers = 5; p.tone = 0.3f; check ("tone-dark", render ("tone-dark", p, holdPedal (0.1f, 6.0f), out)); }

    static const char* fxNames[4] = { "verb", "delay", "chorus", "shimmer" };
    static const char* posNames[3] = { "pre", "loop", "post" };
    for (int f = 0; f < 4; ++f)
        for (int pos = 0; pos < 3; ++pos)
        {
            auto p = base; p.layers = 5; p.fxType = f; p.fxPos = pos; p.fxMix = 0.5f; p.fxAmount = 0.7f; p.killDry = false;
            const std::string name = std::string ("fx-") + fxNames[f] + "-" + posNames[pos];
            auto r = render (name.c_str(), p, holdPedal (0.1f, 5.0f), out);
            check (name.c_str(), r);
        }
    {
        // Shimmer at full amount for a long time must not run away
        auto p = base; p.layers = kInfinite; p.fxType = FxShimmer; p.fxMix = 1.0f; p.fxAmount = 1.0f; p.decay = 1.0f;
        auto r = render ("shimmer-max", p, holdPedal (0.1f, 20.0f), out, 20.0f);
        check ("shimmer-max", r);
    }
    {
        // Switching effects and positions while playing glides through silence, no clicks
        auto p = base; p.layers = 5; p.fxMix = 0.6f; p.killDry = false;
        check ("fx-switching", render ("fx-switching", p, [] (float t, EngineParams& q, AfterglowEngine&)
        {
            q.pedal = t > 0.1f && t < 6.0f ? 1.0f : 0.0f;
            q.fxType = ((int) (t / 0.8f)) % 4;
            q.fxPos = ((int) (t / 1.3f)) % 3;
        }, out));
    }
    {
        // Hide: dry vanishes while the pedal is down, returns after
        auto p = base; p.killDry = false; p.fsOn = true; p.fsMode = FsHide; p.layers = 3;
        check ("hide", render ("hide", p, holdPedal (0.1f, 3.0f), out));
    }
    {
        // Bypass mid-phrase: layers ring out, dry passes clean
        auto p = base; p.killDry = false; p.layers = 4; p.fxMix = 0.4f;
        check ("bypass-tails", render ("bypass-tails", p, [] (float t, EngineParams& q, AfterglowEngine&)
        {
            q.pedal = t > 0.1f ? 1.0f : 0.0f;
            q.bypass = t > 3.0f;
        }, out));
    }
    { auto p = base; p.layers = 4; check ("bass", render ("bass", p, holdPedal (0.1f, 6.0f), out, 7.0f, 55.0f)); }
    {
        // Loud: ten layers of chords with the dry on must stay under full scale
        auto p = base; p.killDry = false; p.layers = 10; p.decay = 1.0f; p.levelDb = 6.0f;
        check ("loud-wall", render ("loud-wall", p, holdPedal (0.1f, 9.0f), out, 10.0f, 220.0f, 10));
    }

    std::printf (failures == 0 ? "All scenarios passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
