# Contributing to ScreenRec

Thanks for helping! Bug reports, ideas and pull requests are all welcome.

## Reporting bugs and requesting features

Use the issue templates on GitHub. For bugs, include your Windows version, monitors (resolution and
scaling), GPU, and the file `%LOCALAPPDATA%\ScreenRec\last_ffmpeg_command.txt` if a recording failed.

## Building

Requirements: Windows 10 2004+ or Windows 11, Visual Studio 2022/2026 with the **Desktop development
with C++** workload (includes CMake and the Windows SDK).

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1          # Release build in build\
powershell -ExecutionPolicy Bypass -File build.ps1 -Config Debug
```

Or open the folder in Visual Studio (it is a CMake project). Recording needs `ffmpeg.exe` next to
`ScreenRec.exe` or on the PATH (`winget install Gyan.FFmpeg`); the build copies it automatically when found.

To build the installer locally, see [docs/RELEASING.md](docs/RELEASING.md).

## Project layout

| Path | What |
|---|---|
| `src/app.*` | App state machine (idle → preparing → countdown → recording → finishing), hotkeys |
| `src/recorder.*`, `src/audio.*` | FFmpeg command building, WASAPI audio capture |
| `src/ui.*`, `src/gfx.*`, `src/icons.*` | Small Direct2D UI toolkit, rendering and vector icons |
| `src/main_window.*`, `src/control_bar.cpp`, `src/area_overlay.cpp` | Main window, recording bar, selection overlay |
| `src/editor.*`, `src/screenshot.*`, `src/toast.cpp` | Screenshot capture, floating preview, editor |
| `src/settings*`, `src/output_window.*`, `src/webcam.*` | Settings, output list/player, webcam |
| `src/updater.*` | Update checks against GitHub Releases |
| `installer/`, `packaging/`, `.github/workflows/` | Installer, release packaging, CI |

## Pull requests

1. Fork, create a branch from `main` (`feature/…` or `fix/…`).
2. Keep changes focused; match the surrounding code style (`.clang-format` and `.editorconfig`:
   4-space indent, 120 columns, `camelCase_` members, `PascalCase` functions).
3. Test what you changed by hand (multi-monitor and high-DPI setups are the usual sources of bugs).
4. Add a line under `[Unreleased]` in `CHANGELOG.md` for user-visible changes.
5. Open the PR; CI must pass before it is merged.

## Code of conduct

Be kind and constructive. We follow the spirit of the
[Contributor Covenant](https://www.contributor-covenant.org/version/2/1/code_of_conduct/).
