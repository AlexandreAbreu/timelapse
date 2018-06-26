/* TODO:
    - Add windows taskbar progression based on the total of revision and annotated revisions (windows only)
    - Show and open other files timelapse of the current revision
    - Show removed lines
    - Port to OSX (and linux)
    - Detect up and down
    - Try to found a way to batch multiple `hg` calls
    - Detect common ancestor to base revisions on (using . for now, should be trunk and most cases, offer a dropdown of all branches?)
    - Detect repo type (git or hg) and use proper scm calls to offer the same experience (or fork the repo for git version of timelapse)
    - Support deleted files
    - Follow renamed files
    - Add option to see merge commits
 */

#include "timelapse.h"
#include "scm_proxy.h"
#include "session.h"
#include "scoped_string.h"
#include "common.h"
#include "resource.h"

#include "foundation/string.h"
#include "foundation/log.h"
#include "foundation/process.h"
#include "foundation/hashstrings.h"
#include "foundation/environment.h"
#include "foundation/array.h"
#include "foundation/foundation.h"

#if FOUNDATION_PLATFORM_WINDOWS
    #include "foundation/windows.h"
    #include <Commdlg.h>
#endif

#include <cstdio>

#include <imgui/imgui.h>

#include <GLFW/glfw3.h>

#define CTEXT(str) string_const(STRING_CONST(str))
#define HASH_TIMELAPSE (static_hash_string("timelapse", 9, 1468255696394573417ULL))

#if FOUNDATION_PLATFORM_WINDOWS
    extern HWND g_WindowHandle;
#endif
extern GLFWwindow* g_Window;

namespace timelapse {

const char* APP_TITLE = PRODUCT_NAME;

bool g_show_patch_view = false;

#if FOUNDATION_PLATFORM_WINDOWS
ITaskbarList3* g_task_bar_list = nullptr;
#endif

static void progress_initialize()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        CoCreateInstance(
            CLSID_TaskbarList, NULL, CLSCTX_ALL,
            IID_ITaskbarList3, (void**)&g_task_bar_list);
        g_task_bar_list->SetProgressState(g_WindowHandle, TBPF_INDETERMINATE);
    #else
        #error "Not implemented"
    #endif
}

static void progress_stop()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        FOUNDATION_ASSERT(g_task_bar_list);

        FlashWindow(g_WindowHandle, FALSE);
        g_task_bar_list->SetProgressState(g_WindowHandle, TBPF_NOPROGRESS);
    #else
        #error "Not implemented"
    #endif
}

static void progress_finalize()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        if (!g_task_bar_list)
            return;

        progress_stop();

        g_task_bar_list->Release();
        g_task_bar_list = nullptr;
    #else
        #error "Not implemented"
    #endif
}

static void progress_set(size_t current, size_t total)
{
    #if FOUNDATION_PLATFORM_WINDOWS
        FOUNDATION_ASSERT(g_task_bar_list);

        g_task_bar_list->SetProgressValue(g_WindowHandle, current, total);
    #else
        #error "Not implemented"
    #endif
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

    progress_initialize();
    set_window_title(window, file_path);
    return session::fetch_revisions();
}

static bool shortcut_executed(bool ctrl, bool alt, bool shift, bool super, int key)
{
    ImGuiIO& io = ImGui::GetIO();
    return ((!ctrl || io.KeyCtrl) && (!alt || io.KeyAlt) && (!shift || io.KeyShift) && (!super || io.KeySuper) &&
        io.KeysDown[key] && io.KeysDownDuration[key] == 0.0f);
}

//static bool shortcut_executed(bool ctrl, bool alt, bool shift, int key) { return shortcut_executed(ctrl, alt, shift, false, key); }
static bool shortcut_executed(bool ctrl, bool alt, int key) { return shortcut_executed(ctrl, alt, false, false, key); }
static bool shortcut_executed(bool ctrl, int key) { return shortcut_executed(ctrl, false, false, false, key); }
static bool shortcut_executed(int key) { return shortcut_executed(false, false, false, false, key); }

