# Pluto for StarCraft: Remastered

비공식 호환 프로젝트입니다. Pluto의 공개 CoG 2026 바이너리를 설치된 StarCraft: Remastered **1.23.10.13515 x86**에서 실행하는 Windows 호환 브리지입니다. 원본 모델을 사용한 오프라인 실전 결과와 리플레이는 [검증 기록](validation/RESULTS.md)에 요약합니다. 개인 경기 로그·리플레이·스크린샷과 게임·모델 원본은 공개 저장소에 포함하지 않습니다.

**2026-09-30 멀티플레이 개발판:** 공개 커스텀 방에서 사람과 Pluto가 1대1로 대결하기 위한 실행 옵션을 추가했습니다. 사용자가 진행한 밀리 공개방 3경기의 실행 로그와 리플레이를 확인했습니다. 이후 봇이 시작하지 않은 경기는 유즈맵·FFA·Top vs Bottom 설정으로 확인됐습니다. [진단 기록](validation/SESSION-DIAGNOSIS-20260930.md)을 참고하세요.

**콘텐츠용 1대다 실험판:** `Start Pluto Challenge.cmd`에서 Top vs Bottom·FFA·밀리의 1대다 경기를 허용합니다. 플루토는 혼자 한 팀이어야 합니다. 원본 모델의 다중 상대 유닛 관측과 자동 검사는 통과했지만 실제 다인전 경기는 아직 검증하지 않았습니다. [실험판 검증과 한계](validation/CHALLENGE.md).

원본 Pluto 저장소에는 소스 코드가 공개되어 있지 않습니다. 따라서 이 프로젝트는 모델을 재작성하지 않고 원래 `pluto.dll`, `pluto_infer.exe`, 가중치를 그대로 사용합니다. BWAPI 4.4.0 인터페이스와 Pluto가 직접 참조하는 Brood War 1.16.1 메모리를 리마스터 실행 환경에 연결합니다.

## 실행

게임을 종료한 뒤 **`Start Pluto.cmd`를 더블클릭**합니다. 또는 이 폴더에서 PowerShell로 실행합니다.

```powershell
& .\scripts\start-pluto.ps1
```

기본 경로가 다르면 지정합니다.

```powershell
& .\scripts\start-pluto.ps1 `
  -StarCraft 'D:\StarCraft\x86\StarCraft.exe' `
  -Pluto 'D:\Bots\pluto.dll'
```

Pluto 폴더 구조는 원본 배포본과 같습니다.

```text
pluto.dll
pluto/
  pluto_infer.exe
  pluto_weights.bin
```

게임에서 **Single Player → Expansion → 프로필 선택 → Play Custom**으로 들어가 **Melee / 상대 Computer 1명**을 선택합니다. 내 종족을 Terran, Protoss, Zerg 또는 Random으로 정하면 Pluto가 내 유닛을 조종합니다. 시작 시 `플루토: 준비 완료. 즐거운 경기 되세요!`와 모델 이름이 표시됩니다. 경기를 직접 조작하면 봇의 선택·명령과 섞이므로 관전할 때는 화면 이동만 사용하세요.

게임·Pluto 원본 파일은 수정하지 않습니다. 실행 중 메모리에 브리지가 로드되며, 게임을 종료하면 사라집니다. 실행기 파일과 설정, 로그는 이 프로젝트 폴더 안에 남습니다. 현재 떠 있는 다른 StarCraft 프로세스는 실행기가 종료하지 않습니다.

## 요구 사항과 지원 범위

