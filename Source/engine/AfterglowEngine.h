#pragma once

// Afterglow: a recreation of the Gamechanger Audio PLUS Pedal II.
//
// The input is analysed all the time. When a layer is caught, a short window of
// recent audio becomes a magnitude spectrum, and the layer resynthesises it with
// fresh random phases every frame, so it sustains without a loop point. All active
// layers share one stereo resynthesis, each with its own envelope, darkening Tone
// curve, Resonance colour and place in the stereo field.
//
// Plain C++17, no JUCE, so the render test can run it on its own.

#include "DspUtil.h"
#include "Detector.h"
#include "Effects.h"
#include <array>

namespace ag
{
enum CatchMode { CatchSingle = 0, CatchGroup, CatchAll };
enum HoldMode { HoldNone = 0, HoldFirst, HoldAll };
enum FxPos { FxPre = 0, FxLoop, FxPost };
enum FsMode { FsWet = 0, FsLatch, FsHide };
enum EngineMode { ModeNormal = 0, ModeRealPiano, ModePianoGroup, ModeSostenuto };
enum LayerState { LOff = 0, LRise, LHold, LDecay, LFade, LDamp };

constexpr int kInfinite = 11;      // the LAYERS knob's last stop
constexpr float kPianoZone = 0.22f; // TONE below this, with HOLD NONE, opens the piano modes

struct Times
{
    // RISE: about 80 ms (fastest auto catch) to 5 s. CATCH SINGLE can go faster.
    static float rise (float v, bool single)
    {
        const float lo = single ? 0.015f : 0.08f;
        return lo * std::pow (5.0f / lo, clampf (v, 0.0f, 1.0f));
    }
    // DECAY: 30 ms to about 20 s, then infinite at the very top
    static bool decayInfinite (float v) { return v >= 0.985f; }
    static float decay (float v) { return 0.03f * std::pow (20.0f / 0.03f, clampf (v / 0.985f, 0.0f, 1.0f)); }
    // In the piano modes TONE sets how fast the dampers fall
    static float damper (float tone) { return 0.04f + 0.6f * clampf (tone / kPianoZone, 0.0f, 1.0f); }
};

struct EngineParams
{
    float levelDb = 0.0f, inputDb = 0.0f;
    int layers = 4; // 1..10, kInfinite = endless
    float rise = 0.25f, decay = 0.6f, tone = 0.75f, resonance = 0.0f; // resonance -1..1
    int catchMode = CatchAll, holdMode = HoldAll;
    int fxPos = FxLoop, fxType = FxVerb;
    float fxAmount = 0.5f, fxMix = 0.3f;
    bool killDry = false;
    int fsMode = FsWet;
    bool fsOn = false;
    float pedal = 0.0f; // brass pedal depth 0..1 from the screen, MIDI or automation
    bool bypass = false;
};

inline EngineMode modeOf (const EngineParams& p)
{
    if (p.holdMode != HoldNone || p.tone >= kPianoZone) return ModeNormal;
    return p.catchMode == CatchSingle ? ModeSostenuto : (p.catchMode == CatchGroup ? ModePianoGroup : ModeRealPiano);
}

class AfterglowEngine
{
public:
    static constexpr int kSlots = 10;

    // ---- read by the panel ----
    std::atomic<float> uiLevel[kSlots];
    std::atomic<int> uiState[kSlots]; // 0 off, 1 rising, 2 held, 3 fading
    std::atomic<int> uiActive { 0 }, uiRising { 0 }, uiHeld { 0 }, uiCaptures { 0 }, uiClears { 0 }, uiMode { 0 };
    std::atomic<float> uiInput { 0.0f }, uiPeak { 0.0f }, uiDepth { 0.0f };
    std::atomic<bool> uiDown { false }, uiGroupClosed { false }, uiLatched { false };

    AfterglowEngine()
    {
        for (int i = 0; i < kSlots; ++i) { uiLevel[i] = 0.0f; uiState[i] = 0; }
    }

