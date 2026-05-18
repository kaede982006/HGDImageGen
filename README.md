# HGDImageGen

PNG 이미지를 Geometry Dash 레벨 파일(`.gmd`)로 변환하는 CLI 도구입니다.

각 픽셀을 GD 오브젝트 하나로 매핑하고, 픽셀 색상을 GD 색상 채널로 자동 할당하여 이미지를 레벨 내에 재현합니다.

## 빌드

**요구사항:** Visual Studio 2022, x64

```
HGDImageGen.slnx를 Visual Studio에서 열기
구성: Debug | x64 또는 Release | x64
빌드 → 솔루션 빌드
```

런타임 라이브러리는 정적 링크로 설정되어 있습니다 (Debug: `/MTd`, Release: `/MT`).

의존 라이브러리(libpng, zlib)는 `libpng/`, `zlib/` 디렉토리에 포함되어 있습니다.

## 사용법

```
HGDImageGen -i <input.png> -o <output.gmd> -n <level name> [options]
```

### 기본 예시

```
HGDImageGen -i photo.png -o photo.gmd -n "My Photo"
HGDImageGen -i photo.png -o small.gmd -n "Small" --width 64 --height 64
HGDImageGen -i photo.png -o gray.gmd -n "Gray" --grayscale
HGDImageGen -i photo.png -o scaled.gmd -n "Half" --scale 0.5
```

## 옵션

### 필수 옵션

| 옵션 | 설명 |
|------|------|
| `-i, --input <path>` | 입력 PNG 파일 |

### 출력 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `-o, --output <path>` | `output.gmd` | 출력 .gmd 파일 경로 |
| `-n, --level-name <name>` | `HGDImageGen` | GD 레벨 이름 |
| `--log <path>` | `hgdimagegen.log` | 로그 파일 경로 |

### 리사이즈 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `-W, --width <px>` | - | 출력 이미지 너비 (한 쪽만 지정 시 비율 유지) |
| `-H, --height <px>` | - | 출력 이미지 높이 (한 쪽만 지정 시 비율 유지) |
| `--scale <factor>` | - | 배율 조정 (width/height 미설정 시 적용) |
| `--filter <name>` | `nearest` | 리사이즈 필터: `nearest` 또는 `bilinear` |

### 색상 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--grayscale` | - | 그레이스케일로 변환 |
| `--color` | - | 컬러 모드 (기본값) |
| `--quant-step <1-255>` | `32` | RGB 색상 양자화 단계 (값이 클수록 색상 수 감소) |
| `--alpha-step <1-255>` | `32` | 알파 양자화 단계 |
| `--alpha-skip-below <0-255>` | `1` | 이 값 미만의 알파를 가진 픽셀은 건너뜀 |
| `--max-channels <n>` | `999` | 최대 생성 색상 채널 수 |
| `--max-objects <n>` | `50000` | 최대 생성 오브젝트 수 |

### GD 레이아웃 옵션

| 옵션 | 기본값 | 설명 |
|------|--------|------|
| `--tile-size <px>` | `30` | 오브젝트 간격 (GD 단위) |
| `--start-x <value>` | `15` | 첫 번째 오브젝트 X 위치 |
| `--start-y <value>` | `15` | 첫 번째 오브젝트 Y 여백 |
| `--object-id <id>` | `211` | 사용할 GD 오브젝트 ID |

## 동작 방식

1. PNG를 RGBA로 불러옵니다.
2. 지정된 크기로 리사이즈합니다 (필요한 경우).
3. 그레이스케일 변환을 적용합니다 (옵션).
4. 픽셀 색상을 양자화하여 색상 수를 줄입니다.
5. 고유한 양자화 색상마다 GD 색상 채널을 할당합니다.
6. 투명도 임계값 이상의 모든 픽셀에 대해 GD 타일 오브젝트를 생성합니다.
7. 레벨 문자열을 gzip 압축 후 base64url로 인코딩하여 `.gmd` plist XML 파일로 저장합니다.

## 색상 채널 초과 오류 대처법

`color channel count exceeded N` 오류가 발생하면 색상 수를 줄여야 합니다.

- `--quant-step` 값을 높입니다 (예: `32` → `64`)
- `--grayscale` 옵션을 사용합니다
- `--width` / `--height`로 이미지 크기를 줄입니다

## 의존성

- [libpng](http://www.libpng.org/) — PNG 파일 읽기
- [zlib](https://zlib.net/) — gzip 압축

## 라이선스

MIT License — [LICENSE](LICENSE) 참고
