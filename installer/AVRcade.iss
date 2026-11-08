#ifndef AppVersion
  #define AppVersion "0.5.8"
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
UsePreviousAppDir=yes
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

[Code]
const
  UninstallKey = 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{D427E392-4F59-4BE7-A2A8-29AF8E60CB29}_is1';

function CheckVersion(const InstalledVersion, Location: String): String;
var
  Installed, Incoming: Int64;
begin
  Result := '';
  if StrToVersion(InstalledVersion, Installed) and
     StrToVersion('{#AppVersion}', Incoming) then
    if ComparePackedVersion(Installed, Incoming) > 0 then
      Result := 'AVRcade downgrade refused: version ' + InstalledVersion +
        ' is already installed at ' + Location + '. This installer is version {#AppVersion}.' + #13#10 +
        'Download the latest AVRcade installer to update. Your existing installation was kept.';
end;

function CheckFolder(const Folder: String): String;
var
  Version: String;
begin
  Result := '';
  if Folder = '' then exit;
  if GetVersionNumbersString(AddBackslash(Folder) + 'AVRcade.exe', Version) then
    Result := CheckVersion(Version, Folder);
  if (Result = '') and
     GetVersionNumbersString(AddBackslash(Folder) + 'avrcade-cli.exe', Version) then
    Result := CheckVersion(Version, Folder);
end;

function CheckRegistration(const RootKey: Integer): String;
var
  Version, Folder: String;
begin
  Result := '';
  if RegQueryStringValue(RootKey, UninstallKey, 'DisplayVersion', Version) then
    Result := CheckVersion(Version, 'the registered installation');
  if (Result = '') and
     RegQueryStringValue(RootKey, UninstallKey, 'Inno Setup: App Path', Folder) then
    Result := CheckFolder(Folder);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  { Check both registration views and the selected folder before closing apps
    or replacing files. Binary checks also protect portable or mixed installs. }
  Result := CheckRegistration(HKCU32);
  if (Result = '') and IsWin64 then
    Result := CheckRegistration(HKCU64);
  if Result = '' then
    Result := CheckFolder(ExpandConstant('{app}'));
  if Result <> '' then Log(Result);
end;
