#include "timelapse.h"
#include "scm_proxy.h"
#include "session.h"

#include <foundation/string.h>
#include <foundation/log.h>
#include <foundation/process.h>
#include <foundation/hashstrings.h>
#include "foundation/environment.h"
#include "foundation/array.h"
#include "foundation/foundation.h"

#include <imgui/imgui.h>

#include <GLFW/glfw3.h>

#include <vector>// REMOVE ME
#include <sstream>// REMOVE ME

#define CTEXT(str) string_const(STRING_CONST(str))

namespace timelapse {

const char* APP_TITLE = "Timelapse";

template<typename Out>
static void split(const std::string &s, char delim, Out result) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

static void set_window_title(GLFWwindow* window, const char* file_path)
{
    char title[1024];
    snprintf(title, sizeof(title) - 1, "%s [%s]", APP_TITLE, file_path);
    glfwSetWindowTitle(window, title);
}

static bool setup(GLFWwindow* window, const char* file_path)
{
    session::setup(file_path);
    if (!session::is_valid())
        return false;

    set_window_title(window, file_path);
    return session::fetch_revisions(21);
}

static void render_main_menu()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open", "Ctrl+O", nullptr))
            {
                // TODO: open file dialog selector
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4", nullptr))
                process_exit(ERROR_NONE);
            ImGui::EndMenu();
        }

        if (session::is_fetching_revisions())
        {
            if (ImGui::BeginMenu("Fetching revisions...", false))
                ImGui::EndMenu();
        }

        if (int revid = session::is_fetching_annotations())
        {
            char fetch_annotations_label[1024] = {'\0'};
            sprintf(fetch_annotations_label, "Fetching annotations for %d...", revid);
            if (ImGui::BeginMenu(fetch_annotations_label, false))
                ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

static void render_waiting()
{
    ImGui::PushItemWidth(-1);
    ImGui::Text("Fetching revisions...");
}

static void render_main_window(const char* window_title, float window_width, float window_height)
{
    ImGui::SetWindowPos(window_title, ImVec2(0, 0));
    ImGui::SetWindowSize(window_title, ImVec2(window_width, window_height));

    render_main_menu();

    if (!session::has_revisions() && session::is_fetching_revisions())
    {
        render_waiting();
        return;
    }
    
    size_t revision = session::revision_curosr();
    const auto& revisions = session::revisions();

    ImGui::PushItemWidth(-1);
    ImGui::Text("Revision: %s - %s", session::file_path(), revision != -1 ? revisions[revision].rev.c_str() :
        "Nothing to timelapse. Select a file or call this program with the file path as the first argument.");

    if (revision != -1)
    {
        ImGui::Text(revisions[revision].description.c_str());
    }

    if (!revisions.empty())
    {
        int irev = (int)revision;
        ImGui::SliderInt("", &irev, 0, (int)revisions.size() - 1);
        revision = session::set_revision_cursor((size_t)irev);

        ImGui::BeginChild("CodeView", ImVec2(ImGui::GetWindowContentRegionWidth(), 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        const scm::revision_t& r = revisions[revision];
        if (!r.annotations.empty())
        {
            //ImGui::InputTextMultiline("", (char*)r.source.c_str(), r.source.size(), ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);

            auto lines = split(r.annotations, '\n');
            for (int i = 0; i < lines.size(); i++)
            {
                const auto pos = lines[i].find(r.rev);
                if (pos != std::string::npos)
                    ImGui::TextColored(ImColor(0.6f, 1.0f, 0.6f), "%04d: %s", i, lines[i].c_str());
                else
                    ImGui::Text("%04d: %s", i, lines[i].c_str());
            }
        }
        else
        {
            ImGui::Text("Fetching annotations, be right back...");
        }

        ImGui::EndChild();
    }

    ImGui::PopItemWidth();
}

int initialize(GLFWwindow* window)
{
    FOUNDATION_UNUSED(window);

    const string_const_t* cmdline = environment_command_line();
    for (int i = 1, end = array_size(cmdline); i < end; ++i)
    {
        string_const_t arg = cmdline[i];
        if (fs_is_file(STRING_ARGS(arg)))
        {
            if (setup(window, arg.str))
                break;
        }
    }

    return 0;
}

void render(int window_width, int window_height)
{
    const char* k_MainWindowTitle = APP_TITLE;

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
        
    if (ImGui::Begin(k_MainWindowTitle, nullptr,
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_MenuBar))
    {
        session::update();

        render_main_window(k_MainWindowTitle, (float)window_width, (float)window_height);
    }

    ImGui::End();
}

void shutdown()
{
    session::shutdown();
}

}

extern void app_exception_handler(const char* dump_file, size_t length)
{
    FOUNDATION_UNUSED(dump_file);
    FOUNDATION_UNUSED(length);
    log_error(HASH_TEST, ERROR_EXCEPTION, STRING_CONST("Unhandled exception"));
    process_exit(-1);
}

extern void app_configure(foundation_config_t& config, application_t& application)
{
    application.name = CTEXT(timelapse::APP_TITLE);
    application.short_name = CTEXT(timelapse::APP_TITLE);
    application.company = CTEXT("Bazesys");
    application.version = string_to_version(STRING_CONST("1.0.0"));
    application.flags = APPLICATION_STANDARD;
    application.exception_handler = app_exception_handler;
}

extern int app_initialize(GLFWwindow* window)
{
    return timelapse::initialize(window);
}

extern void app_render(int display_w, int display_h)
{
    timelapse::render(display_w, display_h);
}

extern void app_shutdown()
{
    timelapse::shutdown();
}

extern const char* app_title()
{
    return timelapse::APP_TITLE;
}