- 64비트 Windows, AVX2 CPU. 모델 추론은 원본의 64비트 별도 프로세스에서 실행합니다. 원본은 6코어 이상과 약 2 GB의 여유 메모리를 권장합니다.
- 실행 파일은 MSVC C++ x86 런타임을 사용합니다. 이 PC에서는 설치된 런타임으로 로드·실행을 확인했습니다.
- 게임 실행 파일은 **x86/StarCraft.exe, 1.23.10.13515**입니다. x64 실행 파일은 이 브리지의 대상이 아닙니다.
- Pluto는 `md07x02_cog2026_2578600_int8mv` 배포본입니다. 실행기는 게임, DLL, 추론기, 가중치의 SHA-256을 확인하고 다른 버전은 실행을 거부합니다.
- 기본 실행기는 오프라인 1대1 Melee용이며 멀티플레이에서 AI를 실행하지 않습니다. 멀티플레이와 1대다 실험 모드는 각각 별도 실행기로 켭니다. 리플레이·캠페인·UMS·플루토에게 아군이 있는 팀전·유닛을 공유하는 Team Melee는 대상이 아닙니다.
- 현재 실전 확인은 설치된 리마스터 엔진의 클래식 그래픽 화면에서 수행했습니다. HD 그래픽에 필요한 게임 계정·소유권은 브리지와 별개입니다.
- Pluto의 실제 실행 경로를 위한 구현입니다. 일반 BWAPI 봇 전체를 지원하는 대체 BWAPI가 아닙니다.
- 구버전 Pluto의 유닛 표현은 동시 1,700개 제한을 유지합니다. 투사체 목록은 리마스터의 실제 개수에 맞춰 동적으로 변환합니다. 지원 범위를 초과하거나 메모리 검증에 실패하면 브리지는 AI 명령을 중단하고 `bridge.log`에 원인을 기록합니다.

## 공개 커스텀 방 대전: 멀티플레이 개발판

`Start Pluto Multiplayer.cmd`를 실행하거나 다음 명령을 사용합니다.

```powershell
& .\scripts\start-pluto.ps1 -Multiplayer -DrawGraph
```

목표 사용 흐름은 **Multiplayer → Expansion → Battle.net 로그인 → 공개 커스텀 방 생성 → Melee, 실제 플레이어 2명 → 시작**입니다. Pluto를 실행한 PC의 진영을 AI가 조종하고, 상대는 일반 리마스터로 참가합니다. 방 이름은 `Pluto AI 1v1`처럼 상대가 AI 대전임을 알 수 있게 정하면 됩니다. 방 생성과 시작은 사용자가 게임 화면에서 진행하며, 자동 매칭·방 반복 생성 기능은 없습니다.

- `-Multiplayer`는 LAN과 배틀넷의 1대1 Melee 경기 인식을 허용합니다. 상대 Computer 1명인 LAN 시험도 허용합니다.
- 멀티플레이 속도는 게임이 관리합니다. `-SpeedMs` 및 봇의 속도 변경 요청이 게임 속도 테이블을 수정하지 않습니다.
- 원본 `BWRL_STRADDLE=1`을 적용하고 기본 추론 대기 예산을 10ms로 둡니다. 늦은 추론 결과는 이후 프레임으로 넘어갑니다. 별도로 지정한 `BWRL_FRAME_BUDGET_MS`는 유지합니다.
- 게임의 기본 명령 전송 큐를 사용합니다. 상대 PC에는 이 브리지나 BWAPI를 설치하지 않는 구조입니다.
- 실제 네트워크 왕복 지연을 측정한 보정은 아직 아닙니다. 현재 BWAPI 지연값은 게임의 턴 레이트와 지연 설정으로 추정합니다. 명령 적용 시점과 두 클라이언트 동기화 검증이 남아 있습니다.
- 로그·오프닝 기록은 `runtime-multiplayer/`에 별도로 저장됩니다. 시작 시 로그의 `mode`가 `multiplayer_experimental`인지 확인합니다.

현재 빌드·명령 변환·경기 허용 조건 테스트를 통과했고, 사용자 공개방 실전 기록을 확인했습니다. 상대 클라이언트의 내부 상태 대조와 모든 종료·기권 상황을 포괄하는 검증은 수행하지 않았습니다. 검증 상태는 [멀티플레이 기록](validation/MULTIPLAYER.md)을 확인하세요.

## 콘텐츠용 1대다 실험 모드

