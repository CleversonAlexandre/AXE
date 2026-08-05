#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include "axe_editor/editor_app.hpp"
#include <cstdio>

int main()
{    
    axe::EditorApp app;
    app.Run();
    _CrtDumpMemoryLeaks();
    return 0;
}