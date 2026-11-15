#include "release.h"

#include "run.h"
#include "words.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString osName(const QString &os)
{
    if (os == QLatin1String("levitateos"))
        return QStringLiteral("LevitateOS");
    if (os == QLatin1String("acornos"))
        return QStringLiteral("AcornOS");
    return os;
}

// The day as the person's own locale writes it; what is not a date is said as it came.
QString dayWords(const QString &iso)
{
    const QDate day = QDate::fromString(iso.left(10), Qt::ISODate);
    return day.isValid() ? QLocale().toString(day, QLocale::LongFormat) : iso;
}

QString whenWords(const QDateTime &at)
{
    return QLocale().toString(at.toLocalTime(), QLocale::ShortFormat);
}

// Only web links are opened from the page.
bool safeLink(const QString &url)
{
    const QUrl parsed(url);
    return parsed.isValid() && (parsed.scheme() == QLatin1String("https") || parsed.scheme() == QLatin1String("http"));
}

} // namespace

QString ReleaseState::installedWords() const
{
    const QString name = osName(os);
    if (installedVersion.isEmpty())
        return QObject::tr("an unknown release");
    return name.isEmpty() ? QObject::tr("release %1").arg(installedVersion)
                          : QObject::tr("%1 %2").arg(name, installedVersion);
}

// ---------------------------------------------------------------- Check

ReleaseCheck::ReleaseCheck(QObject *parent)
    : QObject(parent)
{
}

QString ReleaseCheck::program()
{
    return qEnvironmentVariable("RECUPDATE_BIN", "recupdate");
}

QString ReleaseCheck::filePath()
{
    return QDir(qEnvironmentVariable("RECUPDATE_STATE_DIR", "/var/lib/recupdate")).filePath("available.json");
}

QString ReleaseCheck::releaseFilePath()
{
    return qEnvironmentVariable("RECUPDATE_RELEASE_FILE", "/usr/lib/levitate/release.json");
}

ReleaseState ReleaseCheck::parse(const QByteArray &json, QString *error)
{
    ReleaseState s;
    error->clear();
    QJsonParseError bad;
    const QJsonDocument document = QJsonDocument::fromJson(json, &bad);
    if (!document.isObject()) {
        *error = QObject::tr("recupdate did not answer in a form this app understands.");
        return s;
    }
    const QJsonObject o = document.object();
    if (o["schema"].toInt(1) > 1) {
        *error = QObject::tr("recupdate answers in a newer form than this app understands (schema %1).").arg(o["schema"].toInt());
        return s;
    }
    s.os = o["os"].toString();
    const QJsonObject installed = o["installed"].toObject();
    s.installedVersion = installed["version"].toString();
    s.installedSerial = qint64(installed["serial"].toDouble());
    s.checkedAt = QDateTime::fromString(o["checked_at"].toString(), Qt::ISODate);
    const QJsonValue interrupted = o["interrupted"];
    if (interrupted.isObject()) {
        s.interruptedVersion = interrupted.toObject()["version"].toString();
        s.interruptedSerial = qint64(interrupted.toObject()["serial"].toDouble());
        s.interrupted = true;
    }
    const QJsonValue available = o["available"];
    if (available.isObject()) {
        const QJsonObject a = available.toObject();
        s.kind = ReleaseState::Available;
        s.version = a["version"].toString();
        s.serial = qint64(a["serial"].toDouble());
        s.date = a["date"].toString();
        s.notes = a["notes"].toString();
        s.downloadBytes = qint64(a["download_bytes"].toDouble());
    } else {
        s.kind = ReleaseState::UpToDate;
    }
    return s;
}

void ReleaseCheck::check()
{
    if (run_)
        return;
    state_ = ReleaseState();
    state_.kind = ReleaseState::Checking;
    run_ = new Run(program(), {"check", "--json"}, this);
    connect(run_, &Run::finished, this, [this](bool ok) {
        Run *run = run_;
        run_ = nullptr;
        run->deleteLater();
        QString problem;
        ReleaseState next;
        if (ok) {
            next = parse(run->answer(), &problem);
        } else {
            const QString said = run->problem();
            problem = tr("`%1 check --json` failed: %2")
                          .arg(program(), said.isEmpty() ? tr("it stopped without saying why.") : said);
        }
        if (!problem.isEmpty()) {
            next = ReleaseState();
            next.kind = ReleaseState::Failed;
            next.error = problem;
        }
        state_ = next;
        emit changed();
    });
    emit changed();
    run_->start();
}

