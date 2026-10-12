#pragma once
#include "common.h"

// Bakes recording effects into the Windows mouse cursors themselves (yellow halo, or a dot
// pointer). The hardware cursor has no lag, so the halo stays perfectly centered on the pointer
// both on screen and in the capture. Restore() reloads the user's own cursor scheme.
namespace cursorfx {
void Apply(bool halo, bool dot);
void Restore();
void RecoverIfNeeded();   // call at startup: undo a replacement left behind by a crash
POINT VisualOffset(HCURSOR cursor);   // hotspot -> middle of the visible pixels (px)
}  // namespace cursorfx