    void prepare (double sampleRate)
    {
        fs = (float) sampleRate;
        n = fs > 60000.0f ? 8192 : 4096;
        hop = n / 8;
        half = n / 2;
        fft.init (n);
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (kTwoPi * (float) i / (float) n);
        binHz = fs / (float) n;

        hist.assign ((size_t) nextPow2 (n * 4 + (int) (0.2f * fs)), 0.0f);
        histMask = (int64_t) hist.size() - 1;

        auto initLayer = [&] (Layer& l) { l.mag.assign ((size_t) half + 1, 0.0f); l.state = LOff; l.L = 0.0f; };
        for (auto& l : ring) initLayer (l);
        for (auto& l : ghosts) initLayer (l);
        capture.assign ((size_t) half + 1, 0.0f);
        templ.assign ((size_t) half + 1, 0.0f);
        work.assign ((size_t) n, {});
        powL.assign ((size_t) half + 1, 0.0f);
        powR.assign ((size_t) half + 1, 0.0f);
        olaL.assign ((size_t) n, 0.0f);
        olaR.assign ((size_t) n, 0.0f);
        olaRead = hop;

        det.prepare (fs);
        fx.prepare (fs);
        trim.setTime (0.02f, fs);
        wetLevel.setTime (0.03f, fs);
        dryGain.setTime (0.05f, fs);
        fxMixSm.setTime (0.05f, fs);
        trim.primed = wetLevel.primed = dryGain.primed = fxMixSm.primed = false;

        compEnv = 0.0f;
        compGain = 1.0f;
        compAtt = 1.0f - std::exp (-1.0f / (0.03f * fs));
        compRel = 1.0f - std::exp (-1.0f / (0.35f * fs));
        uiCaptures = 0;
        uiClears = 0;
        uiActive = uiRising = uiHeld = 0;

        clock = 0;
        npend = 0;
        physDown = latched = down = false;
        depthSm = 0.0f;
        haveTemplate = false;
        clearState = 0;
        clearGain = 1.0f;
        fxCurType = -1;
        fxFade = 1.0f;
        resTime = 0.0f;
        lastReleaseSec = -10.0;
        lastPressDur = 10.0;
    }

    float sampleRate() const { return fs; }

    // The pedal's double tap, or the CLEAR button
    void requestClear() { clearRequest.store (true); }

