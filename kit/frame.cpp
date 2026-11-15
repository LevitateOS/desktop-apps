#include "frame.h"

#include "firstrun.h"
#include "help.h"
#include "look.h"
#include "parts.h"
#include "releasepage.h"

#include <QAbstractButton>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleOptionFocusRect>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// The paper the rail sits on: the window colour pulled a little
// towards the text colour, so it is a shade lighter on a dark desktop and a
// shade darker on a light one, whatever the palette is.
QColor shade(const QPalette &palette, qreal by)
{
    const QColor ground = palette.color(QPalette::Window), ink = palette.color(QPalette::WindowText);
    return QColor::fromRgbF(ground.redF() + (ink.redF() - ground.redF()) * by,
                            ground.greenF() + (ink.greenF() - ground.greenF()) * by,
                            ground.blueF() + (ink.blueF() - ground.blueF()) * by);
}

// The pages the frame owns; the app's `follow` leaves them showing. "firstrun" has
// no rail entry: it is marked while the page before the app's own shows.
bool sharedKey(const QString &key)
{
    return key == QLatin1String("help") || key == QLatin1String("firstrun") || key == QLatin1String("release");
}

} // namespace

// The rail's entries are each one of these.
// Up and Down move between the entries, Tab and Shift+Tab as everywhere else.
class RailButton : public QAbstractButton {
    Q_OBJECT
public:
    RailButton(const QString &title, QWidget *parent = nullptr)
        : QAbstractButton(parent)
    {
        setText(title);
        setCheckable(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setAccessibleName(title);
        setFixedHeight(32);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    void setBadge(int count, bool warn)
    {
        count_ = count;
        warn_ = warn;
        setAccessibleName(count > 0 ? tr("%1, %2").arg(text()).arg(count) : text());
        update();
    }
    int badge() const { return count_; }
    bool warns() const { return warn_; }
    QSize sizeHint() const override { return {100, height()}; }

signals:
    void stepped(int by);

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Down || event->key() == Qt::Key_Up) {
            emit stepped(event->key() == Qt::Key_Down ? 1 : -1);
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            click();
            return;
        }
        QAbstractButton::keyPressEvent(event);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette &p = palette();
        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QColor ink = p.color(QPalette::WindowText);
        {
            if (isChecked()) {
                QColor tint = p.color(QPalette::Highlight);
                tint.setAlphaF(0.16f);
                painter.setBrush(tint);
                tint.setAlphaF(0.75f);
                painter.setPen(QPen(tint, 1));
                painter.drawRoundedRect(box, 5, 5);
            } else if (underMouse()) {
                QColor tint = ink;
                tint.setAlphaF(0.07f);
                painter.setPen(Qt::NoPen);
                painter.setBrush(tint);
                painter.drawRoundedRect(box, 5, 5);
            }
            painter.setPen(ink);
            painter.setFont(font());
            int right = width() - 10;
            if (count_ > 0) {
                const QString number = QString::number(count_);
                const int wide = qMax(20, fontMetrics().horizontalAdvance(number) + 12);
                const QRect pill(width() - 10 - wide, (height() - 18) / 2, wide, 18);
                painter.setPen(Qt::NoPen);
                const QColor fill = warn_ ? moodColour(p, Mood::Warn) : p.color(QPalette::Highlight);
                painter.setBrush(fill);
                painter.drawRoundedRect(pill, 9, 9);
                // Dark ink on a light fill, light ink on a dark one: amber is light on the dark look and brown on the light one.
                QFont bold = font();
                bold.setPointSizeF(bold.pointSizeF() * 0.93);
                bold.setBold(true);
                painter.setFont(bold);
                painter.setPen(!warn_ ? p.color(QPalette::HighlightedText)
                                      : fill.lightness() > 140 ? QColor(0x1c, 0x13, 0x00) : QColor(0xff, 0xff, 0xff));
                painter.drawText(pill, Qt::AlignCenter, number);
                right = pill.left() - 8;
            }
            painter.setFont(font());
            painter.setPen(ink);
            painter.drawText(QRect(10, 0, right - 10, height()), Qt::AlignVCenter | Qt::AlignLeft,
                             fontMetrics().elidedText(text(), Qt::ElideRight, right - 10));
        }
        if (hasFocus()) {
            QStyleOptionFocusRect focus;
            focus.initFrom(this);
            focus.rect = rect().adjusted(1, 1, -1, -1);
            focus.backgroundColor = palette().color(QPalette::Window);
            style()->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, this);
        }
    }

