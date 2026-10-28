#ifndef AppVersion
  #define AppVersion "0.5.4"
#endif
#ifndef SourceRoot
  #error SourceRoot must point to the staged AVRcade payload
#endif
#ifndef OutputDir
  #error OutputDir must point to the installer output directory
#endif

[Setup]
AppId={{D427E392-4F59-4BE7-A2A8-29AF8E60CB29}
AppName=AVRcade
AppVersion={#AppVersion}
AppPublisher=AVRcade
AppVerName=AVRcade {#AppVersion}
DefaultDirName={localappdata}\Programs\AVRcade
DefaultGroupName=AVRcade
UsePreviousAppDir=no
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
MinVersion=10.0.17763
DisableProgramGroupPage=yes
DisableReadyMemo=no
InfoBeforeFile=INSTALL-NOTES.txt
OutputDir={#OutputDir}
OutputBaseFilename=AVRcade-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=120
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
UninstallDisplayName=AVRcade
UninstallDisplayIcon={app}\AVRcade.exe
VersionInfoVersion={#AppVersion}
VersionInfoCompany=AVRcade
VersionInfoDescription=AVRcade Windows installer
VersionInfoProductName=AVRcade
VersionInfoProductVersion={#AppVersion}

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#SourceRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\AVRcade"; Filename: "{app}\AVRcade.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\AVRcade"; Filename: "{app}\AVRcade.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\AVRcade.exe"; Description: "Open AVRcade"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
