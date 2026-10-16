#ifndef TestRoot
 #error TestRoot must be a new isolated directory
#endif
[Setup]
AppName=Hyprwallselector cleanup regression test
AppVersion=1.0
CreateAppDir=no
Uninstallable=no
CreateUninstallRegKey=no
PrivilegesRequired=lowest
OutputDir={#TestRoot}
OutputBaseFilename=cleanup-test

[Code]
#include "..\packaging\windows\cleanup-data.iss"

procedure Check(Value: Boolean; const Message: String);
begin
  if not Value then RaiseException(Message);
end;
procedure Seed(const Path: String);
begin
  Check(ForceDirectories(ExtractFileDir(Path)), 'Cannot create fixture directory');
  Check(SaveStringToFile(Path, 'test data', False), 'Cannot create fixture file');
end;

function InitializeSetup: Boolean;
var
  Root, Data, Cache, Personal, Neighbor: String;
  Rejected: Boolean;
begin
  Root := ExpandFileName('{#TestRoot}\profile');
  Data := Root + '\Hyprwallselector';
  Cache := Root + '\cache\hyprwallselector\thumbs';
  Personal := Data + '\My wallpapers\keep.jpg';
  Neighbor := Root + '\cache\AnotherApp\keep.dat';
  Seed(Data + '\config.json');
  Seed(Data + '\state\hyprwallselector\state.json');
  Seed(Data + '\hyprwallselector\updates\Setup.exe');
  Seed(Data + '\hyprwallselector\cache\old-video.mp4');
  Seed(Data + '\hyprwallselector\runtime\instance.lock');
  Seed(Data + '\hyprwallselector\lockscreen\poster.jpg');
  Seed(Cache + '\thumb.jpg');
  Seed(Personal);
  Seed(Neighbor);
  Check(CleanupUserData(Root, False), 'Keep-settings cleanup failed');
  Check(FileExists(Data + '\config.json'), 'Settings were deleted without consent');
  Check(FileExists(Data + '\state\hyprwallselector\state.json'), 'Saved selection was lost');
  Check(not DirExists(Cache), 'Thumbnail cache remains');
  Check(not DirExists(Data + '\hyprwallselector'), 'Generated data remains');
  Check(FileExists(Personal) and FileExists(Neighbor), 'Non-app files were deleted');
  Check(CleanupUserData(Root, True), 'Full cleanup failed');
  Check(not FileExists(Data + '\config.json'), 'Settings remain after full cleanup');
  Check(not DirExists(Data + '\state'), 'Saved state remains after full cleanup');
  Check(FileExists(Personal) and FileExists(Neighbor), 'Full cleanup touched non-app files');
  Rejected := False;
  try
    OwnedDataPath(Data, '..\outside');
  except
    Rejected := True;
  end;
  Check(Rejected, 'Path traversal was accepted');
  Check(SaveStringToFile('{#TestRoot}\passed.txt', 'keep, purge, personal files, path boundaries: passed', False), 'Cannot write test result');
  Result := True; { No payload, uninstall registration or user profile changes. }
end;
