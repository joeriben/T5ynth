; akroasys Windows Installer — Inno Setup Script
; Build with: iscc t5ynth.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StandaloneDir
  #define StandaloneDir "..\..\dist\akroasys"
#endif
#ifndef VST3Dir
  #define VST3Dir "..\..\build\T5ynth_artefacts\Release\VST3"
#endif

[Setup]
AppName=akroasys
AppVersion={#AppVersion}
AppPublisher=AI4ArtsEd / UNESCO Chair in Digital Culture and Arts in Education (UCDCAE)
AppPublisherURL=https://github.com/joeriben/akroasys
DefaultDirName={autopf}\akroasys
DefaultGroupName=akroasys
OutputBaseFilename=akroasys-Windows-Setup
Compression=lzma2/max
SolidCompression=yes
DiskSpanning=yes
DiskSliceSize=2000000000
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\akroasys.exe
LicenseFile=..\..\LICENSE.txt
WizardStyle=modern
; SetupIconFile=..\..\resources\icons\akroasys.ico
MinVersion=10.0

[Types]
Name: "full"; Description: "Full installation"
Name: "standalone"; Description: "Standalone only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "standalone"; Description: "akroasys Standalone App"; Types: full standalone custom; Flags: fixed
Name: "vst3"; Description: "VST3 Plugin"; Types: full custom

[Registry]
Root: HKLM; Subkey: "Software\akroasys"; ValueType: string; ValueName: "InstallDir"; ValueData: "{app}"; Components: standalone; Flags: uninsdeletevalue uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\akroasys"; ValueType: string; ValueName: "BackendDir"; ValueData: "{app}\backend"; Components: standalone; Flags: uninsdeletevalue uninsdeletekeyifempty

[Files]
; Standalone app + bundled backend
Source: "{#StandaloneDir}\*"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion recursesubdirs

; VST3 plugin
Source: "{#VST3Dir}\akroasys.vst3\*"; DestDir: "{commoncf}\VST3\akroasys.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs

; License
Source: "..\..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\akroasys"; Filename: "{app}\akroasys.exe"
Name: "{group}\Uninstall akroasys"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\akroasys.exe"; Description: "Launch akroasys"; Flags: nowait postinstall skipifsilent
