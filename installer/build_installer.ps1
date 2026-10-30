# VocalTuner 설치 파일 + 배포 zip 만들기
# 사용: powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1 -Version 0.1.0 -Vst3Dir <VocalTuner.vst3 경로>
param(
    [string]$Version = "0.1.0",
    [string]$Vst3Dir = "$PSScriptRoot\..\build\VocalTuner_artefacts\Release\VST3\VocalTuner.vst3"
)
$ErrorActionPreference = "Stop"

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw "Inno Setup 6가 없습니다: winget install JRSoftware.InnoSetup" }
if (-not (Test-Path "$Vst3Dir\Contents\x86_64-win\VocalTuner.vst3")) { throw "VST3 빌드가 없습니다: $Vst3Dir" }

$out = Join-Path $PSScriptRoot "Output"
& $iscc "/DAppVersion=$Version" "/DVst3Dir=$((Resolve-Path $Vst3Dir).Path)" "/O$out" "$PSScriptRoot\VocalTuner.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC 실패 ($LASTEXITCODE)" }

$setup = Join-Path $out "VocalTuner-$Version-Setup.exe"
$stage = Join-Path $out "VocalTuner-$Version-Windows"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item $stage -ItemType Directory | Out-Null
Copy-Item $setup $stage
Copy-Item "$PSScriptRoot\설치 방법.txt" $stage
$zip = "$stage.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "완료: $zip"
