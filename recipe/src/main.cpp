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
    QApplication::setApplicationName("recipe-gui");
    QApplication::setApplicationDisplayName(QObject::tr("Recipe"));
    QApplication::setApplicationVersion("0.1");
    QApplication::setDesktopFileName("org.levitateos.recipe");
    QApplication::setWindowIcon(QIcon::fromTheme("org.levitateos.recipe", QIcon::fromTheme("system-software-install")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("Find, install and update software from recipes."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption look("look", "From a checkout: a stand-in look, levitate or acorn.", "name");
#ifndef LEVITATE_NO_STORIES
    const QCommandLineOption shots("shots", "From a checkout: save a picture of every page into this folder, then quit.",
                                   "folder");
    const QCommandLineOption real("try", "From a checkout, with --shots: install this recipe with the real recipe, "
                                         "save a picture at each point, then remove it.", "recipe");
    const QCommandLineOption keep("keep", "With --try: leave the recipe installed.");
    const QCommandLineOption readOnly("read-only", "With --try: install nothing, picture only the pages that read.");
    parser.addOptions({look, shots, real, keep, readOnly});
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
        const int refused = guardPictureMode("recipe-gui", {"RECIPE_GUI_RECIPE", "RECIPE_GUI_PACKAGE_ROOT"}, {"RECIPE_GUI_ROOT_HELPER"});
        if (refused)
            return refused;
    }

#endif

    Window window;
    // Opens at the size it had; the picture modes always use the first size.
    rememberWindowSize(&window, "recipe", {1100, 700}, !pictures);
#ifndef LEVITATE_NO_STORIES
    if (parser.isSet(shots) && parser.isSet(real))
        return tryRecipe(window, parser.value(real), parser.value(shots), parser.isSet(keep), parser.isSet(readOnly));
    if (parser.isSet(shots))
        return takeShots(window, parser.value(shots));
#endif
    window.show();
    return QApplication::exec();
}
