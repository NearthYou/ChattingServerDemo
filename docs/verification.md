# 검증 기록

## 기준

- 기준 브랜치: `main`
- 반영한 기준 커밋: `2ef01dd`
- 운영체제: Windows
- 빌드 도구: Visual Studio 2022 MSBuild 17.14
- 구성: `Debug|x64`, `Release|x64`

2026년 8월 23일 문서 브랜치에 최신 `main`을 병합한 상태에서 아래 검증을 새로 실행했습니다.

## 빌드

Client와 Server solution을 Debug와 Release에서 각각 빌드했습니다. 별도 test project인 protocol과 persistence도 두 구성으로 빌드했습니다.

| 대상 | Debug | Release |
| --- | --- | --- |
| Client | exit 0 | exit 0 |
| Server | exit 0 | exit 0 |
| ChatProtocolTests | exit 0 | exit 0 |
| IocpServerTests | exit 0 | exit 0 |
| ChatPersistenceUnitTests | exit 0 | exit 0 |

## 자동화된 테스트

```powershell
.\tests\x64\Debug\ChatProtocolTests.exe
.\tests\x64\Release\ChatProtocolTests.exe
.\Server\x64\Debug\IocpServerTests.exe
.\Server\x64\Release\IocpServerTests.exe
.\tests\x64\Debug\ChatPersistenceUnitTests.exe
.\tests\x64\Release\ChatPersistenceUnitTests.exe
pwsh -File .\tests\ScriptsContractTests.ps1
```

모든 실행이 종료 코드 0으로 끝났습니다.

- protocol과 client network integration: 두 구성 통과
- IOCP server: 두 구성에서 각 14개 scenario 통과
- persistence unit: 두 구성에서 각 7개 묶음 통과
- PowerShell과 Compose contract: 통과

IOCP scenario에는 100개 client의 exact delivery와 shutdown deadline, fragmented 및 coalesced frame, 인증 identity spoof 거부, database queue overflow와 persisted history가 포함됩니다.

## Release package

```powershell
pwsh -File .\scripts\Package-Release.ps1
```

Server와 Client Release 빌드가 완료됐고 `dist\ChatService-x64-Release.zip`이 생성됐습니다. `dist`와 build output은 검증 산출물이며 문서 커밋에 포함하지 않습니다.

## 실제 MySQL service E2E

이미 실행 중이던 MySQL 8.4 container를 그대로 두고, 현재 빌드한 Server를 `127.0.0.1:8890`에 별도로 시작했습니다. 기존 secret은 process 안에서만 상속했고 출력하거나 문서에 기록하지 않았습니다.

무작위 8자리 run ID로 다음 세 phase를 실행했습니다.

| phase | 확인 내용 | 결과 |
| --- | --- | --- |
| write | 두 계정 가입, 로그인, 양방향 메시지와 timestamp | exit 0 |
| read | Server 재시작 뒤 두 메시지 history 복원 | exit 0 |
| offline | 종료된 endpoint에 두 번 재접속하고 worker 정리 | exit 0 |

이 검증은 같은 PC의 loopback service입니다. 별도 LAN 장치, Windows 방화벽과 router 경계는 확인하지 않았습니다.

write와 read가 끝난 test 계정으로 현재 Release client에 로그인해 `docs/assets/chat-timeline.png`를 캡처했습니다. 화면에는 server에서 복원한 두 계정의 message, 작성 시간과 좌우 bubble이 함께 나타납니다. 로그인과 가입 화면은 대표 자료로 사용하지 않았습니다.

## 발견한 운영 경계

검증 전에 이미 실행 중이던 `127.0.0.1:8888` Server는 TCP 연결은 받았지만 새 registration을 모두 실패시켰습니다. 같은 MySQL의 schema, 권한과 직접 query는 정상이었고 test username은 DB에 삽입되지 않았습니다.

현재 빌드한 Server를 같은 DB에 새로 연결하자 전체 E2E가 통과했습니다. 이 결과는 오래 열린 process에서 ODBC 연결이 끊긴 뒤 자동 복구하는 경로가 아직 검증되지 않았음을 보여줍니다.

기존 8888 process와 volume은 중지하거나 변경하지 않았습니다. 문서 branch 작업 중 시작한 8890 process만 검증 뒤 종료했습니다.