게임을 종료한 뒤 **`Start Pluto Challenge.cmd`**를 실행합니다. 기본 멀티플레이 실행기는 기존 밀리 1대1 조건을 유지합니다.

```powershell
& .\scripts\start-pluto.ps1 -Multiplayer -Challenge -DrawGraph
```

방을 **Top vs Bottom**으로 만들고 플루토가 조종할 내 슬롯은 한쪽 팀에 혼자, 상대 사람들은 반대쪽 팀에 배치합니다. 1대1·1대2·1대3부터 시작할 수 있으며, 코드의 입장 허용 범위는 총 2~8명입니다. 실제 다인전 성능이나 1대7 완주가 검증됐다는 뜻은 아닙니다. FFA와 밀리에서도 플루토와 동맹인 플레이어가 없으면 허용합니다. FFA에서는 상대끼리도 서로 적일 수 있으므로, 사람들이 협력하는 대결은 Top vs Bottom을 사용하세요.

- 내 슬롯 하나만 플루토가 조종합니다. 상대는 일반 리마스터로 참가합니다.
- 원본 관측 함수가 서로 다른 두 적의 유닛을 동시에 읽는 것은 독립 테스트로 확인했습니다. 여러 적을 한 명으로 합치거나 소유권·자원·안개를 바꾸지 않습니다.
- 플루토와 동맹인 참가자가 있으면 시작하지 않습니다. 경기 중 그런 동맹이 생기면 그 경기의 AI 조종을 중단하고 한국어로 알립니다. 사람끼리의 동맹은 허용합니다.
- 원본은 1대1 모델입니다. 다인전 전투력, 복수 기지 상대 운영, 다른 종족이 섞인 팀 상대 판단, 승률 예측은 검증 전입니다. 상대 종족·오프닝 기록 등 일부 원본 정보는 여전히 대표 상대 한 명을 기준으로 합니다.
- 원본의 자동 기권은 유지합니다. 따라서 다인전 승률 추정이 낮아지면 일찍 기권할 수 있습니다. 추론 계산량도 늘 수 있으므로 우선 1대2로 시험하세요.
- 기록은 `runtime-challenge/`에 저장하여 기존 1대1 오프닝 통계와 구분합니다. `bridge.log`의 `mode`는 `challenge_experimental`입니다.
- 오프라인 컴퓨터 여러 명을 상대로 시험하려면 `-Multiplayer` 없이 `-Challenge`로 실행하고 사용자 지정 밀리에서 상대 컴퓨터를 추가합니다.

플루토에게도 아군이 있는 2대2·3대3은 이 실험판의 범위에 포함되지 않습니다. 원본의 관측 코드가 다른 플레이어를 적으로 분류하므로 아군 표현과 행동 처리를 먼저 확장해야 합니다. 현재 공개 배포에는 학습 소스가 없으므로 다인전 전용 재학습까지 완료한 버전은 아닙니다.

## 속도와 로그

게임 중 주요 Pluto 안내는 한국어로 표시합니다. 준비 완료, 명령 적용 지연, CPU 추론 지연, 종료 예고, 지원하지 않는 방 설정과 승률 그래프 문구가 대상입니다. 원본 경고가 두 줄로 나뉘어 전달되는 경우도 처리하며, 시간·프레임 수·모델 이름은 유지합니다. 예를 들면 `플루토: 명령 적용 지연 6프레임 (학습 기준 4프레임).`으로 표시됩니다. 승률 그래프 값은 원본과 같은 0~1 표현을 유지합니다.

번역은 내 게임 화면에만 적용되며 상대에게 채팅으로 전송하지 않습니다. `bridge.log`의 `bot_text`는 진단을 위해 원문을 유지하고, 아직 등록하지 않은 문구와 상세 오류 내용도 원문을 보존합니다. 실행 옵션 이름(`-Multiplayer` 등)은 그대로입니다. 업데이트한 한글 안내는 게임 종료 후 실행기로 다시 시작하면 적용됩니다. 문자열 검증은 통과했으며 실제 게임 내 한글 렌더링은 다음 실행에서 확인이 필요합니다. [검증 상세](validation/KOREAN-MESSAGES.md).