private:
    bool warn_ = false;
    int count_ = 0;
};

namespace {

// A line down the side of the rail.
class Divider : public QWidget {
public:
    Divider() { setFixedWidth(1); }
protected:
    void paintEvent(QPaintEvent *) override
    {
        QColor colour = palette().color(QPalette::WindowText);
        colour.setAlphaF(0.13f);
        QPainter(this).fillRect(rect(), colour);
    }
};

struct AppInfo {
    const char *name, *program, *variable;
};
const AppInfo apps[] = {{"Recipe", "recipe-gui", "LEVITATE_APP_RECIPE"},
                        {"Recab", "recab-gui", "LEVITATE_APP_SYSTEM"},
                        {"Recdirstat", "disk-gui", "LEVITATE_APP_DISK"},
                        {"Updater", "updater-gui", "LEVITATE_APP_UPDATER"}};

QWidget *rail(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    widget->setFixedWidth(196);
    widget->setAutoFillBackground(true);
    QPalette p = widget->palette();
    p.setColor(QPalette::Window, shade(p, 0.07));
    widget->setPalette(p);
    return widget;
}

} // namespace

Frame::Frame(App app, QWidget *parent)
    : QWidget(parent)
    , app_(app)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
    auto *shell = new QHBoxLayout;
    shell->setSpacing(0);

    // ---- the rail
    QWidget *left = rail(this);
    auto *railColumn = new QVBoxLayout(left);
    railColumn->setContentsMargins(8, 8, 8, 8);
    railColumn->setSpacing(2);
    left->setAccessibleName(tr("%1 sections").arg(QString::fromLatin1(apps[app].name)));

    auto *title = new QLabel(tr(apps[app].name));
    {
        QFont f = title->font();
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 1.25);
        title->setFont(f);
        title->setContentsMargins(10, 4, 10, 2);
    }
    railColumn->addWidget(title);
    railColumn->addSpacing(4);
    railColumn->addWidget(rule());
    railColumn->addSpacing(4);

    appEntries_ = new QWidget;
    auto *own = new QVBoxLayout(appEntries_);
    own->setContentsMargins(0, 0, 0, 0);
    own->setSpacing(2);
    railColumn->addWidget(appEntries_);
    railColumn->addStretch();
    // The shared entry, below the gap.
    auto *help = new RailButton(tr("Help"));
    help->setAccessibleDescription(tr("How %1 works and where to look when something is wrong").arg(tr(apps[app].name)));
    entries_.insert(helpKey(), help);
    order_ << help;
    railColumn->addWidget(help);
    connect(help, &QAbstractButton::clicked, this, [this] { choose(helpKey()); });
    connect(help, &RailButton::stepped, this, [this, help](int by) { step(help, by); });
    shell->addWidget(left);
    shell->addWidget(new Divider);

    // ---- the middle
    auto *middle = new QVBoxLayout;
    middle->setSpacing(0);
    stack_ = new QStackedWidget;
    stack_->addWidget(new QWidget); // the app's content takes this place
    help_ = new HelpPage(app);
    stack_->addWidget(help_);
    firstRun_ = new FirstRunPage(app);
    stack_->addWidget(firstRun_);
    release_ = new ReleasePage;
    stack_->addWidget(release_);
    middle->addWidget(stack_, 1);
    shell->addLayout(middle, 1);
    outer->addLayout(shell, 1);

    // ---- the status line
    outer->addWidget(rule());
    auto *line = new QWidget;
    line->setFixedHeight(28);
    auto *status = new QHBoxLayout(line);
    status->setContentsMargins(12, 0, 12, 0);
    status->setSpacing(10);
    status_ = new ElidedLabel(QString());
    {
        QPalette p = status_->palette();
        p.setColor(QPalette::WindowText, moodColour(p, Mood::Plain));
        status_->setPalette(p);
    }
    status_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    statusExtras_ = new QHBoxLayout;
    statusExtras_->setSpacing(6);
    status->addWidget(status_, 0);
    status->addLayout(statusExtras_);
    status->addStretch();
    // The app-level action, at the right end: the release of the whole system.
    releaseButton_ = new QToolButton;
    releaseButton_->setText(tr("Check for updates"));
    releaseButton_->setAutoRaise(true);
    releaseButton_->setToolTip(tr("Ask whether a newer release of the whole system exists. "
                                  "The updates of single programs are on the Updates page of Recipe."));
    connect(releaseButton_, &QToolButton::clicked, this, &Frame::showRelease);
    status->addWidget(releaseButton_);
    outer->addWidget(line);

    flashTimer_ = new QTimer(this);
    flashTimer_->setSingleShot(true);
    connect(flashTimer_, &QTimer::timeout, this, [this] {
        flashWords_.clear();
        status_->setText(statusWords_);
        status_->setToolTip(statusWords_);
    });

    connect(release_, &ReleasePage::back, this, &Frame::leaveShared);
    connect(release_, &ReleasePage::startApp, this, &Frame::startApp);
    connect(help_, &HelpPage::go, this, &Frame::helpGo);
    connect(help_, &HelpPage::startApp, this, &Frame::startApp);
    connect(help_, &HelpPage::firstPageAgain, this, &Frame::showFirstRun);
    // "Go on" is the person's step to the app; the rail's entries leave the page as well.
    connect(firstRun_, &FirstRunPage::goOn, this, &Frame::leaveShared);
    connect(firstRun_, &FirstRunPage::openHelp, this, [this] { choose(helpKey()); });
}

