// A drop-down list whose chosen text ends in "…" when the box is too narrow for it, instead of being cut mid-letter
// (a long book title beside the Word List's search box: the owner's report, 2026-11-25). Hovering shows the whole
// text then; the open list shows every item whole.
#pragma once

#include <QComboBox>
#include <QHelpEvent>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QToolTip>

class ElidingComboBox : public QComboBox {
public:
    using QComboBox::QComboBox;
    bool isElided() const { return m_elided; }   // as last drawn

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter p(this);
        QStyleOptionComboBox opt;
        initStyleOption(&opt);
        p.drawComplexControl(QStyle::CC_ComboBox, opt);
        const QRect text = style()->subControlRect(QStyle::CC_ComboBox, &opt, QStyle::SC_ComboBoxEditField, this);
        const QString full = opt.currentText;
        opt.currentText = fontMetrics().elidedText(full, Qt::ElideRight, text.width());
        m_elided = opt.currentText != full;
        p.drawControl(QStyle::CE_ComboBoxLabel, opt);
    }
    bool event(QEvent* e) override
    {
        if (e->type() == QEvent::ToolTip && toolTip().isEmpty() && m_elided) {
            QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), currentText(), this);
            return true;
        }
        return QComboBox::event(e);
    }

private:
    bool m_elided = false;
};
