; KelvinShift — Inno Setup installer
;
; Packages the native KelvinShift.exe (pure Win32 / C++ build) into
; KelvinShift-<version>-Setup.exe. Run via build.ps1, or directly: iscc setup.iss

#define MyAppName    "KelvinShift"
#define MyAppVersion "1.1.1"
#define MyAppPublisher "David Brustein"
#define MyAppCopyright "Copyright (C) 2026 David Brustein"
#define MyAppExeName "KelvinShift.exe"
#define MyAppURL     "https://github.com/mackid1993/kelvinshift"

[Setup]
AppId={{9B5B5B4F-5C8E-4F1F-9E2B-3F8A0D1E2C3D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppCopyright={#MyAppCopyright}
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
SetupIconFile=..\src\AppIcon.ico
WizardStyle=modern
ShowLanguageDialog=no
DisableWelcomePage=no
DisableDirPage=auto
DisableReadyPage=no
; The running instance is closed by PrepareToInstall (see [Code]) before any
; file copy, so the Restart Manager page is neither needed nor wanted.
CloseApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon";  Description: "Create a &desktop shortcut"; GroupDescription: "Additional shortcuts:"; Flags: unchecked
Name: "startupicon";  Description: "Start KelvinShift when Windows starts"; GroupDescription: "Startup:"; Flags: checkedonce

[Files]
; The C++ build is a single self-contained exe — no .NET runtime to ship.
Source: "..\build\KelvinShift.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Registry]
; Launch-at-startup. Written here if the user checks the startupicon task
; during install; also toggleable at runtime via LaunchAtLoginService.cs.
;
; Note: the gamma-range registry value (HKLM\...\ICM\GdiIcmGammaRange) is
; NO LONGER written by the installer. It's an opt-in toggle in the app's
; Preferences (under General → "Allow warm temperatures below 3500K"),
; which triggers a UAC prompt and a sign-out reminder if the user chooses
; to enable it.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "KelvinShift"; ValueData: """{app}\{#MyAppExeName}"" --tray"; \
    Tasks: startupicon; Flags: uninsdeletevalue

[Run]
Filename: "{app}\{#MyAppExeName}"; \
    Description: "Launch {#MyAppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; Reset the gamma ramp to identity BEFORE removing the EXE, so the display
; reverts to its prior calibration on uninstall.
Filename: "{app}\{#MyAppExeName}"; Parameters: "--uninstall-cleanup"; \
    Flags: runhidden waituntilterminated; RunOnceId: "KelvinShiftGammaReset"

; Best-effort kill of running instance before uninstall removes files
Filename: "taskkill.exe"; Parameters: "/F /IM {#MyAppExeName}"; Flags: runhidden; RunOnceId: "KillKelvinShift"

[UninstallDelete]
Type: filesandordirs; Name: "{userappdata}\KelvinShift"

[Code]
{ Reinstalling over a running copy used to fail: the tray app holds        }
{ KelvinShift.exe open, so Setup could not replace it. PrepareToInstall    }
{ runs just before the file copy — ask the running instance to close (a    }
{ clean exit restores the gamma ramp), give it a moment, then force any    }
{ straggler so the exe is unlocked. Harmless on a first install (taskkill  }
{ simply finds nothing to close).                                         }
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/IM KelvinShift.exe', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Sleep(800);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM KelvinShift.exe', '',
       SW_HIDE, ewWaitUntilTerminated, ResultCode);
  Result := '';
end;
