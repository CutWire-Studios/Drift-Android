#pragma once

#include "AppController.h"

// Backward-compatible alias used by existing QML.
class EditorState : public AppController
{
    Q_OBJECT

public:
    using AppController::AppController;
};
