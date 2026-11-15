// Makes a window open at the size it had: the size is read now (`first` the
// first time) and kept when the window closes, under organisation
// "LevitateOS" and one key per app. With `remember` false (the picture modes)
// the window is only given `first`, and nothing is read or written.
#pragma once

#include <QSize>
#include <QString>

class QWidget;

void rememberWindowSize(QWidget *window, const QString &app, QSize first, bool remember);
