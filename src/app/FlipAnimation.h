// Turning a card over: a picture of the card before the change narrows to nothing, then a picture of it after
// widens back, as a flashcard turning on its upright middle. The widgets underneath are already the new side, so
// the card works at once; the pictures are only drawn over it while it turns.
#pragma once

#include <QColor>

#include <functional>

class QWidget;

namespace flip {

// `change` switches `card` to its other side (and may resize it). `ms` 0: no animation.
void turn(QWidget* card, const std::function<void()>& change, const QColor& background, int radius, int ms = 280);
bool isTurning(const QWidget* card);   // tests

}  // namespace flip
