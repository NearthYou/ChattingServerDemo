# 검증 기록

## 기준

- 기준 브랜치: `main`
- 기준 커밋: `24ef492425ac4b8f496bbb8b584b3ad95998c78d`
- 운영체제: Windows
- 빌드 도구: Visual Studio 2022 MSBuild 17.14
- 구성: `Debug|x64`

## 빌드

Client와 Server 프로젝트는 `v142` 도구 집합을 지정합니다. 현재 환경에는 해당 도구 집합이 없어 명령행에서 설치된 `v143`을 사용했습니다. 프로젝트 파일은 수정하지 않았습니다.

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  Client\Client.sln /m /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal

& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' `
  Server\Server.sln /m /p:Configuration=Debug /p:Platform=x64 /p:PlatformToolset=v143 /v:minimal
```

두 빌드 모두 성공했습니다.

- Client: `Client/x64/Debug/Client.exe`
- Server: `Server/x64/Debug/Server.exe`
- Client 경고: `send` 길이의 `size_t`에서 `int` 변환 3건
- Server 경고: 코드 페이지 경고 1건, `send` 길이 변환 경고 3건

## 서버 실행

빈 포트 입력으로 서버를 실행해 기본값 8888을 사용했습니다. 수신 소켓은 정상적으로 열렸고 다음 메시지까지 확인했습니다.

```text
Server is listening on port 8888...
```

그다음 로컬 `ChatDB` 연결에서 ODBC 오류 `08001`이 발생했고 프로세스는 종료 코드 1로 끝났습니다. 현재 환경에는 저장소가 기대하는 SQL Server 데이터베이스가 없습니다.

## 검증하지 않은 부분

- 실제 SQL Server를 사용한 회원가입과 로그인
- 두 개 이상의 클라이언트 사이 채팅
- 채팅 기록 저장과 조회
- 동시 접속 안정성

저장소에는 자동화된 테스트가 없습니다. 문서에서 위 흐름을 구현과 검증 완료로 구분해 적은 이유입니다.
