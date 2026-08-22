# Chatting Server Demo

Windows IOCP 채팅 서버와 DirectX 11 ImGui 클라이언트 예제입니다. 서버는 MySQL에 사용자 자격 증명과 채팅 기록을 저장합니다. 비밀번호는 평문으로 저장하지 않고 16바이트 임의 salt와 PBKDF2-HMAC-SHA256 600,000회 결과만 저장합니다.

## 준비 사항

- Windows x64
- Visual Studio 2022 C++ 데스크톱 빌드 도구
- Docker Desktop 또는 호환 Docker Engine과 Docker Compose
- x64 MySQL Connector/ODBC Unicode 드라이버

ODBC 드라이버 이름은 서버가 `SQLDriversW`로 찾습니다. 여러 버전이 설치된 경우 가장 높은 Unicode 버전을 선택합니다. 특정 이름을 사용하려면 `CHAT_DB_DRIVER`에 등록된 드라이버 이름을 정확히 지정합니다.

## 빌드와 실행

PowerShell에서 Release 패키지를 만든 뒤 서비스를 시작합니다.

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Package-Release.ps1
.\scripts\Start-ChatService.ps1 -Port 8888
```

처음 시작할 때 `.env.local`이 없으면 강한 임의 MySQL 자격 증명을 생성합니다. 이 파일과 실행 PID 기록은 Git 및 Release ZIP에서 제외됩니다. MySQL은 `127.0.0.1:3307`에만 게시되고 데이터는 Docker named volume에 유지됩니다.

서비스를 중지할 때는 다음 명령을 사용합니다.

```powershell
.\scripts\Stop-ChatService.ps1
```

중지 스크립트는 기록된 PID가 실제 `Server.exe`인지 확인하고 서버의 정상 종료를 기다린 뒤 `docker compose down`을 실행합니다. named volume은 삭제하지 않습니다.

클라이언트는 저장소 빌드에서는 `Client\x64\Release\Client.exe`, 패키지에서는 `bin\Client.exe`입니다.

## 직접 실행 환경 변수

서버를 직접 실행할 때 아래 값이 필요합니다.

```text
CHAT_DB_HOST=127.0.0.1
CHAT_DB_PORT=3307
CHAT_DB_NAME=chatdb
CHAT_DB_USER=chatapp
CHAT_DB_PASSWORD=...
CHAT_DB_DRIVER=exact registered Unicode driver name   # 선택
CHAT_SERVER_PORT=8888                          # 선택
```

명령줄의 `--port`가 `CHAT_SERVER_PORT`보다 우선합니다.

```powershell
.\Server\x64\Release\Server.exe --port 8888
```

연결 문자열과 비밀번호는 로그에 출력하지 않습니다.

## 데이터베이스

`Server\Database\schema.sql`은 MySQL 8.4, InnoDB, utf8mb4 전용입니다. 로그인 성공 응답 뒤 최근 채팅 최대 50개가 오래된 순서로 기존 `ChatDelivered` 패킷을 통해 전달됩니다. 별도 방이나 history 패킷 형식은 추가하지 않습니다.

## 테스트

Visual Studio MSBuild로 다음 x64 프로젝트를 Debug와 Release에서 빌드하고 생성된 실행 파일을 실행합니다.

- `tests\ChatProtocolTests.vcxproj`
- `tests\IocpServerTests.vcxproj`
- `tests\ChatPersistenceUnitTests.vcxproj`

Docker Engine과 x64 Unicode ODBC 드라이버가 모두 준비된 환경에서만 실제 MySQL 초기화, 재시작 후 영속성, 중복 사용자, 장애 복구 통합 검증을 수행할 수 있습니다. 단위 테스트 통과를 실제 MySQL 통합 성공으로 간주하지 않습니다.

## Release ZIP 범위

`scripts\Package-Release.ps1`은 Server와 Client 실행 파일, 네 스크립트, Compose 파일, 스키마, README, `.env.example`만 `dist\ChatService-x64-Release.zip`에 넣습니다. ODBC 설치 파일, `.env.local`, PDB, ILK, Docker volume 데이터는 포함하지 않습니다.
