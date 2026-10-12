#pragma once
#include "common.h"

// Starts an OLE drag of a file (like dragging it from Explorer), so it can be dropped into chat
// apps, browsers, email or folders. Shows a small preview of the image under the
// cursor while dragging. Blocks until the drop or cancel; returns true if it was dropped somewhere.
bool DragFileOut(const std::wstring& path);

// True once the mouse has moved far enough from `start` (screen px) to count as a drag.
bool PastDragThreshold(POINT start, POINT now);
