// Offline acceptance smoke for the Vectron processor (no host required).
// Renders the synth with different mod-matrix routings and checks the audio
// actually changes — PRD §12.5: "routing LFO→VectorX and env→cutoff works".
// Exits 0 on success, 1 on failure (wired into ctest).
#include <cmath>
#include <cstdio>
#include "PluginProcessor.h"
#include "params/TrajectoryState.h"

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;
    constexpr int    kNumBlocks  = 140;             // ~1.5 s

    void setParam (VectronProcessor& p, const juce::String& id, float plainValue)
    {
        auto* param = p.apvts.getParameter (id);
        if (param == nullptr)
        {
            std::printf ("FAIL: unknown param '%s'\n", id.toRawUTF8());
            std::exit (1);
        }
        param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
    }

    // Render ~1.5 s of a held middle C into `out` (stereo).
    void render (VectronProcessor& p, juce::AudioBuffer<float>& out)
    {
        p.prepareToPlay (kSampleRate, kBlockSize);
        out.setSize (2, kBlockSize * kNumBlocks);
        out.clear();

        juce::AudioBuffer<float> block (2, kBlockSize);
        for (int b = 0; b < kNumBlocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            block.clear();
            p.processBlock (block, midi);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * kBlockSize, block, ch, 0, kBlockSize);
        }
        p.releaseResources();
    }

    float rmsDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        double acc = 0.0;
        const int n = a.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const double d = (double) a.getSample (0, i) - (double) b.getSample (0, i);
            acc += d * d;
        }
        return (float) std::sqrt (acc / n);
    }

    float rmsDifferenceWindow (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                               int start, int len)
    {
        double acc = 0.0;
        for (int i = start; i < start + len; ++i)
        {
            const double d = (double) a.getSample (0, i) - (double) b.getSample (0, i);
            acc += d * d;
        }
        return (float) std::sqrt (acc / len);
    }

    bool finiteAndAudible (const juce::AudioBuffer<float>& buf, const char* name)
    {
        double acc = 0.0;
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                const float v = buf.getSample (ch, i);
                if (! std::isfinite (v))
                {
                    std::printf ("FAIL: %s has non-finite samples\n", name);
                    return false;
                }
                acc += (double) v * v;
                peak = std::max (peak, std::abs (v));
            }
        const float rms = (float) std::sqrt (acc / (buf.getNumChannels() * buf.getNumSamples()));
        if (rms < 1.0e-4f) { std::printf ("FAIL: %s is silent (rms %g)\n", name, rms); return false; }
        if (peak > 4.0f)   { std::printf ("FAIL: %s peaks at %g\n", name, peak); return false; }
        std::printf ("ok: %s rms=%.4f peak=%.3f\n", name, rms, peak);
        return true;
    }

    bool trajectoryStateHelpersOk()
    {
        const auto tree = vectron::createDefaultTrajectory();
        const auto m = vectron::trajectoryFromState (tree);
        if (m.numPoints != 4)
        { std::printf ("FAIL: default trajectory has %d points\n", m.numPoints); return false; }
        const float ex[4] { -1.0f, 1.0f, 1.0f, -1.0f };
        const float ey[4] {  1.0f, 1.0f, -1.0f, -1.0f };
        for (int i = 0; i < 4; ++i)
            if (m.points[i].x != ex[i] || m.points[i].y != ey[i] || m.points[i].timeMs != 500.0f)
            { std::printf ("FAIL: default trajectory point %d wrong\n", i); return false; }

        // Out-of-range values clamp; missing properties fall back to defaults.
        juce::ValueTree t (vectron::traj_ids::tree);
        juce::ValueTree p (vectron::traj_ids::point);
        p.setProperty (vectron::traj_ids::x, 5.0f, nullptr);
        p.setProperty (vectron::traj_ids::timeMs, 0.0f, nullptr);
        t.appendChild (p, nullptr);
        t.appendChild (juce::ValueTree (vectron::traj_ids::point), nullptr);
        const auto m2 = vectron::trajectoryFromState (t);
        if (m2.numPoints != 2 || m2.points[0].x != 1.0f || m2.points[0].timeMs != 1.0f
            || m2.points[1].timeMs != 500.0f)
        { std::printf ("FAIL: trajectory clamping/defaults wrong\n"); return false; }

        // More than 16 POINT children cap at 16; an invalid tree parses to 0 points.
        juce::ValueTree big (vectron::traj_ids::tree);
        for (int i = 0; i < 20; ++i)
            big.appendChild (juce::ValueTree (vectron::traj_ids::point), nullptr);
        if (vectron::trajectoryFromState (big).numPoints != 16)
        { std::printf ("FAIL: trajectory point cap wrong\n"); return false; }
        if (vectron::trajectoryFromState (juce::ValueTree()).numPoints != 0)
        { std::printf ("FAIL: invalid tree should parse to 0 points\n"); return false; }

        std::printf ("ok: trajectory state helpers\n");
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (! trajectoryStateHelpersOk()) return 1;

    // Baseline: default patch, matrix off.
    VectronProcessor base;
    juce::AudioBuffer<float> baseBuf;
    render (base, baseBuf);
    if (! finiteAndAudible (baseBuf, "baseline")) return 1;

    // Routing 1 (acceptance): LFO1 -> Vector X, full amount, 5 Hz.
    {
        VectronProcessor p;
        setParam (p, "lfo1_rate", 5.0f);
        setParam (p, "mod1_src", 0.0f);            // LFO 1
        setParam (p, "mod1_dst", 0.0f);            // Vector X
        setParam (p, "mod1_amt", 1.0f);
        setParam (p, "mod1_en",  1.0f);
        juce::AudioBuffer<float> buf;
        render (p, buf);
        if (! finiteAndAudible (buf, "lfo->vectorX")) return 1;
        const float diff = rmsDifference (buf, baseBuf);
        std::printf ("lfo->vectorX diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: LFO->VectorX routing has no audible effect\n"); return 1; }
    }

    // Routing 2 (acceptance): Mod Env -> Filter Cutoff, -5 octaves over a slow attack.
    {
        VectronProcessor p;
        setParam (p, "mod_attack", 1.0f);
        setParam (p, "mod2_src", 4.0f);            // Mod Env
        setParam (p, "mod2_dst", 18.0f);           // Filter Cutoff
        setParam (p, "mod2_amt", -1.0f);
        setParam (p, "mod2_en",  1.0f);
        juce::AudioBuffer<float> buf;
        render (p, buf);
        if (! finiteAndAudible (buf, "modEnv->cutoff")) return 1;
        const float diff = rmsDifference (buf, baseBuf);
        std::printf ("modEnv->cutoff diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: ModEnv->Cutoff routing has no audible effect\n"); return 1; }
    }

    // Routing 3: LFO1 -> Pan must decorrelate L/R.
    {
        VectronProcessor p;
        setParam (p, "lfo1_rate", 3.0f);
        setParam (p, "mod1_src", 0.0f);            // LFO 1
        setParam (p, "mod1_dst", 24.0f);           // Pan
        setParam (p, "mod1_amt", 1.0f);
        setParam (p, "mod1_en",  1.0f);
        juce::AudioBuffer<float> buf;
        render (p, buf);
        double lr = 0.0;
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            const double d = (double) buf.getSample (0, i) - (double) buf.getSample (1, i);
            lr += d * d;
        }
        const float lrRms = (float) std::sqrt (lr / buf.getNumSamples());
        std::printf ("pan L/R diff rms = %.5f\n", lrRms);
        if (lrRms < 1.0e-3f) { std::printf ("FAIL: LFO->Pan leaves L == R\n"); return 1; }
    }

    // Trajectory 1 (acceptance, PRD §12.6): Loop mode audibly moves the vector.
    juce::AudioBuffer<float> loopBuf;
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);               // Loop
        render (p, loopBuf);
        if (! finiteAndAudible (loopBuf, "traj loop")) return 1;
        const float diff = rmsDifference (loopBuf, baseBuf);
        std::printf ("traj loop diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: trajectory Loop has no audible effect\n"); return 1; }
    }

    // Trajectory 2: One-Shot settles and holds at Pn == corner C (-1,-1).
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 1.0f);               // One-Shot
        setParam (p, "traj_rate", 4.0f);               // whole path done in ~375 ms
        juce::AudioBuffer<float> oneShot;
        render (p, oneShot);

        VectronProcessor ref;                          // static patch parked at Pn
        setParam (ref, "vector_x", -1.0f);
        setParam (ref, "vector_y", -1.0f);
        juce::AudioBuffer<float> refBuf;
        render (ref, refBuf);

        const int start = (int) kSampleRate;           // 1 s: well past the path + smoothing
        const int len   = oneShot.getNumSamples() - start;
        const float diff = rmsDifferenceWindow (oneShot, refBuf, start, len);
        std::printf ("traj one-shot settle diff rms = %.5f\n", diff);
        if (diff > 1.0e-3f) { std::printf ("FAIL: One-Shot does not settle at Pn\n"); return 1; }
    }

    // Trajectory 3: matrix LFO1 -> Traj Depth fades the trajectory in and out.
    {
        VectronProcessor still;
        setParam (still, "traj_mode", 2.0f);
        setParam (still, "traj_depth", 0.0f);          // parked until modulated
        juce::AudioBuffer<float> stillBuf;
        render (still, stillBuf);

        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);
        setParam (p, "traj_depth", 0.0f);
        setParam (p, "lfo1_rate", 5.0f);
        setParam (p, "mod1_src", 0.0f);                // LFO 1
        setParam (p, "mod1_dst", 25.0f);               // Traj Depth
        setParam (p, "mod1_amt", 1.0f);
        setParam (p, "mod1_en",  1.0f);
        juce::AudioBuffer<float> buf;
        render (p, buf);
        const float diff = rmsDifference (buf, stillBuf);
        std::printf ("lfo->trajDepth diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: LFO->Traj Depth routing has no audible effect\n"); return 1; }
    }

    // Trajectory 4: editing a point in the state is heard (listener -> snapshot path).
    {
        VectronProcessor p;
        setParam (p, "traj_mode", 2.0f);
        auto traj = p.apvts.state.getChildWithName (vectron::traj_ids::tree);
        if (! traj.isValid() || traj.getNumChildren() < 2)
        { std::printf ("FAIL: TRAJECTORY state child missing\n"); return 1; }
        traj.getChild (1).setProperty (vectron::traj_ids::x, -1.0f, nullptr);   // B -> top-left
        juce::AudioBuffer<float> buf;
        render (p, buf);
        const float diff = rmsDifference (buf, loopBuf);
        std::printf ("traj point-edit diff rms = %.5f\n", diff);
        if (diff < 1.0e-3f) { std::printf ("FAIL: editing a trajectory point has no audible effect\n"); return 1; }
    }

    std::printf ("SMOKE OK\n");
    return 0;
}
