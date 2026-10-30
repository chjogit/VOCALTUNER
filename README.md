<p align="center">
  <img src="assets/banner.svg" alt="VocalTuner" width="760">
</p>

<p align="center">
  <b>흔들리는 보컬을 곧게.</b><br>
  Studio One에서 바로 쓰는 ARA2 기반 보컬 음정·박자 보정 플러그인
</p>

<p align="center">
  <a href="https://github.com/chjogit/VOCALTUNER/releases/latest"><img src="https://img.shields.io/github/v/release/chjogit/VOCALTUNER?style=for-the-badge&color=21213f&labelColor=d8c29a&label=download" alt="Download"></a>
  <img src="https://img.shields.io/badge/VST3-ARA2-21213f?style=for-the-badge&labelColor=d8c29a" alt="VST3 ARA2">
  <img src="https://img.shields.io/badge/Windows-10%20%7C%2011-21213f?style=for-the-badge&labelColor=d8c29a" alt="Windows">
</p>

---

## ⬇️ 다운로드와 설치

1. [**Releases**](https://github.com/chjogit/VOCALTUNER/releases/latest)에서 `VocalTuner-x.y.z-Windows.zip`을 받습니다.
2. 압축을 풀고 `VocalTuner-x.y.z-Setup.exe`를 실행합니다.
   > "Windows의 PC 보호" 창이 뜨면 **추가 정보 → 실행**을 누르세요. 코드 서명이 없는 개인 배포판이라 나오는 창입니다.
3. 플러그인은 `C:\Program Files\Common Files\VST3\VocalTuner.vst3`에 설치됩니다.
4. Studio One을 다시 시작하고, 보컬 이벤트에 **이벤트 FX(ARA)** 로 VocalTuner를 넣습니다.

제거는 **설정 → 앱 → VocalTuner → 제거**에서 할 수 있습니다.

## ✨ 기능

<table>
<tr>
<td width="50%" valign="top">

### 🎯 블록 편집
- 음정을 자동으로 분석해 **블록** 단위로 나눠 보여줍니다
- 위아래로 끌면 음정, 선택한 블록을 좌우로 끌면 타이밍
- **Ctrl**을 누른 채 끌면 1/10 미세 조정 (스냅 없음)
- 블록을 누르고 있으면 **편집된 소리를 미리 듣기**

</td>
<td width="50%" valign="top">

### 〰️ 곡선 편집
- **음정 흐름**: 느린 음정 쏠림만 없애고 비브라토는 그대로
- **비브라토** 깊이·속도 조절, **펴기**로 평탄하게
- 블록 시작·끝 **전환 핸들**로 음 사이 이음 다듬기
- 편집한 모양을 굽기(bake)로 고정

</td>
</tr>
<tr>
<td valign="top">

### ⚖️ 음정 균형
- 조·장단조를 고르면 블록마다 **가장 가까운 음**으로 정리
- 중심 음정 세기와 5센트 허용 범위 조절
- 새 트랙에는 **기초 보정(중심 음정 5%)** 이 자동으로 켜집니다

</td>
<td valign="top">

### 🧭 가이드 모드
- 원곡 보컬이나 다른 트랙을 **기준**으로 지정
- 박자·음정·이음 정도를 슬라이더로 맞추기
- Whisper 가사 인식으로 단어 위치를 잡아 정렬 정확도 향상
- 여러 트랙을 함께 편집하는 **멀티트랙 연동**

</td>
</tr>
</table>

### 🔊 소리
- WORLD 보코더 기반 재합성으로 **음색을 유지한 채** 음정·타이밍 이동
- 편집하지 않은 구간은 원본 오디오를 그대로 사용
- 큰 음량에서도 깨지지 않도록 음량 매칭과 소프트 리미터 적용
- Studio One 재생·바운스에 편집 결과가 그대로 반영

## ⌨️ 단축키

| 키 | 동작 |
|---|---|
| `Ctrl` + `1` / `2` / `3` | 블록 / 곡선 / 박자 모드 |
| `Ctrl` + `4` | 음정 균형 |
| `Ctrl` + 클릭 | 블록 여러 개 선택 |
| `Ctrl` + 끌기 | 미세 조정 |
| `Alt` + 끌기 | 화면 이동 |
| `Ctrl` + `Z` / `Ctrl` + `Shift` + `Z` | 되돌리기 / 다시 하기 |
| 블록 우클릭 | 한 단계 되돌리기 · 처음으로 되돌리기 |

`Space`, `+`, `-` 는 플러그인이 가로채지 않고 DAW로 그대로 전달됩니다.

## 💻 요구 사항

- Windows 10 / 11 (64비트)
- ARA2를 지원하는 VST3 호스트 — **Studio One 6** 에서 확인
- (선택) 가사 기반 정렬: whisper.cpp 모델 약 141MB 추가 설치

## 🛠️ 직접 빌드하기

Visual Studio 2022(C++ 데스크톱), CMake 3.22 이상이 필요합니다.

```powershell
git clone https://github.com/chjogit/VOCALTUNER.git
cd VOCALTUNER
git clone --depth 1 https://github.com/juce-framework/JUCE.git
git clone --recursive https://github.com/Celemony/ARA_SDK.git
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

결과물은 `build\VocalTuner_artefacts\Release\VST3\VocalTuner.vst3` 입니다.
설치 파일은 [Inno Setup 6](https://jrsoftware.org/isinfo.php)을 설치한 뒤 `installer\build_installer.ps1`로 만듭니다.

JUCE 없이 분석·렌더 코어와 테스트만 빌드하려면 `-DVOCALTUNER_CORE_ONLY=ON`을 붙이고 `ctest`로 확인합니다.

## 🙏 사용한 라이브러리

[JUCE](https://juce.com) · [ARA SDK](https://github.com/Celemony/ARA_SDK) · [WORLD](https://github.com/mmorise/World) · [whisper.cpp](https://github.com/ggerganov/whisper.cpp)

## 📄 라이선스

[GNU AGPLv3](LICENSE). 고쳐서 배포하거나 서비스할 때는 바뀐 소스도 같은 라이선스로 공개해야 합니다.
JUCE(AGPLv3/상용), ARA SDK(Apache 2.0), WORLD(수정 BSD), whisper.cpp(MIT)는 각자의 라이선스를 따릅니다.

VST는 Steinberg Media Technologies GmbH의 상표입니다.

---

<p align="center">
  <img src="assets/logo.svg" width="40" alt=""><br>
  <sub>© 2026 chjogit</sub>
</p>