    void process (float* L, float* R, int numSamples, const EngineParams& p)
    {
        cur = p;
        if (clearRequest.exchange (false)) startClear();

        mode = modeOf (p);
        handlePedal (p);

        // effect slot: crossfade through silence when the type or position changes
        if (fxCurType < 0) { fxCurType = p.fxType; fxCurPos = p.fxPos; }
        if (p.fxType != fxCurType || p.fxPos != fxCurPos) fxSwitching = true;
        fx.set (fxCurType, p.fxAmount);

        const float trimTarget = dbToGain (p.inputDb);
        const float levelTarget = dbToGain (p.levelDb);
        const bool killed = ! p.bypass && (p.killDry || (p.fsOn && p.fsMode == FsWet));
        const bool hidden = ! p.bypass && p.fsOn && p.fsMode == FsHide && down;
        const float dryTarget = killed || hidden ? 0.0f : 1.0f;
        const float fxStep = 1.0f / (0.02f * fs);
        const int skip = (int) (0.03f * fs);
        float peak = uiPeak.load (std::memory_order_relaxed) * 0.9f;

        for (int i = 0; i < numSamples; ++i)
        {
            // effect crossfade
            if (fxSwitching)
            {
                fxFade -= fxStep;
                if (fxFade <= 0.0f)
                {
                    fxFade = 0.0f;
                    fxCurType = p.fxType;
                    fxCurPos = p.fxPos;
                    fx.clear();
                    fx.set (fxCurType, p.fxAmount);
                    fxSwitching = false;
                }
            }
            else if (fxFade < 1.0f)
            {
                fxFade = std::min (1.0f, fxFade + fxStep);
            }
            const float mix = fxMixSm.next (p.fxMix) * fxFade;

            const Stereo raw { L[i], R[i] };
            const float g = trim.next (trimTarget);
            Stereo x { raw.l * g, raw.r * g };

            if (fxCurPos == FxPre && ! p.bypass)
                x = mixFx (x, mix);

            // analysis runs all the time, like the pedal
            const float mono = 0.5f * (x.l + x.r);
            hist[(size_t) (clock & histMask)] = mono;
            ++clock;

            const float onset = det.process (mono);
            if (onset > 0.0f && down && ! p.bypass && clearState == 0)
                onOnset (skip);

            while (npend > 0 && clock >= pend[0].at)
                runCapture();

            // resynthesis
            if (olaRead >= hop)
            {
                std::copy (olaL.begin() + hop, olaL.end(), olaL.begin());
                std::copy (olaR.begin() + hop, olaR.end(), olaR.begin());
                std::fill (olaL.end() - hop, olaL.end(), 0.0f);
                std::fill (olaR.end() - hop, olaR.end(), 0.0f);
                frameUpdate ((float) hop / fs);
                synthFrame();
                olaRead = 0;
            }
            Stereo wet { olaL[(size_t) olaRead], olaR[(size_t) olaRead] };
            ++olaRead;

            if (clearState == 1)
            {
                clearGain -= 1.0f / (0.025f * fs);
                if (clearGain <= 0.0f)
                {
                    resetLayers();
                    clearGain = 1.0f;
                    clearState = 0;
                    wet = {};
                }
            }
            wet = wet * (clearGain * wetLevel.next (levelTarget));

            if (fxCurPos == FxLoop)
                wet = mixFx (wet, mix);
            wet = wet * leveller (wet);

            const Stereo drySrc = p.bypass ? raw : x;
            const float dg = dryGain.next (dryTarget);
            Stereo out { drySrc.l * dg + wet.l, drySrc.r * dg + wet.r };

            if (fxCurPos == FxPost)
                out = p.bypass ? drySrc + mixFx (wet, mix) : mixFx (out, mix);

            out.l = softClip (out.l);
            out.r = softClip (out.r);
            if (! std::isfinite (out.l) || ! std::isfinite (out.r)) { out = {}; resetLayers(); fx.clear(); }
            L[i] = out.l;
            R[i] = out.r;
            peak = std::max (peak, std::max (std::fabs (out.l), std::fabs (out.r)));
        }

        uiPeak.store (peak, std::memory_order_relaxed);
        uiInput.store (det.level(), std::memory_order_relaxed);
        uiDepth.store (down ? (latched ? 1.0f : p.pedal) : 0.0f, std::memory_order_relaxed);
        uiDown.store (down, std::memory_order_relaxed);
        uiLatched.store (latched, std::memory_order_relaxed);
        uiMode.store ((int) mode, std::memory_order_relaxed);
        uiGroupClosed.store (groupClosed, std::memory_order_relaxed);
    }

private:
    struct Layer
    {
        std::vector<float> mag;
        int state = LOff;
        float L = 0.0f, startL = 0.0f, prog = 0.0f, cap = 1.0f, age = 0.0f, pan = 0.0f;
        float fadeFrom = 0.0f, fadeTime = 0.05f, seed = 0.0f;
        bool holdable = true;
        uint32_t order = 0;
    };

    enum CaptureKind { CapNormal = 0, CapTemplate, CapRetrigger };
    struct Pending { int64_t at = 0, start = 0; int kind = CapNormal; bool first = false; bool quiet = false; };

    // ---------------------------------------------------------------------------
    // Pedal: press, release, latch and double tap
    // ---------------------------------------------------------------------------
    double nowSec() const { return (double) clock / (double) fs; }

    void handlePedal (const EngineParams& p)
    {
        const bool wantLatch = p.fsOn && p.fsMode == FsLatch && ! p.bypass;
        const bool phys = ! p.bypass && p.pedal > (physDown ? 0.03f : 0.06f);

        if (phys && ! physDown)
        {
            physDown = true;
            onPress();
        }
        else if (! phys && physDown)
        {
            physDown = false;
            if (! latched) onRelease();
        }

        if (wantLatch && ! latched)
        {
            latched = true;
            if (! physDown) onPress();
        }
        else if (! wantLatch && latched)
        {
            latched = false;
            if (! physDown) onRelease();
        }

        down = physDown || latched;
        depth = latched ? 1.0f : clampf (p.pedal, 0.0f, 1.0f);
    }

