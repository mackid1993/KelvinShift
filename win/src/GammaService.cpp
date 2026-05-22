#include "GammaService.h"
#include "Common.h"
#include <algorithm>
#include <cstdio>
#include <string>

// ── Internal gamma write path ─────────────────────────────────────────────
// mscms!InternalSetDeviceGammaRamp writes the gamma at a layer the kernel
// does not reset on shell-flyout open (Action Center, Settings, UAC) — that
// reset is the visible flash — and bypasses the GdiIcmGammaRange registry
// cap so warm temperatures work without the HKLM tweak. Three-arg signature
// (hdc, ramp, dwReserved). Resolved from mscms.dll at startup; falls back to
// the public SetDeviceGammaRamp when the symbol is not exported.
typedef BOOL(WINAPI* InternalSetGammaFn)(HDC, LPVOID, DWORD);

static InternalSetGammaFn LoadInternalSetGamma()
{
    const wchar_t* dlls[] = { L"mscms.dll", L"gdi32.dll" };
    for (const wchar_t* dll : dlls)
    {
        HMODULE h = LoadLibraryW(dll);
        if (!h) continue;
        FARPROC p = GetProcAddress(h, "InternalSetDeviceGammaRamp");
        if (p) return reinterpret_cast<InternalSetGammaFn>(p);
    }
    return nullptr;
}

static const InternalSetGammaFn g_internalSetGamma = LoadInternalSetGamma();

// ── File logging (rotating, capped) ───────────────────────────────────────
static void Log(const std::string& msg)
{
    std::wstring dir = KnownFolder(FOLDERID_LocalAppData) + L"\\KelvinShift";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring path = dir + L"\\debug.log";

    const long long kMaxBytes = 256 * 1024;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
    {
        long long size = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        if (size > kMaxBytes)
        {
            std::wstring rotated = path + L".old";
            DeleteFileW(rotated.c_str());
            MoveFileW(path.c_str(), rotated.c_str());
        }
    }

    HANDLE f = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[512];
    int n = snprintf(line, sizeof(line), "%02d:%02d:%02d.%03d [gamma] %s\n",
                     t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg.c_str());
    DWORD written;
    WriteFile(f, line, (DWORD)n, &written, nullptr);
    CloseHandle(f);
}

