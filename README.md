# Chatting Server Demo

채팅 서버 데모 프로젝트입니다.

## 프로젝트 구조

```
ChattingServerDemo/
├── Common/                     # 공통으로 사용되는 파일들
│   ├── PacketDefine.h         # 패킷 타입과 구조체 정의
│   └── Utils/                 # 유틸리티 함수들
│
├── Server/                     # 서버 관련 파일들
│   ├── Core/                  # 서버 코어 기능
│   ├── Database/             # 데이터베이스 관련
│   └── Network/              # 네트워크 관련
│
├── Client/                     # 클라이언트 관련 파일들
│   ├── Core/                  # 클라이언트 코어 기능
│   ├── Network/              # 네트워크 관련
│   └── UI/                   # 사용자 인터페이스
│
└── Tests/                     # 테스트 코드
```

## 빌드 방법

1. MS-SQL Server 설치
2. ChatDB 데이터베이스 생성
3. Visual Studio로 솔루션 열기
4. 빌드 및 실행

## 기능

- 사용자 등록 및 로그인
- 실시간 채팅
- 채팅 기록 저장 및 조회
- GUI 기반 클라이언트

## 개발 환경

- Windows 10
- Visual Studio 2019 이상
- MS-SQL Server
- ImGui (UI 라이브러리) 