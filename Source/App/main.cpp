// ============================================================================
// WoW Model Viewer — entry point
//
// All application logic lives in the Application class.  This file exists
// solely to provide the CRT entry point.
// ============================================================================

#include "Application.h"

int main(int /*argc*/, char* /*argv*/[])
{
    Application app;
    return app.run();
}
