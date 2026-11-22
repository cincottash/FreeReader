#include "AppData.h"
#include "Commands.h"
#include "MainWindow.h"
#include "ReaderView.h"
#include "Settings.h"
#include "Theme.h"

#include "core/format/FormatPlugin.h"
#include "core/services/BookEngine.h"
#include "core/services/Library.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <functional>

#ifdef Q_OS_LINUX
// An AppImage carries only the X11 ("xcb") and offscreen display plug-ins. Many Wayland desktops tell every Qt
// program to use "wayland", and Qt then aborts instead of falling back, so pick xcb (XWayland) here, before
// QApplication exists.
static bool chooseDisplayPlugin()
{
    const QByteArray asked = qgetenv("QT_QPA_PLATFORM").toLower();
    if (!asked.isEmpty() && !asked.startsWith("wayland")) return true;
    if (!qEnvironmentVariableIsEmpty("DISPLAY")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
        return true;
    }
    fprintf(stderr, "%s needs an X11 display, and none was found (DISPLAY isn't set).\n"
                    "On a Wayland session it runs through XWayland: turn XWayland on in the desktop's settings,\n"
                    "or choose an \"X11\" or \"Xorg\" session at the login screen.\n", APP_NAME);
    return false;
}
#endif

// Waits (running the event loop) until `ready()` or the timeout. False on timeout.
static bool waitFor(const std::function<bool()>& ready, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!ready()) {
        if (t.elapsed() > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

// --screenshot: renders the window off-screen to a PNG and exits. Never shows a dialog or a window: problems are
// reported here and the exit code is non-zero.
static int screenshot(QCommandLineParser& cli, const QString& out)
{
    QTemporaryDir tmp;
    if (!tmp.isValid() || appdata::resolve(tmp.path(), nullptr).isEmpty()) {
        fprintf(stderr, "Can't create a temporary app data folder.\n");
        return 2;
    }
    Settings settings;
    if (cli.isSet(QStringLiteral("columns"))) settings.columns = cli.value(QStringLiteral("columns"));
    if (cli.isSet(QStringLiteral("text-size"))) settings.textSizePct = qBound(60, cli.value(QStringLiteral("text-size")).toInt(), 300);
    settings.showContents = cli.isSet(QStringLiteral("contents"));
    if (cli.isSet(QStringLiteral("theme"))) settings.theme = cli.value(QStringLiteral("theme"));
    core::Library library;
    QString err;
    if (!library.open(QDir(appdata::folder()).filePath(QStringLiteral("library.sqlite")), &err)) {
        fprintf(stderr, "Can't open the library database: %s\n", qPrintable(err));
        return 2;
    }
    MainWindow w(settings, &library, false);
    w.setAttribute(Qt::WA_DontShowOnScreen);
    QSize size(1320, 860);
    if (cli.isSet(QStringLiteral("size"))) {
        const QStringList wh = cli.value(QStringLiteral("size")).split(QLatin1Char('x'));
        if (wh.size() == 2) size = QSize(wh[0].toInt(), wh[1].toInt()).expandedTo(QSize(400, 300));
    }
    w.resize(size);
    w.show();
    const QStringList args = cli.positionalArguments();
    if (!args.isEmpty()) {
        const QString book = args.first();
        if (!QFileInfo::exists(book)) {
            fprintf(stderr, "Can't open %s: the file doesn't exist.\n", qPrintable(book));
            return 2;
        }
        w.openBook(book);
        if (!waitFor([&] { return w.isReading() || !w.lastError().isEmpty(); }, 20000) || !w.lastError().isEmpty()) {
            fprintf(stderr, "Can't open %s: %s\n", qPrintable(book), qPrintable(w.lastError().isEmpty() ? QStringLiteral("timed out") : w.lastError()));
            return 3;
        }
        if (!waitFor([&] { return w.isSettled(); }, 30000)) {
            fprintf(stderr, "The book didn't finish laying out in time.\n");
            return 3;
        }
        QString target;
        if (cli.isSet(QStringLiteral("page"))) target = cli.value(QStringLiteral("page"));
        if (cli.isSet(QStringLiteral("percent"))) target = cli.value(QStringLiteral("percent")) + QLatin1Char('%');
        if (!target.isEmpty()) {
            const QString msg = w.reader()->goToPageOrPercent(target);
            if (!msg.isEmpty()) {
                fprintf(stderr, "%s\n", qPrintable(msg));
                return 2;
            }
            waitFor([&] { return w.isSettled(); }, 30000);
        }
        for (int i = 0; i < cli.value(QStringLiteral("turn")).toInt(); ++i) {
            w.reader()->pages()->nextScreen();
            waitFor([&] { return w.isSettled(); }, 30000);
        }
    }
    if (cli.isSet(QStringLiteral("palette"))) w.runCommand(QStringLiteral("commands"));
    if (cli.isSet(QStringLiteral("theme-list"))) w.runCommand(QStringLiteral("themes"));
    QCoreApplication::processEvents();
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!w.grab().save(out)) {
        fprintf(stderr, "Can't write %s.\n", qPrintable(out));
        return 1;
    }
    return 0;
}

int main(int argc, char* argv[])
{
#ifdef Q_OS_LINUX
    if (!chooseDisplayPlugin()) return 1;
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(APP_NAME));
    QApplication::setOrganizationName(QStringLiteral(APP_ORG));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icon.svg")));
    QFont ui(QStringLiteral("Segoe UI"));
    ui.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    ui.setPointSize(10);
    QApplication::setFont(ui);
    core::registerBuiltInFormats();
    applyTheme(app, Theme::midnight());

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("An e-book reader for DRM-free books."));
    cli.addHelpOption();
    cli.addVersionOption();
    cli.addPositionalArgument(QStringLiteral("book"), QStringLiteral("An EPUB file to open."));
    cli.addOption({QStringLiteral("data-folder"), QStringLiteral("Use this app data folder for this run only."), QStringLiteral("folder")});
    cli.addOption({QStringLiteral("screenshot"), QStringLiteral("Render the window off-screen to a PNG file and exit."), QStringLiteral("file")});
    cli.addOption({QStringLiteral("page"), QStringLiteral("With --screenshot: go to this page."), QStringLiteral("number")});
    cli.addOption({QStringLiteral("percent"), QStringLiteral("With --screenshot: go to this point of the book."), QStringLiteral("percent")});
    cli.addOption({QStringLiteral("turn"), QStringLiteral("With --screenshot: turn this many pages forward."), QStringLiteral("count")});
    cli.addOption({QStringLiteral("size"), QStringLiteral("With --screenshot: window size, e.g. 1320x860."), QStringLiteral("WxH")});
    cli.addOption({QStringLiteral("columns"), QStringLiteral("With --screenshot: one, two or auto."), QStringLiteral("mode")});
    cli.addOption({QStringLiteral("text-size"), QStringLiteral("With --screenshot: text size in percent."), QStringLiteral("percent")});
    cli.addOption({QStringLiteral("contents"), QStringLiteral("With --screenshot: show the contents panel.")});
    cli.addOption({QStringLiteral("palette"), QStringLiteral("With --screenshot: show the command palette.")});
    cli.addOption({QStringLiteral("theme"), QStringLiteral("With --screenshot: use this theme (a key such as midnight, paper, nord)."), QStringLiteral("key")});
    cli.addOption({QStringLiteral("theme-list"), QStringLiteral("With --screenshot: show the theme list.")});
    cli.process(app);

    if (cli.isSet(QStringLiteral("screenshot"))) return screenshot(cli, cli.value(QStringLiteral("screenshot")));

    if (appdata::resolve(cli.value(QStringLiteral("data-folder")), nullptr).isEmpty()) return 0;
    Settings settings;
    settings.load(appdata::settingsFile());
    core::Library library;
    QString err;
    library.open(QDir(appdata::folder()).filePath(QStringLiteral("library.sqlite")), &err);
    MainWindow w(settings, &library, true);
    w.show();
    if (!cli.positionalArguments().isEmpty()) w.openBook(cli.positionalArguments().first());
    return app.exec();
}
