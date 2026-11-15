// For running from a checkout only: look at a folder, save a picture of what
// was found, and compare the two systems with a stand-in recab.
#pragma once

#include <QString>

class Window;

int takeShots(Window &window, const QString &folder);
