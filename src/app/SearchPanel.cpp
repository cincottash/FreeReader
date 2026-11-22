#include "SearchPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QVBoxLayout>

SearchPanel::SearchPanel(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Panel"));
    setAttribute(Qt::WA_StyledBackground);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 8);
    lay->setSpacing(6);
    auto* title = new QLabel(tr("SEARCH THIS BOOK"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    lay->addWidget(title);
    auto* box = new QVBoxLayout;
    box->setContentsMargins(16, 0, 16, 0);
    box->setSpacing(6);
    m_edit = new QLineEdit;
    m_edit->setPlaceholderText(tr("Words to find"));
    m_edit->setClearButtonEnabled(true);
    m_edit->setProperty("testId", "searchBox");
    box->addWidget(m_edit);
    auto* opts = new QHBoxLayout;
    m_whole = new QCheckBox(tr("Whole Words"));
    m_whole->setProperty("testId", "searchWhole");
    m_accents = new QCheckBox(tr("Match Accents"));
    m_accents->setToolTip(tr("Off: “razon” also finds “razón”."));
    m_accents->setProperty("testId", "searchAccents");
    opts->addWidget(m_whole);
    opts->addWidget(m_accents);
    opts->addStretch(1);
    box->addLayout(opts);
    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("Muted"));
    m_status->setWordWrap(true);
    m_status->setProperty("testId", "searchStatus");
    box->addWidget(m_status);
    lay->addLayout(box);
    m_list = new QListWidget;
    m_list->setProperty("testId", "searchResults");
    m_list->setWordWrap(true);
    m_list->setTextElideMode(Qt::ElideNone);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setSpacing(2);
    lay->addWidget(m_list, 1);

    m_typing.setSingleShot(true);
    m_typing.setInterval(350);
    connect(&m_typing, &QTimer::timeout, this, &SearchPanel::start);
    connect(m_edit, &QLineEdit::textEdited, this, [this] { m_typing.start(); });
    connect(m_edit, &QLineEdit::returnPressed, this, [this] {
        m_typing.stop();
        start();
    });
    connect(m_whole, &QCheckBox::toggled, this, &SearchPanel::start);
    connect(m_accents, &QCheckBox::toggled, this, &SearchPanel::start);
    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem* it) { emit hitActivated(m_list->row(it)); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { emit hitActivated(m_list->row(it)); });
}

QString SearchPanel::query() const { return m_edit->text().simplified(); }

core::SearchOptions SearchPanel::options() const
{
    core::SearchOptions o;
    o.wholeWord = m_whole->isChecked();
    o.matchAccents = m_accents->isChecked();
    return o;
}

void SearchPanel::setQuery(const QString& q)
{
    m_edit->setText(q.simplified());
    start();
}

void SearchPanel::focusBox()
{
    m_edit->setFocus();
    m_edit->selectAll();
}

void SearchPanel::start()
{
    m_typing.stop();
    const QString q = query();
    if (q.size() < 2) {
        m_hits.clear();
        m_list->clear();
        m_status->setText(q.isEmpty() ? QString() : tr("Type at least 2 letters."));
        return;
    }
    emit searchRequested(q, options());
}

void SearchPanel::setSearching()
{
    m_status->setText(tr("Searching…"));
}

void SearchPanel::setResults(const QList<core::SearchHit>& hits, const QStringList& pageLabels, bool truncated)
{
    m_hits = hits;
    m_list->clear();
    for (int i = 0; i < hits.size(); ++i) {
        const core::SearchHit& h = hits[i];
        const QString context = (h.before + QStringLiteral("‹") + h.match + QStringLiteral("›") + h.after).simplified();
        auto* it = new QListWidgetItem(context + QLatin1Char('\n') + tr("Page %1").arg(pageLabels.value(i)), m_list);
        it->setToolTip(h.before + h.match + h.after);
    }
    if (hits.isEmpty()) m_status->setText(tr("No results for “%1”.").arg(query()));
    else if (truncated) m_status->setText(tr("The first %1 results.").arg(QLocale().toString(hits.size())));
    else m_status->setText(hits.size() == 1 ? tr("1 result.") : tr("%1 results.").arg(QLocale().toString(hits.size())));
}

void SearchPanel::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        emit closeRequested();
        return;
    }
    QFrame::keyPressEvent(e);
}
