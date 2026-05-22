#pragma once

// Applies color temperature via the GPU gamma ramp. Port of
// win/src/KelvinShift/Services/GammaService.cs.
//
// Write strategy: stable ramp (no rotating offset) + read-back check before
// each watchdog reapply. The watchdog only writes when the GPU LUT actually
// deviates — every unnecessary SetDeviceGammaRamp call is itself visible as
// flicker, so suppressing them keeps the display calm.
//
// Color math: 91-entry Redshift blackbody table (CIE, Ingo Thies 2013).
// D65 at 6500K. Identical to the macOS port.

#include <windows.h>
#include <mutex>

class GammaService
{
public:
    GammaService() = default;

    bool ApplyKelvin(int kelvin) { return ApplyKelvinWithBrightness(kelvin, 1.0); }
    bool ApplyKelvinWithBrightness(int kelvin, double brightness);

    // Re-write the ramp ONLY if the current GPU LUT drifted from what we
    // expect. Called by the watchdog on every shell event.
    bool Reapply();

    void Reset();

    int CurrentKelvin() const { return currentKelvin_; }
    double CurrentBrightness() const { return currentBrightness_; }

    struct Rgb { float r, g, b; };
    static Rgb KelvinToRgb(int kelvin);

private:
    void BuildRamp(float r, float g, float b);
    bool WriteRamp();
    bool GammaMatchesExpected();

    WORD ramp_[768] = {};        // R[0..255], G[256..511], B[512..767]
    WORD probeBuffer_[768] = {};
    int currentKelvin_ = 6500;
    double currentBrightness_ = 1.0;
    bool hasApplied_ = false;
    bool loggedFirstSuccess_ = false;
    std::mutex writeLock_;
};
