#include "Settings.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {

// One table drives load and save, so the two never disagree. Unknown keys in the file are ignored and missing
// keys keep their defaults, so old or newer settings files never stop the app from starting.
template <typename F>
void fields(Settings& s, F&& f)
{
    f("geometry", s.geometry);
    f("show_contents", s.showContents);
    f("show_notebook", s.showNotebook);
    f("show_bilingual", s.showBilingual);
    f("show_practice", s.showPractice);
    f("text_size_pct", s.look.textSizePct);
    f("line_spacing_pct", s.look.lineSpacingPct);
    f("margin_px", s.look.marginPx);
    f("page_width_em", s.look.pageWidthEm);
    f("columns", s.look.columns);
    f("justify", s.look.justify);
    f("hyphenate", s.look.hyphenate);
    f("font", s.look.font);
    f("theme", s.theme);
    f("recent_themes", s.recentThemes);
    f("reading_speed", s.readingSpeed);
    f("footer_shows_pages", s.footerPagesLeft);
    f("highlight_order", s.highlightOrder);
    f("library_list", s.libraryList);
    f("library_sort", s.librarySort);
    f("library_zoom_pct", s.libraryZoomPct);
    f("page_turn", s.pageTurn);
    f("page_turn_ms", s.pageTurnMs);
    f("page_turn_motion", s.pageTurnMotion);
    f("translate_on_select", s.translateOnSelect);
    f("offline_books", s.offlineBooks);
    f("page_directions", s.pageDirections);
    f("use_gemini", s.useGemini);
    f("gemini_model", s.geminiModel);
    f("use_lm_studio", s.useLmStudio);
    f("lm_studio_address", s.lmStudioAddress);
    f("lm_studio_model", s.lmStudioModel);
    f("use_dictionary", s.useDictionary);
    f("use_offline_translator", s.useOfflineTranslator);
    f("practice_directions", s.practiceDirections);
    f("practice_typed", s.practiceTyped);
    f("practice_sentence_front", s.practiceSentenceFront);
    f("practice_passages", s.practicePassages);
    f("practice_highlights", s.practiceHighlights);
    f("practice_new_per_day", s.practiceNewPerDay);
    f("practice_order", s.practiceOrder);
    f("last_folder", s.lastFolder);
    f("shortcuts", s.shortcuts);
    f("mouse_shortcuts", s.mouseShortcuts);
    f("library_folders", s.libraryFolders);
}

void get(const QJsonObject& o, const char* k, QString& v) { if (o.value(QLatin1String(k)).isString()) v = o.value(QLatin1String(k)).toString(); }
void get(const QJsonObject& o, const char* k, bool& v) { if (o.value(QLatin1String(k)).isBool()) v = o.value(QLatin1String(k)).toBool(); }
void get(const QJsonObject& o, const char* k, int& v) { if (o.value(QLatin1String(k)).isDouble()) v = o.value(QLatin1String(k)).toInt(); }
void get(const QJsonObject& o, const char* k, QByteArray& v)
{
    if (o.value(QLatin1String(k)).isString()) v = QByteArray::fromBase64(o.value(QLatin1String(k)).toString().toLatin1());
}
void get(const QJsonObject& o, const char* k, QStringList& v)
{
    if (!o.value(QLatin1String(k)).isArray()) return;
    v.clear();
    for (const QJsonValue& x : o.value(QLatin1String(k)).toArray()) v << x.toString();
}

void put(QJsonObject& o, const char* k, const QString& v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, bool v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, int v) { o[QLatin1String(k)] = v; }
void put(QJsonObject& o, const char* k, const QByteArray& v) { o[QLatin1String(k)] = QString::fromLatin1(v.toBase64()); }
void put(QJsonObject& o, const char* k, const QStringList& v) { o[QLatin1String(k)] = QJsonArray::fromStringList(v); }

}  // namespace

bool Settings::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    fields(*this, [&](const char* k, auto& v) { get(o, k, v); });
    // Offline Only (before 2026-11-23) was the same as Use Gemini off: a file saved with it on keeps Gemini off.
    if (o.value(QLatin1String("offline_only")).toBool()) useGemini = false;
    // Keep values sane even if the file was edited by hand.
    look.clamp();
    readingSpeed = qBound(200, readingSpeed, 6000);
    practiceNewPerDay = qBound(0, practiceNewPerDay, 500);
    libraryZoomPct = qBound(50, libraryZoomPct, 250);
    pageTurnMs = qBound(100, pageTurnMs, 1000);
    if (!QStringList{"none", "slide", "cover", "fade", "curl"}.contains(pageTurn)) pageTurn = QStringLiteral("none");
    if (!QStringList{"smooth", "even", "gentle"}.contains(pageTurnMotion)) pageTurnMotion = QStringLiteral("smooth");
    {
        QStringList keep;
        for (const QString& d : practiceDirections)
            if (QStringList{"recognize", "recall", "definition", "cloze"}.contains(d) && !keep.contains(d)) keep << d;
        practiceDirections = keep.isEmpty() ? QStringList{QStringLiteral("recognize")} : keep;
    }
    while (recentThemes.size() > 4) recentThemes.removeLast();
    return true;
}

bool Settings::save(const QString& path) const
{
    QJsonObject o;
    fields(const_cast<Settings&>(*this), [&](const char* k, auto& v) { put(o, k, v); });
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}
