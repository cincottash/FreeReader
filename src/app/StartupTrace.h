// Where each start and close of the app got to: one line a stage, with the process and the time, in
// FreeReader-startup.log in the temporary folder (started again when it passes 256 KB). The owner saw the app start
// with no window after closing it (2026-11-26); this shows the last stage a stuck copy reached. Plain C file
// calls: it works before Qt is up.
// Only in builds made with the CMake option STARTUP_TRACE (local diagnostic builds): in a release it does nothing
// (the owner's choice, 2026-11-26).
#pragma once

void startupTrace(const char* stage);
