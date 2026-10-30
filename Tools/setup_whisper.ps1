# Whisper(whisper.cpp) 설치: 레퍼런스 박자 보정의 가사 앵커용 (선택). 한 번만 실행.
# 모델을 바꾸려면 -Model small (더 정확, 느림) / tiny (더 빠름)
param([string]$Model = "base")
$dir = Join-Path $env:APPDATA "VocalTuner\whisper"
New-Item -ItemType Directory -Force $dir | Out-Null
$zip = Join-Path $dir "whisper-bin-x64.zip"
Invoke-WebRequest "https://github.com/ggml-org/whisper.cpp/releases/download/b5454/whisper-bin-x64.zip" -OutFile $zip
Expand-Archive $zip -DestinationPath $dir -Force
Get-ChildItem $dir -Recurse -Filter "whisper-cli.exe" | Select-Object -First 1 | ForEach-Object {
    if ($_.DirectoryName -ne $dir) { Copy-Item "$($_.DirectoryName)\*" $dir -Force }
}
Invoke-WebRequest "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-$Model.bin" -OutFile (Join-Path $dir "ggml-$Model.bin")
Remove-Item $zip
Write-Host "완료: $dir"
