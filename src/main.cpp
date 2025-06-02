/*
 * Copyright 2025 Barracuda Bits
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this work and associated documentation files (the "Work"), to deal in the
 * Work without restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies of the Work,
 * and to permit persons to whom the Work is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Work.
 *
 * THE WORK IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
 * PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM, OUT OF, OR IN
 * CONNECTION WITH THE WORK OR THE USE OR OTHER DEALINGS IN THE WORK.
 */

 // EXTERNAL INCLUDES
#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>
#include <iostream>
#include <vector>
#include <time.h>
// INTERNAL INCLUDES
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_opengl2.h"
#include "resource.h"

typedef unsigned char byte;

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

#pragma comment( lib, "opengl32.lib" )

//********************************************************************************************
typedef enum log_severity_e
{
    INFO,
    WARN,
    FAIL,
    SUCC,
    CRIT,
    DBUG,
    TRCE
} log_severity_e;
//********************************************************************************************
typedef struct log_entry_t
{
    u64 timestamp;
    log_severity_e severity;
    std::string origin;
    std::string content;
} log_entry_t;
//********************************************************************************************
static HGLRC g_GLRC = NULL;
static HDC g_HDC = NULL;
static HWND g_HWND = NULL;
static bool g_Running = true;
static std::vector<log_entry_t> log_messages;
static bool auto_scroll = false;
static bool scroll_refresh = false;
static HANDLE evenlight_handle;
static char filter_buf[128];
//********************************************************************************************
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);
//********************************************************************************************
void open_in_browser(const std::string& url)
{
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    std::string command = "open \"" + url + "\"";
    system(command.c_str());
#elif defined(__linux__)
    std::string command = "xdg-open \"" + url + "\"";
    system(command.c_str());
#else
#warning Unsupported platform
#endif
}
//********************************************************************************************
const char* severity_to_string(log_severity_e severity)
{
    switch (severity)
    {
    case INFO: return "INFO";
    case WARN: return "WARN";
	case FAIL: return "FAIL";
    case SUCC: return "SUCC";
    case CRIT: return "CRIT";
    case DBUG: return "DBUG";
    case TRCE: return "TRCE";
    default: return "UNKN";
    }
}
//********************************************************************************************
log_severity_e parse_severity(const char* str) {
    if (_stricmp(str, "INFO") == 0) return INFO;
    if (_stricmp(str, "WARN") == 0) return WARN;
	if (_stricmp(str, "FAIL") == 0) return FAIL;
    if (_stricmp(str, "SUCC") == 0) return SUCC;
    if (_stricmp(str, "CRIT") == 0) return CRIT;
    if (_stricmp(str, "DBUG") == 0) return DBUG;
    if (_stricmp(str, "TRCE") == 0) return TRCE;
    return INFO; // default
}
//********************************************************************************************
std::vector<std::string> find_files(const char* extension)
{
    std::vector<std::string> result;

    // Search for all files in the current directory
    WIN32_FIND_DATAA find_data;
    HANDLE hFind = FindFirstFileA("*.*", &find_data);

    if (hFind == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if (!(find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            const char* filename = find_data.cFileName;
            const char* ext = strrchr(filename, '.');
            if (ext && _stricmp(ext, extension) == 0) {
                result.push_back(filename);
            }
        }
    } while (FindNextFileA(hFind, &find_data) != 0);

    FindClose(hFind);
    return result;
}
//********************************************************************************************
bool save_logs_to_csv(const char* filename, std::vector<log_entry_t>& logs) {
    FILE* f = fopen(filename, "w");
    if (!f) return false;

    // Write CSV header
    fprintf(f, "Timestamp,Severity,Origin,Content\n");

    for (int i = 0; i < logs.size(); ++i) {
        const log_entry_t* log = &logs[i];
        fprintf(f, "%llu,%s,%s,%s\n",
            log->timestamp,
            severity_to_string(log->severity),
            log->origin.c_str(),
            log->content.c_str());
    }

    fclose(f);
    return true;
}
//********************************************************************************************
bool load_logs_from_csv(const char* filename, std::vector<log_entry_t>& logs) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;

    logs.clear(); // clear existing logs

    char line[1024];
    bool is_header = true;

    while (fgets(line, sizeof(line), f))
    {
        if (is_header)
        {
            // Check for: [Timestamp,Severity,Origin,Content]
            char* tok = strtok(line, ",");
            if (strcmp(tok, "Timestamp") != 0)
            {
                fclose(f);
                return false;
            }
            tok = strtok(NULL, ",");
            if (strcmp(tok, "Severity") != 0)
            {
                fclose(f);
                return false;
            }
            tok = strtok(NULL, ",");
            if (strcmp(tok, "Origin") != 0)
            {
                fclose(f);
                return false;
            }
            tok = strtok(NULL, "\n");
            if (strcmp(tok, "Content") != 0)
            {
                fclose(f);
                return false;
            }
            is_header = false;
            continue;
        }

        // Tokenize CSV line
        char* tok = strtok(line, ",");
        if (!tok) continue;

        log_entry_t entry = {};

        // Parse timestamp
        entry.timestamp = _strtoui64(tok, NULL, 10);

        // Parse severity
        tok = strtok(NULL, ",");
        if (!tok) continue;
        entry.severity = parse_severity(tok);

        // Parse origin
        tok = strtok(NULL, ",");
        if (!tok) continue;
        entry.origin = tok;

        // Parse content (may contain quotes or commas)
        tok = strtok(NULL, "\n");
        if (!tok) continue;

        // Remove wrapping quotes if any
        char* content = tok;
        size_t len = strlen(content);
        if (len >= 2 && content[0] == '"' && content[len - 1] == '"') {
            content[len - 1] = '\0';
            content++;
        }

        entry.content = content;

        logs.push_back(entry);
    }

    if (auto_scroll) scroll_refresh = true;

    fclose(f);
    return true;
}
//********************************************************************************************
void render_line_with_links (const std::string& line, ImVec4 text_color)
{
    ImGui::PushStyleColor(ImGuiCol_Text, text_color);

    const std::string http = "http://";
    const std::string https = "https://";
    size_t pos = 0;
    const size_t len = line.length();

    while (pos < len)
    {
        // Find next occurrence of http:// or https://
        size_t http_pos = line.find(http, pos);
        size_t https_pos = line.find(https, pos);

        // Choose the earliest one found
        size_t link_start = std::string::npos;
        if (http_pos != std::string::npos && https_pos != std::string::npos)
			link_start = http_pos < https_pos ? http_pos : https_pos;
        else if (http_pos != std::string::npos)
            link_start = http_pos;
        else if (https_pos != std::string::npos)
            link_start = https_pos;

        // If no more links found, print the rest as normal text
        if (link_start == std::string::npos)
        {
            std::string normal_text = line.substr(pos);
            ImGui::TextWrapped("%s", normal_text.c_str());
            break;
        }

        // Print normal text before the link
        if (link_start > pos)
        {
            std::string normal_text = line.substr(pos, link_start - pos);
            ImGui::TextWrapped("%s", normal_text.c_str());
            ImGui::SameLine(0.0f, 0.0f);
        }

        // Find the end of the link (first space or end of string)
        size_t link_end = line.find_first_of(" \t\n", link_start);
        if (link_end == std::string::npos)
            link_end = len;

        std::string link = line.substr(link_start, link_end - link_start);

        // Render the link as a button
        if (ImGui::Button(link.c_str()))
        {
            open_in_browser(link);
        }

        pos = link_end;

        // Only SameLine if there's still more content
        if (pos < len)
        {
            ImGui::SameLine(0.0f, 0.0f);
        }
    }

    ImGui::PopStyleColor();
}
//********************************************************************************************
void show_log_window(std::vector<log_entry_t>& logs)
{
    // Fullscreen setup
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    // ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_MenuBar;

    ImGui::Begin("Log Viewer", NULL, window_flags);

    // Menu Bar
    static bool open_load_modal = false;
    static bool open_save_modal = false;
    static char save_filename[256] = "evenlight.log";
    static bool save_failed = false;
    if (ImGui::BeginMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Load Log"))
            {
                open_load_modal = true;
            }
            if (ImGui::MenuItem("Save Log"))
            {
                open_save_modal = true;
                save_failed = false;
            }
            if (ImGui::MenuItem("Quit"))
            {
                g_Running = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Macro"))
        {
            if (ImGui::MenuItem("Start Evenlight"))
            {
                // start process
                PROCESS_INFORMATION pi;
                memset(&pi, 0, sizeof(pi));
                STARTUPINFOA si;
                memset(&si, 0, sizeof(si));
                si.cb = sizeof(si);  // MUST set this!
				// build first argument string

				char path[MAX_PATH];
                memset(&path, 0, sizeof(path));
                GetCurrentDirectoryA(MAX_PATH, path);
				strcat_s(path, "\\evenlight.exe");

                const char* args = "--logging --dump";
                if (strlen(path) > 0)
                {
                    // prepend path to args
                    char full_args[MAX_PATH + 10] = { 0 };
                    sprintf_s(full_args, "%s %s", path, args);
                    args = full_args;
				}
                if (CreateProcessA(
                    "evenlight.exe",
                    (char*)args,
                    NULL, NULL, NULL, NULL, NULL, NULL,
                    &si,
                    &pi))
                {
                    evenlight_handle = pi.hProcess;
                }
            }
            if (ImGui::MenuItem("Kill Evenlight"))
            {
                // Kill program logic
                TerminateProcess(evenlight_handle, 0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::Button("Clear"))
        {
            log_messages.clear();
        }
        ImGui::Text("Filter");
        ImGui::PushItemWidth(200);
        ImGui::InputTextWithHint("##Filter", "Text or severity", filter_buf, sizeof(filter_buf));
        ImGui::PopItemWidth();

        ImGui::Checkbox("Auto Scroll", &auto_scroll);
        ImGui::EndMenuBar();
    }
    // Modal implementation
    if (open_save_modal)
    {
        ImGui::OpenPopup("Save Log As");
        if (ImGui::BeginPopupModal("Save Log As", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Enter filename to save log:");
            ImGui::InputText("##Filename", save_filename, IM_ARRAYSIZE(save_filename));

            if (save_failed)
            {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to save file!");
            }

            ImGui::Separator();

            if (ImGui::Button("Save"))
            {
                if (!save_logs_to_csv(save_filename, logs))
                {
                    save_failed = true;
                }
                else
                {
                    open_save_modal = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                open_save_modal = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }
    else
    {
        open_save_modal = false;
    }

    if (open_load_modal)
    {
        ImGui::OpenPopup("Load Log");
        if (ImGui::BeginPopupModal("Load Log", NULL, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Select a file:");
            std::vector<std::string> files;
            static u32 selected_index = 0;
            // Load list of .csv files once when modal is opened
            if (files.empty()) {
                files = find_files(".log");
            }

            if (!files.empty())
            {
                // Build combo box from file list
                const char* preview_value = files[selected_index].c_str();
                if (ImGui::BeginCombo("##file_combo", preview_value))
                {
                    for (int i = 0; i < (int)files.size(); ++i)
                    {
                        bool is_selected = (selected_index == i);
                        if (ImGui::Selectable(files[i].c_str(), is_selected))
                        {
                            selected_index = i;
                        }
                        if (is_selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(1, 0, 0, 1), "No *.log files found.");
            }

            ImGui::Separator();

            if (ImGui::Button("Load"))
            {
                if (!files.empty())
                {
                    log_messages.clear();
                    const std::string& selected_filename = files[selected_index];
                    load_logs_from_csv(selected_filename.c_str(), log_messages);
                }
                open_load_modal = false;
                ImGui::CloseCurrentPopup();
                files.clear(); // Reset for next open
            }

            ImGui::SameLine();

            if (ImGui::Button("Cancel"))
            {
                open_load_modal = false;
                ImGui::CloseCurrentPopup();
                files.clear(); // Reset for next open
            }

            ImGui::EndPopup();
        }
    }
    else
    {
        open_load_modal = false;
    }

    // Table for logs
    ImGui::BeginChild("LogTableRegion", ImVec2(0, 0), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    if (ImGui::BeginTable("LogTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Datetime");
        ImGui::TableSetupColumn("Severity");
        ImGui::TableSetupColumn("Origin");
        ImGui::TableSetupColumn("Content");
        ImGui::TableHeadersRow();

        std::vector<log_entry_t*> filtered_logs;
        filtered_logs.clear();

        if (strlen(filter_buf) > 0)
        {
            for (auto& log : logs)
            {
                if (strstr(log.content.c_str(), filter_buf) ||
                    strstr(severity_to_string(log.severity), filter_buf) ||
                    strstr(log.origin.c_str(), filter_buf))
                {
                    filtered_logs.push_back(&log);
                }
            }
        }
        else
        {
            for (auto& log : logs)
            {
                filtered_logs.push_back(&log);
            }
        }

        for (int i = 0; i < filtered_logs.size(); ++i)
        {
            ImGui::TableNextRow();

            time_t tm = logs[i].timestamp;
            char buffer[26];
            struct tm* tm_info = localtime(&tm);
            strftime(buffer, 26, "%Y-%m-%d %H:%M:%S", tm_info);

            ImVec4 text_color;
            switch (filtered_logs[i]->severity)
            {
                case INFO:
                {
                    text_color = { 0.6f, 0.8f, 0.8f, 1.0f };
                    break;
                }
                case WARN:
                {
                    text_color = { 0.8f, 0.8f, 0.0f, 1.0f };
                    break;
                }
                case FAIL:
                {
                    text_color = { 1.0f, 0.3f, 0.3f, 1.0f };
                    break;
			    }
                case SUCC:
                {
                    text_color = { 0.0f, 1.0f, 0.0f, 1.0f };
                    break;
                }
                case CRIT:
                {
                    text_color = { 1.0f, 0.0f, 0.0f, 1.0f };
                    break;
                }
                case DBUG:
                {
                    text_color = { 0.1f, 0.7f, 0.1f, 1.0f };
                    break;
                }
                case TRCE:
                {
                    text_color = { 0.8f, 0.2f, 0.8f, 1.0f };
                    break;
                }
                default:
                {
                    text_color = { 1.0f, 0.0f, 1.0f, 1.0f };
                    break;
                }
            }

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(text_color, "%s", buffer);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(text_color, "%s", severity_to_string(filtered_logs[i]->severity));

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(text_color, "%s", filtered_logs[i]->origin.c_str());

            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_Text, text_color);
			render_line_with_links(filtered_logs[i]->content, text_color);
            ImGui::PopStyleColor();
        }

        if (scroll_refresh && ImGui::GetScrollY() < ImGui::GetScrollMaxY())
        {
            ImGui::SetScrollHereY(1.0f);
			scroll_refresh = false;
        }

        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::End(); // End main window
}
//********************************************************************************************
LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            glViewport(0, 0, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_DESTROY:
        g_Running = false;
        PostQuitMessage(0);
        return 0;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT* cds = (COPYDATASTRUCT*)lParam;
        if (cds->dwData == 0xBA88AC0DA) // Custom data identifier
        {
            // each message is a single log entry in the format: "timestamp;severity;origin;content"
            char* data = (char*)cds->lpData;
            char* tok = strtok(data, ",");
            if (!tok) return 0;

            log_entry_t entry = {};

            entry.timestamp = _strtoui64(tok, NULL, 10);
            tok = strtok(NULL, ",");
            if (!tok) return 0;
            entry.severity = parse_severity(tok);
            tok = strtok(NULL, ",");
            if (!tok) return 0;
            entry.origin = tok;
            tok = strtok(NULL, "\n");
            if (!tok) return 0;
            // Remove wrapping quotes if any
            char* content = tok;
            size_t len = strlen(content);
            if (len >= 2 && content[0] == '"' && content[len - 1] == '"')
            {
                content[len - 1] = '\0';
                content++;
            }

            entry.content = content;
            log_messages.push_back(entry);

            if (auto_scroll) scroll_refresh = true;
        }
    }
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}
//********************************************************************************************
void set_vsync(bool sync)
{
    typedef BOOL(APIENTRY* PFNWGLSWAPINTERVALPROC)(int);
    PFNWGLSWAPINTERVALPROC wglSwapIntervalEXT = 0;

    const char* extensions = (char*)glGetString(GL_EXTENSIONS);

    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALPROC)wglGetProcAddress("wglSwapIntervalEXT");

    if (wglSwapIntervalEXT)
        wglSwapIntervalEXT(sync);
}
//********************************************************************************************
bool create_gl_window(const char* title, int width, int height)
{
    WNDCLASS wc = { 0 };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"BrcdLogger";
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ENGINE_ICON));
    RegisterClass(&wc);

    // convert title to wchar_t
    wchar_t buffer[128];
    size_t num_converted;
    errno_t err = mbstowcs_s(&num_converted, buffer, title, 128);

    g_HWND = CreateWindow(wc.lpszClassName, buffer, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, NULL, NULL, wc.hInstance, NULL);

    g_HDC = GetDC(g_HWND);

    PIXELFORMATDESCRIPTOR pfd =
    {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32,
        0,0,0,0,0,0, 0, 0, 0, 0, 0, 0, 0,
        24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0
    };
    int pf = ChoosePixelFormat(g_HDC, &pfd);
    SetPixelFormat(g_HDC, pf, &pfd);
    g_GLRC = wglCreateContext(g_HDC);
    wglMakeCurrent(g_HDC, g_GLRC);

    set_vsync(true);

    ShowWindow(g_HWND, SW_SHOW);
    return true;
}
//********************************************************************************************
void cleanup()
{
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    if (g_GLRC)
    {
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(g_GLRC);
    }
    if (g_HDC && g_HWND)
    {
        ReleaseDC(g_HWND, g_HDC);
    }
    if (g_HWND)
    {
        DestroyWindow(g_HWND);
    }
    UnregisterClass(L"BrcdLogger", GetModuleHandle(NULL));
}
//********************************************************************************************
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    if (!create_gl_window("bSpy", 1024, 768))
    {
        return 1;
    }

    // Setup Dear ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO* io = &ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_HWND);
    ImGui_ImplOpenGL2_Init();

    time_t tm;
    time(&tm);

    // Main loop
    MSG msg;
    while (g_Running)
    {
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Example ImGui window
        show_log_window(log_messages);

        ImGui::Render();
        glViewport(0, 0, (int)io->DisplaySize.x, (int)io->DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        SwapBuffers(g_HDC);
    }

    cleanup();
    return 0;
}
//********************************************************************************************
