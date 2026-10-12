# Changelog

All notable changes to ScreenRec are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/). The release workflow publishes the section for each
version as its GitHub release notes, so write entries for users.

## [Unreleased]

## [1.0.0] - 2026-10-12

First public release.

### Added
- Recording modes: selected area (with handles and size presets), full screen (with a screen picker
  on multi-monitor setups), window, webcam only and audio only.
- Records a 4K screen at 1080p (or any max resolution) at up to 60 FPS, encoded on the GPU
  (NVIDIA NVENC, Intel Quick Sync, AMD AMF) with a CPU fallback.
- System audio and microphone recording with live level meters, pause/resume and a countdown.
- Webcam overlay bubble (circle or rounded rectangle, mirror, border, shadow).
- Lag-free yellow cursor highlight and click ripples baked into the recording.
- Drawing tools while recording (pen, highlighter, shapes, arrows, text).
- Screenshots: area, window or full screen, instant capture, PNG/JPG, copy to clipboard.
- Floating preview after every capture: drag it into any app, click to open it.
- Screenshot editor: arrows, shapes, text, numbered steps, callouts, spotlight, magnifier, blur,
  pixelate, crop, undo/redo, copy image or path, drag out.
- Output window with an embedded player, thumbnails, trim, rename, copy and delete.
- Multi-monitor and mixed-DPI support, global hotkeys, dark UI inspired by Bandicam for Mac.
- Installer with in-place upgrades and in-app update checks via GitHub Releases.

[Unreleased]: https://github.com/MurbX/screenrecorder/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/MurbX/screenrecorder/releases/tag/v1.0.0
