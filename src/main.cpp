#include <SDL3/SDL_main.h>
#include "Application/Application.h"

int main(int argc, char* argv[]) {
    Application app;
    
    if (!app.Initialize()) {
        return -1;
    }

    app.Run();

    return 0;
}