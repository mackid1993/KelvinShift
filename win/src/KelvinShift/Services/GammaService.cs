using System;
using System.IO;
using System.Runtime.InteropServices;

namespace KelvinShift.Services;

// Applies color temperature via the gamma ramp at the GPU output stage.
// Cursor (hardware overlay) and screenshots (pre-compositor BitBlt) both
// see the warm shift, so cursor stays correctly tinted and captures
// remain clean regardless of which screenshot tool / hotkey is used.
//
// Write strategy: stable ramp (no rotating offset) + read-back check
// before each watchdog reapply. The watchdog only writes when the GPU
// LUT actually deviates from what we want — this is what keeps the
// display calm. Every unnecessary SetDeviceGammaRamp call disturbs the
// GPU LUT and is itself visible as flicker; suppressing them eliminates
// the self-inflicted flicker that the event-driven approach would
// otherwise produce.
//
// Color math: 91-entry Redshift blackbody table (CIE matching, Ingo
// Thies 2013). D65 at 6500K. Identical to macOS port.
public sealed class GammaService : IDisposable
{
    private readonly ushort[] _ramp = new ushort[768]; // R[0..255], G[256..511], B[512..767]
    private readonly ushort[] _probeBuffer = new ushort[768];

    private int _currentKelvin = 6500;
    private double _currentBrightness = 1.0;
    private bool _hasApplied;

    public int CurrentKelvin => _currentKelvin;
    public double CurrentBrightness => _currentBrightness;

