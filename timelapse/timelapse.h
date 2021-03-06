#pragma once

struct GLFWwindow;

namespace timelapse {

    /// Initialize the timelapse application
    int initialize(GLFWwindow* window);

    /// Render each frame of the main window.
    void render(int display_w, int display_h);

    /// Clean application resources and shutdown.
    void shutdown();

}
