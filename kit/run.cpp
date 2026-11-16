#include "run.h"

#include <QJsonDocument>
#include <QRegularExpression>

Run::Run(const QString &program, const QStringList &arguments, QObject *parent)
    : QObject(parent)
{
    process_.setProgram(program);
    process_.setArguments(arguments);
    connect(&process_, &QProcess::readyReadStandardError, this, &Run::drain);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
        if (eventsOnStdout_)
            drainOut();
        else
            answer_ += process_.readAllStandardOutput();
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        drain();
        if (eventsOnStdout_) {
            drainOut();
            // A last line the command ended without a newline.
            if (!pendingOut_.isEmpty()) {
                pendingOut_ += '\n';
                lines(pendingOut_);
            }
        } else {
            answer_ += process_.readAllStandardOutput();
        }
        exitCode_ = status == QProcess::NormalExit ? code : -1;
        emit finished(status == QProcess::NormalExit && code == 0);
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        failure_ = QJsonObject{{"reason", tr("%1 could not be started").arg(process_.program())}};
        emit finished(false);
    });
}

Run::~Run()
{
    // A command whose object goes is not heard any more: its end must not call
    // into pages that are going too. One that is still going is ended here
    // (without Qt's warning).
    QObject::disconnect(&process_, nullptr, nullptr, nullptr);
    QObject::disconnect(this, nullptr, nullptr, nullptr);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(500);
    }
}

QString Run::problem() const
{
    const QString reason = failure_["reason"].toString();
    if (!reason.isEmpty())
        return reason;
    return said_.join(QLatin1Char('\n')).trimmed();
}

void Run::start()
{
    process_.start();
    if (!input_.isEmpty())
        process_.write(input_);
    process_.closeWriteChannel();
}

void Run::stop()
{
    process_.terminate();
}

void Run::drain()
{
    pending_ += process_.readAllStandardError();
    lines(pending_);
}

void Run::drainOut()
{
    pendingOut_ += process_.readAllStandardOutput();
    lines(pendingOut_);
}

// Every complete line in `pending` is an event or a line of output.
void Run::lines(QByteArray &pending)
{
    int end;
    while ((end = pending.indexOf('\n')) >= 0) {
        const QByteArray line = pending.left(end);
        pending.remove(0, end + 1);
        const QJsonObject object = line.startsWith('{') ? QJsonDocument::fromJson(line).object() : QJsonObject();
        if (object.isEmpty()) {
            // `recipe` colours its lines even into a pipe.
            static const QRegularExpression colour(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
            const QString words = QString::fromUtf8(line).remove(colour);
            if (words.startsWith(QLatin1String("Error:")) && !failure_.contains("reason"))
                failure_["reason"] = words.mid(6).trimmed();
            if (!words.trimmed().isEmpty()) {
                said_ << words;
                if (said_.size() > 40)
                    said_.removeFirst();
                emit output(words);
            }
            continue;
        }
        // The last word on a failure. What an earlier event said of the recipe
        // and the step stays where this one names none.
        if (object["event"] == QLatin1String("failed")) {
            for (auto field = object.begin(); field != object.end(); ++field)
                if (!field.value().isNull() || !failure_.contains(field.key()))
                    failure_[field.key()] = field.value();
        }
        // recab's own error: one JSON object with a class, a code and a message.
        if (!object.contains("event") && object.contains("message")) {
            failure_ = object;
            failure_["reason"] = object["message"];
            continue;
        }
        // recipe's `step` event: `recipe`, `step`, `status` (other commands' `step` events name no recipe).
        if (object["event"] == QLatin1String("step") && object.contains("recipe") && object["status"] == QLatin1String("failed")) {
            failure_["recipe"] = object["recipe"];
            failure_["step"] = object["step"];
        }
        if (object["event"] == QLatin1String("output"))
            emit output(object["line"].toString());
        else
            emit event(object);
    }
}
