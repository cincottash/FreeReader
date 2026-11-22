#include "Popups.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

NotePopup::NotePopup(QWidget* parent) : QFrame(parent)
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
    lay->setContentsMargins(16, 14, 16, 10);
    lay->setSpacing(6);
    auto* title = new QLabel(tr("NOTE"));
    title->setObjectName(QStringLiteral("PanelTitle"));
    title->setContentsMargins(0, 0, 0, 0);
    title->setStyleSheet(QStringLiteral("padding: 0;"));
    lay->addWidget(title);
    m_text = new QLabel;
    m_text->setWordWrap(true);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_scroll = new QScrollArea;
    m_scroll->setWidget(m_text);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));
    lay->addWidget(m_scroll, 1);
    m_go = new QPushButton(tr("Go to Note"));
    m_go->setObjectName(QStringLiteral("Link"));
    m_go->setCursor(Qt::PointingHandCursor);
    m_go->setProperty("testId", "goToNote");
    lay->addWidget(m_go, 0, Qt::AlignLeft);
    connect(m_go, &QPushButton::clicked, this, [this] {
        hide();
        if (m_goTo) m_goTo();
    });
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        if (isVisible() && now && now != this && !isAncestorOf(now)) hide();
    });
    hide();
}

void NotePopup::showNote(const QString& text, const QRect& anchor, std::function<void()> goToNote)
{
    m_goTo = std::move(goToNote);
    m_text->setText(text.toHtmlEscaped().replace(QStringLiteral("\n\n"), QStringLiteral("<br><br>")));
    m_text->setTextFormat(Qt::RichText);
    QWidget* p = parentWidget();
    const int w = qMin(420, p->width() - 32);
    m_text->setFixedWidth(w - 32 - 12);
    const int textH = m_text->heightForWidth(w - 44);
    const int h = qMin(qMin(360, p->height() - 32), textH + 90);
    resize(w, h);
    int x = qBound(16, anchor.center().x() - w / 2, p->width() - w - 16);
    int y = anchor.bottom() + 10;
    if (y + h > p->height() - 16) y = anchor.top() - h - 10;
    move(x, qMax(16, y));
    show();
    raise();
    setFocus();
}

QString NotePopup::text() const { return m_text->text(); }

void NotePopup::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        hide();
        if (parentWidget()) parentWidget()->setFocus();
        return;
    }
    QFrame::keyPressEvent(e);
}

ImageViewer::ImageViewer(QWidget* parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Click or press Esc to close."));
    hide();
}

void ImageViewer::showImage(const QImage& img)
{
    m_image = img;
    setGeometry(parentWidget()->rect());
    show();
    raise();
    setFocus();
}

void ImageViewer::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    QColor back = m_theme.bg;
    back.setAlphaF(0.94f);
    p.fillRect(rect(), back);
    if (m_image.isNull()) {
        p.setPen(m_theme.muted);
        p.drawText(rect(), Qt::AlignCenter, tr("This image can't be shown."));
        return;
    }
    const QSize avail = size() - QSize(64, 64);
    QSizeF s = m_image.size();
    s.scale(QSizeF(avail).boundedTo(QSizeF(m_image.size()) * 2.0), Qt::KeepAspectRatio);
    const QRectF r((width() - s.width()) / 2, (height() - s.height()) / 2, s.width(), s.height());
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(r, m_image);
}

void ImageViewer::mousePressEvent(QMouseEvent*) { hide(); }

void ImageViewer::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Space || e->key() == Qt::Key_Return) hide();
    else QWidget::keyPressEvent(e);
}
