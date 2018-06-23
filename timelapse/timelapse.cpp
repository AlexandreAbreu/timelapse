#include "timelapse.h"
#include "scm_proxy.h"
#include "session.h"
#include "scoped_string.h"

#include <foundation/string.h>
#include <foundation/log.h>
#include <foundation/process.h>
#include <foundation/hashstrings.h>
#include "foundation/environment.h"
#include "foundation/array.h"
#include "foundation/foundation.h"
#include "foundation/windows.h"

#include <Commdlg.h>

#include <imgui/imgui.h>

#include <GLFW/glfw3.h>

#include <vector>// REMOVE ME
#include <sstream>// REMOVE ME

#define CTEXT(str) string_const(STRING_CONST(str))

extern HWND g_WindowHandle;
extern GLFWwindow* g_Window;

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
    scoped_string_t dirpath = path_directory_name(file_path, strlen(file_path));
    scoped_string_t filename = path_file_name(file_path, strlen(file_path));
    scoped_string_t title = string_allocate_format(STRING_CONST("%s [%s] - %s"), (const char*)filename, (const char*)dirpath, APP_TITLE);
    glfwSetWindowTitle(window, title);
}

static bool setup(GLFWwindow* window, const char* file_path)
{
    session::setup(file_path);
    if (!session::is_valid())
        return false;

    set_window_title(window, file_path);
    return session::fetch_revisions();
}

static bool shortcut_executed(bool ctrl, bool alt, bool shift, bool super, int key)
{
    ImGuiIO& io = ImGui::GetIO();
    return ((!ctrl || io.KeyCtrl) && (!alt || io.KeyAlt) && (!shift || io.KeyShift) && (!super || io.KeySuper) &&
        io.KeysDown[key] && io.KeysDownDuration[key] == 0.0f);
}

static bool shortcut_executed(bool ctrl, bool alt, bool shift, int key) { return shortcut_executed(ctrl, alt, shift, false, key); }
static bool shortcut_executed(bool ctrl, bool alt, int key) { return shortcut_executed(ctrl, alt, false, false, key); }
static bool shortcut_executed(bool ctrl, int key) { return shortcut_executed(ctrl, false, false, false, key); }
static bool shortcut_executed(int key) { return shortcut_executed(false, false, false, false, key); }

static void open_file()
{
    char file_path[1024] = {'\0'};
    string_format(file_path, sizeof(file_path)-1, STRING_CONST("%s"), session::file_path());
    path_clean(file_path, strlen(file_path), sizeof(file_path)-1);
    string_replace(file_path, strlen(file_path), sizeof(file_path)-1, STRING_CONST("/"), STRING_CONST("\\"), true);
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_WindowHandle;
    ofn.lpstrFile = file_path;
    ofn.nMaxFile = sizeof(file_path);
    ofn.lpstrFilter = "All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = (LPSTR)"Select file to timelapse...";
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (GetOpenFileName(&ofn))
    {
        setup(g_Window, ofn.lpstrFile);
    }
}

static void goto_previous_revision()
{
    session::set_revision_cursor(session::revision_curosr()-1);
}

static void goto_next_revision()
{
    session::set_revision_cursor(session::revision_curosr()+1);
}

static void goto_next_change()
{
    log_info(0, STRING_CONST("goto next change..."));
}

static void render_main_menu()
{
    if (shortcut_executed(true, 'O'))
        open_file();
    else  if (shortcut_executed(GLFW_KEY_PAGE_UP))
        goto_previous_revision();
    else if (shortcut_executed(GLFW_KEY_PAGE_DOWN))
        goto_next_revision();
    else if (shortcut_executed(GLFW_KEY_SPACE))
        goto_next_change();

    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open", "Ctrl+O", nullptr))
                open_file();

            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4", nullptr))
                process_exit(ERROR_NONE);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Revision", session::has_revisions()))
        {
            if (ImGui::MenuItem("Previous", "Pg. Down", nullptr))
                goto_previous_revision();

            if (ImGui::MenuItem("Next", "Pg. Up", nullptr))
                goto_next_revision();

            ImGui::Separator();

            if (ImGui::MenuItem("Cycle changes", "Space", nullptr))
                goto_next_change();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Siblings", false))
        {
            ImGui::EndMenu();
        }

        if (session::is_fetching_revisions())
        {
            if (ImGui::BeginMenu("Fetching revisions...", false))
                ImGui::EndMenu();
        }
        else if (int revid = session::is_fetching_annotations())
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
    ImGui::Text("Building up a time machine for you...");
}

template <class T>
static int num_digits(T number)
{
    int digits = 0;
    if (number < 0) digits = 1; // remove this line if '-' counts as a digit
    while (number) {
        number /= 10;
        digits++;
    }
    return digits;
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

    ImGui::PushItemWidth(-1);
    
    size_t revision = session::revision_curosr();
    bool has_revision_to_show = revision != -1;

    if (!has_revision_to_show)
        ImGui::Text("Nothing to timelapse. Select a file or call this program with the file path as the first argument.");
    
    const auto& revisions = session::revisions();
    if (!revisions.empty())
    {
        const auto& crev = revisions[revision];
        char clog[2048] = {'\0'};
        string_format(STRING_CONST(clog), STRING_CONST("%s | %s | %s | %s"), crev.rev.c_str(), crev.author.c_str(), crev.date.c_str(), crev.description.c_str());
        ImGui::InputText("summary", clog, sizeof(clog)-1, ImGuiInputTextFlags_ReadOnly);
        //ImGui::TextWrapped(clog);

        int irev = (int)revision+1;
        ImGui::SliderInt("", &irev, 1, (int)revisions.size());
        session::set_revision_cursor((size_t)irev - 1);

        ImGui::BeginChild("CodeView", ImVec2(ImGui::GetWindowContentRegionWidth(), 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        if (!crev.annotations.empty())
        {
            //ImGui::InputTextMultiline("", (char*)r.source.c_str(), r.source.size(), ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);

            auto lines = split(crev.annotations, '\n');
            for (int i = 0, line_count = (int)lines.size(), max_line_digit_count = num_digits(line_count); i < line_count; ++i)
            {
                const auto pos = lines[i].find(crev.rev);
                if (pos != std::string::npos)
                    ImGui::TextColored(ImColor(0.6f, 1.0f, 0.6f), "%0*d: %s", max_line_digit_count, i+1, lines[i].c_str());
                else
                    ImGui::Text("%0*d: %s", max_line_digit_count, i+1, lines[i].c_str());
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
        scoped_string_t file_path = path_allocate_absolute(STRING_ARGS(arg));
        
        if (fs_is_file(STRING_ARGS(file_path.value)))
        {
            if (setup(window, file_path))
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
        //ImGuiWindowFlags_NoSavedSettings |
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
