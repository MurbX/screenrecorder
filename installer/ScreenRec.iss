; ScreenRec installer (Inno Setup 6). Built by packaging/package.ps1:
;   ISCC /DAppVersion=1.2.3 /DStageDir=<dir with ScreenRec.exe, ffmpeg.exe, licenses> installer\ScreenRec.iss
;
; Upgrades: AppId must NEVER change. Installing a newer version over an older one upgrades in place,
; closes the running app first and keeps the user's settings (%APPDATA%\ScreenRec) and recordings.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif
#define AppName "ScreenRec"
#define AppExe "ScreenRec.exe"
#define RepoUrl "https://github.com/MurbX/screenrecorder"

[Setup]
AppId={{7A4C33D4-A28B-43CA-93AE-B4BEC63B2FE9}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=ScreenRec contributors
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
AppUpdatesURL={#RepoUrl}/releases
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
; Per-user by default (no admin prompt, silent self-updates); the dialog allows all users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Desktop Duplication + capture exclusion need Windows 10 2004 (build 19041) or newer.
MinVersion=10.0.19041
LicenseFile=..\LICENSE
SetupIconFile=..\res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=..\dist
OutputBaseFilename=ScreenRec-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#StageDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; ScreenRec's own "Launch at login" setting writes this value; remove it on uninstall.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueName: "ScreenRec"; Flags: uninsdeletevalue dontcreatekey

[Run]
Filename: "{app}\{#AppExe}"; Description: "Launch {#AppName}"; Flags: nowait postinstall skipifsilent
; In-app updates run the installer silently; bring the app back afterwards.
Filename: "{app}\{#AppExe}"; Flags: nowait; Check: WizardSilent

[UninstallDelete]
; Thumbnails/temp data only. Settings (%APPDATA%\ScreenRec) and recordings are kept.
Type: filesandordirs; Name: "{localappdata}\ScreenRec"
