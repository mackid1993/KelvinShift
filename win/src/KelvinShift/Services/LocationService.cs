using System;
using System.Threading.Tasks;
using Windows.Devices.Geolocation;

namespace KelvinShift.Services;

// One-shot location lookup via Windows Geolocator. Returns null on denied
// permission, timeout, or hardware unavailable — caller falls back to
// manual lat/lon entry. No reverse-geocoding in v1 (would need a Bing key).
public static class LocationService
{
    public static async Task<(double Lat, double Lon)?> GetCurrentAsync()
    {
        try
        {
            var access = await Geolocator.RequestAccessAsync();
            if (access != GeolocationAccessStatus.Allowed) return null;

            var geo = new Geolocator { DesiredAccuracyInMeters = 1000 };
            using var cts = new System.Threading.CancellationTokenSource(TimeSpan.FromSeconds(10));
            var pos = await geo.GetGeopositionAsync().AsTask(cts.Token);
            return (pos.Coordinate.Point.Position.Latitude, pos.Coordinate.Point.Position.Longitude);
        }
        catch
        {
            return null;
        }
    }
}