기본 속도는 원본 Pluto처럼 제한을 해제합니다. 실제 속도는 CPU 추론 시간에 따라 달라집니다. 관전 속도는 다음처럼 설정할 수 있습니다.

```powershell
& .\scripts\start-pluto.ps1 -SpeedMs 42  # 프레임당 42ms
& .\scripts\start-pluto.ps1 -SpeedMs -1  # 게임 기본 속도 유지
& .\scripts\start-pluto.ps1 -VerifyOnly # 게임을 띄우지 않고 파일 검사
& .\scripts\start-pluto.ps1 -DrawGraph  # 모델의 승률 추이 표시
```

`-SpeedMs`는 Pluto가 요청하는 기본 속도보다 우선합니다. 경기 종료 시 원래 게임 속도로 복구합니다. `-DrawGraph` 또는 `BWRL_DRAW=1`은 봇이 그리는 승률 그래프를 클릭을 가로채지 않는 관전 창으로 표시합니다. 클래식 4:3 게임 화면에 맞춰 표시하며, 게임을 다른 창 뒤로 보내거나 경기가 끝나면 숨깁니다. 그래프는 기본적으로 꺼져 있습니다.

`runtime/bridge.log`에는 초기화, 프레임, 실제 전송한 명령, 예외, 경기 종료를 기록합니다. `runtime/pluto.log`와 `runtime/pluto_infer.log`는 원본 봇과 추론기의 로그입니다. 이전 실행 로그는 `runtime/logs/`에 보관하고, 상대별 오프닝 통계는 `runtime/bwapi-data/write/`에 유지합니다. 실행 도중 수동으로 DLL을 교체하지 마세요.

원본 README의 `BWRL_STRADDLE`, `BWRL_FRAME_BUDGET_MS` 환경 변수는 그대로 추론기에 전달됩니다. 모델 서버가 실패하면 원본 Pluto의 정책에 따라 게임 프로세스가 종료될 수 있으며 해당 이유는 Pluto 로그에 남습니다.

원본의 선택적 CPU 벤치마크도 그대로 사용할 수 있습니다. 경기를 종료하고 `pluto/pluto_infer.exe --bench`를 실행하면 원본 추론기가 스레드 설정을 저장합니다. 이 포트의 검증에서는 기본 6스레드를 사용했습니다.

## 문제가 생겼을 때

- `Unsupported or damaged file`: 게임 또는 Pluto 배포본이 지원 해시와 다릅니다. 게임이 업데이트되었다면 주소 프로필의 재검증이 필요합니다.
- `Missing file`: 원본 Pluto 배포본의 DLL·추론기·가중치 세 파일과 폴더 구조를 확인합니다.
- 봇이 시작하지 않음: Expansion → Play Custom, Melee, 상대 Computer 1명인지 확인합니다. 지원하지 않는 오프라인 경기 설정은 `session_skipped`로 기록합니다.
- 일반 멀티플레이 실행기에서 처음부터 가만히 있음: 방 생성의 **게임 방식이 밀리(Melee), 참가자 2명**인지 확인합니다. 2026-09-30 실패 리플레이에서는 유즈맵·FFA·Top vs Bottom이 원인이었습니다. Top vs Bottom과 다인전은 새 `Start Pluto Challenge.cmd`를 사용하고 플루토는 혼자 한 팀에 두세요. 유즈맵은 실험판에서도 지원하지 않습니다. 브리지는 차단 이유를 로컬 화면 및 `session_skipped` 로그에 표시합니다.
- 게임 진행이 느림: 모델의 CPU 추론을 기다리는 동작입니다. `pluto.log`의 추론 응답 시간과 원본의 벤치마크 설정을 확인합니다.
- `C++ failure` 또는 `bridge_exception`: 해당 경기는 AI 명령을 중단합니다. 게임을 종료하고 로그와 재현 조건을 보관합니다.

