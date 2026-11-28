#include "AppearancePanel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

QPushButton* chip(const QString& text, const char* testId)
{
    auto* b = new QPushButton(text);
    b->setObjectName(QStringLiteral("Chip"));
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    b->setProperty("testId", testId);
    return b;
}

QLabel* rowLabel(const QString& text)
{
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("Muted"));
    return l;
}

QLabel* valueLabel(const char* testId)
{
    auto* l = new QLabel;
    l->setMinimumWidth(56);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    l->setProperty("testId", testId);
    return l;
}

}  // namespace

AppearancePanel::AppearancePanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Pop"));
    setAttribute(Qt::WA_StyledBackground);
    setFocusPolicy(Qt::StrongFocus);
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 10, 16, 14);
    lay->setSpacing(10);
    auto* title = new QLabel(tr("TEXT & LAYOUT"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    title->setStyleSheet(QStringLiteral("padding: 4px 0 0 0;"));
    lay->addWidget(title);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(10);
    grid->setColumnStretch(1, 1);
    int row = 0;

    m_theme = new QPushButton;
    m_theme->setObjectName(QStringLiteral("Link"));
    m_theme->setCursor(Qt::PointingHandCursor);
    m_theme->setToolTip(tr("Choose from all themes (Ctrl+Shift+T)."));
    m_theme->setProperty("testId", "panelTheme");
    grid->addWidget(rowLabel(tr("Theme")), row, 0);
    grid->addWidget(m_theme, row++, 1, 1, 2);
    connect(m_theme, &QPushButton::clicked, this, &AppearancePanel::themesRequested);

    m_font = new QComboBox;
    m_font->setProperty("testId", "font");
    m_font->addItem(tr("Book's Fonts"), QString());
    m_font->setItemData(0, tr("The fonts the book asks for (Biblioglot's Literata where it names none)."), Qt::ToolTipRole);
    for (const QString& f : Appearance::bundledFonts()) m_font->addItem(f, f);
    m_font->insertSeparator(m_font->count());
    for (const QString& f : QFontDatabase::families())
        if (!f.startsWith(QLatin1Char('@')) && !Appearance::bundledFonts().contains(f) && !QFontDatabase::isPrivateFamily(f)) m_font->addItem(f, f);
    m_font->setMaxVisibleItems(16);
    grid->addWidget(rowLabel(tr("Font")), row, 0);
    grid->addWidget(m_font, row++, 1, 1, 2);
    connect(m_font, &QComboBox::activated, this, [this](int i) { emit valueChanged(QStringLiteral("font"), m_font->itemData(i).toString()); });

    auto addSlider = [&](const QString& label, QSlider* s, QLabel* v) {
        grid->addWidget(rowLabel(label), row, 0);
        grid->addWidget(s, row, 1);
        grid->addWidget(v, row++, 2);
    };
    m_size = slider(60, 300, 10, "text_size_pct", "textSize");
    m_spacing = slider(100, 250, 5, "line_spacing_pct", "lineSpacing");
    m_margins = slider(8, 200, 4, "margin_px", "margins");
    m_width = slider(16, 60, 1, "page_width_em", "pageWidth");
    m_sizeValue = valueLabel("textSizeValue");
    m_spacingValue = valueLabel("lineSpacingValue");
    m_marginsValue = valueLabel("marginsValue");
    m_widthValue = valueLabel("pageWidthValue");
    addSlider(tr("Text Size"), m_size, m_sizeValue);
    addSlider(tr("Line Spacing"), m_spacing, m_spacingValue);
    addSlider(tr("Margins"), m_margins, m_marginsValue);
    {
        // One click to a common width (also Ctrl+[ and Ctrl+], or Ctrl+Shift+wheel over the page).
        auto* presets = new QHBoxLayout;
        presets->setSpacing(6);
        const struct { QString text; int px; const char* id; } defs[] = {
            {tr("Narrow"), 16, "marginsNarrow"}, {tr("Normal"), 56, "marginsNormal"}, {tr("Wide"), 120, "marginsWide"}};
        for (const auto& d : defs) {
            QPushButton* b = chip(d.text, d.id);
            b->setCheckable(false);
            b->setToolTip(tr("%1 px").arg(d.px));
            presets->addWidget(b);
            const int px = d.px;
            connect(b, &QPushButton::clicked, this, [this, px] { m_margins->setValue(px); });
        }
        presets->addStretch(1);
        grid->addLayout(presets, row++, 1, 1, 2);
    }
    addSlider(tr("Page Width"), m_width, m_widthValue);
    m_width->setToolTip(tr("The longest a line of text may be. Wider windows show two columns when Columns is Automatic."));
    m_margins->setToolTip(tr("The smallest space left and right of the text."));

    m_columns = new QButtonGroup(this);
    auto* cols = new QHBoxLayout;
    cols->setSpacing(6);
    const struct { const char* key; QString text; const char* id; } colDefs[] = {
        {"auto", tr("Automatic"), "columnsAuto"}, {"one", tr("One"), "columnsOne"}, {"two", tr("Two"), "columnsTwo"}};
    int idx = 0;
    for (const auto& c : colDefs) {
        QPushButton* b = chip(c.text, c.id);
        b->setProperty("value", QString::fromLatin1(c.key));
        m_columns->addButton(b, idx++);
        cols->addWidget(b);
    }
    m_columns->button(0)->setToolTip(tr("Two columns when the window is wide enough."));
    cols->addStretch(1);
    grid->addWidget(rowLabel(tr("Columns")), row, 0);
    grid->addLayout(cols, row++, 1, 1, 2);
    connect(m_columns, &QButtonGroup::idClicked, this,
            [this](int id) { emit valueChanged(QStringLiteral("columns"), m_columns->button(id)->property("value").toString()); });

    m_justify = new QCheckBox(tr("Justify Text"));
    m_justify->setToolTip(tr("Straight right edges. Off: lines end where their words end."));
    m_justify->setProperty("testId", "justify");
    m_hyphenate = new QCheckBox(tr("Hyphenate"));
    m_hyphenate->setToolTip(tr("Break long words at the end of a line, by the rules of the book's language."));
    m_hyphenate->setProperty("testId", "hyphenate");
    auto* checks = new QHBoxLayout;
    checks->addWidget(m_justify);
    checks->addSpacing(16);
    checks->addWidget(m_hyphenate);
    checks->addStretch(1);
    grid->addLayout(checks, row++, 0, 1, 3);
    connect(m_justify, &QCheckBox::toggled, this, [this](bool on) { emit valueChanged(QStringLiteral("justify"), on); });
    connect(m_hyphenate, &QCheckBox::toggled, this, [this](bool on) { emit valueChanged(QStringLiteral("hyphenate"), on); });

    lay->addLayout(grid);
    auto* rule = new QFrame;
    rule->setFixedHeight(1);
    rule->setStyleSheet(QStringLiteral("background: palette(mid);"));
    lay->addWidget(rule);

    // Which books a change applies to.
    m_scopes = new QButtonGroup(this);
    auto* scopes = new QHBoxLayout;
    scopes->setSpacing(6);
    QPushButton* all = chip(tr("All Books"), "scopeAll");
    m_languageScope = chip(QString(), "scopeLanguage");
    QPushButton* book = chip(tr("This Book"), "scopeBook");
    m_scopes->addButton(all, AllBooks);
    m_scopes->addButton(m_languageScope, Language);
    m_scopes->addButton(book, ThisBook);
    scopes->addWidget(rowLabel(tr("Apply To")));
    scopes->addWidget(all);
    scopes->addWidget(m_languageScope);
    scopes->addWidget(book);
    scopes->addStretch(1);
    lay->addLayout(scopes);
    all->setChecked(true);
    connect(m_scopes, &QButtonGroup::idClicked, this, [this](int id) { setScope(Scope(id)); });

    m_scopeNote = new QLabel;
    m_scopeNote->setObjectName(QStringLiteral("Muted"));
    m_scopeNote->setWordWrap(true);
    m_scopeNote->setStyleSheet(QStringLiteral("font-size: 12px;"));
    lay->addWidget(m_scopeNote);
    auto* resets = new QHBoxLayout;
    m_resetBook = new QPushButton(tr("Reset This Book"));
    m_resetLanguage = new QPushButton;
    for (QPushButton* b : {m_resetBook, m_resetLanguage}) {
        b->setObjectName(QStringLiteral("Link"));
        b->setCursor(Qt::PointingHandCursor);
        resets->addWidget(b);
    }
    m_resetBook->setProperty("testId", "resetBook");
    m_resetLanguage->setProperty("testId", "resetLanguage");
    m_resetBook->setToolTip(tr("This book goes back to the settings of its language or of all books."));
    resets->addStretch(1);
    lay->addLayout(resets);
    connect(m_resetBook, &QPushButton::clicked, this, [this] { emit resetRequested(ThisBook); });
    connect(m_resetLanguage, &QPushButton::clicked, this, [this] { emit resetRequested(Language); });

    // Page turns: for all books (kept in the settings, not per book or language).
    auto* rule2 = new QFrame;
    rule2->setFixedHeight(1);
    rule2->setStyleSheet(QStringLiteral("background: palette(mid);"));
    lay->addWidget(rule2);
    auto* turnTitle = new QLabel(tr("PAGE TURNS (ALL BOOKS)"));
    turnTitle->setObjectName(QStringLiteral("PanelTitle"));
    turnTitle->setStyleSheet(QStringLiteral("padding: 4px 0 0 0;"));
    lay->addWidget(turnTitle);
    auto* turnGrid = new QGridLayout;
    turnGrid->setHorizontalSpacing(10);
    turnGrid->setVerticalSpacing(10);
    turnGrid->setColumnStretch(1, 1);
    m_turnStyle = new QComboBox;
    m_turnStyle->setProperty("testId", "pageTurn");
    addPageTurnStyles(m_turnStyle);
    turnGrid->addWidget(rowLabel(tr("Animation")), 0, 0);
    turnGrid->addWidget(m_turnStyle, 0, 1, 1, 2);
    m_turnSpeed = new QSlider(Qt::Horizontal);
    m_turnSpeed->setRange(100, 1000);
    m_turnSpeed->setSingleStep(50);
    m_turnSpeed->setPageStep(100);
    m_turnSpeed->setProperty("testId", "pageTurnMs");
    m_turnSpeed->setToolTip(tr("How long one page turn takes."));
    m_turnSpeedValue = valueLabel("pageTurnMsValue");
    turnGrid->addWidget(rowLabel(tr("Duration")), 1, 0);
    turnGrid->addWidget(m_turnSpeed, 1, 1);
    turnGrid->addWidget(m_turnSpeedValue, 1, 2);
    m_turnMotion = new QComboBox;
    m_turnMotion->setProperty("testId", "pageTurnMotion");
    addPageTurnMotions(m_turnMotion);
    turnGrid->addWidget(rowLabel(tr("Motion")), 2, 0);
    turnGrid->addWidget(m_turnMotion, 2, 1, 1, 2);
    // Faint arrows at the page's sides: a click there turns the page (the owner's request, 2026-11-26).
    m_turnArrows = new QCheckBox(tr("Arrows at the Sides"));
    m_turnArrows->setProperty("testId", "pageTurnArrows");
    m_turnArrows->setToolTip(tr("A faint arrow at each side of the page, where a click turns it."));
    turnGrid->addWidget(m_turnArrows, 3, 1, 1, 2);
    connect(m_turnArrows, &QCheckBox::toggled, this, &AppearancePanel::turnArrowsChanged);
    lay->addLayout(turnGrid);
    auto sendTurn = [this] {
        const bool on = m_turnStyle->currentData().toString() != QLatin1String("none");
        m_turnSpeed->setEnabled(on);
        m_turnMotion->setEnabled(on);
        m_turnSpeedValue->setText(QStringLiteral("%1 s").arg(QLocale().toString(m_turnSpeed->value() / 1000.0, 'f', 2)));
        emit pageTurnChanged(m_turnStyle->currentData().toString(), m_turnSpeed->value(), m_turnMotion->currentData().toString());
    };
    connect(m_turnStyle, &QComboBox::activated, this, sendTurn);
    connect(m_turnMotion, &QComboBox::activated, this, sendTurn);
    connect(m_turnSpeed, &QSlider::valueChanged, this, [this, sendTurn](int v) {
        const int snapped = qRound(v / 50.0) * 50;
        if (snapped != v) {
            QSignalBlocker b(m_turnSpeed);
            m_turnSpeed->setValue(snapped);
        }
        sendTurn();
    });

    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        // Clicks in the lists' pop-ups belong to the panel too.
        if (!isVisible() || !now || now == this || isAncestorOf(now)) return;
        for (QComboBox* c : {m_font, m_turnStyle, m_turnMotion})
            if (now == c->view() || c->view()->isAncestorOf(now)) return;
        hide();
    });
    setScope(AllBooks);
    hide();
}