static void open_file()
{
    #if FOUNDATION_PLATFORM_WINDOWS
        char file_path[1024] = { '\0' };
        string_format(file_path, sizeof(file_path) - 1, STRING_CONST("%s"), session::file_path());
        path_clean(file_path, strlen(file_path), sizeof(file_path) - 1);
        string_replace(file_path, strlen(file_path), sizeof(file_path) - 1, STRING_CONST("/"), STRING_CONST("\\"), true);
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
    #else
        #error "Not implemented"
    #endif
}

static void goto_previous_revision()
{
    session::set_revision_cursor(session::revision_cursor()-1);
}

static void goto_next_revision()
{
    session::set_revision_cursor(session::revision_cursor()+1);
}

static void goto_next_change()
{
    if (!session::has_revisions())
        return;

    log_info(0, STRING_CONST("TODO: goto next change..."));
}

static void execute_tool(const string_const_t& name, string_const_t* argv, size_t argc)
{
    process_t* tool = process_allocate();
    process_set_executable_path(tool, STRING_ARGS(name));
    process_set_working_directory(tool, session::working_dir(), strlen(session::working_dir()));
    process_set_arguments(tool, argv, argc);
    process_set_flags(tool, PROCESS_DETACHED | PROCESS_HIDE_WINDOW);
    process_spawn(tool);
    process_deallocate(tool);
}

static void open_compare_tool()
{
    if (!session::has_revisions())
        return;
        
    string_const_t args[3] = {CTEXT("bcomp"), CTEXT("-c"), session::rev_node()};
    execute_tool(CTEXT("hg"), args, 3);
}

static void open_revdetails_tool()
{
    if (!session::has_revisions())
        return;

    string_const_t args[3] = { CTEXT("revdetails"), CTEXT("-r"), session::rev_node() };
    execute_tool(CTEXT("thg"), args, 3);
}

struct fetch_annotations_stats_t
{
    size_t fetched{};
    size_t total{};
};