// ── Blackbody table (Redshift / CIE, Ingo Thies 2013) ─────────────────────
namespace {
struct Entry { float r, g, b; };
const Entry kBlackbody[] = {
    {1.00000000f, 0.18172716f, 0.00000000f}, // 1000K
    {1.00000000f, 0.25503671f, 0.00000000f}, // 1100K
    {1.00000000f, 0.30942099f, 0.00000000f}, // 1200K
    {1.00000000f, 0.35357379f, 0.00000000f}, // 1300K
    {1.00000000f, 0.39091524f, 0.00000000f}, // 1400K
    {1.00000000f, 0.42322816f, 0.00000000f}, // 1500K
    {1.00000000f, 0.45159884f, 0.00000000f}, // 1600K
    {1.00000000f, 0.47675916f, 0.00000000f}, // 1700K
    {1.00000000f, 0.49923747f, 0.00000000f}, // 1800K
    {1.00000000f, 0.51943421f, 0.00000000f}, // 1900K
    {1.00000000f, 0.54360078f, 0.08679949f}, // 2000K
    {1.00000000f, 0.56618736f, 0.14065513f}, // 2100K
    {1.00000000f, 0.58734976f, 0.18362641f}, // 2200K
    {1.00000000f, 0.60724493f, 0.22137978f}, // 2300K
    {1.00000000f, 0.62600248f, 0.25591950f}, // 2400K
    {1.00000000f, 0.64373109f, 0.28819679f}, // 2500K
    {1.00000000f, 0.66052319f, 0.31873863f}, // 2600K
    {1.00000000f, 0.67645822f, 0.34786758f}, // 2700K
    {1.00000000f, 0.69160518f, 0.37579588f}, // 2800K
    {1.00000000f, 0.70602449f, 0.40267128f}, // 2900K
    {1.00000000f, 0.71976951f, 0.42860152f}, // 3000K
    {1.00000000f, 0.73288760f, 0.45366838f}, // 3100K
    {1.00000000f, 0.74542112f, 0.47793608f}, // 3200K
    {1.00000000f, 0.75740814f, 0.50145662f}, // 3300K
    {1.00000000f, 0.76888303f, 0.52427322f}, // 3400K
    {1.00000000f, 0.77987699f, 0.54642268f}, // 3500K
    {1.00000000f, 0.79041843f, 0.56793692f}, // 3600K
    {1.00000000f, 0.80053332f, 0.58884417f}, // 3700K
    {1.00000000f, 0.81024551f, 0.60916971f}, // 3800K
    {1.00000000f, 0.81957693f, 0.62893653f}, // 3900K
    {1.00000000f, 0.82854786f, 0.64816570f}, // 4000K
    {1.00000000f, 0.83717703f, 0.66687674f}, // 4100K
    {1.00000000f, 0.84548188f, 0.68508786f}, // 4200K
    {1.00000000f, 0.85347859f, 0.70281616f}, // 4300K
    {1.00000000f, 0.86118227f, 0.72007777f}, // 4400K
    {1.00000000f, 0.86860704f, 0.73688797f}, // 4500K
    {1.00000000f, 0.87576611f, 0.75326132f}, // 4600K
    {1.00000000f, 0.88267187f, 0.76921169f}, // 4700K
    {1.00000000f, 0.88933596f, 0.78475236f}, // 4800K
    {1.00000000f, 0.89576933f, 0.79989606f}, // 4900K
    {1.00000000f, 0.90198230f, 0.81465502f}, // 5000K
    {1.00000000f, 0.90963069f, 0.82838210f}, // 5100K
    {1.00000000f, 0.91710889f, 0.84190889f}, // 5200K
    {1.00000000f, 0.92441842f, 0.85523742f}, // 5300K
    {1.00000000f, 0.93156127f, 0.86836903f}, // 5400K
    {1.00000000f, 0.93853986f, 0.88130458f}, // 5500K
    {1.00000000f, 0.94535695f, 0.89404470f}, // 5600K
    {1.00000000f, 0.95201559f, 0.90658983f}, // 5700K
    {1.00000000f, 0.95851906f, 0.91894041f}, // 5800K
    {1.00000000f, 0.96487079f, 0.93109690f}, // 5900K
    {1.00000000f, 0.97107439f, 0.94305985f}, // 6000K
    {1.00000000f, 0.97713351f, 0.95482993f}, // 6100K
    {1.00000000f, 0.98305189f, 0.96640795f}, // 6200K
    {1.00000000f, 0.98883326f, 0.97779486f}, // 6300K
    {1.00000000f, 0.99448139f, 0.98899179f}, // 6400K
    {1.00000000f, 1.00000000f, 1.00000000f}, // 6500K (D65)
    {0.98947904f, 0.99348723f, 1.00000000f}, // 6600K
    {0.97940448f, 0.98722715f, 1.00000000f}, // 6700K
    {0.96975025f, 0.98120637f, 1.00000000f}, // 6800K
    {0.96049223f, 0.97541240f, 1.00000000f}, // 6900K
    {0.95160805f, 0.96983355f, 1.00000000f}, // 7000K
    {0.94303638f, 0.96443333f, 1.00000000f}, // 7100K
    {0.93480451f, 0.95923080f, 1.00000000f}, // 7200K
    {0.92689056f, 0.95421394f, 1.00000000f}, // 7300K
    {0.91927697f, 0.94937330f, 1.00000000f}, // 7400K
    {0.91194747f, 0.94470005f, 1.00000000f}, // 7500K
    {0.90488690f, 0.94018594f, 1.00000000f}, // 7600K
    {0.89808115f, 0.93582323f, 1.00000000f}, // 7700K
    {0.89151710f, 0.93160469f, 1.00000000f}, // 7800K
    {0.88518247f, 0.92752354f, 1.00000000f}, // 7900K
    {0.87906581f, 0.92357340f, 1.00000000f}, // 8000K
    {0.87315640f, 0.91974827f, 1.00000000f}, // 8100K
    {0.86744421f, 0.91604254f, 1.00000000f}, // 8200K
    {0.86191983f, 0.91245088f, 1.00000000f}, // 8300K
    {0.85657444f, 0.90896831f, 1.00000000f}, // 8400K
    {0.85139976f, 0.90559011f, 1.00000000f}, // 8500K
    {0.84638799f, 0.90231183f, 1.00000000f}, // 8600K
    {0.84153180f, 0.89912926f, 1.00000000f}, // 8700K
    {0.83682430f, 0.89603843f, 1.00000000f}, // 8800K
    {0.83225897f, 0.89303558f, 1.00000000f}, // 8900K
    {0.82782969f, 0.89011714f, 1.00000000f}, // 9000K
    {0.82353066f, 0.88727974f, 1.00000000f}, // 9100K
    {0.81935641f, 0.88452017f, 1.00000000f}, // 9200K
    {0.81530175f, 0.88183541f, 1.00000000f}, // 9300K
    {0.81136180f, 0.87922257f, 1.00000000f}, // 9400K
    {0.80753191f, 0.87667891f, 1.00000000f}, // 9500K
    {0.80380769f, 0.87420182f, 1.00000000f}, // 9600K
    {0.80018497f, 0.87178882f, 1.00000000f}, // 9700K
    {0.79665980f, 0.86943756f, 1.00000000f}, // 9800K
    {0.79322843f, 0.86714579f, 1.00000000f}, // 9900K
    {0.78988728f, 0.86491137f, 1.00000000f}, // 10000K
};
const int kBlackbodyCount = (int)(sizeof(kBlackbody) / sizeof(kBlackbody[0]));
} // namespace

