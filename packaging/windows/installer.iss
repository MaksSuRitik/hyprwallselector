#ifndef BundleDir
 #error BundleDir must point to the deployed application directory
#endif
#ifndef AppVersion
 #define AppVersion "0.3.2"
#endif
#ifndef OutputDir
 #define OutputDir "."
#endif
[Setup]
AppId={{E4657B20-72ED-41B8-B5DE-26AF6110D75F}
AppName=Hyprwallselector
AppVersion={#AppVersion}
AppPublisher=Hyprwallselector contributors
AppPublisherURL=https://github.com/MaksSuRitik/hyprwallselector
AppSupportURL=https://github.com/MaksSuRitik/hyprwallselector/issues
DefaultDirName={localappdata}\Programs\Hyprwallselector
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputDir={#OutputDir}
OutputBaseFilename=Hyprwallselector-{#AppVersion}-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\hyprwallselector.exe
SetupIconFile=app.ico
CloseApplications=yes
RestartApplications=no
LicenseFile={#BundleDir}\LICENSE

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "Start Hyprwallselector when I sign in"

[Files]
Source: "{#BundleDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{autoprograms}\Hyprwallselector"; Filename: "{app}\hyprwallselector.exe"
Name: "{autodesktop}\Hyprwallselector"; Filename: "{app}\hyprwallselector.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\hyprwallselector.exe"; Description: "Launch Hyprwallselector"; Flags: nowait postinstall skipifsilent
Filename: "{app}\hyprwallselector.exe"; Parameters: "--daemon"; Flags: nowait runhidden; Check: IsUpdate

[UninstallRun]
Filename: "{app}\hyprwallselector.exe"; Parameters: "--quit"; Flags: runhidden waituntilterminated; RunOnceId: "StopHyprwallselector"

[UninstallDelete]
Type: files; Name: "{app}\hyprwallselector.log"

[Code]
function IsUpdate: Boolean;
begin
  Result := ExpandConstant('{param:UPDATE|0}') <> '0';
end;

procedure StopApp;
var
  Code: Integer;
begin
  if FileExists(ExpandConstant('{app}\hyprwallselector.exe')) then begin
    Exec(ExpandConstant('{app}\hyprwallselector.exe'), '--quit', '', SW_HIDE, ewWaitUntilTerminated, Code);
    Sleep(800);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopApp;
  Result := '';
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Code: Integer;
  Mode: String;
begin
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('autostart') then Mode := 'on' else Mode := 'off';
    if not Exec(ExpandConstant('{app}\hyprwallselector.exe'), '--autostart ' + Mode, '', SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
      Log('Could not configure Hyprwallselector autostart');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Code: Integer;
begin
  if CurUninstallStep = usUninstall then begin
    StopApp;
    Exec(ExpandConstant('{app}\hyprwallselector.exe'), '--autostart off', '', SW_HIDE, ewWaitUntilTerminated, Code);
    RegDeleteValue(HKCU, 'Software\Microsoft\Windows\CurrentVersion\Run', 'Hyprwallselector');
  end;
end;
