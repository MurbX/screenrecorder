#pragma once
#include "gfx.h"

enum class Icon {
    SelectArea, Monitor, Window, Webcam, Audio,
    Cursor, Camcorder, Speaker, SpeakerOff, Mic, MicOff, Annotate,
    Gear, Folder, List, Pause, Stop, Record, Close, Minimize, Play, Chevron,
    Pointer, Pen, Highlighter, Line, Arrow, Rect, Ellipse, Text, Width, Undo, Trash,
    Scissors, Copy, Rename, Camera, Keyboard, Overlay, Check, Dot, Link,
    Step, Blur, Pixelate, Spotlight, Magnify, Callout, Crop, Redo, Save, Grip, Fill,
};

// Draws a 24x24-unit vector icon fitted (centered) into `box`.
void DrawIcon(gfx::Painter& p, Icon icon, D2D1_RECT_F box, D2D1_COLOR_F color, float stroke = 1.8f);
