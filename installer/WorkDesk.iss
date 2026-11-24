#ifndef AppVersion
  #define AppVersion "0.2.0"
#endif
#define AppName "WorkDesk"
#define AppPublisher "WorkDesk"
#define StageDir "..\dist\WorkDesk-v" + AppVersion + "-win64"
#define RedistPath "..\dist\redist\vc_redist.x64.exe"

[Setup]
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayName=WorkDesk
UninstallDisplayIcon={app}\workdesk.ico
AppId={{B7D1E81D-45E0-4C6E-9BC9-5C41D5C54A10}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\WorkDesk
DefaultGroupName=WorkDesk
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

; Installer and uninstaller icon
SetupIconFile=..\resources\icons\workdesk.ico

OutputDir=..\dist
OutputBaseFilename=WorkDesk-Setup-v{#AppVersion}-x64
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Types]
Name: "full"; Description: "完整安装（推荐，包含中文 OCR）"
Name: "compact"; Description: "精简安装（不包含 OCR 组件）"
Name: "custom"; Description: "自定义安装"; Flags: iscustom
[Components]
Name: "main"; Description: "WorkDesk 主程序与文件搜索引擎"; Types: full compact custom; Flags: fixed
Name: "ocr"; Description: "OCR 扫描 PDF 识别（Tesseract + 简体中文/英文模型）"; Types: full custom

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加快捷方式："; Flags: unchecked

[Files]
; Deploy the main app and Qt runtime, leaving OCR in a separate optional component.
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "ocr\*"; Components: main; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\ocr\*"; DestDir: "{app}\ocr"; Components: ocr; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#RedistPath}"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{autoprograms}\WorkDesk"; Filename: "{app}\workdesk.exe"; WorkingDir: "{app}"; IconFilename: "{app}\workdesk.ico"
Name: "{autodesktop}\WorkDesk"; Filename: "{app}\workdesk.exe"; WorkingDir: "{app}"; IconFilename: "{app}\workdesk.ico"; Tasks: desktopicon
Name: "{group}\卸载 WorkDesk"; Filename: "{uninstallexe}"; WorkingDir: "{app}"; IconFilename: "{app}\workdesk.ico"

[Run]
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "正在安装 Microsoft Visual C++ 运行库..."; Flags: waituntilterminated; Check: not IsVCRedistInstalled
Filename: "{app}\workdesk.exe"; Description: "启动 WorkDesk"; Flags: postinstall nowait skipifsilent

[Code]
function IsVCRedistInstalled: Boolean;
var
  Installed: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM64,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64',
    'Installed', Installed) and (Installed = 1);
end;
