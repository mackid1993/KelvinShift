#include "LocationService.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <cstdio>

using namespace winrt;
using namespace winrt::Windows::Devices::Geolocation;

std::optional<std::pair<double, double>> LocationService::GetCurrent()
{
    try
    {
        init_apartment();

        auto access = Geolocator::RequestAccessAsync().get();
        if (access != GeolocationAccessStatus::Allowed)
            return std::nullopt;

        Geolocator geo;
        geo.DesiredAccuracyInMeters(1000);
        auto pos = geo.GetGeopositionAsync().get();
        auto p = pos.Coordinate().Point().Position();
        return std::make_pair(p.Latitude, p.Longitude);
    }
    catch (...)
    {
        return std::nullopt; // permission denied, no hardware, etc.
    }
}

namespace {

// Crude flat extraction of a "key":"value" string from a JSON body. Good
// enough for Nominatim's address fields (plain place names).
std::string JsonField(const std::string& json, const char* key)
{
    std::string pat = std::string("\"") + key + "\":\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return "";
    p += pat.size();
    std::string out;
    for (size_t i = p; i < json.size(); ++i)
    {
        char c = json[i];
        if (c == '"') break;
        if (c == '\\' && i + 1 < json.size()) { out += json[++i]; continue; }
        out += c;
    }
    return out;
}

// First non-empty field from a candidate list.
std::string FirstOf(const std::string& json, std::initializer_list<const char*> keys)
{
    for (const char* k : keys)
    {
        std::string v = JsonField(json, k);
        if (!v.empty()) return v;
    }
    return "";
}

} // namespace

std::string LocationService::DescribeLocation(double latitude, double longitude)
{
    HINTERNET session = WinHttpOpen(L"KelvinShift/1.1",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return "";
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 4000);

    std::string body;
    HINTERNET conn = WinHttpConnect(session, L"nominatim.openstreetmap.org",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn)
    {
        wchar_t path[256];
        swprintf(path, 256,
                 L"/reverse?format=jsonv2&zoom=10&addressdetails=1&lat=%.6f&lon=%.6f",
                 latitude, longitude);
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (req)
        {
            if (WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(req, nullptr))
            {
                for (;;)
                {
                    DWORD avail = 0;
                    if (!WinHttpQueryDataAvailable(req, &avail) || avail == 0) break;
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(req, &chunk[0], avail, &read) || read == 0) break;
                    body.append(chunk.data(), read);
                }
            }
            WinHttpCloseHandle(req);
        }
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(session);
    if (body.empty()) return "";

    // Works worldwide: pick the best locality, then the best region, falling
    // back to country — so US gets "City, State", elsewhere "City, Country"
    // or "City, Province", etc.
    std::string locality = FirstOf(body,
        { "city", "town", "village", "municipality", "suburb", "hamlet" });
    std::string region = FirstOf(body,
        { "state", "province", "region", "state_district", "county" });
    std::string country = JsonField(body, "country");

    if (!locality.empty() && !region.empty()) return locality + ", " + region;
    if (!locality.empty() && !country.empty()) return locality + ", " + country;
    if (!locality.empty()) return locality;
    if (!region.empty() && !country.empty()) return region + ", " + country;
    if (!region.empty()) return region;
    return country;
}
