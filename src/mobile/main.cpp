// FreeReader for phones and tablets.
//
// Test mode (desktop builds): --data-folder <folder> keeps everything in a folder of its own; --screenshot <png>
// renders the window off-screen and exits, never showing anything (with --book <epub> to add and open a book,
// --page N to turn N pages, --lookup to long-press a word, --size WxH, --theme paper|sepia|night, --screen
// library|reader|settings). Problems are reported on the command line, never in a dialog.
#include "BookPage.h"
#include "MobileApp.h"

#include "app/Appearance.h"
#include "core/format/FormatPlugin.h"
#include "core/services/Library.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFont>
#include <QFileInfo>
#include <functional>
#include <QEventLoop>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickImageProvider>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <cstdio>

namespace {

// Book covers for the library: image://covers/<book id>.
class CoverProvider : public QQuickImageProvider {
public:
    explicit CoverProvider(core::Library* library) : QQuickImageProvider(QQuickImageProvider::Image), m_library(library) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requested) override
    {
        QImage img;
        if (m_library) img.loadFromData(m_library->cover(id.section(QLatin1Char('?'), 0, 0).toInt()));
        if (!img.isNull() && requested.isValid()) img = img.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (size) *size = img.size();
        return img;
    }

private:
    core::Library* m_library;
};

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()>& done, int ms)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) wait(20);
    return done();
}

int fail(const QString& message)
{
    std::fprintf(stderr, "%s\n", qPrintable(message));
    return 3;
}

}  // namespace

int main(int argc, char** argv)
{
    // The screenshot mode is drawn in software, off-screen.
    bool screenshot = false;
    for (int i = 1; i < argc; ++i)
        if (QByteArray(argv[i]) == "--screenshot") screenshot = true;
    if (screenshot) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral(APP_NAME));
    QGuiApplication::setOrganizationName(QStringLiteral(APP_ORG));
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({QStringLiteral("data-folder"), QStringLiteral("Keep the app's data in this folder."), QStringLiteral("folder")});
    cli.addOption({QStringLiteral("screenshot"), QStringLiteral("Render the window to a PNG file and exit."), QStringLiteral("file")});
    cli.addOption({QStringLiteral("book"), QStringLiteral("With --screenshot: add and open this book."), QStringLiteral("epub")});
    cli.addOption({QStringLiteral("page"), QStringLiteral("With --screenshot: turn this many pages."), QStringLiteral("count")});
    cli.addOption({QStringLiteral("lookup"), QStringLiteral("With --screenshot: long-press a word on the page.")});
    cli.addOption({QStringLiteral("size"), QStringLiteral("With --screenshot: window size, e.g. 412x915."), QStringLiteral("WxH")});
    cli.addOption({QStringLiteral("theme"), QStringLiteral("With --screenshot: paper, sepia or night."), QStringLiteral("key")});
    cli.addOption({QStringLiteral("screen"), QStringLiteral("With --screenshot: library, reader, settings or menu."), QStringLiteral("name")});
    cli.process(app);

    QString folder = cli.value(QStringLiteral("data-folder"));
    if (folder.isEmpty()) folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (screenshot && cli.value(QStringLiteral("data-folder")).isEmpty()) return fail(QStringLiteral("--screenshot needs --data-folder"));

    core::registerBuiltInFormats();
    registerBundledFonts();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // The interface font: the system's own (Roboto on Android, Segoe UI on Windows), else the bundled one.
    QFont ui = QGuiApplication::font();
    ui.setFamilies({QStringLiteral("Roboto"), QStringLiteral("Segoe UI"), QStringLiteral("Noto Sans"), QStringLiteral("Atkinson Hyperlegible")});
    QGuiApplication::setFont(ui);

    MobileApp mobile(folder);
    if (!mobile.isOpen()) return fail(mobile.message());
    if (cli.isSet(QStringLiteral("theme"))) mobile.setTheme(cli.value(QStringLiteral("theme")));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("covers"), new CoverProvider(mobile.library()));
    engine.setInitialProperties({{QStringLiteral("app"), QVariant::fromValue(&mobile)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(4); }, Qt::QueuedConnection);
    engine.loadFromModule("FreeReaderMobile", "Main");
    if (engine.rootObjects().isEmpty()) return fail(QStringLiteral("The interface couldn't be loaded."));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) return fail(QStringLiteral("No window."));

    if (!screenshot) return app.exec();

    // ---- the screenshot mode
    const QStringList size = cli.value(QStringLiteral("size")).split(QLatin1Char('x'));
    window->resize(size.size() == 2 ? size[0].toInt() : 412, size.size() == 2 ? size[1].toInt() : 915);
    window->show();
    wait(100);
    if (cli.isSet(QStringLiteral("book"))) {
        const QString path = QDir::fromNativeSeparators(cli.value(QStringLiteral("book")));
        if (!QFileInfo::exists(path)) return fail(QStringLiteral("No such book: ") + path);
        const int before = mobile.bookCount();
        mobile.importBook(QUrl::fromLocalFile(path));
        if (!waitFor([&] { return !mobile.isBusy(); }, 30000) || mobile.bookCount() <= before)
            return fail(QStringLiteral("The book couldn't be added: ") + mobile.message());
        if (cli.value(QStringLiteral("screen")) != QLatin1String("library")) {
            const int id = mobile.books()->data(mobile.books()->index(0, 0), BookListModel::IdRole).toInt();
            mobile.openBook(id);
            if (!waitFor([&] { return mobile.isReading(); }, 30000)) return fail(QStringLiteral("The book couldn't be opened: ") + mobile.message());
            BookPage* page = nullptr;
            if (!waitFor([&] {
                    page = window->findChild<BookPage*>();
                    return page && page->isReady();
                }, 30000))
                return fail(QStringLiteral("The page wasn't laid out."));
            for (int i = 0; i < cli.value(QStringLiteral("page")).toInt(); ++i) {
                page->nextPage();
                waitFor([&] { return page->isReady(); }, 10000);
            }
            wait(100);
            if (cli.isSet(QStringLiteral("lookup"))) {
                // The first word a little below the top of the page.
                for (qreal y = 80; y < page->height() - 40 && !page->hasSelection(); y += 24)
                    page->selectWordAt(page->width() / 2, y);
                if (!page->hasSelection()) return fail(QStringLiteral("No word to select."));
                mobile.lookUp(page);
                waitFor([&] { return !mobile.lookup().value(QStringLiteral("loading")).toBool(); }, 15000);
            }
        }
    }
    const QString screen = cli.value(QStringLiteral("screen"));
    if (screen == QLatin1String("settings") || screen == QLatin1String("menu"))
        QMetaObject::invokeMethod(window, "showScreen", Q_ARG(QVariant, screen));
    wait(400);
    const QImage img = window->grabWindow();
    if (img.isNull() || !img.save(cli.value(QStringLiteral("screenshot")))) return fail(QStringLiteral("The picture couldn't be saved."));
    return 0;
}
