using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;

namespace KelvinShift.Services;

// Applies color temperature via the WDDM kernel-mode gamma path
// (D3DKMTSetGammaRamp), not the legacy SetDeviceGammaRamp that Light Bulb
// uses. The kernel path is what f.lux and CareUEyes use — it bypasses the
// GDI gamma-validation/reset behaviors that cause visible flashes when
// shell surfaces (Action Center, Settings, UAC) compose.
//
// Color math uses the 91-entry Redshift blackbody table (CIE color matching,
// Ingo Thies 2013), interpolated 100K → any K. D65 white point at 6500K.
// Identical to macos/Sources/KelvinShift/GammaController.swift.
public sealed class GammaService : IDisposable
{
    private readonly List<AdapterEntry> _adapters = new();
    private bool _adaptersValid;
    private readonly ushort[] _ramp = new ushort[768]; // R[0..255], G[256..511], B[512..767]
    private byte _rotatingOffset; // defeats driver caching of identical ramps

    private int _currentKelvin = 6500;
    private double _currentBrightness = 1.0;
    private bool _hasApplied;

    public int CurrentKelvin => _currentKelvin;
    public double CurrentBrightness => _currentBrightness;

    public bool ApplyKelvin(int kelvin) => ApplyKelvinWithBrightness(kelvin, 1.0);

    public bool ApplyKelvinWithBrightness(int kelvin, double brightness)
    {
        var k = Math.Clamp(kelvin, 1000, 10000);
        var b = Math.Clamp(brightness, 0.1, 1.0);

        if (_hasApplied && k == _currentKelvin && Math.Abs(b - _currentBrightness) < 0.001)
            return true;

        _currentKelvin = k;
        _currentBrightness = b;

        var rgb = KelvinToRgb(k);
        BuildRamp(rgb.R * (float)b, rgb.G * (float)b, rgb.B * (float)b);
        var ok = WriteRamp();
        if (ok) _hasApplied = true;
        return ok;
    }

    public bool Reapply()
    {
        if (!_hasApplied) return true;
        var rgb = KelvinToRgb(_currentKelvin);
        BuildRamp(rgb.R * (float)_currentBrightness, rgb.G * (float)_currentBrightness, rgb.B * (float)_currentBrightness);
        return WriteRamp();
    }

    public void InvalidateAdapters()
    {
        CloseAdapters();
        _adaptersValid = false;
    }

    public void Reset()
    {
        _currentKelvin = 6500;
        _currentBrightness = 1.0;
        _hasApplied = false;
        BuildRamp(1f, 1f, 1f);
        WriteRamp();
    }

    public void Dispose()
    {
        // Do not reset gamma on dispose — when contexts become invalidated this
        // flickers. The app's own Quit path explicitly calls Reset() first.
        CloseAdapters();
    }

    // ── Ramp construction ─────────────────────────────────

    private void BuildRamp(float r, float g, float b)
    {
        // Cycle 0..4 so consecutive identical writes still differ in one byte —
        // some display drivers ignore SetGammaRamp calls with an unchanged ramp
        // even after an external entity (lock screen, HDR toggle) reset it.
        _rotatingOffset = (byte)((_rotatingOffset + 1) % 5);
        var off = _rotatingOffset;

        for (var i = 0; i < 256; i++)
        {
            var t = i / 255f;
            _ramp[i]       = (ushort)Math.Clamp((int)(t * r * 65535f), 0, 65535);
            _ramp[i + 256] = (ushort)Math.Clamp((int)(t * g * 65535f), 0, 65535);
            _ramp[i + 512] = (ushort)Math.Clamp((int)(t * b * 65535f), 0, 65535);
        }
        // perturb last sample of each channel
        _ramp[255] = (ushort)Math.Clamp(_ramp[255] + off, 0, 65535);
        _ramp[511] = (ushort)Math.Clamp(_ramp[511] + off, 0, 65535);
        _ramp[767] = (ushort)Math.Clamp(_ramp[767] + off, 0, 65535);
    }

    private bool WriteRamp()
    {
        EnsureAdapters();
        if (_adapters.Count == 0) return false;

        var pin = GCHandle.Alloc(_ramp, GCHandleType.Pinned);
        try
        {
            var ptr = pin.AddrOfPinnedObject();
            var allOk = true;
            foreach (var a in _adapters)
            {
                var info = new D3DKMT_SETGAMMARAMP
                {
                    hDevice = a.HAdapter,
                    VidPnSourceId = a.VidPnSourceId,
                    Type = D3DDDI_GAMMARAMP_TYPE.RGB256x3x16,
                    pGammaRamp = ptr,
                    Size = 1536, // 256 * 2 bytes * 3 channels
                };
                var status = D3DKMTSetGammaRamp(ref info);
                if (status != 0)
                {
                    Debug.WriteLine($"[KelvinShift] D3DKMTSetGammaRamp failed: 0x{status:X8} on adapter {a.Device}");
                    allOk = false;
                }
            }
            return allOk;
        }
        finally
        {
            pin.Free();
        }
    }

