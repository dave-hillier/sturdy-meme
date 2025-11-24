#include "Application.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    Application app;

    if (!app.init("Vulkan Game", 1280, 720)) {
        return 1;
    }

    app.run();
    app.shutdown();

    return 0;
}
