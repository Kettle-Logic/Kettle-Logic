; Inno Setup script for the Windows installer. Build it with apps\windows\make-installer.ps1, which passes the
; defines below. Inno Setup 7 is required (https://jrsoftware.org/isinfo.php).
;
; The in-app update (apps/windows/KettleLogic/AppUpdates.cpp) runs this installer with /SILENT, /CLOSEAPPLICATIONS,
; and /RESTARTAPPLICATIONS. Keep those switches working, and keep the AppId and the rules in docs/updates.md:
;   - the AppId never changes, so a new installer upgrades the program in place;
;   - user data is never deleted: recipes live in %USERPROFILE%\KettleLogic\Recipes, and settings and the user's
;     libraries live in %LOCALAPPDATA%\KettleLogic. Neither is under the install folder, and neither is listed below.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\..\build\windows-app\x64\Release"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\..\dist"
#endif
#ifndef Arch
  #define Arch "x64"
#endif

#define AppName "Kettle Logic"
#define AppExe "KettleLogic.exe"

[Setup]
; A fixed identity for this program. Generated once; do not change it.
AppId={{6F1C2A8E-4B7D-4E0A-9C3B-5D2E8F1A7B60}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Kettle Logic
AppPublisherURL=https://github.com/Kettle-Logic/Kettle-Logic
AppSupportURL=https://github.com/Kettle-Logic/Kettle-Logic/issues
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\KettleLogic
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Installs for everyone (Program Files) when run as an administrator, and offers a per-user install otherwise.
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed={#Arch}
ArchitecturesInstallIn64BitMode={#Arch}
; Closes the app if it is open, and reopens it after an update. The in-app update depends on both.
CloseApplications=yes
RestartApplications=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
OutputDir={#OutputDir}
OutputBaseFilename=KettleLogic-Setup-{#AppVersion}-{#Arch}
; Signing is off unless make-installer.ps1 is given a sign command. When it is given one, the installer and its
; uninstaller are both signed, and the in-app update checks the installer's signature.
#ifdef SignCommand
SignTool=signer
SignedUninstaller=yes
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
; The program, its Windows App SDK runtime (the app is self-contained), and the Help folder beside it.
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
; Not run after a silent install, so an update reopens the app through RestartApplications only.
Filename: "{app}\{#AppExe}"; Description: "Open {#AppName}"; Flags: nowait postinstall skipifsilent

; There is no [UninstallDelete] section. Uninstalling removes the program files and nothing else. Recipes and settings
; stay, so a reinstall or an upgrade finds them.
