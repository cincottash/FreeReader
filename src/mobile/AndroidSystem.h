// What the touch app needs from Android itself: the room the status and navigation bars take (from Android 15 on,
// apps draw under them), and dark or light icons in those bars to suit the theme. Elsewhere: nothing.
#pragma once

#include <QMargins>

namespace android_system {

// The system bars' size in device-independent pixels; empty until the window is shown, and off Android.
QMargins systemBarInsets();
// Dark icons on a light theme (`light`), light icons on a dark one.
void setLightSystemBars(bool light);

}  // namespace android_system
