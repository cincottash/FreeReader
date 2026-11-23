#include "Commands.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

// Label on the left, shortcut on the right.
class CommandDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* p, const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QStyleOptionViewItem o(opt);
        initStyleOption(&o, idx);
        const QString shortcut = idx.data(Qt::UserRole + 1).toString();
        o.text.clear();
        QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        const QRect r = opt.rect.adjusted(12, 0, -12, 0);
        p->save();
        p->setPen(opt.palette.color(QPalette::Text));
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, idx.data(Qt::DisplayRole).toString());
        p->setPen(opt.palette.color(QPalette::PlaceholderText));
        p->drawText(r, Qt::AlignVCenter | Qt::AlignRight, shortcut);
        p->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& opt, const QModelIndex& idx) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(opt, idx);
        s.setHeight(qMax(s.height(), opt.fontMetrics.height() + 16));
        return s;
    }
};

}  // namespace

CommandPalette::CommandPalette(QWidget* parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("Pop"));
    setAttribute(Qt::WA_StyledBackground);
    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(32);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 90));
    setGraphicsEffect(shadow);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);
    m_title = new QLabel;
    m_title->setObjectName(QStringLiteral("Muted"));
    lay->addWidget(m_title);
    m_edit = new QLineEdit;
    m_edit->setProperty("testId", "paletteEdit");
    lay->addWidget(m_edit);
    m_error = new QLabel;
    m_error->setWordWrap(true);
    lay->addWidget(m_error);
    m_list = new QListWidget;
    m_list->setItemDelegate(new CommandDelegate(m_list));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setMinimumHeight(260);
    m_list->setProperty("testId", "paletteList");
    m_error->setProperty("testId", "paletteError");
    lay->addWidget(m_list, 1);
    m_edit->installEventFilter(this);
    connect(m_edit, &QLineEdit::textChanged, this, [this] {
        m_error->hide();
        if (!m_prompt) refill();
    });
    connect(m_edit, &QLineEdit::returnPressed, this, &CommandPalette::runCurrent);
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        m_list->setCurrentItem(it);
        runCurrent();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (isVisible() && now && !isAncestorOf(now) && now != this) hide();
    });
    hide();
}

void CommandPalette::place()
{
    QWidget* p = parentWidget();
    const int w = qMin(560, p->width() - 32);
    const int h = m_prompt ? sizeHint().height() : qMin(460, p->height() - 80);
    setGeometry((p->width() - w) / 2, 64, w, h);
}

void CommandPalette::showCommands()
{
    m_prompt = nullptr;
    m_title->setText(tr("Commands"));
    m_edit->setPlaceholderText(tr("Type to search commands"));
    m_edit->clear();
    m_error->hide();
    m_list->show();
    refill();
    place();
    show();
    raise();
    m_edit->setFocus();
}

void CommandPalette::showPrompt(const QString& title, const QString& placeholder, std::function<QString(const QString&)> accept)
{
    m_prompt = std::move(accept);
    m_title->setText(title);
    m_edit->setPlaceholderText(placeholder);
    m_edit->clear();
    m_error->hide();
    m_list->hide();
    adjustSize();
    place();
    show();
    raise();
    m_edit->setFocus();
}

void CommandPalette::refill()
{
    m_list->clear();
    const QStringList words = m_edit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (Command* c : m_commands) {
        if (!c->isEnabled()) continue;
        bool ok = true;
        for (const QString& w : words)
            if (!c->label.contains(w, Qt::CaseInsensitive)) ok = false;
        if (!ok) continue;
        auto* it = new QListWidgetItem(c->label, m_list);
        it->setData(Qt::UserRole, c->id);
        it->setData(Qt::UserRole + 1, c->shortcut.toString(QKeySequence::NativeText));
        if (!c->hint.isEmpty()) it->setToolTip(c->hint);
    }
    if (m_list->count()) m_list->setCurrentRow(0);
}

void CommandPalette::runCurrent()
{
    if (m_prompt) {
        const QString err = m_prompt(m_edit->text().trimmed());
        if (err.isEmpty()) {
            hide();
            m_prompt = nullptr;
        } else {
            m_error->setText(err);
            m_error->show();
            adjustSize();
            place();
        }
        return;
    }
    QListWidgetItem* it = m_list->currentItem();
    if (!it) return;
    const QString id = it->data(Qt::UserRole).toString();
    hide();
    for (Command* c : m_commands)
        if (c->id == id && c->run) {
            c->run();
            break;
        }
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_edit && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape) {
            hide();
            m_prompt = nullptr;
            return true;
        }
        if (!m_prompt && (k->key() == Qt::Key_Down || k->key() == Qt::Key_Up)) {
            const int n = m_list->count();
            if (n) m_list->setCurrentRow((m_list->currentRow() + (k->key() == Qt::Key_Down ? 1 : n - 1)) % n);
            return true;
        }
    }
    return QFrame::eventFilter(o, e);
}

namespace mouse_shortcut {

QString fromEvent(Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    QString name;
    if (button == Qt::MiddleButton) name = QStringLiteral("Middle");
    else if (button == Qt::BackButton) name = QStringLiteral("Back");
    else if (button == Qt::ForwardButton) name = QStringLiteral("Forward");
    else return {};
    QString mods;
    if (modifiers & Qt::ControlModifier) mods += QStringLiteral("Ctrl+");
    if (modifiers & Qt::ShiftModifier) mods += QStringLiteral("Shift+");
    if (modifiers & Qt::AltModifier) mods += QStringLiteral("Alt+");
    return mods + name;
}

QString display(const QString& binding)
{
    if (binding.isEmpty()) return {};
    QString d = binding;
    for (const char* b : {"Middle", "Back", "Forward"})
        if (d.endsWith(QLatin1String(b))) d += QObject::tr(" Button");
    return d;
}

}  // namespace mouse_shortcut