기본 실행기는 이 PC에 이미 있는 `C:\StarCraft1161\bwapi-data\AI\pluto.dll`을 사용합니다. 게임·봇 바이너리를 이 폴더에 복사할 필요가 없습니다. 제거하려면 게임을 종료한 뒤 이 프로젝트 폴더를 제거하면 됩니다. 게임의 기본 자동 저장 리플레이는 StarCraft의 문서 폴더에 남습니다.

## 빌드

Visual Studio의 C++ 데스크톱 개발 도구와 Windows SDK, CMake가 필요합니다.

```powershell
& .\scripts\build.ps1
```

Win32 Release 빌드와 명령 변환·경기 허용 조건·한글 안내 테스트를 수행한 후 `bin/pluto-scr.dll`, `bin/scr-loader.exe`를 생성합니다. BWAPI 클라이언트 소스와 MinHook은 `vendor/`에 포함했습니다. 별도 Rust 도구나 디스어셈블러는 일반 빌드·실행에 필요하지 않습니다.

`scripts/package.ps1`은 실행 파일·소스·문서·검증 자료를 ZIP으로 묶습니다. 로컬 실행 로그와 오프닝 통계, 게임·모델 원본은 포함하지 않습니다. 유지보수용 `generate_pluto_bindings.py`만 Python의 `pefile`, `capstone` 패키지를 사용합니다.

## 구현

- `src/bridge.cpp`: 검증된 리마스터 프레임 호출 지점에서 Pluto 실행, BWAPI 수명 주기, 채팅·종료·속도 연결.
- `src/legacy_view.cpp`: 게임·플레이어·유닛·스프라이트·투사체 상태를 원본 메모리 형식으로 변환. 디스크의 Pluto DLL은 그대로 두고, 로드된 DLL의 검증된 피연산자 250곳만 호환 메모리로 연결.
- `src/commands.cpp`: 구형 16비트 유닛 ID를 리마스터의 확장 ID로 변환하고 선택·우클릭·대상 지정·수송선 하차 패킷 재구성. 나머지 지원 게임 명령은 원래 순서를 유지.
- `src/snapshot.cpp`: Pluto가 사용하는 BWAPI 4.4.0 게임 인터페이스에 스냅샷 제공.
- `src/scr_profile_13515_x86.h`: 설치된 실행 파일에서 확인한 주소와 구조체 프로필. 게임 업데이트 후 주소를 추측해 계속 실행하지 않습니다.
- `src/overlay.cpp`: 원본 `BWRL_DRAW`의 화면 좌표 도형을 별도 관전 창에 표시.
- `src/messages.cpp`: 실제 관측한 Pluto 안내와 경기 차단 사유를 UTF-8 한국어로 변환. 승률 그래프는 UTF-16으로 변환하여 `DrawTextW`로 표시.

StarCraft의 실행 코드 섹션은 변경하지 않습니다. 프레임 관찰은 Win32 타이머 함수의 호출자를 제한하는 훅을 사용합니다. 게임 명령은 리마스터의 기존 명령 큐에 넣습니다.

## 원본 및 라이선스

- [Pluto README 및 배포본](https://github.com/tscmoo/pluto): 모델과 봇 바이너리는 원저작자의 배포본을 별도로 사용합니다.
- [BWAPI 공식 문서](https://bwapi.github.io/), [BWAPI v4.4.0 소스](https://github.com/bwapi/bwapi/tree/v4.4.0): LGPL v3. 라이선스와 GPL v3 전문, 재빌드 가능한 소스가 `vendor/bwapi/`에 있습니다.
- [MinHook](https://github.com/TsudaKageyu/minhook): BSD 계열 라이선스. `vendor/minhook/LICENSE.txt` 참고.

검증된 실행 파일과 게임 모드를 벗어나는 범위까지 완전 호환한다고 주장하지 않습니다. x64는 지원하지 않으며, HD 렌더링 및 온라인 대전은 별도 검증이 필요합니다.