void Frame::setContent(QWidget *content)
{
    QWidget *old = stack_->widget(0);
    stack_->removeWidget(old);
    old->deleteLater();
    stack_->insertWidget(0, content);
    if (!sharedKey(selected_))
        stack_->setCurrentIndex(0);
}

void Frame::addEntry(const QString &key, const QString &title)
{
    auto *button = new RailButton(title);
    entries_.insert(key, button);
    // Before the shared entry (Help), which is last in the order.
    order_.insert(order_.size() - 1, button);
    static_cast<QVBoxLayout *>(appEntries_->layout())->addWidget(button);
    connect(button, &QAbstractButton::clicked, this, [this, key] { choose(key); });
    connect(button, &RailButton::stepped, this, [this, button](int by) { step(button, by); });
    if (selected_.isEmpty())
        mark(key);
}

void Frame::setBadge(const QString &key, int count, bool warn)
{
    if (RailButton *button = entries_.value(key))
        button->setBadge(count, warn);
}

int Frame::badge(const QString &key) const
{
    RailButton *button = entries_.value(key);
    return button ? button->badge() : 0;
}

bool Frame::badgeWarns(const QString &key) const
{
    RailButton *button = entries_.value(key);
    return button && button->warns();
}

void Frame::mark(const QString &key)
{
    selected_ = key;
    for (auto at = entries_.begin(); at != entries_.end(); ++at)
        at.value()->setChecked(at.key() == key);
}

void Frame::select(const QString &key)
{
    if (!entries_.contains(key))
        return;
    followed_ = key;
    mark(key);
    stack_->setCurrentIndex(indexOf(key));
}

// Where a page is in the stack: the app's content first, then the frame's own.
int Frame::indexOf(const QString &key) const
{
    return key == helpKey() ? 1 : 0; // the first page (2) and the release page (3) are shown by their own calls
}

void Frame::follow(const QString &key)
{
    if (!entries_.contains(key) || sharedKey(key))
        return;
    followed_ = key;
    if (!sharedKey(selected_))
        mark(key);
}