    private static readonly string LogPath = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "KelvinShift", "debug.log");
    private const long MaxLogBytes = 256 * 1024;
    private bool _loggedFirstSuccess;

    private static void Log(string msg)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            var fi = new FileInfo(LogPath);
            if (fi.Exists && fi.Length > MaxLogBytes)
            {
                var rotated = LogPath + ".old";
                try { File.Delete(rotated); } catch { }
                try { File.Move(LogPath, rotated); } catch { }
            }
            File.AppendAllText(LogPath, $"{DateTime.Now:HH:mm:ss.fff} [gamma] {msg}\n");
        }
        catch { }
    }

    public bool ApplyKelvin(int kelvin) => ApplyKelvinWithBrightness(kelvin, 1.0);

    private readonly object _writeLock = new();

    public bool ApplyKelvinWithBrightness(int kelvin, double brightness)
    {
        var k = Math.Clamp(kelvin, 1000, 10000);
        var b = Math.Clamp(brightness, 0.1, 1.0);
        if (_hasApplied && k == _currentKelvin && Math.Abs(b - _currentBrightness) < 0.001)
            return true;
        _currentKelvin = k;
        _currentBrightness = b;
        var rgb = KelvinToRgb(k);
        lock (_writeLock)
        {
            BuildRamp(rgb.R * (float)b, rgb.G * (float)b, rgb.B * (float)b);
            WriteRamp();
        }
        _hasApplied = true;
        return true;
    }

    /// <summary>Re-write the gamma ramp ONLY if the current GPU LUT has
    /// drifted from what we expect. The watchdog fires this on every
    /// shell event; without the read-back, we'd spam SetDeviceGammaRamp
    /// hundreds of times per minute even when Windows hasn't touched the
    /// LUT — and each unnecessary write is itself visible as flicker.</summary>
    public bool Reapply()
    {
        if (!_hasApplied) return true;
        lock (_writeLock)
        {
            if (!GammaMatchesExpected())
            {
                WriteRamp();
            }
            return true;
        }
    }

    private bool GammaMatchesExpected()
    {
        var hdc = GetDC(IntPtr.Zero);
        if (hdc == IntPtr.Zero) return true;  // assume OK rather than spam writes
        var pin = GCHandle.Alloc(_probeBuffer, GCHandleType.Pinned);
        try
        {
            if (!GetDeviceGammaRamp(hdc, pin.AddrOfPinnedObject())) return true;
        }
        finally
        {
            pin.Free();
            ReleaseDC(IntPtr.Zero, hdc);
        }
        // Sample mid-points of each channel — covers the case where Windows
        // resets to identity (mid = 0x8000) while still cheap to check.
        // Tolerance ±256 / 65535 absorbs floating-point quantization in our
        // ramp build, so the threshold only trips on real resets.
        const int tol = 256;
        return Math.Abs(_probeBuffer[128] - _ramp[128]) <= tol
            && Math.Abs(_probeBuffer[256 + 128] - _ramp[256 + 128]) <= tol
            && Math.Abs(_probeBuffer[512 + 128] - _ramp[512 + 128]) <= tol;
    }

    public void InvalidateAdapters() { /* fresh HDC per write — nothing to invalidate */ }

    public void Reset()
    {
        _currentKelvin = 6500;
        _currentBrightness = 1.0;
        _hasApplied = false;
        lock (_writeLock)
        {
            BuildRamp(1f, 1f, 1f);
            WriteRamp();
        }
    }

    public void OnSystemColorPipelineToggled() { /* single path — toggle is a no-op */ }

    public void Dispose() { }

    // ── Ramp construction ─────────────────────────────────

    private void BuildRamp(float r, float g, float b)
    {
        // Stable ramp — no per-write perturbation. Driver-side caching of
        // identical ramps used to be a problem the rotating offset solved,
        // but the read-back check in Reapply already gates writes to only
        // when the LUT has actually deviated, so identical successive
        // writes never happen at our layer.
        for (var i = 0; i < 256; i++)
        {
            var t = i / 255f;
            _ramp[i]       = (ushort)Math.Clamp((int)(t * r * 65535f), 0, 65535);
            _ramp[i + 256] = (ushort)Math.Clamp((int)(t * g * 65535f), 0, 65535);
            _ramp[i + 512] = (ushort)Math.Clamp((int)(t * b * 65535f), 0, 65535);
        }
    }

    // Primary write path: mscms!InternalSetDeviceGammaRamp.
    // SetDeviceGammaRamp goes through Windows' application-calibration
    // tracking, which the kernel resets on shell-flyout open (Action Center,
    // Settings, UAC) — that's the visible flash. The Internal* variant
    // writes the gamma at a layer Windows doesn't touch on those events,
    // and bypasses the GdiIcmGammaRange registry cap so warm temperatures
    // work without the install-time HKLM tweak.
    //
    // Three-arg signature (hdc, ramp, dwReserved=0).
    //
    // Resolved from mscms.dll at startup; falls back to the public API
    // when the symbol isn't exported (older Windows, Wine, etc.).
    [UnmanagedFunctionPointer(CallingConvention.StdCall, SetLastError = true)]
    private delegate bool InternalSetGammaDelegate(IntPtr hDC, IntPtr lpRamp, uint dwReserved);
    private static readonly InternalSetGammaDelegate? _internalSetGamma = LoadInternalSetGamma();

    private static InternalSetGammaDelegate? LoadInternalSetGamma()
    {
        // mscms.dll is where this symbol lives — gdi32 doesn't export it,
        // even though there's a similarly-named D3DKMT family there.
        foreach (var dll in new[] { "mscms.dll", "gdi32.dll" })
        {
            try
            {
                var h = LoadLibraryW(dll);
                if (h == IntPtr.Zero) continue;
                var p = GetProcAddress(h, "InternalSetDeviceGammaRamp");
                if (p == IntPtr.Zero) continue;
                Log($"resolved InternalSetDeviceGammaRamp from {dll}");
                return Marshal.GetDelegateForFunctionPointer<InternalSetGammaDelegate>(p);
            }
            catch { }
        }
        return null;
    }

    private bool WriteRamp()
    {
        var hdc = GetDC(IntPtr.Zero);
        if (hdc == IntPtr.Zero)
        {
            Log("GetDC(NULL) returned 0");
            return false;
        }

        var pin = GCHandle.Alloc(_ramp, GCHandleType.Pinned);
        try
        {
            var ptr = pin.AddrOfPinnedObject();
            // Retry loop — some drivers fail the first call but succeed
            // on the second. Prefer the kernel path; fall back to the
            // documented one if Internal* isn't there.
            var ok = false;
            for (var attempt = 0; attempt < 3 && !ok; attempt++)
            {
                ok = _internalSetGamma is not null
                    ? _internalSetGamma(hdc, ptr, 0)
                    : SetDeviceGammaRamp(hdc, ptr);
            }

            if (!ok)
            {
                var err = Marshal.GetLastWin32Error();
                Log($"gamma write failed (Win32 err={err}, path={(_internalSetGamma is not null ? "internal" : "public")})");
            }
            else if (!_loggedFirstSuccess)
            {
                var path = _internalSetGamma is not null ? "InternalSetDeviceGammaRamp" : "SetDeviceGammaRamp";
                Log($"{path} OK rampSample=[R0={_ramp[0]} R128={_ramp[128]} R255={_ramp[255]} G128={_ramp[256+128]} B128={_ramp[512+128]}]");
                _loggedFirstSuccess = true;
            }
            return ok;
        }
        finally
        {
            pin.Free();
            ReleaseDC(IntPtr.Zero, hdc);
        }
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr LoadLibraryW(string lpFileName);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true)]
    private static extern IntPtr GetProcAddress(IntPtr hModule, [MarshalAs(UnmanagedType.LPStr)] string lpProcName);

    // ── Blackbody table (Redshift / CIE, Ingo Thies 2013) ─

    public static (float R, float G, float B) KelvinToRgb(int kelvin)
    {
        var k = Math.Clamp(kelvin, 1000, 10000);
        var index = (k - 1000) / 100;
        var alpha = (float)((k - 1000) % 100) / 100f;

        if (index >= BlackbodyTable.Length - 1)
            return BlackbodyTable[^1];

        var lo = BlackbodyTable[index];
        var hi = BlackbodyTable[index + 1];
        return (
            lo.R + alpha * (hi.R - lo.R),
            lo.G + alpha * (hi.G - lo.G),
            lo.B + alpha * (hi.B - lo.B)
        );
    }

    private static readonly (float R, float G, float B)[] BlackbodyTable = new (float, float, float)[]
    {
        (1.00000000f, 0.18172716f, 0.00000000f), // 1000K
        (1.00000000f, 0.25503671f, 0.00000000f), // 1100K
        (1.00000000f, 0.30942099f, 0.00000000f), // 1200K
        (1.00000000f, 0.35357379f, 0.00000000f), // 1300K
        (1.00000000f, 0.39091524f, 0.00000000f), // 1400K
        (1.00000000f, 0.42322816f, 0.00000000f), // 1500K
        (1.00000000f, 0.45159884f, 0.00000000f), // 1600K
        (1.00000000f, 0.47675916f, 0.00000000f), // 1700K
        (1.00000000f, 0.49923747f, 0.00000000f), // 1800K
        (1.00000000f, 0.51943421f, 0.00000000f), // 1900K
        (1.00000000f, 0.54360078f, 0.08679949f), // 2000K
        (1.00000000f, 0.56618736f, 0.14065513f), // 2100K
        (1.00000000f, 0.58734976f, 0.18362641f), // 2200K
        (1.00000000f, 0.60724493f, 0.22137978f), // 2300K
        (1.00000000f, 0.62600248f, 0.25591950f), // 2400K
        (1.00000000f, 0.64373109f, 0.28819679f), // 2500K
        (1.00000000f, 0.66052319f, 0.31873863f), // 2600K
        (1.00000000f, 0.67645822f, 0.34786758f), // 2700K
        (1.00000000f, 0.69160518f, 0.37579588f), // 2800K
        (1.00000000f, 0.70602449f, 0.40267128f), // 2900K
        (1.00000000f, 0.71976951f, 0.42860152f), // 3000K
        (1.00000000f, 0.73288760f, 0.45366838f), // 3100K
        (1.00000000f, 0.74542112f, 0.47793608f), // 3200K
        (1.00000000f, 0.75740814f, 0.50145662f), // 3300K
        (1.00000000f, 0.76888303f, 0.52427322f), // 3400K
        (1.00000000f, 0.77987699f, 0.54642268f), // 3500K
        (1.00000000f, 0.79041843f, 0.56793692f), // 3600K
        (1.00000000f, 0.80053332f, 0.58884417f), // 3700K
        (1.00000000f, 0.81024551f, 0.60916971f), // 3800K
        (1.00000000f, 0.81957693f, 0.62893653f), // 3900K
        (1.00000000f, 0.82854786f, 0.64816570f), // 4000K
        (1.00000000f, 0.83717703f, 0.66687674f), // 4100K
        (1.00000000f, 0.84548188f, 0.68508786f), // 4200K
        (1.00000000f, 0.85347859f, 0.70281616f), // 4300K
        (1.00000000f, 0.86118227f, 0.72007777f), // 4400K
        (1.00000000f, 0.86860704f, 0.73688797f), // 4500K
        (1.00000000f, 0.87576611f, 0.75326132f), // 4600K
        (1.00000000f, 0.88267187f, 0.76921169f), // 4700K
        (1.00000000f, 0.88933596f, 0.78475236f), // 4800K
        (1.00000000f, 0.89576933f, 0.79989606f), // 4900K
        (1.00000000f, 0.90198230f, 0.81465502f), // 5000K
        (1.00000000f, 0.90963069f, 0.82838210f), // 5100K
        (1.00000000f, 0.91710889f, 0.84190889f), // 5200K
        (1.00000000f, 0.92441842f, 0.85523742f), // 5300K
        (1.00000000f, 0.93156127f, 0.86836903f), // 5400K
        (1.00000000f, 0.93853986f, 0.88130458f), // 5500K
        (1.00000000f, 0.94535695f, 0.89404470f), // 5600K
        (1.00000000f, 0.95201559f, 0.90658983f), // 5700K
        (1.00000000f, 0.95851906f, 0.91894041f), // 5800K
        (1.00000000f, 0.96487079f, 0.93109690f), // 5900K
        (1.00000000f, 0.97107439f, 0.94305985f), // 6000K
        (1.00000000f, 0.97713351f, 0.95482993f), // 6100K
        (1.00000000f, 0.98305189f, 0.96640795f), // 6200K
        (1.00000000f, 0.98883326f, 0.97779486f), // 6300K
        (1.00000000f, 0.99448139f, 0.98899179f), // 6400K
        (1.00000000f, 1.00000000f, 1.00000000f), // 6500K (D65)
        (0.98947904f, 0.99348723f, 1.00000000f), // 6600K
        (0.97940448f, 0.98722715f, 1.00000000f), // 6700K
        (0.96975025f, 0.98120637f, 1.00000000f), // 6800K
        (0.96049223f, 0.97541240f, 1.00000000f), // 6900K
        (0.95160805f, 0.96983355f, 1.00000000f), // 7000K
        (0.94303638f, 0.96443333f, 1.00000000f), // 7100K
        (0.93480451f, 0.95923080f, 1.00000000f), // 7200K
        (0.92689056f, 0.95421394f, 1.00000000f), // 7300K
        (0.91927697f, 0.94937330f, 1.00000000f), // 7400K
        (0.91194747f, 0.94470005f, 1.00000000f), // 7500K
        (0.90488690f, 0.94018594f, 1.00000000f), // 7600K
        (0.89808115f, 0.93582323f, 1.00000000f), // 7700K
        (0.89151710f, 0.93160469f, 1.00000000f), // 7800K
        (0.88518247f, 0.92752354f, 1.00000000f), // 7900K
        (0.87906581f, 0.92357340f, 1.00000000f), // 8000K
        (0.87315640f, 0.91974827f, 1.00000000f), // 8100K
        (0.86744421f, 0.91604254f, 1.00000000f), // 8200K
        (0.86191983f, 0.91245088f, 1.00000000f), // 8300K
        (0.85657444f, 0.90896831f, 1.00000000f), // 8400K
        (0.85139976f, 0.90559011f, 1.00000000f), // 8500K
        (0.84638799f, 0.90231183f, 1.00000000f), // 8600K
        (0.84153180f, 0.89912926f, 1.00000000f), // 8700K
        (0.83682430f, 0.89603843f, 1.00000000f), // 8800K
        (0.83225897f, 0.89303558f, 1.00000000f), // 8900K
        (0.82782969f, 0.89011714f, 1.00000000f), // 9000K
        (0.82353066f, 0.88727974f, 1.00000000f), // 9100K
        (0.81935641f, 0.88452017f, 1.00000000f), // 9200K
        (0.81530175f, 0.88183541f, 1.00000000f), // 9300K
        (0.81136180f, 0.87922257f, 1.00000000f), // 9400K
        (0.80753191f, 0.87667891f, 1.00000000f), // 9500K
        (0.80380769f, 0.87420182f, 1.00000000f), // 9600K
        (0.80018497f, 0.87178882f, 1.00000000f), // 9700K
        (0.79665980f, 0.86943756f, 1.00000000f), // 9800K
        (0.79322843f, 0.86714579f, 1.00000000f), // 9900K
        (0.78988728f, 0.86491137f, 1.00000000f), // 10000K
    };

    // ── Native interop ────────────────────────────────────

    [DllImport("user32.dll")]
    private static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    private static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetDeviceGammaRamp(IntPtr hDC, IntPtr lpRamp);

    [DllImport("gdi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetDeviceGammaRamp(IntPtr hDC, IntPtr lpRamp);
}
