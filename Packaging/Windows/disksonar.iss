; Inno Setup script for DiskSonar
; Generates a modern 64-bit Windows setup installer with permissions selection,
; Explorer integration, PATH configuration, and detailed setup wizard pages.

#define MyAppName "DiskSonar"
#define MyAppVersion "0.1.1"
#define MyAppPublisher "spydersyrup"
#define MyAppURL "https://github.com/spydersyrup/disksonar"
#define MyAppExeName "DiskSonar.exe"
#define MyAppDescription "DiskSonar Native Disk Storage Analyzer"

[Setup]
; Unique application GUID
AppId={{8B84F05A-F5BD-4C8D-9E9C-3E0D8D6C97F1}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
AppUpdatesURL={#MyAppURL}/releases
AppComments={#MyAppDescription}
AppContact={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
LicenseFile=..\..\LICENSE
InfoBeforeFile=preinstall_info.txt
OutputDir=..\..\dist
OutputBaseFilename=DiskSonar-{#MyAppVersion}-windows-x86_64-setup
SetupIconFile=disksonar.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName} {#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
WizardSizePercent=100
WizardResizable=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableWelcomePage=no
DisableDirPage=no
DisableProgramGroupPage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
VersionInfoVersion=0.1.1.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppDescription}
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion=0.1.1.0
VersionInfoCopyright=Copyright (C) 2026 {#MyAppPublisher}
ChangesEnvironment=yes
ChangesAssociations=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
WelcomeLabel1=Welcome to the [name] Setup Wizard
WelcomeLabel2=This will install [name/ver] on your computer.%n%nDiskSonar is a high-performance, native disk storage space analyzer designed to scan, visualize, and inspect local drives and directories.%n%nIt is recommended that you close all other applications before continuing.

[Tasks]
Name: "adminlaunch"; Description: "Configure DiskSonar to run as Administrator (recommended for full scans of C:\, System Volume Information, and restricted system folders)"; GroupDescription: "Permissions & Elevation:"; Flags: unchecked
Name: "contextmenu"; Description: "Add 'Scan with DiskSonar' to folder and drive right-click menus"; GroupDescription: "Windows Explorer Integration:"; Flags: checkablealone
Name: "contextmenubackground"; Description: "Add 'Scan with DiskSonar' to directory background right-click menu"; GroupDescription: "Windows Explorer Integration:"; Flags: unchecked
Name: "addtopath"; Description: "Add installation directory to PATH environment variable (enables 'disksonar' in CMD and PowerShell)"; GroupDescription: "Command Line Integration:"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\..\dist\{#MyAppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "preinstall_info.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "disksonar.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Comment: "{#MyAppDescription}"
Name: "{group}\{#MyAppName} Documentation"; Filename: "{app}\README.md"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; Comment: "{#MyAppDescription}"

[Registry]
; Windows Explorer folder context menu
Root: HKA; Subkey: "Software\Classes\Directory\shell\DiskSonar"; ValueType: string; ValueName: ""; ValueData: "Scan with DiskSonar"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Directory\shell\DiskSonar"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\DiskSonar\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: contextmenu

; Windows Explorer drive context menu
Root: HKA; Subkey: "Software\Classes\Drive\shell\DiskSonar"; ValueType: string; ValueName: ""; ValueData: "Scan with DiskSonar"; Tasks: contextmenu; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Drive\shell\DiskSonar"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Drive\shell\DiskSonar\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: contextmenu

; Windows Explorer directory background context menu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\DiskSonar"; ValueType: string; ValueName: ""; ValueData: "Scan with DiskSonar"; Tasks: contextmenubackground; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\DiskSonar"; ValueType: string; ValueName: "Icon"; ValueData: """{app}\{#MyAppExeName}"",0"; Tasks: contextmenubackground
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\DiskSonar\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%V"""; Tasks: contextmenubackground

; Administrator privileges layer flag
Root: HKA; Subkey: "Software\Microsoft\Windows NT\CurrentVersion\AppCompatFlags\Layers"; ValueType: string; ValueName: "{app}\{#MyAppExeName}"; ValueData: "~ RUNASADMIN"; Tasks: adminlaunch; Flags: uninsdeletevalue

; PATH environment variable configuration
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\Session Manager\Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}')) and IsAdminInstallMode; Tasks: addtopath
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; ValueData: "{olddata};{app}"; Check: NeedsAddPath(ExpandConstant('{app}')) and not IsAdminInstallMode; Tasks: addtopath

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\README.md"; Description: "View DiskSonar Documentation"; Flags: shellexec nowait postinstall skipifsilent unchecked

[Code]
function NeedsAddPath(Param: string): boolean;
var
  OrigPath: string;
begin
  if IsAdminInstallMode then
  begin
    if not RegQueryStringValue(HKEY_LOCAL_MACHINE,
      'SYSTEM\CurrentControlSet\Control\Session Manager\Environment',
      'Path', OrigPath)
    then
      OrigPath := '';
  end
  else
  begin
    if not RegQueryStringValue(HKEY_CURRENT_USER,
      'Environment',
      'Path', OrigPath)
    then
      OrigPath := '';
  end;
  Result := Pos(';' + Param + ';', ';' + OrigPath + ';') = 0;
end;

procedure RemovePath(Param: string);
var
  OrigPath: string;
  P: Integer;
  RootKey: Integer;
  SubKey: string;
begin
  if IsAdminInstallMode then
  begin
    RootKey := HKEY_LOCAL_MACHINE;
    SubKey := 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment';
  end
  else
  begin
    RootKey := HKEY_CURRENT_USER;
    SubKey := 'Environment';
  end;

  if RegQueryStringValue(RootKey, SubKey, 'Path', OrigPath) then
  begin
    P := Pos(';' + Param + ';', ';' + OrigPath + ';');
    if P > 0 then
    begin
      Delete(OrigPath, P, Length(Param) + 1);
      RegWriteStringValue(RootKey, SubKey, 'Path', OrigPath);
    end;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    RemovePath(ExpandConstant('{app}'));
  end;
end;