QSlider* AppearancePanel::slider(int min, int max, int step, const char* key, const char* testId)
{
    auto* s = new QSlider(Qt::Horizontal);
    s->setRange(min, max);
    s->setSingleStep(step);
    s->setPageStep(step * 2);
    s->setMinimumWidth(150);
    s->setProperty("testId", testId);
    const QString k = QString::fromLatin1(key);
    connect(s, &QSlider::valueChanged, this, [this, s, step, k](int v) {
        const int snapped = qRound(double(v) / step) * step;
        if (snapped != v) {
            QSignalBlocker b(s);
            s->setValue(snapped);
        }
        updateLabels();
        emit valueChanged(k, snapped);
    });
    return s;
}

void AppearancePanel::updateLabels()
{
    const QLocale loc;
    const double bodyPx = 20.0 * m_size->value() / 100.0;
    m_sizeValue->setText(QStringLiteral("%1 px").arg(qRound(bodyPx)));
    m_spacingValue->setText(loc.toString(m_spacing->value() / 100.0, 'f', 2));
    m_marginsValue->setText(QStringLiteral("%1 px").arg(m_margins->value()));
    m_widthValue->setText(QStringLiteral("%1 px").arg(qRound(m_width->value() * bodyPx)));
}

void AppearancePanel::setValues(const Appearance& a, const QString& themeName, const QString& languageName, bool languageHasOwn, bool bookHasOwn)
{
    const QSignalBlocker b1(m_font), b2(m_size), b3(m_spacing), b4(m_margins), b5(m_width), b6(m_justify), b7(m_hyphenate);
    m_theme->setText(themeName + QStringLiteral("  ›"));
    int fi = m_font->findData(a.font);
    if (fi < 0) {
        m_font->addItem(a.font, a.font);   // a font that has since been removed from this computer
        fi = m_font->count() - 1;
    }
    m_font->setCurrentIndex(fi);
    m_size->setValue(a.textSizePct);
    m_spacing->setValue(a.lineSpacingPct);
    m_margins->setValue(a.marginPx);
    m_width->setValue(a.pageWidthEm);
    for (QAbstractButton* b : m_columns->buttons()) b->setChecked(b->property("value").toString() == a.columns);
    m_justify->setChecked(a.justify);
    m_hyphenate->setChecked(a.hyphenate);
    updateLabels();

    m_languageScope->setText(tr("%1 Books").arg(languageName));
    m_languageScope->setVisible(!languageName.isEmpty());
    m_resetLanguage->setText(tr("Reset %1 Books").arg(languageName));
    m_resetLanguage->setToolTip(tr("%1 books go back to the settings of all books.").arg(languageName));
    m_resetLanguage->setVisible(languageHasOwn && !languageName.isEmpty());
    m_resetBook->setVisible(bookHasOwn);
    if (languageName.isEmpty() && m_scope == Language) setScope(AllBooks);
    setScope(m_scope);
}

