#include "windowsize.h"

#include <QEvent>
#include <QSettings>
#include <QWidget>

namespace {

class SizeKeeper : public QObject {
public:
    SizeKeeper(QWidget *window, const QString &key)
        : QObject(window)
        , window_(window)
        , key_(key)
    {
        window->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == window_ && event->type() == QEvent::Close) {
            // A maximised window keeps the size it had before, not the screen's.
            const QSize size = window_->isMaximized() || window_->isFullScreen() ? window_->normalGeometry().size()
                                                                                  : window_->size();
            QSettings settings(QStringLiteral("LevitateOS"), QStringLiteral("windows"));
            if (size.isValid() && !size.isEmpty())
                settings.setValue(key_, size);
            settings.sync();
        }
        return false;
    }

private:
    QWidget *window_;
    QString key_;
};

} // namespace

void rememberWindowSize(QWidget *window, const QString &app, QSize first, bool remember)
{
    QSize size = first;
    if (remember) {
        QSettings settings(QStringLiteral("LevitateOS"), QStringLiteral("windows"));
        const QSize kept = settings.value(app).toSize();
        // A size nobody could use (a hand-edited file) is the first size.
        if (kept.width() >= 400 && kept.height() >= 300)
            size = kept;
        new SizeKeeper(window, app);
    }
    window->resize(size);
}