void ReleaseCheck::readFile()
{
    if (run_)
        return;
    const QString available = filePath(), release = releaseFilePath();
    QPointer<ReleaseCheck> self(this);
    // The files are small, but a state is never read on the window's thread.
    QThread *thread = QThread::create([self, available, release] {
        auto read = [](const QString &path) {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };
        const QByteArray a = read(available), r = read(release);
        QMetaObject::invokeMethod(self.data(), [self, a, r] {
            if (self)
                self->useFiles(a, r);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// What the files said: the installed release (the manifest the system was built
// from), and the daily check's answer, used when nothing is known or it is newer than what is.
void ReleaseCheck::useFiles(const QByteArray &availableJson, const QByteArray &releaseJson)
{
    if (run_)
        return;
    bool different = false;
    const QJsonObject manifest = QJsonDocument::fromJson(releaseJson).object();
    if (!manifest["version"].toString().isEmpty()) {
        ReleaseState now;
        now.os = manifest["os"].toString();
        now.installedVersion = manifest["version"].toString();
        now.installedSerial = qint64(manifest["serial"].toDouble());
        now.date = manifest["date"].toString();
        different = now.os != installed_.os || now.installedVersion != installed_.installedVersion
                  || now.installedSerial != installed_.installedSerial;
        installed_ = now;
    }
    QString problem;
    ReleaseState file = parse(availableJson, &problem);
    if (!availableJson.isEmpty() && problem.isEmpty()) { // a damaged or unfinished file says nothing; the person can check
        file.fromFile = true;
        // A release the system runs already is not news: the file may be older than the last update.
        if (file.kind == ReleaseState::Available && installed_.installedSerial > 0 && file.serial <= installed_.installedSerial) {
            file.kind = ReleaseState::UpToDate;
            file.version.clear();
            file.date.clear();
            file.notes.clear();
            file.serial = file.downloadBytes = 0;
            file.installedVersion = installed_.installedVersion;
            file.installedSerial = installed_.installedSerial;
        }
        cached_ = file;
        const bool known = state_.kind == ReleaseState::UpToDate || state_.kind == ReleaseState::Available;
        const bool older = known && state_.checkedAt.isValid() && file.checkedAt.isValid() && file.checkedAt <= state_.checkedAt;
        if (!older && (known || state_.kind == ReleaseState::NotChecked || state_.kind == ReleaseState::Failed)) {
            state_ = file;
            different = true;
        }
    }
    if (different)
        emit changed();
    emit fileRead();
}

QString ReleaseCheck::forAgent() const
{
    return tr("%1 check --json\n\n%2").arg(program(), state_.error);
}

// ---------------------------------------------------------------- Card

ReleaseInstaller releaseInstaller()
{
    const QString forced = qEnvironmentVariable("LEVITATE_AB");
    if (forced == QLatin1String("1"))
        return ReleaseInstaller::Recab;
    if (forced == QLatin1String("0"))
        return ReleaseInstaller::Updater;
    return QFileInfo::exists(QStringLiteral("/usr/bin/recab")) ? ReleaseInstaller::Recab : ReleaseInstaller::Updater;
}

ReleaseCard::ReleaseCard(ReleaseCheck *check, QWidget *parent)
    : Card(Card::Plain, parent)
    , model_(check)
{
    connect(model_, &ReleaseCheck::changed, this, &ReleaseCard::draw);
    draw();
}

void ReleaseCard::setAction(const QString &words)
{
    actionWords_ = words;
    draw();
}

void ReleaseCard::setNote(const QString &words)
{
    note_ = words;
    draw();
}

void ReleaseCard::setInstalledNotRunning(const QString &version, const QString &note)
{
    doneVersion_ = version;
    doneNote_ = note;
    draw();
}

void ReleaseCard::setBusy(bool busy)
{
    busy_ = busy;
    draw();
}

void ReleaseCard::draw()
{
    if (body_) {
        body_->hide();
        column()->removeWidget(body_);
        body_->deleteLater();
    }
    check_ = action_ = nullptr;
    body_ = new QWidget(this);
    column()->addWidget(body_);
    auto *inside = new QVBoxLayout(body_);
    inside->setContentsMargins(0, 0, 0, 0);
    inside->setSpacing(8);

    const ReleaseState &s = model_->state();
    const bool installedHere = s.kind == ReleaseState::Available && !doneVersion_.isEmpty() && s.version == doneVersion_;

    switch (s.kind) {
    case ReleaseState::NotChecked:
        inside->addWidget(strong(tr("Not checked yet")));
        inside->addWidget(text(model_->installed().installedVersion.isEmpty()
                                   ? tr("Nothing has asked whether a newer release exists.")
                                   : tr("This system runs %1. Nothing has asked whether a newer release exists.")
                                         .arg(model_->installed().installedWords()),
                               true));
        break;
    case ReleaseState::Checking: {
        inside->addWidget(strong(tr("Checking for updates…")));
        auto *bar = new QProgressBar;
        bar->setRange(0, 0);
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        bar->setMaximumWidth(360);
        inside->addWidget(bar);
        break;
    }
    case ReleaseState::UpToDate:
        inside->addWidget(strong(tr("Up to date")));
        inside->addWidget(text(tr("This system runs %1, the newest release.").arg(s.installedWords()), true));
        break;
    case ReleaseState::Available: {
        inside->addWidget(strong(tr("Release %1 is available").arg(s.version)));
        QStringList facts;
        facts << tr("This system runs %1.").arg(s.installedWords());
        if (!s.date.isEmpty())
            facts << tr("Released %1.").arg(dayWords(s.date));
        if (s.downloadBytes > 0)
            facts << tr("Download size: %1.").arg(bytesText(s.downloadBytes));
        inside->addWidget(text(facts.join(QLatin1Char(' ')), true));
        if (safeLink(s.notes)) {
            auto *link = new QLabel(tr("<a href=\"%1\">Read what is new in release %2</a>").arg(s.notes.toHtmlEscaped(), s.version.toHtmlEscaped()));
            link->setTextFormat(Qt::RichText);
            link->setOpenExternalLinks(true);
            link->setTextInteractionFlags(Qt::TextBrowserInteraction);
            inside->addWidget(link);
        }
        if (!note_.isEmpty())
            inside->addWidget(text(note_, true));
        if (installedHere)
            inside->addWidget(strong(doneNote_));
        break;
    }
    case ReleaseState::Failed:
        inside->addWidget(strong(tr("The check did not work")));
        inside->addWidget(text(s.error, true));
        // What the automatic check found earlier still holds without the network.
        if (model_->cached().kind == ReleaseState::Available)
            inside->addWidget(dim(text(tr("The automatic check%1 found release %2.")
                                           .arg(model_->cached().checkedAt.isValid() ? tr(" on %1").arg(whenWords(model_->cached().checkedAt)) : QString(),
                                                model_->cached().version),
                                       true)));
        break;
    }
    if (s.fromFile && s.checkedAt.isValid())
        inside->addWidget(dim(text(tr("Found by the automatic check on %1.").arg(whenWords(s.checkedAt)), true)));
    else if (!s.fromFile && s.checkedAt.isValid() && (s.kind == ReleaseState::UpToDate || s.kind == ReleaseState::Available))
        inside->addWidget(dim(text(tr("Checked %1.").arg(whenWords(s.checkedAt)), true)));

    auto *buttons = new QHBoxLayout;
    check_ = new QPushButton(s.kind == ReleaseState::Failed ? tr("Check again") : tr("Check for updates"));
    check_->setEnabled(s.kind != ReleaseState::Checking && !busy_);
    connect(check_, &QPushButton::clicked, model_, &ReleaseCheck::check);
    buttons->addWidget(check_);
    if (s.kind == ReleaseState::Available && !actionWords_.isEmpty() && !installedHere) {
        action_ = new QPushButton(actionWords_.contains(QLatin1String("%1")) ? actionWords_.arg(s.version) : actionWords_);
        action_->setDefault(true);
        action_->setEnabled(!busy_);
        connect(action_, &QPushButton::clicked, this, &ReleaseCard::actionClicked);
        buttons->addWidget(action_);
    }
    if (s.kind == ReleaseState::Failed)
        buttons->addWidget(copyForAgentButton([this] { return model_->forAgent(); }));
    buttons->addStretch();
    inside->addLayout(buttons);
}

QString ReleaseCard::pageText() const
{
    QStringList all;
    for (QLabel *label : findChildren<QLabel *>())
        if (label->isVisibleTo(const_cast<ReleaseCard *>(this)))
            all << label->text();
    for (QPushButton *button : findChildren<QPushButton *>())
        if (button->isVisibleTo(const_cast<ReleaseCard *>(this)))
            all << button->text();
    return all.join(QLatin1Char('\n'));
}