    void onPress()
    {
        const double t = nowSec();
        const bool doubleTap = t - lastReleaseSec < 0.32 && lastPressDur < 0.4;
        pressStartSec = t;
        scheduledThisPress = 0;
        groupClosed = false;
        down = true;

        if (doubleTap)
        {
            startClear();
            lastReleaseSec = -10.0;
            return;
        }

        if (mode == ModeNormal && cur.holdMode == HoldAll)
        {
            // re-pressing during the fade holds every layer where it is
            for (auto& l : ring)
                if (l.state == LDecay)
                {
                    l.state = LHold;
                    l.cap = l.L;
                    l.holdable = true;
                }
        }

        if (mode == ModeSostenuto)
            schedule (clock, clock - n, CapTemplate, true, true);
        else if (cur.catchMode == CatchSingle)
            schedule (clock, clock - n, CapNormal, true, true);
        else if (det.sounding())
            schedule (clock, clock - n, CapNormal, true, false); // let what's ringing now carry on
    }

    void onRelease()
    {
        down = false;
        lastReleaseSec = nowSec();
        lastPressDur = lastReleaseSec - pressStartSec;

        if (mode != ModeNormal)
        {
            // dampers fall back onto the strings
            for (auto* set : { &ring, &ghosts })
                for (auto& l : *set)
                    if (l.state != LOff) l.state = LDamp;
            npend = 0;
            haveTemplate = false;
            return;
        }

        for (auto& l : ring)
            if (l.state == LRise || l.state == LHold)
            {
                l.state = LDecay;
                l.startL = l.L;
            }
    }

    void onOnset (int skip)
    {
        if (mode == ModeNormal && cur.catchMode == CatchSingle) return;
        if (mode == ModeSostenuto && ! haveTemplate) return;
        if (groupClosed) return;
        const bool retrigger = mode == ModeSostenuto;
        schedule (clock + skip + n, clock + skip, retrigger ? CapRetrigger : CapNormal, scheduledThisPress == 0, false);
    }

    void schedule (int64_t at, int64_t start, int kind, bool first, bool quiet)
    {
        if (npend >= (int) pend.size()) return;
        if (start < 0) start = 0;
        pend[(size_t) npend++] = { at, start, kind, first, quiet };
        ++scheduledThisPress;
        const bool grouping = cur.catchMode == CatchGroup && cur.layers != kInfinite && kind != CapTemplate;
        if (grouping && scheduledThisPress >= cur.layers) groupClosed = true;
    }

    // ---------------------------------------------------------------------------
    // Catching a layer
    // ---------------------------------------------------------------------------
    void runCapture()
    {
        const Pending pc = pend[0];
        for (int i = 1; i < npend; ++i) pend[(size_t) i - 1] = pend[(size_t) i];
        --npend;

        double energy = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float v = hist[(size_t) ((pc.start + i) & histMask)];
            energy += (double) v * v;
            work[(size_t) i] = { v * window[(size_t) i], 0.0f };
        }
        const float rms = (float) std::sqrt (energy / n);
        if (rms < (pc.quiet ? 2.0e-6f : 2.5e-4f)) return; // nothing there to hold

        fft.run (work.data(), false);
        const int lo = std::max (1, (int) (25.0f / binHz)), hi = std::min (half - 1, (int) (19000.0f / binHz));
        double before = 0.0, after = 0.0;
        for (int k = 0; k <= half; ++k)
        {
            const float m = (k >= lo && k <= hi) ? std::abs (work[(size_t) k]) : 0.0f;
            before += (double) m * m;
            capture[(size_t) k] = std::pow (m, 1.75f); // sharpen the partials so held notes stay pure
            after += (double) capture[(size_t) k] * capture[(size_t) k];
        }
        if (after <= 0.0) return;
        const float norm = (float) std::sqrt (before / after);
        for (auto& m : capture) m *= norm;