static fetch_annotations_stats_t fetch_annotations_stats()
{
    const auto& revisions = session::revisions();
    fetch_annotations_stats_t stats = { 0, revisions.size() };

    if (stats.total == 0)
        return stats;

    for (const auto& rev : revisions)
    {
        if (array_size(rev.annotations) != 0)
            stats.fetched++;
    }

    return stats;
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
    else if (shortcut_executed(true, 'D'))
        open_compare_tool();
    else if (shortcut_executed(GLFW_KEY_F1))
        open_revdetails_tool();
    else if (shortcut_executed(false, true, GLFW_KEY_GRAVE_ACCENT))
        g_show_patch_view = !g_show_patch_view;

    if (ImGui::BeginMenuBar())
    {
        bool has_revision = session::has_revisions();

        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open", "Ctrl+O")) open_file();
            ImGui::Separator();

            if (ImGui::MenuItem("Exit", "Alt+F4")) process_exit(ERROR_NONE);
            
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Revision", has_revision))
        {
            if (ImGui::BeginMenu("Show"))
            {
                ImGui::MenuItem("Patch", "Alt+`", &g_show_patch_view);
                ImGui::EndMenu();
            };
            
            ImGui::Separator();
            if (ImGui::MenuItem("Previous", "Pg. Down")) goto_previous_revision();
            if (ImGui::MenuItem("Next", "Pg. Up")) goto_next_revision();
            
            ImGui::Separator();
            if (ImGui::MenuItem("Cycle changes", "Space")) goto_next_change();

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools", has_revision))
        {
            if (ImGui::MenuItem("Details", "F1\t\t(open thg revdetails)")) open_revdetails_tool();
            if (ImGui::MenuItem("Compare", "Ctrl+D\t(open Beyond Compare Folder View)")) open_compare_tool();

            ImGui::EndMenu();
        }

//         if (ImGui::BeginMenu("Siblings", false))
//         {
//             // TODO:
//             ImGui::EndMenu();
//         }

        if (session::is_fetching_annotations())
        {
            char fetch_annotations_label[1024] = {'\0'};
            const auto& stats = fetch_annotations_stats();
            string_format(STRING_CONST(fetch_annotations_label), STRING_CONST("Fetching patches, annotations and some magic powder... (%zu/%zu)"), stats.fetched, stats.total);
            if (ImGui::BeginMenu(fetch_annotations_label, true))
            {
                ImGui::MenuItem("Do not panic if things get shuffled and re-order while time is being reconstructed for you!");
                ImGui::EndMenu();
            }
        }
        else if (session::is_fetching_revisions())
        {
            if (ImGui::BeginMenu("Fetching revisions...", false))
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
    
    int revision = session::revision_cursor();
    bool has_revision_to_show = revision >= 0;

    if (!has_revision_to_show)
        ImGui::Text("Nothing to timelapse. Select a file or call this program with the file path as the first argument.");
    
    const auto& revisions = session::revisions();
    if (!revisions.empty())
    {
        const auto& crev = revisions[revision];
        char clog[2048] = { '\0' };
        if (crev.merged_date.length > 0)
            string_format(STRING_CONST(clog), STRING_CONST("%d | %.*s | %.*s | %.*s | change made %.*s | merged on %.*s"),
                crev.id,  STRING_FORMAT(crev.rev), STRING_FORMAT(crev.branch), STRING_FORMAT(crev.author), STRING_FORMAT(crev.dateold), STRING_FORMAT(crev.merged_date));
        else
            string_format(STRING_CONST(clog), STRING_CONST("%d | %.*s | %.*s | %.*s | commited on %.*s"),
                crev.id, STRING_FORMAT(crev.rev), STRING_FORMAT(crev.branch), STRING_FORMAT(crev.author), STRING_FORMAT(crev.date));

        ImGui::PushItemWidth(-1);
        ImGui::BeginGroup();
        ImGui::InputText("summary", STRING_CONST(clog), ImGuiInputTextFlags_ReadOnly);
        if (crev.base_summary.length > 0)
            ImGui::InputText("  pr", STRING_ARGS(crev.base_summary), ImGuiInputTextFlags_ReadOnly);
        ImGui::TextWrapped(crev.description.str);

        int irev = (int)revision+1;
        if (ImGui::SliderInt("", &irev, 1, (int)revisions.size()))
            session::set_revision_cursor((size_t)irev - 1);
        ImGui::EndGroup();
        ImGui::PopItemWidth();

        const bool show_patch_view = g_show_patch_view && crev.patch.length > 0;
        if (show_patch_view)
        {
            scoped_string_t patch_window_title = string_allocate_format(STRING_CONST("%.*s - patch"), STRING_FORMAT(crev.rev));
            if (ImGui::Begin(patch_window_title, &g_show_patch_view, ImVec2(600, 400), 0.8f,
                ImGuiWindowFlags_ResizeFromAnySide | ImGuiWindowFlags_HorizontalScrollbar))
            {
                ImGui::PushItemWidth(-1);
                ImGui::TextWrapped(crev.description.str);
                ImGui::PopItemWidth();
                ImGui::InputTextMultiline("  patch", (char*)crev.patch.str, crev.patch.length, ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
            }

            ImGui::End();
        }
        
        if (ImGui::BeginChild("CodeView", ImGui::GetContentRegionAvail(), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            size_t line_count = array_size(crev.annotations);
            if (line_count > 0)
            {
                for (int i = 0, max_line_digit_count = num_digits(line_count); i < line_count; ++i)
                {
                    const string_t& line = crev.annotations[i];
                    const auto pos = string_find_string(STRING_ARGS(line), STRING_ARGS(crev.rev), 0);
                    if (pos != STRING_NPOS)
                        ImGui::TextColored(ImColor(0.6f, 1.0f, 0.6f), "%0*d: %.*s", max_line_digit_count, i + 1, STRING_FORMAT(line));
                    else
                        ImGui::Text("%0*d: %.*s", max_line_digit_count, i + 1, STRING_FORMAT(line));
                }
            }
            else
            {
                ImGui::Text("Fetching annotations, be right back...");
            }

            ImGui::EndChild();
        }
    }
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

void render_progress()
{
    if (!g_task_bar_list)
        return;

    const auto& stats = fetch_annotations_stats();
    if (stats.total == 0)
        return;

    progress_set(stats.fetched, stats.total);
    if (stats.fetched == stats.total)
        progress_finalize();
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
        render_progress();
        render_main_window(k_MainWindowTitle, (float)window_width, (float)window_height);
    }

    ImGui::End();
}

void shutdown()
{
    progress_finalize();
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
    application.company = string_const(STRING_CONST(PRODUCT_COMPANY));
    application.version = string_to_version(STRING_CONST(PRODUCT_VERSION));
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
