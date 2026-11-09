#define MyAppName "Codex Voice Input · 机器人版"
#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#define MyAppPublisher "Codex Voice Input Robot"
#define MyAppExeName "CodexVoiceInput.exe"

[Setup]
AppId={{2FA71D1B-2EC8-4BC5-AB7D-73256BE381C9}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={localappdata}\Programs\CodexVoiceInputRobot
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
OutputDir=..\dist\releases
OutputBaseFilename=RobotToCodex-Setup-{#MyAppVersion}
SetupIconFile=..\src\VoiceInput.App\Assets\codex-voice-input.ico
LicenseFile=..\LICENSE
Compression=lzma2/normal
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
AppMutex=Local\CodexVoiceInputRobot.SingleInstance
UninstallDisplayIcon={app}\{#MyAppExeName}
VersionInfoVersion={#MyAppVersion}
VersionInfoDescription={#MyAppName}
ShowLanguageDialog=no
LanguageDetectionMethod=none

[Languages]
Name: "zhcn"; MessagesFile: "languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: checkedonce

[Files]
Source: "..\dist\CodexVoiceInputRobot\*"; DestDir: "{app}"; Excludes: "*.pdb,*.log,*.key,.env,.env.*,*.tmp"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--open-settings"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; AppUserModelID: "Xiaozhi.CodexVoiceInputRobot"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Parameters: "--open-settings"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; AppUserModelID: "Xiaozhi.CodexVoiceInputRobot"

[Run]
Filename: "{app}\{#MyAppExeName}"; Parameters: "--open-settings"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
