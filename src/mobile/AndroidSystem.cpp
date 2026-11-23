#include "AndroidSystem.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJniObject>
#include <QScreen>

#include <cmath>
#endif

namespace android_system {

#ifdef Q_OS_ANDROID

namespace {

QJniObject decorView()
{
    const QJniObject activity = QNativeInterface::QAndroidApplication::context();
    if (!activity.isValid()) return {};
    const QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
    if (!window.isValid()) return {};
    return window.callObjectMethod("getDecorView", "()Landroid/view/View;");
}

}  // namespace

QMargins systemBarInsets()
{
    QMargins m;
    // Android's views are read on its own main thread.
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([&m] {
        const QJniObject decor = decorView();
        if (!decor.isValid()) return;
        const QJniObject insets = decor.callObjectMethod("getRootWindowInsets", "()Landroid/view/WindowInsets;");
        if (!insets.isValid()) return;
        const jint types = QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "systemBars", "()I") |
                           QJniObject::callStaticMethod<jint>("android/view/WindowInsets$Type", "displayCutout", "()I");
        const QJniObject in = insets.callObjectMethod("getInsets", "(I)Landroid/graphics/Insets;", types);
        if (!in.isValid()) return;
        m = QMargins(in.getField<jint>("left"), in.getField<jint>("top"), in.getField<jint>("right"), in.getField<jint>("bottom"));
    }).waitForFinished();
    // Android gives physical pixels; the interface works in device-independent ones.
    const qreal dpr = QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    auto dip = [dpr](int px) { return int(std::ceil(px / dpr)); };
    return QMargins(dip(m.left()), dip(m.top()), dip(m.right()), dip(m.bottom()));
}

void setLightSystemBars(bool light)
{
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([light] {
        const QJniObject activity = QNativeInterface::QAndroidApplication::context();
        if (!activity.isValid()) return;
        const QJniObject window = activity.callObjectMethod("getWindow", "()Landroid/view/Window;");
        if (!window.isValid()) return;
        const QJniObject controller = window.callObjectMethod("getInsetsController", "()Landroid/view/WindowInsetsController;");
        if (!controller.isValid()) return;
        // APPEARANCE_LIGHT_STATUS_BARS (8) | APPEARANCE_LIGHT_NAVIGATION_BARS (16): dark icons for a light background.
        const jint both = 8 | 16;
        controller.callMethod<void>("setSystemBarsAppearance", "(II)V", light ? both : 0, both);
    });
}

#else

QMargins systemBarInsets() { return {}; }
void setLightSystemBars(bool) {}

#endif

}  // namespace android_system
