; RoboDrummer(TM) Windows installer (Inno Setup 6)
; Copyright (c) 2026 Sheldon Davidson. All rights reserved.
; Build: iscc /DAppVersion=0.3.0 /DSourceDir=<staged folder> /DOutputDir=<out> RoboDrummer.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\dist\RoboDrummer-Windows-x64"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{8C3B6F0E-5E7A-4C55-9B7E-52D0A1F3C7D1}
AppName=RoboDrummer
AppVersion={#AppVersion}
AppPublisher=Circuit Drift Labs
AppPublisherURL=https://djshellshoxxx.github.io/circuitdriftlabs/
DefaultDirName={autopf}\Circuit Drift Labs\RoboDrummer
DefaultGroupName=Circuit Drift Labs
DisableProgramGroupPage=yes
LicenseFile={#SourceDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=RoboDrummer-{#AppVersion}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
UninstallDisplayIcon={app}\RoboDrummer.exe

[Types]
Name: "full"; Description: "Standalone, VST3 and CLAP"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "standalone"; Description: "Standalone application"; Types: full custom
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom

[Files]
Source: "{#SourceDir}\RoboDrummer.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion
Source: "{#SourceDir}\RoboDrummer.vst3\*"; DestDir: "{commoncf64}\VST3\RoboDrummer.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceDir}\RoboDrummer.clap"; DestDir: "{commoncf64}\CLAP"; Components: clap; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\COPYRIGHT-TRADEMARK.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\RoboDrummer"; Filename: "{app}\RoboDrummer.exe"; Components: standalone
Name: "{autodesktop}\RoboDrummer"; Filename: "{app}\RoboDrummer.exe"; Components: standalone; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Components: standalone; Flags: unchecked

[Run]
Filename: "{app}\RoboDrummer.exe"; Description: "Launch RoboDrummer"; Components: standalone; Flags: nowait postinstall skipifsilent
