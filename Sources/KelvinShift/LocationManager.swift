// KelvinShift – LocationManager.swift

import Foundation
import CoreLocation

final class LocationManager: NSObject, ObservableObject, CLLocationManagerDelegate {
    static let shared = LocationManager()

    private let manager = CLLocationManager()

    @Published var authorizationStatus: CLAuthorizationStatus = .notDetermined
    @Published var lastLocation: CLLocation?
    @Published var isLocating = false
    @Published var error: String?

    private var completion: ((CLLocation?) -> Void)?

    private override init() {
        super.init()
        manager.delegate = self
        manager.desiredAccuracy = kCLLocationAccuracyKilometer // Don't need high precision
        authorizationStatus = manager.authorizationStatus
    }

    /// Request location and call completion with result
    func requestLocation(completion: @escaping (CLLocation?) -> Void) {
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
            completion(nil)
        @unknown default:
            completion(nil)
        }
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
            completion?(nil)
            completion = nil
        }
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        isLocating = false
        if let location = locations.last {
            lastLocation = location
            completion?(location)
        } else {
            completion?(nil)
        }
        completion = nil
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        isLocating = false
        self.error = error.localizedDescription
        NSLog("[KelvinShift] Location error: \(error)")
        completion?(nil)
        completion = nil
    }
}
