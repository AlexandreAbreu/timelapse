#pragma once

struct GLFWwindow;

namespace timelapse {

int initialize(GLFWwindow* window);
void render(int display_w, int display_h);
void shutdown();

}
