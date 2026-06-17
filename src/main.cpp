#include "renderer.hpp"

#include <cstdio>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: glb_renderer <model.glb>\n");
        return 1;
    }

    const char* path = argv[1];
    printf("GLB Renderer - Loading: %s\n\n", path);

    Renderer renderer;
    if (!renderer.init(path)) {
        fprintf(stderr, "FATAL: Failed to initialize renderer\n");
        return 1;
    }

    printf("\n--- Rendering ---\n");
    renderer.run();

    return 0;
}
