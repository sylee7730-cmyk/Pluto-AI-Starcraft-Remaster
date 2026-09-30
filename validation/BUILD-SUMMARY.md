# 공개 배포본 빌드·검증 요약

2026-09-30. Win32 Release, MSVC 14.51, Windows SDK 10.0.26100.0.

- `command-translation`: 통과.
- `session-admission`: 통과. 기존 1대1 및 Challenge의 1대1~1대7, 슬롯 8가지, 동맹·비참가자·미지원 모드 조건을 검사합니다.
- `korean-messages`: 통과. UTF-8, 분리된 안내, 수치·모델 이름 보존과 한국어 해결 안내를 검사합니다.
- 고정된 원본 DLL의 별도 관측 프로브: 통과. 결과는 `challenge-observation-probe.txt`에 있습니다.
- Windows PowerShell 5.1: `start-pluto.ps1 -Multiplayer -Challenge -VerifyOnly` 통과.
- 배포 DLL·실행기 SHA-256은 `SHA256SUMS-multiplayer.txt`와 대조했습니다.

전체 빌드 출력과 개인 경기 로그·리플레이·스크린샷은 공개 저장소에 포함하지 않습니다. 이 요약은 로컬에서 수행한 검증 결과이며, 새 다인전 모드의 실전 완주를 의미하지 않습니다. 빌드는 `scripts/build.ps1`로 재현할 수 있습니다. 실제 경기 검증 범위는 `RESULTS.md`, `MULTIPLAYER.md`, `CHALLENGE.md`를 참고하세요.
