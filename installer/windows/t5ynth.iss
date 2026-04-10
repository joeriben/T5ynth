; T5ynth Windows Installer — Inno Setup Script
; Build with: iscc t5ynth.iss

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StandaloneDir
  #define StandaloneDir "..\..\dist\T5ynth"
#endif
#ifndef VST3Dir
  #define VST3Dir "..\..\build\T5ynth_artefacts\Release\VST3"
#endif
#ifndef PresetsDir
  #define PresetsDir "..\..\resources\presets"
#endif

[Setup]
AppName=T5ynth
AppVersion={#AppVersion}
AppPublisher=AI4ArtsEd / UNESCO Chair DCAIAE
AppPublisherURL=https://github.com/joeriben/t5ynth
DefaultDirName={autopf}\T5ynth
DefaultGroupName=T5ynth
OutputBaseFilename=T5ynth-Windows-Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayIcon={app}\T5ynth.exe
LicenseFile=..\..\LICENSE.txt
WizardStyle=modern
; SetupIconFile=..\..\resources\icons\t5ynth.ico
MinVersion=10.0

[Types]
Name: "full"; Description: "Full installation"
Name: "standalone"; Description: "Standalone only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "standalone"; Description: "T5ynth Standalone App"; Types: full standalone custom; Flags: fixed
Name: "vst3"; Description: "VST3 Plugin"; Types: full custom

[Dirs]
; Shared data directories with write permissions for model downloads and presets
Name: "{commonappdata}\T5ynth\models"; Permissions: users-modify
Name: "{commonappdata}\T5ynth\presets"; Permissions: users-readexec

[Files]
; Standalone app + bundled backend
Source: "{#StandaloneDir}\*"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion recursesubdirs

; VST3 plugin
Source: "{#VST3Dir}\T5ynth.vst3\*"; DestDir: "{commoncf}\VST3\T5ynth.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs

; Factory presets
Source: "{#PresetsDir}\*.t5p"; DestDir: "{commonappdata}\T5ynth\presets"; Flags: ignoreversion

; License
Source: "..\..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\THIRD_PARTY_LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\T5ynth"; Filename: "{app}\T5ynth.exe"
Name: "{group}\Uninstall T5ynth"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\T5ynth.exe"; Description: "Launch T5ynth"; Flags: nowait postinstall skipifsilent
