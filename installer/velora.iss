; Velora per-user installer (Inno Setup 6). No administrator rights. Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.0.0 /DStageDir=..\dist\stage installer\velora.iss
; The uninstaller restores the Windows desktop first (shell exits, then velora-watchdog --restore).

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage"
#endif

[Setup]
AppId={{37CB4853-A7DC-4D8F-BFA0-777AD2D49D3B}
AppName=Velora
AppVersion={#AppVersion}
AppVerName=Velora {#AppVersion}
AppPublisher=Velora contributors
AppPublisherURL=https://github.com/moelsaka01/velora
AppSupportURL=https://github.com/moelsaka01/velora/issues
AppUpdatesURL=https://github.com/moelsaka01/velora/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Velora
DefaultGroupName=Velora
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=Velora-{#AppVersion}-setup
SetupIconFile=velora.ico
UninstallDisplayIcon={app}\velora-shell.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
CloseApplications=no
RestartApplications=no

[Files]
Source: "{#StageDir}\velora-shell.exe";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\velora-watchdog.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\velora-files.exe";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\velora-settings.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE";                  DestDir: "{app}"; Flags: ignoreversion
Source: "..\NOTICE";                   DestDir: "{app}"; Flags: ignoreversion
Source: "..\THIRD_PARTY_NOTICES.md";   DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Velora\Velora";                   Filename: "{app}\velora-shell.exe"
Name: "{autoprograms}\Velora\Velora Settings";          Filename: "{app}\velora-settings.exe"
Name: "{autoprograms}\Velora\Restore Windows Desktop";  Filename: "{app}\velora-watchdog.exe"; Parameters: "--restore"; Comment: "Brings back the taskbar, wallpaper and window styles if Velora is not responding"

[Run]
Filename: "{app}\velora-shell.exe"; Description: "Start Velora now"; Flags: nowait postinstall skipifsilent unchecked

[UninstallRun]
; 1. leave Velora Mode normally, 2. restore Windows whatever state is left (idempotent), before any file is removed.
Filename: "{app}\velora-shell.exe";    Parameters: "--exit";    Flags: runhidden waituntilterminated; RunOnceId: "VeloraExit"
Filename: "{app}\velora-watchdog.exe"; Parameters: "--restore"; Flags: runhidden waituntilterminated; RunOnceId: "VeloraRestore"

[Code]
// Upgrade: leave Velora Mode before the files are replaced (the running shell would otherwise lock them).
function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  ResultCode: Integer;
  Shell: String;
begin
  Result := '';
  Shell := ExpandConstant('{app}\velora-shell.exe');
  if FileExists(Shell) then
    Exec(Shell, '--exit', '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

// Uninstall: the user's settings, state and logs stay unless they say otherwise (a silent uninstall keeps them).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and (not UninstallSilent) then
    if MsgBox('Also delete your Velora settings and logs (' + ExpandConstant('{localappdata}\Velora') + ')?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      DelTree(ExpandConstant('{localappdata}\Velora'), True, True, True);
end;
