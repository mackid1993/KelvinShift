; KelvinShift — Inno Setup installer
;
; Builds KelvinShift-<version>-Setup.exe from the dotnet publish output.
; Run via build.ps1, or directly: iscc setup.iss

#define MyAppName    "KelvinShift"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "David Brustein"
#define MyAppExeName "KelvinShift.exe"
#define MyAppURL     "https://github.com/davidbrustein/kelvinshift"

[Setup]
AppId={{9B5B5B4F-5C8E-4F1F-9E2B-3F8A0D1E2C3D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
DefaultDirName={commonpf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#MyAppExeName}
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
Compression=lzma2/ultra
SolidCompression=yes
OutputDir=..
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-Setup
SetupIconFile=..\src\KelvinShift\AppIcon.ico
WizardStyle=modern
ShowLanguageDialog=no
DisableWelcomePage=no
DisableDirPage=auto
DisableReadyPage=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon";  Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "startupicon";  Description: "Start KelvinShift when Windows starts"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
Source: "..\src\KelvinShift\bin\Release\net8.0-windows10.0.19041.0\win-x64\publish\*"; \
    DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Lift the Vista+ gamma-range cap so warm temperatures below ~3500K are
; not clipped by GDI validation. Belt-and-suspenders — our D3DKMT path
; mostly bypasses this anyway, but the legacy SetDeviceGammaRamp path
; (some HDR fallback scenarios) still respects the cap.
Root: HKLM; Subkey: "SOFTWARE\Microsoft\Windows NT\CurrentVersion\ICM"; \
    ValueType: dword; ValueName: "GdiIcmGammaRange"; ValueData: 256; \
    Flags: uninsdeletevalue

; Launch-at-startup is written to HKCU at runtime when the user toggles
; the option (see LaunchAtLoginService.cs). Also set it during install
; if the user checked the startupicon task.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "KelvinShift"; ValueData: """{app}\{#MyAppExeName}"" --tray"; \
    Tasks: startupicon; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Best-effort kill of running instance before uninstall removes files
Filename: "taskkill.exe"; Parameters: "/F /IM {#MyAppExeName}"; Flags: runhidden; RunOnceId: "KillKelvinShift"

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\KelvinShift"

[Code]
// The GdiIcmGammaRange registry value we just wrote takes effect on next
// sign-in. KelvinShift's primary path (D3DKMTSetGammaRamp) works at full
// warm temperatures immediately, but the legacy fallback path used during
// some HDR-mode transitions will clip below ~3500K until restart.
//
// Returning True from NeedRestart() makes Inno Setup show the standard
// "Yes, restart now / No, I will restart later" prompt at the end of
// install — and only writes a fresh value to the registry, so it's safe
// to defer.
function NeedRestart(): Boolean;
begin
  Result := True;
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
end;
