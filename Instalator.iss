[Setup]
AppId={{A4A5E2A7-9D2C-4F1A-9E8A-9E87A9F9DE11}}
AppName=RoTools
AppVersion=1.0.0
AppPublisher=Zayo
DefaultDirName={autopf64}\RoTools
DisableDirPage=no
UsePreviousAppDir=no
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputDir=C:\Users\Zayo\Desktop\RoTools\build
OutputBaseFilename=RoTools
SetupIconFile=C:\Users\Zayo\Desktop\RoTools\assets\icon.ico
UninstallDisplayIcon={app}\MultiRoblox.exe
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern

[Files]
Source: "build\Release\MultiRoblox.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "build\Release\WebView2Loader.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "assets\icon.ico"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "assets\info.jpg"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "assets\instrukcja.png"; DestDir: "{app}\assets"; Flags: ignoreversion
Source: "external\WebView2Runtime\*"; DestDir: "{app}\WebView2Runtime"; Flags: ignoreversion recursesubdirs createallsubdirs

[Tasks]
Name: "desktopicon"; Description: "Utworz ikone na pulpicie"; GroupDescription: "Ikony:"; Flags: unchecked

[Icons]
Name: "{autoprograms}\RoTools"; Filename: "{app}\MultiRoblox.exe"; IconFilename: "{app}\assets\icon.ico"
Name: "{autodesktop}\RoTools"; Filename: "{app}\MultiRoblox.exe"; IconFilename: "{app}\assets\icon.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\MultiRoblox.exe"; Description: "Uruchom RoTools"; Flags: nowait postinstall skipifsilent

[Code]
procedure InitializeWizard;
begin
  WizardForm.DirEdit.ReadOnly := True;
  WizardForm.DirBrowseButton.Enabled := False;
  WizardForm.GroupEdit.ReadOnly := True;
  WizardForm.GroupBrowseButton.Enabled := False;
end;
