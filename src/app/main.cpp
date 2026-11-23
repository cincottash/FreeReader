#include "AppData.h"
#include "Appearance.h"
#include "Commands.h"
#include "LibraryView.h"
#include "MainWindow.h"
#include "ThemePicker.h"
#include "ReaderView.h"
#include "Settings.h"
#include "Theme.h"

#include "core/format/FormatPlugin.h"
#include "core/services/BookEngine.h"
#include "core/services/Library.h"
#include "core/services/Secrets.h"
#include "core/translate/AiProviders.h"

#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QSslSocket>
#include <QTextStream>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFont>
#include <QTemporaryDir>
#include <QTimer>

#ifdef HAVE_TEXT_TO_SPEECH
#include <QTextToSpeech>
#include <QVoice>
#endif

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

// --check-translation: one real lookup through Gemini with the saved key (one of today's free lookups), the
// answer written to a file, then exit. Shows nothing; for checking a build can reach Gemini.
static int checkTranslation(const QString& out)
{
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "Can't write %s\n", qPrintable(out));
        return 2;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "TLS backends: " << QSslSocket::availableBackends().join(QStringLiteral(", ")) << " (active: " << QSslSocket::activeBackend() << ")\n";
    QNetworkAccessManager net;
    core::GeminiProvider gemini(&net);
    gemini.setKey(core::secrets::read(QStringLiteral("gemini-api-key")));
    ts << "Gemini key saved: " << (gemini.hasKey() ? "yes" : "no") << ", model " << gemini.model() << "\n";
    if (!gemini.hasKey()) return 3;
    core::TranslationRequest r;
    r.text = QStringLiteral("biblioteca");
    r.sentence = QStringLiteral("Pasó la tarde en la biblioteca.");
    r.from = QStringLiteral("es");
    int code = 4;
    QElapsedTimer t;
    t.start();
    gemini.translate(r, [&](const core::TranslationResult& res, core::TranslationFailure fail, const QString& detail) {
        ts << "Answer in " << t.elapsed() << " ms: " << (fail == core::TranslationFailure::None ? QStringLiteral("ok") : core::failureText(fail) + QStringLiteral(" ") + detail) << "\n";
        ts << QString::fromUtf8(QJsonDocument(res.toJson()).toJson(QJsonDocument::Indented)) << "\n";
        code = fail == core::TranslationFailure::None ? 0 : 5;
        QCoreApplication::quit();
    });
    QTimer::singleShot(40000, qApp, [] { QCoreApplication::exit(6); });
    const int loop = QCoreApplication::exec();
    return loop == 6 ? 6 : code;
}

// --check-speech: the speech engines this build has, and each one's languages and voices, written to a file. Says
// nothing aloud and shows nothing; for checking a build (and CI) can read words aloud on this system.
static int checkSpeech(const QString& out)
{
    QFile f(out);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        fprintf(stderr, "Can't write %s\n", qPrintable(out));
        return 2;
    }
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
#ifdef HAVE_TEXT_TO_SPEECH
    const QStringList engines = QTextToSpeech::availableEngines();
    ts << "Speech engines: " << (engines.isEmpty() ? QStringLiteral("none") : engines.join(QStringLiteral(", "))) << "\n";
    for (const QString& e : engines) {
        QTextToSpeech tts(e);
        // Some engines (speech-dispatcher) report their voices a moment after they start.
        waitFor([&] { return !tts.availableLocales().isEmpty() || tts.state() == QTextToSpeech::Error; }, 3000);
        ts << e << ": state " << int(tts.state()) << (tts.state() == QTextToSpeech::Error ? QStringLiteral(" (error: ") + tts.errorString() + QLatin1Char(')') : QString()) << "\n";
        for (const QLocale& l : tts.availableLocales()) {
            tts.setLocale(l);
            QStringList voices;
            for (const QVoice& v : tts.availableVoices()) voices << v.name();
            ts << "  " << l.name() << ": " << voices.join(QStringLiteral(", ")) << "\n";
        }
    }
    return engines.isEmpty() ? 3 : 0;
#else
    ts << "Speech engines: none (built without Qt TextToSpeech)\n";
    return 3;
#endif
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
    if (cli.isSet(QStringLiteral("columns"))) settings.look.columns = cli.value(QStringLiteral("columns"));
    if (cli.isSet(QStringLiteral("text-size"))) settings.look.textSizePct = qBound(60, cli.value(QStringLiteral("text-size")).toInt(), 300);
    if (cli.isSet(QStringLiteral("font"))) settings.look.font = cli.value(QStringLiteral("font"));
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
    if (cli.isSet(QStringLiteral("add-books"))) {
        w.library()->addBooks({cli.value(QStringLiteral("add-books"))});
        if (!waitFor([&] { return !w.library()->isImporting(); }, 60000)) {
            fprintf(stderr, "Adding books didn't finish in time.\n");
            return 3;
        }
        w.library()->setMessage(QString());   // pictures show the library, not the import report
    }
    if (cli.isSet(QStringLiteral("list-view"))) w.library()->setListMode(true);
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
            w.reader()->forgetHistory();
        }
        for (int i = 0; i < cli.value(QStringLiteral("turn")).toInt(); ++i) {
            w.reader()->pages()->nextScreen();
            waitFor([&] { return w.isSettled(); }, 30000);
        }
    }
    if (cli.isSet(QStringLiteral("palette"))) w.runCommand(QStringLiteral("commands"));
    if (cli.isSet(QStringLiteral("theme-list"))) w.runCommand(QStringLiteral("themes"));
    if (cli.isSet(QStringLiteral("appearance"))) w.runCommand(QStringLiteral("appearance"));
    QCoreApplication::processEvents();
    QElapsedTimer settle;
    settle.start();
    while (settle.elapsed() < 300) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (cli.isSet(QStringLiteral("theme-list")) && w.themePicker()) {
        w.themePicker()->stopPreview();
        settle.restart();
        while (settle.elapsed() < 400) QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
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
    registerBundledFonts();
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
    cli.addOption({QStringLiteral("add-books"), QStringLiteral("With --screenshot: add the books in this folder to the library first."), QStringLiteral("folder")});
    cli.addOption({QStringLiteral("list-view"), QStringLiteral("With --screenshot: show the library as a list.")});
    cli.addOption({QStringLiteral("appearance"), QStringLiteral("With --screenshot: show the text and layout panel.")});
    cli.addOption({QStringLiteral("font"), QStringLiteral("With --screenshot: use this font for the text."), QStringLiteral("family")});
    cli.addOption({QStringLiteral("check-speech"), QStringLiteral("Write the speech engines, languages and voices to this file and exit."), QStringLiteral("file")});
    cli.addOption({QStringLiteral("check-translation"), QStringLiteral("Look up one word with Gemini (the saved key), write the answer to this file and exit."), QStringLiteral("file")});
    cli.process(app);

    if (cli.isSet(QStringLiteral("check-translation"))) return checkTranslation(cli.value(QStringLiteral("check-translation")));
    if (cli.isSet(QStringLiteral("check-speech"))) return checkSpeech(cli.value(QStringLiteral("check-speech")));
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
