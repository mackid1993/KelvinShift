// swift-tools-version:5.7
import PackageDescription

let package = Package(
    name: "KelvinShift",
    platforms: [.macOS(.v12)],
    targets: [
        .executableTarget(
            name: "KelvinShift",
            path: "Sources/KelvinShift"
        )
    ]
)