bool Frame::helpShown() const
{
    return selected_ == helpKey();
}

bool Frame::firstRunShown() const
{
    return selected_ == QLatin1String("firstrun");
}

// The page before the app's own: nothing in the rail is marked.
void Frame::showFirstRun()
{
    mark(QStringLiteral("firstrun"));
    stack_->setCurrentIndex(2);
    FirstRunPage::markSeen(app_);
}

// "Check for updates": the release page, then the check itself. The rail marks nothing.
void Frame::showRelease()
{
    mark(releaseKey());
    stack_->setCurrentWidget(release_);
    release_->open();
}

bool Frame::releaseShown() const
{
    return selected_ == releaseKey();
}

void Frame::refreshRelease()
{
    if (releaseShown())
        release_->refresh();
}

// Back to the app's own page, the one it is on: from Help, from the first page.
void Frame::leaveShared()
{
    mark(followed_);
    stack_->setCurrentIndex(0);
}

// A link of Help: a rail entry, chosen as a click would.
void Frame::helpGo(const QString &target)
{
    if (entries_.contains(target))
        choose(target);
}

// The person chose an entry: show what it stands for and say so.
void Frame::choose(const QString &key)
{
    if (key == helpKey()) {
        mark(key);
        // Drawn for the pages the app has now: no link leads to a page that is not there.
        help_->draw(entries_.keys());
        stack_->setCurrentIndex(indexOf(key));
        return; // the frame's own page: the app has nothing to switch to
    }
    select(key);
    emit chosen(key);
}

void Frame::step(RailButton *from, int by)
{
    const int at = order_.indexOf(from);
    if (at < 0)
        return;
    const QList<RailButton *> entries = order_;
    const int index = entries.indexOf(from);
    if (index < 0)
        return;
    const int next = qBound(0, index + by, int(entries.size()) - 1);
    entries[next]->setFocus(Qt::TabFocusReason);
}

// ---- The status line

void Frame::setStatus(const QString &words)
{
    statusWords_ = words;
    if (flashWords_.isEmpty()) {
        status_->setText(words);
        status_->setToolTip(words);
    }
}

QString Frame::statusText() const
{
    return flashWords_.isEmpty() ? statusWords_ : flashWords_;
}

void Frame::addStatusWidget(QWidget *widget)
{
    statusExtras_->addWidget(widget);
}

void Frame::flash(const QString &words)
{
    flashWords_ = words;
    const QString shown = words.isEmpty() ? statusWords_ : words;
    status_->setText(shown);
    status_->setToolTip(shown);
    if (words.isEmpty())
        flashTimer_->stop();
    else
        flashTimer_->start(8000);
}

void Frame::startApp(App app)
{
    if (app == app_)
        return;
    // A picture run does not start the person's real apps: one it was not told of is not started.
    if (pictureMode() && !qEnvironmentVariableIsSet(apps[app].variable)) {
        flash(tr("%1 is not started in a picture run: its program is not named in the environment (%2).")
                  .arg(tr(apps[app].name), QString::fromLatin1(apps[app].variable)));
        return;
    }
    const QString program = qEnvironmentVariable(apps[app].variable, QString::fromLatin1(apps[app].program));
    if (program.isEmpty() || !QProcess::startDetached(program, {}))
        flash(tr("%1 could not be started: %2 was not found or would not run.")
                  .arg(tr(apps[app].name), program.isEmpty() ? QString::fromLatin1(apps[app].program) : program));
}

QStringList Frame::entryTitles() const
{
    QStringList titles;
    for (RailButton *button : order_)
        titles << button->text();
    return titles;
}

void Frame::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The first time this person opens this app: the page before the app's own.
    // The app has been reading since it was made; nothing here waits for it.
    if (!firstRunChecked_) {
        firstRunChecked_ = true;
        if (FirstRunPage::wanted(app_))
            showFirstRun();
    }
}

#include "frame.moc"
