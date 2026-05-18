# HGDImageGen

PNG image to Geometry Dash `.gmd` level file converter.

Reads a PNG file, maps each pixel to a colored tile object, and outputs a `.gmd` file that can be imported directly into Geometry Dash.

## Dependencies

- [libpng](http://www.libpng.org/) 1.6
- [zlib](https://zlib.net/)

The required DLLs (`libpng16.dll`, `zlib.dll`) are included under `libpng/bin/` and `zlib/lib/`.

## Building

**Requirements**

- Visual Studio 2022 (MSVC v145 toolset)
- Windows SDK 10.0
- x64 platform only
- Runtime library: `/MDd` (Debug) · `/MD` (Release)

**Steps**

1. Open `HGDImageGen.slnx` in Visual Studio.
2. Select `Debug | x64` or `Release | x64`.
3. Build Solution (`Ctrl+Shift+B`).

The output executable is placed in `x64/Debug/` or `x64/Release/`.

## Runtime requirements

Place the following DLLs in the same directory as `HGDImageGen.exe` before running:

```
libpng16.dll
zlib.dll
```

The Visual C++ Redistributable (2022, x64) must also be installed on the target machine.

## Usage

```
HGDImageGen -i <input.png> -o <output.gmd> -n <level name> [options]
```

### Required / primary options

| Option | Description |
|--------|-------------|
| `-i`, `--input <path>` | Input PNG file |
| `-o`, `--output <path>` | Output `.gmd` file (default: `output.gmd`) |
| `-n`, `--level-name <name>` | Level name in Geometry Dash (default: `HGDImageGen`) |

### Resize options

| Option | Description |
|--------|-------------|
| `-W`, `--width <px>` | Target width in pixels |
| `-H`, `--height <px>` | Target height in pixels |
| `--scale <factor>` | Resize by scale factor (ignored if width/height is set) |
| `--filter <name>` | Resize filter: `nearest` (default) or `bilinear` |

If only one of `--width` / `--height` is given, the other dimension is calculated to preserve the aspect ratio.

### Color options

| Option | Description |
|--------|-------------|
| `--grayscale` | Convert image to grayscale before quantization |
| `--color` | Force color mode (default) |
| `--quant-step <1-255>` | RGB quantization step (default: `32`) |
| `--alpha-step <1-255>` | Alpha quantization step (default: `32`) |
| `--alpha-skip-below <0-255>` | Skip pixels whose alpha is below this threshold (default: `1`) |
| `--max-channels <n>` | Maximum number of color channels to generate (default: `999`) |
| `--max-objects <n>` | Maximum number of objects to generate (default: `50000`) |

### Geometry Dash layout options

| Option | Description |
|--------|-------------|
| `--tile-size <px>` | Spacing between objects in GD units (default: `30`) |
| `--start-x <value>` | X position of the first object (default: `15`) |
| `--start-y <value>` | Y margin of the first object (default: `15`) |
| `--object-id <id>` | Geometry Dash object ID to use for tiles (default: `211`) |

### Other options

| Option | Description |
|--------|-------------|
| `--log <path>` | Log file path (default: `hgdimagegen.log`) |
| `--version` | Print version |
| `-h`, `--help` | Show help |

## Examples

Basic conversion:

```
HGDImageGen -i image.png -o image.gmd -n MyLevel
```

Resize to 64×64 before converting:

```
HGDImageGen -i image.png -o image.gmd -n MyLevel --width 64 --height 64
```

Convert at 50% scale with bilinear filtering:

```
HGDImageGen -i image.png -o image.gmd -n MyLevel --scale 0.5 --filter bilinear
```

Grayscale output with finer color quantization:

```
HGDImageGen -i image.png -o image.gmd -n MyLevel --grayscale --quant-step 16
```

## Output

The tool writes two files:

- **`<output>.gmd`** — Geometry Dash level file, importable via the game's level editor.
- **`hgdimagegen.log`** (or the path given by `--log`) — Per-pixel conversion log including quantized colors, channel assignments, and object positions.

## License

See [LICENSE](LICENSE).
