#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "."
#endif

[Setup]
AppId={{6F3C2A8E-4B1D-4E7A-9C52-1A7D0B9E5F31}
AppName=Joué Revival
AppVersion={#AppVersion}
AppPublisher=Simon Bourdareau
DefaultDirName={autopf}\Joue Revival
DefaultGroupName=Joué Revival
UninstallDisplayIcon={app}\JoueRevival.exe
OutputBaseFilename=joue-revival-windows-x64-setup
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequiredOverridesAllowed=dialog
WizardStyle=modern
LicenseFile={#SourceDir}\LICENSE
SetupIconFile=..\..\cpp\app\icons\joue-revival.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\JoueRevival.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\JoueRevival-safe-graphics.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\*.png"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourceDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\Joué Revival"; Filename: "{app}\JoueRevival.exe"
Name: "{group}\Joué Revival (safe graphics)"; Filename: "{app}\JoueRevival-safe-graphics.bat"
Name: "{group}\README"; Filename: "{app}\README.md"
Name: "{autodesktop}\Joué Revival"; Filename: "{app}\JoueRevival.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\JoueRevival.exe"; Description: "{cm:LaunchProgram,Joué Revival}"; Flags: nowait postinstall skipifsilent
