// Runs one command and reads its events. Shared by the front ends.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>

// One running command that changes something (`recipe`, `recab`).
// It reports the JSON events the command writes to stderr, one object per
// line; any other line is passed on as output.
class Run : public QObject {
    Q_OBJECT
public:
    Run(const QString &program, const QStringList &arguments, QObject *parent);
    // Text the command reads on its standard input, then end of input.
    void setInput(const QByteArray &input) { input_ = input; }
    // The process id while it runs, else 0.
    qint64 pid() const { return process_.processId(); }
    // Whether the process is going.
    bool running() const { return process_.state() == QProcess::Running; }
    // The exit code of the process once it has finished.
    int exitCode() const { return exitCode_; }
    // For commands that write their JSON lines on standard output (`recupdate`,
    // `recab update`): events are read from there, line by line, as they come, and
    // `answer()` stays empty. Set before `start()`. The default is the events on
    // standard error, as `recipe` and `recab copy` write them.
    void setEventsOnStdout(bool on) { eventsOnStdout_ = on; }
    void start();
    // Asks it to end.
    void stop();
    ~Run() override;
    QJsonObject failure() const { return failure_; }
    // Why it failed, in one text: the reason it gave, else what it wrote
    // that was not an event, else that it could not be started.
    QString problem() const;
    // What the command wrote on its standard output, complete once finished.
    QByteArray answer() const { return answer_; }

signals:
    void event(const QJsonObject &event);
    void output(const QString &line);
    void finished(bool ok);

private:
    void drain();
    void drainOut();
    void lines(QByteArray &pending);
    QProcess process_;
    QByteArray pending_, pendingOut_, input_, answer_;
    bool eventsOnStdout_ = false;
    QJsonObject failure_;
    QStringList said_;
    int exitCode_ = -1;
};
