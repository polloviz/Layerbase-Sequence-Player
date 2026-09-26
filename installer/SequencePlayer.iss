; Inno Setup 6/7 script for Layerbase Sequence Player
; Build: ISCC.exe installer\SequencePlayer.iss   (or run build.ps1)

#define AppName "Layerbase Sequence Player"
#define AppVersion "1.2.0"
#define AppExe "SequencePlayer.exe"
#define AppPublisher "Layerbase Luxury Vision"
#define AppURL "https://layerbase.it"
#define BuildDir "..\build\Release"

[Setup]
AppId={{7C4B8E1A-3F2D-4B7E-9A51-6E2F0C9D8B34}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
AppCopyright=(c) 2026 {#AppPublisher} - Freeware
AppComments=Freeware image sequence player - {#AppURL}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} Setup
VersionInfoVersion={#AppVersion}
VersionInfoCopyright=(c) 2026 {#AppPublisher}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=..\dist
OutputBaseFilename=LayerbaseSequencePlayer-{#AppVersion}-Setup
SetupIconFile=..\res\app.ico
WizardSmallImageFile=..\res\logo.png
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install by default (no admin); the user may choose all-users.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
ChangesAssociations=yes
CloseApplications=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; LicenseFile: "..\LICENSE.txt"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"; LicenseFile: "..\LICENSE_IT.txt"

[CustomMessages]
english.AssocTask=Register supported image formats (appear in "Open with", default for EXR/DPX when unassigned)
italian.AssocTask=Registra i formati supportati (compaiono in "Apri con", predefinito per EXR/DPX se non assegnati)
english.AssocGroup=File associations:
italian.AssocGroup=Associazioni file:
english.DefaultAppsTask=Open Windows "Default apps" to choose Layerbase Sequence Player as default
italian.DefaultAppsTask=Apri "App predefinite" di Windows per scegliere Layerbase Sequence Player come predefinito
english.Website=Visit layerbase.it
italian.Website=Visita layerbase.it

[Tasks]
Name: "assoc"; Description: "{cm:AssocTask}"; GroupDescription: "{cm:AssocGroup}"
Name: "defaultapps"; Description: "{cm:DefaultAppsTask}"; GroupDescription: "{cm:AssocGroup}"; Flags: unchecked
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#BuildDir}\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE_IT.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\res\THIRD_PARTY_NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\FEATURES.md"; DestDir: "{app}"; Flags: ignoreversion
; FFmpeg for movie export (GPLv3, separate program: see third_party\ffmpeg\FFMPEG_README.txt).
Source: "..\third_party\ffmpeg\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\ffmpeg\FFMPEG_LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\third_party\ffmpeg\FFMPEG_README.txt"; DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Shortcuts of 1.1 and earlier, named "Sequence Player"
Type: files; Name: "{autoprograms}\Sequence Player.lnk"
Type: files; Name: "{autodesktop}\Sequence Player.lnk"

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#AppExe}"; Comment: "{#AppName} - {#AppPublisher}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Parameters: "--register"; Flags: runhidden waituntilterminated; Tasks: assoc
Filename: "ms-settings:defaultapps?registeredAppUser=SequencePlayer"; Flags: shellexec postinstall skipifsilent nowait; Tasks: defaultapps; Description: "{cm:DefaultAppsTask}"
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
Filename: "{#AppURL}"; Description: "{cm:Website}"; Flags: shellexec postinstall skipifsilent nowait unchecked

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--unregister"; Flags: runhidden waituntilterminated; RunOnceId: "UnregisterAssoc"