GammaService::Rgb GammaService::KelvinToRgb(int kelvin)
{
    int k = std::clamp(kelvin, 1000, 10000);
    int index = (k - 1000) / 100;
    float alpha = (float)((k - 1000) % 100) / 100.0f;

    if (index >= kBlackbodyCount - 1)
    {
        const Entry& e = kBlackbody[kBlackbodyCount - 1];
        return { e.r, e.g, e.b };
    }
    const Entry& lo = kBlackbody[index];
    const Entry& hi = kBlackbody[index + 1];
    return {
        lo.r + alpha * (hi.r - lo.r),
        lo.g + alpha * (hi.g - lo.g),
        lo.b + alpha * (hi.b - lo.b),
    };
}

// ── Public API ────────────────────────────────────────────────────────────

bool GammaService::ApplyKelvinWithBrightness(int kelvin, double brightness)
{
    int k = std::clamp(kelvin, 1000, 10000);
    double b = std::clamp(brightness, 0.1, 1.0);
    if (hasApplied_ && k == currentKelvin_ && fabs(b - currentBrightness_) < 0.001)
        return true;
    currentKelvin_ = k;
    currentBrightness_ = b;
    Rgb rgb = KelvinToRgb(k);
    {
        std::lock_guard<std::mutex> lock(writeLock_);
        BuildRamp(rgb.r * (float)b, rgb.g * (float)b, rgb.b * (float)b);
        WriteRamp();
    }
    hasApplied_ = true;
    return true;
}

bool GammaService::Reapply()
{
    if (!hasApplied_) return true;
    std::lock_guard<std::mutex> lock(writeLock_);
    if (!GammaMatchesExpected())
        WriteRamp();
    return true;
}

void GammaService::Reset()
{
    currentKelvin_ = 6500;
    currentBrightness_ = 1.0;
    hasApplied_ = false;
    std::lock_guard<std::mutex> lock(writeLock_);
    BuildRamp(1.0f, 1.0f, 1.0f);
    WriteRamp();
}

// ── Ramp construction / write ─────────────────────────────────────────────

void GammaService::BuildRamp(float r, float g, float b)
{
    // Stable ramp — no per-write perturbation. The read-back gate in Reapply
    // already ensures identical successive writes never happen at our layer.
    for (int i = 0; i < 256; ++i)
    {
        float t = i / 255.0f;
        ramp_[i]       = (WORD)std::clamp((int)(t * r * 65535.0f), 0, 65535);
        ramp_[i + 256] = (WORD)std::clamp((int)(t * g * 65535.0f), 0, 65535);
        ramp_[i + 512] = (WORD)std::clamp((int)(t * b * 65535.0f), 0, 65535);
    }
}

bool GammaService::GammaMatchesExpected()
{
    HDC hdc = GetDC(nullptr);
    if (!hdc) return true; // assume OK rather than spam writes
    BOOL ok = GetDeviceGammaRamp(hdc, probeBuffer_);
    ReleaseDC(nullptr, hdc);
    if (!ok) return true;

    // Sample channel mid-points — catches the identity reset (mid = 0x8000)
    // cheaply. Tolerance absorbs floating-point quantization in our build.
    const int tol = 256;
    return abs(probeBuffer_[128] - ramp_[128]) <= tol
        && abs(probeBuffer_[256 + 128] - ramp_[256 + 128]) <= tol
        && abs(probeBuffer_[512 + 128] - ramp_[512 + 128]) <= tol;
}

bool GammaService::WriteRamp()
{
    HDC hdc = GetDC(nullptr);
    if (!hdc)
    {
        Log("GetDC(NULL) returned 0");
        return false;
    }

    // Some drivers fail the first call but succeed on the second. Prefer the
    // kernel path; fall back to the documented one if Internal* is missing.
    bool ok = false;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt)
    {
        ok = g_internalSetGamma
            ? (g_internalSetGamma(hdc, ramp_, 0) != FALSE)
            : (SetDeviceGammaRamp(hdc, ramp_) != FALSE);
    }

    if (!ok)
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "gamma write failed (Win32 err=%lu, path=%s)",
                 GetLastError(), g_internalSetGamma ? "internal" : "public");
        Log(buf);
    }
    else if (!loggedFirstSuccess_)
    {
        char buf[200];
        snprintf(buf, sizeof(buf),
                 "%s OK rampSample=[R0=%u R128=%u R255=%u G128=%u B128=%u]",
                 g_internalSetGamma ? "InternalSetDeviceGammaRamp" : "SetDeviceGammaRamp",
                 ramp_[0], ramp_[128], ramp_[255], ramp_[256 + 128], ramp_[512 + 128]);
        Log(buf);
        loggedFirstSuccess_ = true;
    }

    ReleaseDC(nullptr, hdc);
    return ok;
}
