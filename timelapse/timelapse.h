#pragma once

struct GLFWwindow;
struct application_t;
struct foundation_config_t;

namespace tl {

void configure(foundation_config_t& config, application_t& application);
int initialize(GLFWwindow* window);
void render(int display_w, int display_h);
void shutdown();

}

