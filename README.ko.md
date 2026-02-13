# Geany SFTP Plugin

**언어**: [中文](README.md) | [English](README.en.md) | **한국어** | [日本語](README.ja.md)

Geany IDE용 원격 SFTP 파일 관리 플러그인. C, GTK+3, libssh2로 구현.

## 기능

- 다중 서버 SFTP 연결 관리 (비밀번호 & 키 인증)
- JSON 설정 저장 (json-glib)
- 사이드바 원격 파일 트리 브라우저
- 실시간 진행률 표시 및 취소 지원 비동기 파일 업로드/다운로드
- 스레드 안전 전송 (GMutex + g_atomic)
- 외부 diff 도구와 파일 동기화 (meld, kdiff3)
- 저장 시 자동 업로드
- 숨김 파일 표시/숨김
- Geany 메뉴 및 사이드바 통합

## 스크린샷

*추가 예정*

## 의존성

**필수**:
- GCC 4.8+
- Geany 1.36+
- GTK+3 개발 패키지
- libssh2 1.8+ 개발 패키지
- GLib2 개발 패키지
- json-glib 개발 패키지
- Make

**선택사항**: meld, kdiff3

## 지원 플랫폼

- Linux (Ubuntu/Debian, Fedora/RHEL, Arch, openSUSE)
- macOS (Homebrew)
- Windows (MSYS2)

## 빌드 및 설치

**자동 설치** (OS 자동 감지 및 의존성 설치):
```bash
./install.sh
```

**수동 설치**:
```bash
# Ubuntu/Debian
sudo apt-get install build-essential geany libgeany-dev libgtk-3-dev libssh2-1-dev libglib2.0-dev libjson-glib-dev

# Fedora/RHEL
sudo dnf install gcc make geany geany-devel gtk3-devel libssh2-devel glib2-devel json-glib-devel

# Arch
sudo pacman -S base-devel geany gtk3 libssh2 glib2 json-glib

# macOS
brew install geany gtk+3 libssh2 glib json-glib pkg-config

# 빌드 & 설치
make
sudo make install
```

## 사용법

1. **플러그인 활성화**: 도구 → 플러그인 관리자 → "SFTP Client" 체크
2. **연결 설정**: 플러그인 관리자 → SFTP Client → 설정
3. **파일 브라우징**: 사이드바 "Remote Files" 패널 사용
4. **파일 전송**: 더블클릭으로 다운로드, 업로드 버튼 사용
5. **동기화**: 우클릭 → "원격 버전과 동기화"

## 프로젝트 구조

```
sftp-plugin.h   - 헤더/타입 정의
compat.h        - 크로스 플랫폼 호환 레이어
sftp-plugin.c   - 플러그인 진입점, Geany API 통합
connection.c    - SFTP 연결, 비동기 전송
config.c        - JSON 설정 (json-glib)
ui.c            - GTK+3 UI, 진행률 대화상자
sync.c          - 파일 동기화 및 diff
Makefile        - 빌드 시스템 (Linux/macOS/Windows)
install.sh      - 설치 스크립트 (배포판 자동 감지)
```

## 라이선스

GPLv2

---

버전 1.0.0 | ~3000줄 C 코드