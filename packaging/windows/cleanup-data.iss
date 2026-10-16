{ Shared by the uninstaller and its isolated cleanup regression test. }
function OwnedDataPath(const Root, Child: String): String;
var
  Base, Target: String;
begin
  Base := AddBackslash(ExpandFileName(Root));
  Target := ExpandFileName(Base + Child);
  if (Length(Base) <= 3) or (ExtractFileDrive(Base) = '') or
     (Child = '') or (CompareText(Copy(Target, 1, Length(Base)), Base) <> 0) then
    RaiseException('Invalid application data cleanup path');
  Result := Target;
end;

function RemoveOwnedDirectory(const Root, Child: String): Boolean;
var
  Path: String;
begin
  Path := OwnedDataPath(Root, Child);
  Result := True;
  if DirExists(Path) then Result := DelTree(Path, True, True, True);
  if not Result then Log('Could not remove application cache: ' + Path);
end;

function RemoveOwnedFile(const Root, Child: String): Boolean;
var
  Path: String;
begin
  Path := OwnedDataPath(Root, Child);
  Result := True;
  if FileExists(Path) then Result := DeleteFile(Path);
  if not Result then Log('Could not remove application setting: ' + Path);
end;

function CleanupUserData(const LocalData: String; PurgeSettings: Boolean): Boolean;
var
  DataRoot, CacheRoot: String;
begin
  DataRoot := OwnedDataPath(LocalData, 'Hyprwallselector');
  CacheRoot := OwnedDataPath(LocalData, 'cache\hyprwallselector');
  Result := True;
  { Only generated directories, never the library path or the whole app directory.
    Inno's DelTree removes reparse points without traversing their targets. }
  if not RemoveOwnedDirectory(CacheRoot, 'thumbs') then Result := False;
  if not RemoveOwnedDirectory(DataRoot, 'hyprwallselector\cache') then Result := False;
  if not RemoveOwnedDirectory(DataRoot, 'hyprwallselector\updates') then Result := False;
  if not RemoveOwnedDirectory(DataRoot, 'hyprwallselector\runtime') then Result := False;
  if not RemoveOwnedDirectory(DataRoot, 'hyprwallselector\lockscreen') then Result := False;
  if PurgeSettings then begin
    if not RemoveOwnedFile(DataRoot, 'config.json') then Result := False;
    if not RemoveOwnedFile(DataRoot, 'state\hyprwallselector\state.json') then Result := False;
    if not RemoveOwnedFile(DataRoot, 'state\hyprwallselector\selection.json') then Result := False;
    if not RemoveOwnedFile(LocalData, 'hyprwallselector.json') then Result := False;
  end;
  { Remove empty containers only; leave any unrecognized user files intact. }
  RemoveDir(OwnedDataPath(DataRoot, 'state\hyprwallselector'));
  RemoveDir(OwnedDataPath(DataRoot, 'state'));
  RemoveDir(OwnedDataPath(DataRoot, 'hyprwallselector'));
  RemoveDir(DataRoot);
  RemoveDir(CacheRoot);
end;