        if (pc.kind == CapTemplate)
        {
            std::copy (capture.begin(), capture.end(), templ.begin());
            haveTemplate = true;
        }
        else if (pc.kind == CapRetrigger)
        {
            if (! haveTemplate || ! down) return;
            // only what the new note shares with the held chord rings again
            double shared = 0.0, total = 0.0;
            for (int k = 0; k <= half; ++k)
            {
                const float m = std::sqrt (capture[(size_t) k] * templ[(size_t) k]) * 1.5f;
                total += (double) capture[(size_t) k] * capture[(size_t) k];
                capture[(size_t) k] = m;
                shared += (double) m * m;
            }
            if (shared < 0.02 * total) return;
        }

        addLayer (pc.first);
    }

    int maxLayers() const { return cur.layers >= kInfinite ? kSlots : std::max (1, cur.layers); }

    int activeCount() const
    {
        int c = 0;
        for (const auto& l : ring) c += l.state != LOff ? 1 : 0;
        return c;
    }

    int oldest (int skipIndex = -1) const
    {
        int best = -1;
        for (int i = 0; i < kSlots; ++i)
            if (ring[(size_t) i].state != LOff && i != skipIndex && (best < 0 || ring[(size_t) i].order < ring[(size_t) best].order))
                best = i;
        return best;
    }

    // An old layer gives up its slot and fades out on the side
    void retire (int i, float time)
    {
        if (i < 0) return;
        auto& src = ring[(size_t) i];
        int g = -1;
        float quietest = 1.0e9f;
        for (int k = 0; k < kSlots; ++k)
        {
            if (ghosts[(size_t) k].state == LOff) { g = k; break; }
            if (ghosts[(size_t) k].L < quietest) { quietest = ghosts[(size_t) k].L; g = k; }
        }
        auto& dst = ghosts[(size_t) g];
        std::copy (src.mag.begin(), src.mag.end(), dst.mag.begin());
        dst.state = LFade;
        dst.L = dst.fadeFrom = src.L;
        dst.fadeTime = std::max (0.03f, time);
        dst.age = src.age;
        dst.pan = src.pan;
        dst.seed = src.seed;
        src.state = LOff;
        src.L = 0.0f;
    }

    // Infinite mode: the two oldest layers fold into one, nothing is lost
    void mergeOldest()
    {
        const int a = oldest();
        const int b = oldest (a);
        if (a < 0 || b < 0) return;
        auto& A = ring[(size_t) a];
        auto& B = ring[(size_t) b];
        const float wa = std::max (A.L, 0.05f), wb = std::max (B.L, 0.05f);
        const float out = std::max (wa, wb);
        for (size_t k = 0; k < A.mag.size(); ++k)
        {
            const float x = A.mag[k] * wa, y = B.mag[k] * wb;
            A.mag[k] = std::sqrt (x * x + y * y) / out;
        }
        A.L = std::max (A.L, B.L);
        A.cap = std::max (A.cap, B.cap);
        if (B.state == LHold || A.state == LHold) { A.state = LHold; A.holdable = true; }
        A.age = std::min (A.age, B.age);
        B.state = LOff;
        B.L = 0.0f;
    }

