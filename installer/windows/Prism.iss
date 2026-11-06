#ifndef MyAppVersion
  #define MyAppVersion "0.0.0"
#endif

#ifndef MyAppSource
  #define MyAppSource "dist\\bin"
#endif

#ifdef Nightly
  #define MyAppName "Prism Nightly"
  #define MyAppId "AB056FBC-A940-45F2-BFA4-B3D11A1DDA62"
  #define MyOutputBase "Prism-Setup-Nightly-x64"
#else
  #define MyAppName "Prism"
  #define MyAppId "CD0E807F-9EEC-4F07-B8A2-1433A8E5B3C2"
  #define MyOutputBase "Prism-Setup-x64"
#endif
#define MyAppPublisher "CutWire"
#define MyAppExeName "Prism.exe"

; /DNightly builds the nightly channel installer: its own AppId, name and install directory, so it
; sits beside a stable install instead of upgrading it. Never change either AppId: it is what lets
; an installer upgrade an existing install in place.

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
Compression=lzma
SolidCompression=yes
WizardStyle=modern
OutputDir=output
OutputBaseFilename={#MyOutputBase}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#MyAppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