    // ── Adapter enumeration ───────────────────────────────

    private void EnsureAdapters()
    {
        if (_adaptersValid) return;
        CloseAdapters();
        var found = new List<AdapterEntry>();

        bool Enum(IntPtr hMonitor, IntPtr hdcMonitor, ref RECT lprc, IntPtr data)
        {
            var info = new MONITORINFOEX { cbSize = Marshal.SizeOf<MONITORINFOEX>() };
            if (!GetMonitorInfo(hMonitor, ref info)) return true;

            var hdc = CreateDC("DISPLAY", info.szDevice, null, IntPtr.Zero);
            if (hdc == IntPtr.Zero) return true;
            try
            {
                var open = new D3DKMT_OPENADAPTERFROMHDC { hDc = hdc };
                var status = D3DKMTOpenAdapterFromHdc(ref open);
                if (status == 0)
                    found.Add(new AdapterEntry(open.hAdapter, open.VidPnSourceId, info.szDevice));
                else
                    Debug.WriteLine($"[KelvinShift] D3DKMTOpenAdapterFromHdc failed for {info.szDevice}: 0x{status:X8}");
            }
            finally { DeleteDC(hdc); }
            return true;
        }

        EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, Enum, IntPtr.Zero);
        _adapters.AddRange(found);
        _adaptersValid = true;
    }

    private void CloseAdapters()
    {
        foreach (var a in _adapters)
        {
            var close = new D3DKMT_CLOSEADAPTER { hAdapter = a.HAdapter };
            D3DKMTCloseAdapter(ref close);
        }
        _adapters.Clear();
    }

    private readonly record struct AdapterEntry(uint HAdapter, uint VidPnSourceId, string Device);

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

    [StructLayout(LayoutKind.Sequential)]
    private struct LUID { public uint Low; public int High; }

    [StructLayout(LayoutKind.Sequential)]
    private struct D3DKMT_OPENADAPTERFROMHDC
    {
        public IntPtr hDc;
        public uint hAdapter;
        public LUID AdapterLuid;
        public uint VidPnSourceId;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct D3DKMT_CLOSEADAPTER { public uint hAdapter; }

    private enum D3DDDI_GAMMARAMP_TYPE
    {
        Uninitialized = 0,
        Default = 1,
        RGB256x3x16 = 2,
        DXGI_1 = 3,
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct D3DKMT_SETGAMMARAMP
    {
        public uint hDevice;
        public uint VidPnSourceId;
        public D3DDDI_GAMMARAMP_TYPE Type;
        public IntPtr pGammaRamp;
        public uint Size;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    private struct MONITORINFOEX
    {
        public int cbSize;
        public RECT rcMonitor;
        public RECT rcWork;
        public uint dwFlags;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string szDevice;
    }

    private delegate bool MonitorEnumProc(IntPtr hMonitor, IntPtr hdcMonitor, ref RECT lprcMonitor, IntPtr dwData);

    [DllImport("user32.dll")]
    private static extern bool EnumDisplayMonitors(IntPtr hdc, IntPtr lprcClip, MonitorEnumProc proc, IntPtr data);

    [DllImport("user32.dll", CharSet = CharSet.Auto)]
    private static extern bool GetMonitorInfo(IntPtr hMonitor, ref MONITORINFOEX info);

    [DllImport("gdi32.dll", CharSet = CharSet.Auto, EntryPoint = "CreateDCW", SetLastError = true)]
    private static extern IntPtr CreateDC(string lpszDriver, string lpszDevice, string? lpszOutput, IntPtr lpInitData);

    [DllImport("gdi32.dll")]
    private static extern bool DeleteDC(IntPtr hdc);

    [DllImport("gdi32.dll", ExactSpelling = true)]
    private static extern int D3DKMTOpenAdapterFromHdc(ref D3DKMT_OPENADAPTERFROMHDC data);

    [DllImport("gdi32.dll", ExactSpelling = true)]
    private static extern int D3DKMTSetGammaRamp(ref D3DKMT_SETGAMMARAMP data);

    [DllImport("gdi32.dll", ExactSpelling = true)]
    private static extern int D3DKMTCloseAdapter(ref D3DKMT_CLOSEADAPTER data);
}