    void addLayer (bool firstOfPress)
    {
        const bool single = cur.catchMode == CatchSingle;
        const float riseSec = Times::rise (cur.rise, single);

        for (int guard = 0; guard < kSlots && activeCount() >= maxLayers(); ++guard)
        {
            if (cur.layers >= kInfinite) mergeOldest();
            else retire (oldest(), riseSec); // the replaced layer crossfades out over RISE
        }

        int slot = 0;
        while (slot < kSlots && ring[(size_t) slot].state != LOff) ++slot;
        if (slot >= kSlots) return;

        if (mode == ModeNormal && cur.holdMode == HoldFirst && single)
        {
            // CATCH SINGLE + HOLD FIRST: the newest layer is the held one
            for (auto& l : ring)
                if (l.state != LOff && l.holdable)
                {
                    l.holdable = false;
                    if (l.state == LHold) { l.state = LDecay; l.startL = l.L; }
                }
        }

        auto& ly = ring[(size_t) slot];
        std::copy (capture.begin(), capture.end(), ly.mag.begin());
        ly.state = LRise;
        ly.L = ly.startL = 0.0f;
        ly.prog = 0.0f;
        ly.cap = 1.0f;
        ly.age = 0.0f;
        ly.order = ++orderCounter;
        ly.seed = rng.uni();
        ly.pan = 0.6f * std::sin ((float) ly.order * 2.39996f); // golden angle: layers spread out
        if (mode != ModeNormal) ly.holdable = false;
        else if (cur.holdMode == HoldAll) ly.holdable = true;
        else if (cur.holdMode == HoldNone) ly.holdable = false;
        else ly.holdable = single ? true : firstOfPress;

        uiCaptures.fetch_add (1, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------------------
    // Envelopes, once per resynthesis frame
    // ---------------------------------------------------------------------------
    static float pedalGain (float d) { return std::pow (clampf ((d - 0.03f) / 0.97f, 0.0f, 1.0f), 0.8f); }

    void frameUpdate (float dt)
    {
        const bool single = cur.catchMode == CatchSingle;
        const float riseSec = Times::rise (cur.rise, single);
        const bool inf = Times::decayInfinite (cur.decay) && mode == ModeNormal;
        const float decaySec = Times::decay (cur.decay);
        const float decayMul = inf ? 1.0f : std::pow (0.001f, dt / decaySec);
        const float dampMul = std::pow (0.001f, dt / Times::damper (cur.tone));

        // the pedal's depth sets the held level: RISE smooths pushing in, DECAY easing off
        if (down)
        {
            const float target = pedalGain (depth);
            const float tau = target > depthSm ? std::max (0.02f, riseSec * 0.4f) : clampf (decaySec * 0.3f, 0.02f, 2.0f);
            depthSm += (target - depthSm) * (1.0f - std::exp (-dt / tau));
        }

        // LAYERS turned down: the oldest extras bow out
        for (int guard = 0; guard < kSlots && activeCount() > maxLayers(); ++guard)
            retire (oldest(), std::max (0.08f, riseSec));

        int active = 0, rising = 0, held = 0;
        for (int i = 0; i < kSlots; ++i)
        {
            auto& l = ring[(size_t) i];
            step (l, dt, riseSec, decayMul, dampMul);
            if (l.state != LOff) ++active;
            if (l.state == LRise) ++rising;
            if (l.state == LHold) ++held;
            uiLevel[i].store (l.L, std::memory_order_relaxed);
            uiState[i].store (l.state == LOff ? 0 : (l.state == LRise ? 1 : (l.state == LHold ? 2 : 3)), std::memory_order_relaxed);
        }
        for (auto& l : ghosts) step (l, dt, riseSec, decayMul, dampMul);

        uiActive.store (active, std::memory_order_relaxed);
        uiRising.store (rising, std::memory_order_relaxed);
        uiHeld.store (held, std::memory_order_relaxed);
        resTime += dt;
    }

    void step (Layer& l, float dt, float riseSec, float decayMul, float dampMul)
    {
        switch (l.state)
        {
            case LRise:
            {
                // a note caught just as the pedal lifted still blooms, then fades
                const float target = (depthSm > 0.05f ? depthSm : 1.0f) * l.cap;
                l.prog += dt / riseSec;
                const float s = clampf (l.prog, 0.0f, 1.0f);
                l.L = l.startL + (target - l.startL) * (s * s * (3.0f - 2.0f * s));
                if (l.prog >= 1.0f)
                {
                    const bool hold = l.holdable && down && mode == ModeNormal;
                    l.state = hold ? LHold : LDecay;
                }
                break;
            }
            case LHold:
                if (! down) { l.state = LDecay; break; }
                l.L += (depthSm * l.cap - l.L) * std::min (1.0f, dt / 0.02f);
                break;
            case LDecay: l.L *= decayMul; break;
            case LDamp:  l.L *= dampMul; break;
            case LFade:  l.L -= l.fadeFrom * dt / l.fadeTime; break;
            default: break;
        }
        if (l.state >= LDecay && l.L < 1.0e-4f)
        {
            l.state = LOff;
            l.L = 0.0f;
        }
        l.age += dt;
    }

    // ---------------------------------------------------------------------------
    // Resynthesis
    // ---------------------------------------------------------------------------
    void addLayerPower (const Layer& l, float resUp, float resDown, bool filter, float fmin, float speed)
    {
        const float a = (l.pan + 1.0f) * (kPi * 0.25f);
        const float gl = std::cos (a) * 1.41421356f * l.L, gr = std::sin (a) * 1.41421356f * l.L;

        // Dual resonator: upper or lower harmonic relations of the layer itself, gently drifting
        float w[3] = { 0, 0, 0 };
        float ratio[3] = { 1, 1, 1 };
        const bool up = resUp > 0.001f, dn = resDown > 0.001f;
        if (up || dn)
        {
            const float r = up ? resUp : resDown;
            const float base[3] = { up ? 0.55f : 0.65f, up ? 0.40f : 0.45f, up ? 0.30f : 0.35f };
            const float upR[3] = { 2.0f, 3.0f, 4.0f }, dnR[3] = { 0.5f, 1.0f / 3.0f, 0.25f };
            for (int j = 0; j < 3; ++j)
            {
                const float amt = clampf (r * 2.0f - 0.6f * (float) j, 0.0f, 1.0f);
                const float drift = 0.7f + 0.3f * std::sin (kTwoPi * (0.07f * (float) (j + 1) * resTime + l.seed) + (float) j);
                w[j] = amt * base[j] * drift;
                ratio[j] = up ? upR[j] : dnR[j];
            }
        }
        const float resNorm = 1.0f / std::sqrt (1.0f + w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        const float fc = fmin + (20000.0f - fmin) * std::exp (-l.age * speed);
        const float invFc = 1.0f / fc;

        for (int k = 1; k < half; ++k)
        {
            float e = l.mag[(size_t) k];
            if (w[0] > 0.0f)
            {
                for (int j = 0; j < 3; ++j)
                {
                    if (w[j] <= 0.0f) continue;
                    const float src = (float) k / ratio[j];
                    const int i0 = (int) src;
                    if (i0 + 1 >= half) continue;
                    const float fr = src - (float) i0;
                    e += w[j] * lerpf (l.mag[(size_t) i0], l.mag[(size_t) i0 + 1], fr);
                }
                e *= resNorm;
            }
            if (filter)
            {
                const float x = (float) k * binHz * invFc;
                e /= std::sqrt (1.0f + x * x * x * x);
            }
            powL[(size_t) k] += e * e * gl * gl;
            powR[(size_t) k] += e * e * gr * gr;
        }
    }

    void synthFrame()
    {
        std::fill (powL.begin(), powL.end(), 0.0f);
        std::fill (powR.begin(), powR.end(), 0.0f);

        // TONE: each layer darkens as it ages; the piano modes always darken fully
        const float toneEff = mode == ModeNormal ? cur.tone : 0.0f;
        const bool filter = toneEff < 0.995f;
        const float dark = 1.0f - toneEff;
        const float fmin = 20000.0f * std::pow (250.0f / 20000.0f, dark);
        const float speed = 0.25f + 2.0f * dark;
        const float res = clampf (cur.resonance, -1.0f, 1.0f);
        const float resUp = std::max (0.0f, res), resDown = std::max (0.0f, -res);

        bool any = false;
        for (const auto* set : { &ring, &ghosts })
            for (const auto& l : *set)
                if (l.state != LOff && l.L > 1.0e-5f)
                {
                    addLayerPower (l, resUp, resDown, filter, fmin, speed);
                    any = true;
                }
        if (! any) return;

        // stereo: left and right get related but not identical phases
        constexpr float kGain = 0.943f; // random-phase overlap-add at 8x overlap
        for (int side = 0; side < 2; ++side)
        {
            const auto& pw = side == 0 ? powL : powR;
            work[0] = {};
            work[(size_t) half] = {};
            for (int k = 1; k < half; ++k)
            {
                const float ph = side == 0 ? kPi * rng.next() : phaseL[(size_t) k] + 0.55f * kPi * rng.next();
                if (side == 0) phaseL[(size_t) k] = ph;
                const auto c = std::polar (std::sqrt (pw[(size_t) k]) * kGain, ph);
                work[(size_t) k] = c;
                work[(size_t) (n - k)] = std::conj (c);
            }
            fft.run (work.data(), true);
            auto& ola = side == 0 ? olaL : olaR;
            for (int i = 0; i < n; ++i)
                ola[(size_t) i] += work[(size_t) i].real() * window[(size_t) i];
        }
    }

    // Stacked layers add up. A soft-knee leveller on the layers keeps a wall of ten
    // under full scale while a single layer passes untouched.
    float leveller (Stereo w)
    {
        const float pw = 0.5f * (w.l * w.l + w.r * w.r);
        compEnv += (pw - compEnv) * (pw > compEnv ? compAtt : compRel);
        const float over = 10.0f * std::log10 (compEnv + 1.0e-12f) + 12.0f; // threshold -12 dBFS RMS
        constexpr float slope = 1.0f - 1.0f / 3.0f;                          // 3:1
        float gainDb = 0.0f;
        if (over > 3.0f) gainDb = -slope * over;
        else if (over > -3.0f) gainDb = -slope * (over + 3.0f) * (over + 3.0f) / 12.0f;
        compGain += (dbToGain (gainDb) - compGain) * 0.002f;
        return compGain;
    }

    Stereo mixFx (Stereo in, float mix)
    {
        if (mix <= 0.0001f && ! fxSwitching) { fx.process (in); return in; } // keep tails warm
        const Stereo y = fx.process (in);
        return { in.l * (1.0f - mix) + y.l * mix, in.r * (1.0f - mix) + y.r * mix };
    }

    void startClear()
    {
        clearState = 1;
        npend = 0;
        haveTemplate = false;
        uiClears.fetch_add (1, std::memory_order_relaxed);
    }

    void resetLayers()
    {
        for (auto& l : ring) { l.state = LOff; l.L = 0.0f; }
        for (auto& l : ghosts) { l.state = LOff; l.L = 0.0f; }
        std::fill (olaL.begin(), olaL.end(), 0.0f);
        std::fill (olaR.begin(), olaR.end(), 0.0f);
        npend = 0;
    }

    // ---- state ----
    float fs = 48000.0f, binHz = 11.7f;
    int n = 4096, hop = 512, half = 2048;
    Fft fft;
    std::vector<float> window;
    std::vector<float> hist;
    int64_t histMask = 0, clock = 0;
    Detector det;
    FxSlot fx;

    std::array<Layer, kSlots> ring, ghosts;
    std::vector<float> capture, templ, powL, powR;
    std::array<float, 8193> phaseL {};
    std::vector<std::complex<float>> work;
    std::vector<float> olaL, olaR;
    int olaRead = 0;

    std::array<Pending, 12> pend;
    int npend = 0, scheduledThisPress = 0;
    uint32_t orderCounter = 0;
    Rng rng;

    EngineParams cur;
    EngineMode mode = ModeNormal;
    bool physDown = false, latched = false, down = false, groupClosed = false, haveTemplate = false;
    float depth = 0.0f, depthSm = 0.0f, resTime = 0.0f;
    double pressStartSec = 0.0, lastReleaseSec = -10.0, lastPressDur = 10.0;

    std::atomic<bool> clearRequest { false };
    int clearState = 0;
    float clearGain = 1.0f;

    Smoother trim, wetLevel, dryGain, fxMixSm;
    float compEnv = 0.0f, compGain = 1.0f, compAtt = 0.001f, compRel = 0.0001f;
    int fxCurType = -1, fxCurPos = FxLoop;
    float fxFade = 1.0f;
    bool fxSwitching = false;
};
} // namespace ag
