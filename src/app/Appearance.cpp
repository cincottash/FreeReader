#include "Appearance.h"

#include <QFontDatabase>

QJsonObject Appearance::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("text_size_pct")] = textSizePct;
    o[QStringLiteral("line_spacing_pct")] = lineSpacingPct;
    o[QStringLiteral("margin_px")] = marginPx;
    o[QStringLiteral("page_width_em")] = pageWidthEm;
    o[QStringLiteral("columns")] = columns;
    o[QStringLiteral("justify")] = justify;
    o[QStringLiteral("hyphenate")] = hyphenate;
    o[QStringLiteral("font")] = font;
    return o;
}

QStringList Appearance::keys()
{
    return {QStringLiteral("text_size_pct"), QStringLiteral("line_spacing_pct"), QStringLiteral("margin_px"), QStringLiteral("page_width_em"),
            QStringLiteral("columns"),       QStringLiteral("justify"),          QStringLiteral("hyphenate"), QStringLiteral("font")};
}

void Appearance::merge(const QJsonObject& o)
{
    auto num = [&](const char* k, int& v) {
        if (o.value(QLatin1String(k)).isDouble()) v = o.value(QLatin1String(k)).toInt();
    };
    auto flag = [&](const char* k, bool& v) {
        if (o.value(QLatin1String(k)).isBool()) v = o.value(QLatin1String(k)).toBool();
    };
    num("text_size_pct", textSizePct);
    num("line_spacing_pct", lineSpacingPct);
    num("margin_px", marginPx);
    num("page_width_em", pageWidthEm);
    if (o.value(QLatin1String("columns")).isString()) columns = o.value(QLatin1String("columns")).toString();
    flag("justify", justify);
    flag("hyphenate", hyphenate);
    if (o.value(QLatin1String("font")).isString()) font = o.value(QLatin1String("font")).toString();
    clamp();
}

void Appearance::clamp()
{
    textSizePct = qBound(60, textSizePct, 300);
    lineSpacingPct = qBound(100, lineSpacingPct, 250);
    marginPx = qBound(8, marginPx, 240);
    pageWidthEm = qBound(16, pageWidthEm, 80);
    if (columns != QLatin1String("one") && columns != QLatin1String("two")) columns = QStringLiteral("auto");
}

QStringList Appearance::bundledFonts() { return {QStringLiteral("Literata"), QStringLiteral("Atkinson Hyperlegible")}; }

void registerBundledFonts()
{
    static bool done = false;
    if (done) return;
    done = true;
    for (const char* f : {"Literata-Regular", "Literata-Italic", "Literata-Bold", "Literata-BoldItalic", "AtkinsonHyperlegible-Regular",
                          "AtkinsonHyperlegible-Italic", "AtkinsonHyperlegible-Bold", "AtkinsonHyperlegible-BoldItalic"})
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1.ttf").arg(QLatin1String(f)));
}
