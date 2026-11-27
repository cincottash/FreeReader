// What the touch app needs from Android itself: the room the status and navigation bars take (from Android 15 on,
// apps draw under them), and dark or light icons in those bars to suit the theme. Elsewhere: nothing.
#pragma once

#include <QMargins>
#include <QString>

namespace android_system {

// The system bars' size in device-independent pixels; empty until the window is shown, and off Android.
QMargins systemBarInsets();
// Dark icons on a light theme (`light`), light icons on a dark one.
void setLightSystemBars(bool light);
// Android's share sheet with this text (email, Drive, Keep, messages…); false off Android (nothing is shown).
bool shareText(const QString& chooserTitle, const QString& subject, const QString& text, const QString& mimeType);

}  // namespace android_system
