#include "timelapse.h"
#include "scm_proxy.h"
#include "session.h"

#include <foundation/string.h>
#include <foundation/log.h>
#include <foundation/process.h>
#include <foundation/hashstrings.h>
#include <foundation/windows.h>
#include "foundation/environment.h"
#include "foundation/array.h"
#include "foundation/foundation.h"

#include <imgui/imgui.h>

#include <vector>
#include <sstream>
#include <algorithm>

#define CTEXT(str) string_const(STRING_CONST(str))

namespace timelapse {

const char* APP_TITLE = "Timelapse";

std::vector<scm::revision_t> s_Revisions;

template<typename Out>
void split(const std::string &s, char delim, Out result) {
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> elems;
    split(s, delim, std::back_inserter(elems));
    return elems;
}

static const char* find_file_path_last_path_sep(const char* file_path, char sep = '\\')
{
    const char *p = strrchr(file_path, sep);
    if (p)
        return p;

    if (sep != '/')
        return find_file_path_last_path_sep(file_path, '/');

    return nullptr;
}

std::string execute_command(const char* cmd, const char* working_directory)
{
    std::string strResult;
    HANDLE hPipeRead, hPipeWrite;

    SECURITY_ATTRIBUTES saAttr = { sizeof(SECURITY_ATTRIBUTES) };
    saAttr.bInheritHandle = TRUE;   //Pipe handles are inherited by child process.
    saAttr.lpSecurityDescriptor = nullptr;

    // Create a pipe to get results from child's stdout.
    if (!CreatePipe(&hPipeRead, &hPipeWrite, &saAttr, 0))
        return strResult;

    STARTUPINFOA si = { sizeof(STARTUPINFOA) };
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.hStdOutput = hPipeWrite;
    si.hStdError = hPipeWrite;
    si.wShowWindow = SW_HIDE;       // Prevents cmd window from flashing. Requires STARTF_USESHOWWINDOW in dwFlags.

    PROCESS_INFORMATION pi = { nullptr };

    const BOOL fSuccess = CreateProcessA(nullptr, (LPSTR)cmd, nullptr, nullptr, TRUE, CREATE_NEW_CONSOLE, nullptr,
        working_directory ? working_directory : session::working_dir(), &si, &pi);
    if (!fSuccess)
    {
        CloseHandle(hPipeWrite);
        CloseHandle(hPipeRead);
        return strResult;
    }

    bool bProcessEnded = false;
    for (; !bProcessEnded;)
    {
        // Give some timeslice (50ms), so we won't waste 100% cpu.
        bProcessEnded = WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0;

        // Even if process exited - we continue reading, if there is some data available over pipe.
        for (;;)
        {
            char buf[1024];
            DWORD dwRead = 0;
            DWORD dwAvail = 0;

            if (!::PeekNamedPipe(hPipeRead, nullptr, 0, nullptr, &dwAvail, nullptr))
                break;

            if (!dwAvail) // no data available, return
                break;

            if (!::ReadFile(hPipeRead, buf, (DWORD)std::min(sizeof(buf) - 1, (size_t)dwAvail), &dwRead, nullptr) || !dwRead)
                // error, the child process might ended
                break;

            buf[dwRead] = 0;
            strResult += buf;
        }
    }

    CloseHandle(hPipeWrite);
    CloseHandle(hPipeRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return strResult;
}

bool setup(const char* file_path)
{
    session::setup(file_path);
    if (!session::is_valid())
        return false;

    std::string cmd = "hg log --template \"{rev}|{author|user}|{node|short}|" \
        "{date | shortdate}|{count(file_mods)}|{desc | strip | firstline}\\n\" " \
        "--no-merges -l 50 \"" + std::string(session::file_path()) + "\"";
    std::string output = execute_command(cmd.c_str(), session::working_dir());

    auto changes = split(output, '\n');
    for (const auto& c : changes)
    {
        auto infos = split(c, '|');
        scm::revision_t r;
        r.id = std::stoi(infos[0]);
        r.author = infos[1];
        r.rev = infos[2];
        r.date = infos[3];
        r.mods = std::stoi(infos[4]);
        r.description = infos[5];

        s_Revisions.push_back(r);
    }

    std::sort(s_Revisions.begin(), s_Revisions.end(), [](const scm::revision_t& a, const scm::revision_t& b)
    {
        return a.id < b.id;
    });

    return !output.empty();
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
            if (setup(arg.str))
                break;
        }
    }
    
    return 0;
}

void render_main_menu()
{
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File", true))
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

        ImGui::EndMenuBar();
    }
}

void render_main_window(const char* window_title, float window_width, float window_height)
{
    ImGui::SetWindowPos(window_title, ImVec2(0, 0));
    ImGui::SetWindowSize(window_title, ImVec2(window_width, window_height));

    render_main_menu();

    static int revision = (int)s_Revisions.size() - 1;

    ImGui::PushItemWidth(-1);
    ImGui::Text("Revision: %s - %s", session::file_path(), revision >= 0 ? s_Revisions[revision].rev.c_str() :
        "Nothing to timelapse. Select a file or call this program with the file path as the first argument.");

    if (!s_Revisions.empty())
    {
        scm::revision_t& r = s_Revisions[revision];
        ImGui::SliderInt("", &revision, 0, (int)s_Revisions.size() - 1);

        if (r.source.empty())
        {
            std::string cmd = "hg annotate --user -c -w -b -B -r " + r.rev + " \"" + session::file_path() + "\"";
            r.source = execute_command(cmd.c_str(), session::working_dir());
        }

        ImGui::BeginChild("CodeView", ImVec2(ImGui::GetWindowContentRegionWidth(), 0), true, ImGuiWindowFlags_HorizontalScrollbar);

        //ImGui::InputTextMultiline("", (char*)r.source.c_str(), r.source.size(), ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);

        auto lines = split(r.source, '\n');
        for (int i = 0; i < lines.size(); i++)
        {
            const auto pos = lines[i].find(r.rev);
            if (pos != std::string::npos)
                ImGui::TextColored(ImColor(0.6f, 1.0f, 0.6f), "%04d: %s", i, lines[i].c_str());
            else
                ImGui::Text("%04d: %s", i, lines[i].c_str());
        }
        ImGui::EndChild();
    }

    ImGui::PopItemWidth();
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
        render_main_window(k_MainWindowTitle, (float)window_width, (float)window_height);

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