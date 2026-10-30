; VocalTuner 설치 파일 (Inno Setup 6)
; 빌드: installer\build_installer.ps1 이 버전과 경로를 넘겨 ISCC로 컴파일한다.
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Vst3Dir
  #define Vst3Dir "..\build\VocalTuner_artefacts\Release\VST3\VocalTuner.vst3"
#endif

[Setup]
AppId={{6E3B1A52-9C1F-4C3E-8B7A-21213F0D8C29}
AppName=VocalTuner
AppVersion={#AppVersion}
AppPublisher=chjogit
AppPublisherURL=https://github.com/chjogit/VOCALTUNER
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={commonpf64}\VocalTuner
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
OutputBaseFilename=VocalTuner-{#AppVersion}-Setup
SetupIconFile=VocalTuner.ico
UninstallDisplayIcon={commonpf64}\VocalTuner\VocalTuner.ico
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#Vst3Dir}\*"; DestDir: "{commoncf64}\VST3\VocalTuner.vst3"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "VocalTuner.ico"; DestDir: "{commonpf64}\VocalTuner"; Flags: ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\VocalTuner.vst3"

[Messages]
korean.FinishedLabel=VocalTuner가 VST3 폴더에 설치되었습니다.%n%nStudio One을 다시 시작한 뒤 플러그인 목록에서 VocalTuner를 찾으세요.
english.FinishedLabel=VocalTuner was installed to the VST3 folder.%n%nRestart your DAW and rescan plugins.
