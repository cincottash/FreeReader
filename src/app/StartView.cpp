#include "StartView.h"

#include <QDir>
#include <QFileInfo>
#include <QtMath>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

StartView::StartView(QWidget* parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("StartView"));
    setAttribute(Qt::WA_StyledBackground);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);
    auto* top = new QHBoxLayout;
    top->addStretch(1);
    m_notKept = new QPushButton(tr("Settings Not Kept"));
    m_notKept->setObjectName(QStringLiteral("Link"));
    m_notKept->setCursor(Qt::PointingHandCursor);
    m_notKept->setToolTip(tr("App data is kept in a temporary folder and deleted when the app closes. Click to choose a folder."));
    m_notKept->hide();
    top->addWidget(m_notKept);
    outer->addLayout(top);
    outer->addStretch(1);

    auto* col = new QVBoxLayout;
    col->setSpacing(10);
    auto* title = new QLabel(QStringLiteral(APP_NAME));
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 600;"));
    title->setAlignment(Qt::AlignHCenter);
    col->addWidget(title);
    auto* sub = new QLabel(tr("An e-book reader for DRM-free EPUB books."));
    sub->setObjectName(QStringLiteral("Muted"));
    sub->setAlignment(Qt::AlignHCenter);
    col->addWidget(sub);
    col->addSpacing(18);
    auto* open = new QPushButton(tr("Open Book…"));
    open->setObjectName(QStringLiteral("Primary"));
    open->setCursor(Qt::PointingHandCursor);
    open->setMinimumWidth(220);
    open->setProperty("testId", "open");
    col->addWidget(open, 0, Qt::AlignHCenter);
    auto* drop = new QLabel(tr("or drop an EPUB file anywhere in this window"));
    drop->setObjectName(QStringLiteral("Muted"));
    drop->setAlignment(Qt::AlignHCenter);
    col->addWidget(drop);
    m_message = new QLabel;
    m_message->setWordWrap(true);
    m_message->setAlignment(Qt::AlignHCenter);
    m_message->setMaximumWidth(520);
    m_message->hide();
    m_message->setProperty("testId", "message");
    col->addWidget(m_message, 0, Qt::AlignHCenter);
    col->addSpacing(24);
    m_recentTitle = new QLabel(tr("RECENT BOOKS"));
    m_recentTitle->setObjectName(QStringLiteral("PanelTitle"));
    m_recentTitle->setStyleSheet(QStringLiteral("padding: 0 0 4px 4px;"));
    col->addWidget(m_recentTitle);
    m_recent = new QListWidget;
    m_recent->setFixedWidth(520);
    m_recent->setMinimumHeight(60);
    m_recent->setFocusPolicy(Qt::NoFocus);
    m_recent->setCursor(Qt::PointingHandCursor);
    m_recent->setProperty("testId", "recent");
    col->addWidget(m_recent, 0, Qt::AlignHCenter);

    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addLayout(col);
    row->addStretch(1);
    outer->addLayout(row);
    outer->addStretch(2);

    connect(open, &QPushButton::clicked, this, &StartView::openRequested);
    connect(m_notKept, &QPushButton::clicked, this, &StartView::settingsLinkClicked);
    connect(m_recent, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) { emit bookRequested(it->data(Qt::UserRole).toString()); });
    setRecent({});
}

void StartView::setRecent(const QList<core::LibraryBook>& books)
{
    m_recent->clear();
    for (const auto& b : books) {
        const QString progress = QLocale().toString(qFloor(b.progress * 100)) + QStringLiteral("%");
        QString line = b.title.isEmpty() ? QFileInfo(b.path).completeBaseName() : b.title;
        line += QStringLiteral("\n") + (b.author.isEmpty() ? QString() : b.author + QStringLiteral(" · ")) + progress;
        if (!QFileInfo::exists(b.path)) line += tr(" · File not found");
        auto* it = new QListWidgetItem(line, m_recent);
        it->setData(Qt::UserRole, b.path);
        it->setToolTip(QDir::toNativeSeparators(b.path));
    }
    m_recentTitle->setVisible(!books.isEmpty());
    m_recent->setVisible(!books.isEmpty());
    m_recent->setFixedHeight(qMin(6, int(books.size())) * 58 + 4);
}

void StartView::setMessage(const QString& text)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
}

void StartView::setSettingsNotKept(bool on) { m_notKept->setVisible(on); }
