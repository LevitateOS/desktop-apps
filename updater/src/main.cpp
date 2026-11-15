#include "look.h"
#include "windowsize.h"
#include "window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("updater-gui");
    QApplication::setApplicationDisplayName(QObject::tr("Updater"));
    QApplication::setApplicationVersion("0.1");
    QApplication::setDesktopFileName("org.levitateos.updater");
    QApplication::setWindowIcon(QIcon::fromTheme("org.levitateos.updater", QIcon::fromTheme("system-software-update")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Check for a newer release of this system and install it."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption look("look", "From a checkout: a stand-in look, levitate or acorn.", "name");
    parser.addOption(look);
    parser.process(app);
    if (parser.isSet(look))
        applyLook(parser.value(look));

    Window window;
    rememberWindowSize(&window, "updater", {760, 640}, true);
    window.show();
    return QApplication::exec();
}
