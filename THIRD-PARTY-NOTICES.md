# Third-party notices

ScreenRec itself is licensed under the MIT License (see `LICENSE`).

## FFmpeg

ScreenRec installers and portable builds include an unmodified copy of **FFmpeg** (`ffmpeg.exe`), which
ScreenRec runs as a separate program to capture and encode recordings.

- Project: https://ffmpeg.org
- Build: the "full" Windows build by Gyan Doshi, https://www.gyan.dev/ffmpeg/builds/
  (binary release: https://github.com/GyanD/codexffmpeg/releases)
- License: this build is licensed under the **GNU General Public License v3**. The full license text is
  included as `licenses/FFmpeg-LICENSE.txt` next to `ffmpeg.exe`.
- Source code: available from https://ffmpeg.org/download.html and, for the exact build, from the
  build's release page above (source archives and the list of included libraries).

FFmpeg is a trademark of Fabrice Bellard, originator of the FFmpeg project.

## Windows platform components

ScreenRec uses APIs that ship with Windows (Direct3D 11, Direct2D, DirectWrite, DirectComposition,
Media Foundation, WASAPI, WIC). No redistributable libraries are required.