void AppearancePanel::setPageTurn(const QString& style, int ms, const QString& motion)
{
    const QSignalBlocker b1(m_turnStyle), b2(m_turnSpeed), b3(m_turnMotion);
    m_turnStyle->setCurrentIndex(qMax(0, m_turnStyle->findData(style)));
    m_turnSpeed->setValue(ms);
    m_turnMotion->setCurrentIndex(qMax(0, m_turnMotion->findData(motion)));
    const bool on = style != QLatin1String("none");
    m_turnSpeed->setEnabled(on);
    m_turnMotion->setEnabled(on);
    m_turnSpeedValue->setText(QStringLiteral("%1 s").arg(QLocale().toString(ms / 1000.0, 'f', 2)));
}

void AppearancePanel::addPageTurnStyles(QComboBox* box)
{
    box->addItem(tr("None"), QStringLiteral("none"));
    box->addItem(tr("Slide"), QStringLiteral("slide"));
    box->addItem(tr("Cover"), QStringLiteral("cover"));
    box->addItem(tr("Fade"), QStringLiteral("fade"));
    box->addItem(tr("Page Curl"), QStringLiteral("curl"));
    // The styles other e-readers offer (the owner's request, 2026-11-26: Apple Books' Curl, Slide and Fast Fade,
    // Kotobee's Page Flip, Hardcover Flip, Card Flip, Stack and Fade, Moon+ Reader's page flips, Kindle's on or off).
    box->addItem(tr("Uncover"), QStringLiteral("uncover"));
    box->addItem(tr("Hardcover Flip"), QStringLiteral("flip"));
    box->addItem(tr("Card Flip"), QStringLiteral("card"));
    box->setItemData(1, tr("The new page pushes the old one aside."), Qt::ToolTipRole);
    box->setItemData(2, tr("The new page slides in over the old one."), Qt::ToolTipRole);
    box->setItemData(3, tr("The old page fades into the new one."), Qt::ToolTipRole);
    box->setItemData(4, tr("The page folds back like paper."), Qt::ToolTipRole);
    box->setItemData(5, tr("The old page slides away, showing the new one under it."), Qt::ToolTipRole);
    box->setItemData(6, tr("The page swings over on its spine, like a stiff page in a hardcover book."), Qt::ToolTipRole);
    box->setItemData(7, tr("The page turns over like a card, the new page on its back."), Qt::ToolTipRole);
}

