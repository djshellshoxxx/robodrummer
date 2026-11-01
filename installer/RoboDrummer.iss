; RoboDrummer Windows installer (Inno Setup 6). Built by .github/workflows/release-build.yml:
;   ISCC.exe /DAppVersion=<version> /DStageDir=<dist\RoboDrummer-Windows-x64> installer\RoboDrummer.iss
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef AppNumericVersion
  #define AppNumericVersion "0.0.0"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif
#ifndef OutName
  #define OutName "RoboDrummer-" + AppVersion + "-Windows-x64-Setup"
#endif
#ifndef StageDir
  #define StageDir "..\dist\RoboDrummer-Windows-x64"
#endif

#ifdef TestInstallRoot
  ; CI-only build of the same installer, isolated from standard plug-in folders.
  #define Vst3InstallDir TestInstallRoot + "\vst3\RoboDrummer.vst3"
  #define ClapInstallDir TestInstallRoot + "\clap"
#else
  #define Vst3InstallDir "{commoncf64}\VST3\RoboDrummer.vst3"
  #define ClapInstallDir "{commoncf64}\CLAP"
#endif

[Setup]
#ifdef TestInstallRoot
AppId={{4BDA1A16-9D95-4D84-9A21-5C465E9799D5}
#else
AppId={{6E0D6E7B-52A4-4F5B-9B1B-3C8E5D2A7F01}
#endif
AppName=RoboDrummer
AppVersion={#AppVersion}
VersionInfoVersion={#AppNumericVersion}
AppPublisher=Circuit Drift Labs
AppPublisherURL=https://djshellshoxxx.github.io/circuitdriftlabs/
#ifdef TestInstallRoot
DefaultDirName={#TestInstallRoot}\app
#else
DefaultDirName={autopf}\Circuit Drift Labs\RoboDrummer
#endif
DefaultGroupName=Circuit Drift Labs
DisableProgramGroupPage=yes
OutputDir={#OutDir}
OutputBaseFilename={#OutName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
#ifdef TestInstallRoot
PrivilegesRequired=lowest
#else
PrivilegesRequired=admin
#endif
#ifdef EnableSigning
SignTool=rd_signing
SignedUninstaller=yes
#endif
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
Source: "{#StageDir}\RoboDrummer.vst3\*"; DestDir: "{#Vst3InstallDir}"; Flags: recursesubdirs createallsubdirs ignoreversion; Components: vst3
Source: "{#StageDir}\RoboDrummer.clap"; DestDir: "{#ClapInstallDir}"; Flags: ignoreversion; Components: clap
Source: "{#StageDir}\RoboDrummer.exe"; DestDir: "{app}"; Flags: ignoreversion; Components: standalone
Source: "{#StageDir}\INSTALL.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\COPYRIGHT-TRADEMARK.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\PARAMETERS.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\SIGNING.json"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\HELP.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\third-party-notices\*"; DestDir: "{app}\third-party-notices"; Flags: recursesubdirs createallsubdirs ignoreversion

#ifndef TestInstallRoot
[Icons]
Name: "{group}\RoboDrummer"; Filename: "{app}\RoboDrummer.exe"; Components: standalone
Name: "{group}\Uninstall RoboDrummer"; Filename: "{uninstallexe}"

#endif

[Run]
Filename: "{app}\RoboDrummer.exe"; Description: "Launch RoboDrummer"; Flags: nowait postinstall skipifsilent unchecked; Components: standalone
