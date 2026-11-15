#include "look.h"
#include "windowsize.h"
#include "shots.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("recab-gui");
    QApplication::setApplicationDisplayName(QObject::tr("Recab"));
    QApplication::setApplicationVersion("0.1");
    QApplication::setDesktopFileName("org.levitateos.recab");
    QApplication::setWindowIcon(QIcon::fromTheme("system-software-update"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("The two roots: the booted root is locked, the other is unlocked."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption look("look", "From a checkout: a stand-in look, levitate or acorn.", "name");
    const QCommandLineOption shots("shots", "From a checkout: walk every state and action against the stand-in "
                                            "(fixtures/recab), save a picture at each point into this folder, then quit.",
                                   "folder");
    parser.addOptions({look, shots});
    parser.process(app);
    if (parser.isSet(look))
        applyLook(parser.value(look));
    // A picture run must not reach the person's real system: it needs every stand-in named.
    if (parser.isSet(shots)) {
        const int refused = guardPictureMode("recab-gui", {"RECAB_GUI_RECAB", "RECAB_GUI_REBOOT", "RECAB_STANDIN_STATE", "RECAB_STANDIN_REBOOT_MARK"}, {"RECAB_GUI_ROOT_HELPER"});
        if (refused)
            return refused;
    }

    Window window;
    // Opens at the size it had; the picture modes always use the first size.
    rememberWindowSize(&window, "recab", {760, 560}, !parser.isSet(shots));
    if (parser.isSet(shots))
        return takeShots(window, parser.value(shots));
    window.show();
    return QApplication::exec();
}