void AppearancePanel::addPageTurnMotions(QComboBox* box)
{
    // Many ways a turn can move (the owner's request, 2026-11-26: "many more motion settings"); the keys are
    // PageView::motions(), in the same order.
    const QList<std::pair<const char*, QString>> motions{
        {"smooth", tr("Smooth (Fast, Then Slowing)")},
        {"even", tr("Even")},
        {"gentle", tr("Gentle (Slow Start and End)")},
        {"soft", tr("Soft (Slowing a Little)")},
        {"quick", tr("Quick (Very Fast, Long Slow Finish)")},
        {"snappy", tr("Snappy (Fast, Sudden Stop)")},
        {"rounded", tr("Rounded (Fast, Round Finish)")},
        {"speeding", tr("Speeding Up")},
        {"ease", tr("Ease In and Out")},
        {"strong", tr("Strong (Slow, Fast, Slow)")},
        {"overshoot", tr("Overshoot (Goes Past, Settles Back)")},
        {"windup", tr("Wind-Up (Pulls Back First)")},
        {"springy", tr("Springy (Wobbles Into Place)")},
        {"bounce", tr("Bounce (Bounces Into Place)")}};
    for (const auto& [key, label] : motions) box->addItem(label, QString::fromLatin1(key));
}

void AppearancePanel::setTurnArrows(bool on)
{
    const QSignalBlocker b(m_turnArrows);
    m_turnArrows->setChecked(on);
}

void AppearancePanel::setScope(Scope s)
{
    m_scope = s;
    if (QAbstractButton* b = m_scopes->button(s)) b->setChecked(true);
    const QString lang = m_languageScope->text();
    switch (s) {
    case AllBooks: m_scopeNote->setText(tr("Changes apply to every book, except where a language or a book has its own setting.")); break;
    case Language: m_scopeNote->setText(tr("Changes apply to %1 only.").arg(lang)); break;
    case ThisBook: m_scopeNote->setText(tr("Changes apply to this book only.")); break;
    }
}

void AppearancePanel::showBelow(const QPoint& topRight)
{
    adjustSize();
    QWidget* p = parentWidget();
    const int w = qMax(380, sizeHint().width());
    resize(w, sizeHint().height());
    move(qBound(8, topRight.x() - w, p->width() - w - 8), topRight.y());
    show();
    raise();
    setFocus();
}

void AppearancePanel::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QFrame::keyPressEvent(e);
}
