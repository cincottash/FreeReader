#include "OfflineTranslator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QTimer>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace core {

namespace {

// The first file in `dir` matching `pattern`, or empty.
QString findFile(const QDir& dir, const QString& pattern)
{
    const QStringList found = dir.entryList({pattern}, QDir::Files, QDir::Name);
    return found.isEmpty() ? QString() : dir.absoluteFilePath(found.first());
}

}  // namespace

OfflineTranslator::OfflineTranslator(QObject* parent) : TranslationProvider(parent) {}

void OfflineTranslator::setFolder(const QString& folder) { m_folder = folder; }

QString OfflineTranslator::program() const
{
#ifdef Q_OS_WIN
    return QDir(m_folder).absoluteFilePath(QStringLiteral("freereader-translator.exe"));
#else
    return QDir(m_folder).absoluteFilePath(QStringLiteral("freereader-translator"));
#endif
}

bool OfflineTranslator::hasProgram() const { return !m_folder.isEmpty() && QFileInfo(program()).isFile(); }

bool OfflineTranslator::hasPair(const QString& from, const QString& to) const
{
    if (m_folder.isEmpty() || from.isEmpty() || to.isEmpty() || from == to) return false;
    const QDir dir(QDir(m_folder).absoluteFilePath(from + QLatin1Char('-') + to));
    return !findFile(dir, QStringLiteral("model.*.bin")).isEmpty() && !findFile(dir, QStringLiteral("vocab.*.spm")).isEmpty();
}

QStringList OfflineTranslator::pairs() const
{
    QStringList out;
    if (m_folder.isEmpty()) return out;
    for (const QString& d : QDir(m_folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QStringList two = d.split(QLatin1Char('-'));
        if (two.size() == 2 && hasPair(two[0], two[1])) out << d;
    }
    return out;
}

bool OfflineTranslator::isInstalled() const { return hasProgram() && !pairs().isEmpty(); }

namespace {
// The pair a request asks for: the book's language (Spanish when not known, for an English reader; English for a
// Spanish one) into the reader's.
std::pair<QString, QString> pairOf(const TranslationRequest& request)
{
    const QString to = request.to.isEmpty() ? QStringLiteral("en") : request.to;
    const QString from = !request.from.isEmpty() ? request.from : to == QLatin1String("en") ? QStringLiteral("es") : QStringLiteral("en");
    return {from, to};
}
}  // namespace

bool OfflineTranslator::handles(const TranslationRequest& request) const
{
    if (request.kind == TranslationRequest::Kind::Explain) return false;
    const auto [from, to] = pairOf(request);
    return hasPair(from, to);
}

QString OfflineTranslator::configFor(const QString& from, const QString& to) const
{
    if (m_folder.isEmpty()) return {};
    const QDir dir(QDir(m_folder).absoluteFilePath(from + QLatin1Char('-') + to));
    const QString model = findFile(dir, QStringLiteral("model.*.bin"));
    const QString lex = findFile(dir, QStringLiteral("lex.*.bin"));
    const QString vocab = findFile(dir, QStringLiteral("vocab.*.spm"));
    const QString prefix = findFile(dir, QStringLiteral("nonbreaking_prefix.*"));
    if (model.isEmpty() || vocab.isEmpty()) return {};
    // Bergamot's settings for Mozilla's models (the values Firefox uses), with this computer's paths.
    QString yml;
    yml += QStringLiteral("models:\n  - \"%1\"\n").arg(model);
    yml += QStringLiteral("vocabs:\n  - \"%1\"\n  - \"%1\"\n").arg(vocab);
    if (!lex.isEmpty()) yml += QStringLiteral("shortlist:\n  - \"%1\"\n  - false\n").arg(lex);
    yml += QStringLiteral("beam-size: 1\nnormalize: 1.0\nword-penalty: 0\nmax-length-break: 128\nmini-batch-words: 1024\nworkspace: 128\n"
                          "max-length-factor: 2.0\nskip-cost: true\nquiet: true\nquiet-translation: true\n"
                          "gemm-precision: int8shiftAlphaAll\nssplit-mode: paragraph\n");
    if (!prefix.isEmpty()) yml += QStringLiteral("ssplit-prefix-file: \"%1\"\n").arg(prefix);
    const QString path = dir.absoluteFilePath(QStringLiteral("config.yml"));
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && QString::fromUtf8(existing.readAll()) == yml) return path;
    existing.close();
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(yml.toUtf8()) < 0 || !f.commit()) return {};
    return path;
}

void OfflineTranslator::translate(const TranslationRequest& request, Done done)
{
    const auto [from, to] = pairOf(request);
    const QString config = isSetUp() ? configFor(from, to) : QString();
    if (config.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done({}, TranslationFailure::NotSetUp, {}); });
        return;
    }
    // Paragraphs are sent with a blank line between them and come back the same way; Bergamot keeps them apart.
    QStringList parts = request.kind == TranslationRequest::Kind::Paragraphs ? request.paragraphs : QStringList{request.text};
    for (QString& p : parts) p = p.simplified();
    const QByteArray input = parts.join(QStringLiteral("\n\n")).toUtf8() + '\n';

    auto* proc = new QProcess(this);
#ifdef Q_OS_WIN
    // A console program started from a window: no console window flashing up on the screen.
    proc->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    proc->setProgram(program());
    proc->setArguments({QStringLiteral("--model-config-paths"), config, QStringLiteral("--cpu-threads"), QStringLiteral("2")});
    proc->setWorkingDirectory(m_folder);
    auto timer = new QTimer(proc);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, proc, [proc] { proc->kill(); });
    const QString name = this->name();
    connect(proc, &QProcess::finished, this, [this, proc, request, parts, done, name](int code, QProcess::ExitStatus status) {
        // Windows console programs end lines with \r\n.
        const QString out = QString::fromUtf8(proc->readAllStandardOutput()).replace(QLatin1String("\r\n"), QLatin1String("\n")).trimmed();
        proc->deleteLater();
        if (status != QProcess::NormalExit || code != 0 || out.isEmpty()) {
            done({}, TranslationFailure::Failed, QStringLiteral("exit %1").arg(code));
            return;
        }
        TranslationResult r;
        r.text = request.text.trimmed();
        r.provider = name;
        if (request.kind == TranslationRequest::Kind::Paragraphs) {
            QStringList back = out.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts);
            for (QString& b : back) b = b.trimmed();
            if (back.size() != parts.size()) {
                done({}, TranslationFailure::Failed, QStringLiteral("%1 paragraphs back for %2").arg(back.size()).arg(parts.size()));
                return;
            }
            r.paragraphs = back;
            r.translation = back.first();
        } else {
            r.translation = out.simplified();
        }
        done(r, TranslationFailure::None, {});
    });
    connect(proc, &QProcess::errorOccurred, this, [proc, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;   // a crash or a kill also ends in finished()
        proc->deleteLater();
        done({}, TranslationFailure::Failed, QStringLiteral("can't start"));
    });
    proc->start();
    proc->write(input);
    proc->closeWriteChannel();
    timer->start(60000);
}

}  // namespace core
