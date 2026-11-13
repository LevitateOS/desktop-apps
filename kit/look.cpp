#include "look.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

#include <cstdio>
#include <pwd.h>
#include <unistd.h>

void applyLook(const QString &look)
{
    const bool acorn = look == QLatin1String("acorn");
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QFont font("Noto Sans");
    font.setPointSizeF(10);
    QApplication::setFont(font);

    // These values are copies of `[look.light]` (levitate) and `[look.dark]`
    // (acorn) in `distro/variants/<id>/distro.toml` of the LevitateOS repo;
    // change them there first. LevitateOS starts light, AcornOS starts dark.
    struct Colours {
        const char *surface, *raised, *sunken, *text, *secondary, *muted, *rule, *hairline, *accent, *onAccent,
            *highlight, *button, *bevel, *deep, *shadow;
    };
    static const Colours light = {"#DDE2D2", "#E8ECDF", "#CFD6C3", "#14221A", "#465047", "#5C6459", "#AEB8A4",
                                  "#C6CDB9", "#214438", "#E8ECDF", "#644532", "#D3D9C8", "#F0F3E9", "#8E9887",
                                  "#6F776A"};
    static const Colours dark = {"#1F1711", "#2B2019", "#150F0A", "#F2EEE4", "#CDBFAE", "#A89886", "#4D3D30",
                                 "#382B21", "#E4CBA6", "#1F1711", "#C7D9BA", "#2B2019", "#5E4B3B", "#0F0A07",
                                 "#070402"};
    const Colours &c = acorn ? dark : light;

    QPalette p;
    auto set = [&p](QPalette::ColorRole role, const char *colour) { p.setColor(role, QColor(colour)); };
    set(QPalette::Window, c.surface);
    set(QPalette::WindowText, c.text);
    set(QPalette::Base, c.raised);
    set(QPalette::AlternateBase, c.sunken);
    set(QPalette::Text, c.text);
    set(QPalette::Button, c.button);
    set(QPalette::ButtonText, c.text);
    set(QPalette::Highlight, c.accent);
    set(QPalette::HighlightedText, c.onAccent);
    set(QPalette::Link, c.accent);
    set(QPalette::LinkVisited, c.highlight);
    set(QPalette::PlaceholderText, c.muted);
    set(QPalette::ToolTipBase, c.raised);
    set(QPalette::ToolTipText, c.text);
    set(QPalette::Light, c.bevel);
    set(QPalette::Midlight, c.hairline);
    set(QPalette::Mid, c.rule);
    set(QPalette::Dark, c.deep);
    set(QPalette::Shadow, c.shadow);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(c.muted));
    QApplication::setPalette(p);
}

void settle(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()> &holds, int seconds)
{
    QElapsedTimer clock;
    clock.start();
    const qint64 limit = qint64(qMax(seconds * 3, 60)) * 1000;
    while (!holds() && clock.elapsed() < limit)
        settle(50);
    const bool held = holds();
    settle(150);
    return held;
}

namespace {
bool inPictureMode = false;
}

bool pictureMode()
{
    return inPictureMode;
}

int guardPictureMode(const QString &program, const QStringList &programs, const QStringList &mayBeEmpty)
{
    QStringList named{"LEVITATE_ACTIVITY_AGENT", "LEVITATE_AGENT_CODEX", "LEVITATE_AGENT_CLAUDE", "HOME",
                      "XDG_CONFIG_HOME",         "XDG_STATE_HOME",        "XDG_DATA_HOME",        "XDG_RUNTIME_DIR"};
    named << programs;
    QStringList empty{"LEVITATE_AGENT_ROOT_HELPER"};
    empty << mayBeEmpty;
    QStringList missing;
    for (const QString &name : std::as_const(named))
        if (qEnvironmentVariable(name.toLatin1().constData()).isEmpty())
            missing << name;
    for (const QString &name : std::as_const(empty))
        if (!qEnvironmentVariableIsSet(name.toLatin1().constData()))
            missing << name;
    QString sentence;
    if (!missing.isEmpty())
        sentence = QStringLiteral("%1 --shots would reach this computer for real, so it does not start. Name these in the "
                                  "environment first, each a stand-in or a folder of scratch space (the root helper may be "
                                  "named empty): %2. The README of the app has the command.")
                       .arg(program, missing.join(QStringLiteral(", ")));
    const passwd *entry = getpwuid(getuid());
    if (sentence.isEmpty() && entry && QDir::cleanPath(qEnvironmentVariable("HOME")) == QDir::cleanPath(QString::fromLocal8Bit(entry->pw_dir)))
        sentence = QStringLiteral("%1 --shots would write into your own home folder, so it does not start. Point HOME and the "
                                  "XDG folders into a folder of scratch space.")
                       .arg(program);
    if (!sentence.isEmpty()) {
        fprintf(stderr, "%s\n", qPrintable(sentence));
        return 2;
    }
    // A notice on the desktop of whoever runs a check is not wanted; one that names a notifier gets it.
    if (!qEnvironmentVariableIsSet("LEVITATE_AGENT_NOTIFIER"))
        qputenv("LEVITATE_AGENT_NOTIFIER", "");
    inPictureMode = true;
    return 0;
}
