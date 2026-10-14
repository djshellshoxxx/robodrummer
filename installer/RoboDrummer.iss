; RoboDrummer Windows installer (Inno Setup 6). Built by .github/workflows/release-build.yml:
;   ISCC.exe /DAppVersion=<version> /DStageDir=<dist\RoboDrummer-Windows-x64> installer\RoboDrummer.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\RoboDrummer-Windows-x64"
#endif

[Setup]
AppId={{6E0D6E7B-52A4-4F5B-9B1B-3C8E5D2A7F01}
AppName=RoboDrummer
AppVersion={#AppVersion}
AppPublisher=Circuit Drift Labs
AppPublisherURL=https://djshellshoxxx.github.io/circuitdriftlabs/
DefaultDirName={autopf}\Circuit Drift Labs\RoboDrummer
DefaultGroupName=Circuit Drift Labs
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=RoboDrummer-{#AppVersion}-Windows-x64-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
UninstallDisplayName=RoboDrummer

[Types]
Name: "full"; Description: "Full installation (VST3, CLAP and Standalone)"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full custom
Name: "clap"; Description: "CLAP plug-in"; Types: full custom
Name: "standalone"; Description: "Standalone application"; Types: full custom

[Files]
Source: "{#StageDir}\RoboDrummer.vst3\*"; DestDir: "{commoncf64}\VST3\RoboDrummer.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#StageDir}\RoboDrummer.clap"; DestDir: "{commoncf64}\CLAP"; Flags: ignoreversion; Components: clap
Source: "{#StageDir}\RoboDrummer.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "{#StageDir}\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\COPYRIGHT-TRADEMARK.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\RoboDrummer"; Filename: "{app}\RoboDrummer.exe"; Components: standalone
Name: "{group}\Uninstall RoboDrummer"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\RoboDrummer.exe"; Description: "Launch RoboDrummer"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone
