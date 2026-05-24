#pragma once

// One-shot location lookup via the Windows Geolocator (C++/WinRT). Returns
// nullopt on denied permission, timeout, or hardware unavailable — the caller
// falls back to manual lat/lon entry. Blocking; call from a worker thread.
// Port of LocationService.cs.

#include <optional>
#include <utility>
#include <string>

namespace LocationService
{
    std::optional<std::pair<double, double>> GetCurrent();

    // Reverse-geocode lat/lon to a "City, State" string via the OpenStreetMap
    // Nominatim service (no API key). Returns "" on network failure. Blocking;
    // call from a worker thread.
    std::string DescribeLocation(double latitude, double longitude);
}
