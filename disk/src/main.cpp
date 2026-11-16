#include "look.h"
#include "windowsize.h"
#ifndef LEVITATE_NO_STORIES
#include "shots.h"
#endif
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("disk-gui");
    QApplication::setApplicationDisplayName(QObject::tr("Recdirstat"));
    QApplication::setApplicationVersion("0.1");
    QApplication::setDesktopFileName("org.levitateos.disk");
    QApplication::setWindowIcon(QIcon::fromTheme("org.levitateos.disk", QIcon::fromTheme("drive-harddisk")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("See where the mess on the disk is, and how alike the two roots are."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption look("look", "From a checkout: a stand-in look, levitate or acorn.", "name");
#ifndef LEVITATE_NO_STORIES
    const QCommandLineOption shots("shots", "From a checkout: look and save a picture of what was found into this "
                                            "folder, then quit.",
                                   "folder");
    parser.addOptions({look, shots});
#else
    parser.addOption(look);
#endif
    parser.process(app);
#ifndef LEVITATE_NO_STORIES
    const bool pictures = parser.isSet(shots);
#else
    const bool pictures = false;
#endif
    if (parser.isSet(look))
        applyLook(parser.value(look));
#ifndef LEVITATE_NO_STORIES
    // A picture run must not reach the person's real system: it needs every stand-in named.
    if (parser.isSet(shots)) {
        const int refused = guardPictureMode("disk-gui", {"DISK_GUI_RECAB", "DISK_GUI_CMDLINE", "DISK_GUI_FILES", "DISK_GUI_LSBLK", "DISK_GUI_ROOTS", "DISK_GUI_HOME"}, {"DISK_GUI_ROOT_HELPER"});
        if (refused)
            return refused;
    }

#endif

    Window window;
    // Opens at the size it had; the picture modes always use the first size.
    rememberWindowSize(&window, "disk", {1100, 700}, !pictures);
#ifndef LEVITATE_NO_STORIES
    if (parser.isSet(shots))
        return takeShots(window, parser.value(shots));
#endif
    window.show();
    return QApplication::exec();
}
