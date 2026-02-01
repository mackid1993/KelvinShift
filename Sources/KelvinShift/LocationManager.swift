// KelvinShift – LocationManager.swift

import Foundation
import CoreLocation

final class LocationManager: NSObject, ObservableObject, CLLocationManagerDelegate {
    static let shared = LocationManager()

    private let manager = CLLocationManager()
    private let geocoder = CLGeocoder()

    @Published var authorizationStatus: CLAuthorizationStatus = .notDetermined
    @Published var lastLocation: CLLocation?
    @Published var isLocating = false
    @Published var error: String?
    @Published var locationName: String?

    private var completion: ((CLLocation?, String?) -> Void)?

    private override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyKilometer // Don't need high precision
        authorizationStatus = manager.authorizationStatus
    }

    /// Request location and call completion with result (location and place name)
    func requestLocation(completion: @escaping (CLLocation?, String?) -> Void) {
        self.completion = completion
        self.error = nil

        switch manager.authorizationStatus {
        case .notDetermined:
            isLocating = true
            manager.requestAlwaysAuthorization()
        case .authorized, .authorizedAlways:
            isLocating = true
            manager.requestLocation()
        case .denied, .restricted:
            self.error = "Location access denied. Enable in System Settings → Privacy & Security → Location Services."
            completion(nil, nil)
        @unknown default:
            completion(nil, nil)
        }
    }

    /// Reverse geocode coordinates to get a place name
    func reverseGeocode(latitude: Double, longitude: Double, completion: @escaping (String?) -> Void) {
        let location = CLLocation(latitude: latitude, longitude: longitude)
        geocoder.reverseGeocodeLocation(location) { placemarks, error in
            if let placemark = placemarks?.first {
                let name = Self.formatPlacemark(placemark)
                DispatchQueue.main.async {
                    self.locationName = name
                }
                completion(name)
            } else {
                completion(nil)
            }
        }
    }

    private static func formatPlacemark(_ placemark: CLPlacemark) -> String {
        var parts: [String] = []
        if let locality = placemark.locality {
            parts.append(locality)
        }
        if let adminArea = placemark.administrativeArea {
            parts.append(adminArea)
        }
        if parts.isEmpty, let country = placemark.country {
            parts.append(country)
        }
        return parts.joined(separator: ", ")
    }

    // MARK: - CLLocationManagerDelegate

    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        authorizationStatus = manager.authorizationStatus

        if authorizationStatus == .authorized || authorizationStatus == .authorizedAlways {
            if isLocating {
                manager.requestLocation()
            }
        } else if authorizationStatus == .denied || authorizationStatus == .restricted {
            isLocating = false
            error = "Location access denied. Enable in System Settings → Privacy & Security → Location Services."
            completion?(nil, nil)
            completion = nil
        }
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        if let location = locations.last {
            lastLocation = location
            // Reverse geocode to get place name
            geocoder.reverseGeocodeLocation(location) { [weak self] placemarks, _ in
                DispatchQueue.main.async {
                    self?.isLocating = false
                    let name = placemarks?.first.flatMap { Self.formatPlacemark($0) }
                    self?.locationName = name
                    self?.completion?(location, name)
                    self?.completion = nil
                }
            }
        } else {
            isLocating = false
            completion?(nil, nil)
            completion = nil
        }
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        isLocating = false
        self.error = error.localizedDescription
        NSLog("[KelvinShift] Location error: \(error)")
        completion?(nil, nil)
        completion = nil
    }
}
