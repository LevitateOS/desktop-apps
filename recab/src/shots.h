#pragma once

#include <QString>

class Window;

// Walks every state of the two roots and every action against the stand-in
// (fixtures/recab), saves a picture at each point into `folder`, checks what the
// window says, and returns 0 when all was as expected.
int takeShots(Window &window, const QString &folder);
