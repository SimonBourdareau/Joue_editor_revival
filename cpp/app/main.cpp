#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "about.hpp"
#include "editor.hpp"
#include "fonts.hpp"
#include "logo.hpp"
#include "texture.hpp"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

namespace fs = std::filesystem;

namespace {

std::string defaultDataDir() {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");
    return std::string(appData ? appData : ".") + "\\JoueEditorNG";
#endif
    const char* home = std::getenv("HOME");
    std::string base = home ? home : ".";
#ifdef __APPLE__
    return base + "/Library/Application Support/JoueEditorNG";
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    return (xdg ? std::string(xdg) : base + "/.local/share") + "/JoueEditorNG";
#endif
}

// Writes the current framebuffer as a binary PPM; used to check the layout without a screen.
bool saveScreenshot(const char* path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int row = height - 1; row >= 0; --row) out.write(reinterpret_cast<const char*>(&pixels[static_cast<std::size_t>(row) * width * 3]), static_cast<std::streamsize>(width) * 3);
    return true;
}

const char* firstExistingFont() {
    static const char* candidates[] = {
        "/System/Library/Fonts/SFNS.ttf", "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", "C:\\Windows\\Fonts\\segoeui.ttf"};
    for (const char* path : candidates) {
        if (fs::exists(path)) return path;
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    EditorOptions options;
    options.dataDir = defaultDataDir();
    const char* screenshot = nullptr;
    int frames = 3, width = 1360, height = 860;
    float forcedScale = 0.0f;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (arg == "--data") options.dataDir = next();
        else if (arg == "--library") options.library = next();
        else if (arg == "--select") options.select = next();
        else if (arg == "--element") options.element = next();
        else if (arg == "--designer") options.designer = next();
        else if (arg == "--live") options.live = true;
        else if (arg == "--about") options.about = true;
        else if (arg == "--safe-graphics") options.safeGraphics = true;
        else if (arg == "--no-touches") options.noTouches = true;
        else if (arg == "--lang") options.language = next();
        else if (arg == "--no-update-check") options.noUpdateCheck = true;
        else if (arg == "--single-window") options.singleWindow = true;
        else if (arg == "--designer-element") options.designerElement = std::atoi(next());
        else if (arg == "--open-first") options.openFirst = true;
        else if (arg == "--screenshot") { screenshot = next(); options.noUpdateCheck = true; options.singleWindow = true; }
        else if (arg == "--frames") frames = std::atoi(next());
        else if (arg == "--size") std::sscanf(next(), "%dx%d", &width, &height);
        else if (arg == "--scale") forcedScale = static_cast<float>(std::atof(next()));
        else if (arg == "-h" || arg == "--help") {
            std::puts("joue_editor [--library FILE] [--data DIR] [--select MODULE] [--open-first] [--scale S] [--safe-graphics] [--no-touches] [--lang en|fr] [--single-window] [--screenshot OUT.ppm --frames N --size WxH]");
            return 0;
        }
    }

    if (!glfwInit()) {
        std::fputs("Could not start GLFW (is a display available?).\n", stderr);
        return 1;
    }
    // The OpenGL context asked for is the newest one the editor can use; if the system cannot give it (virtual machines, remote sessions,
    // old drivers) the next older one is tried. --safe-graphics starts from the oldest and draws the device without a texture.
    struct GlChoice {
        int major, minor;
        bool core;
        const char* glsl;
    };
#ifdef __APPLE__
    static const GlChoice choices[] = {{3, 2, true, "#version 150"}};
#else
    static const GlChoice choices[] = {{3, 3, true, "#version 330"}, {3, 0, false, "#version 130"}, {2, 1, false, "#version 120"}};
#endif
    const int choiceCount = static_cast<int>(sizeof choices / sizeof choices[0]);
    GLFWwindow* window = nullptr;
    const char* glsl = choices[0].glsl;
    int chosen = 0;
    for (int i = options.safeGraphics ? choiceCount - 1 : 0; i < choiceCount && !window; ++i) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, choices[i].major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, choices[i].minor);
        if (choices[i].core) glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#ifdef _WIN32
        glfwWindowHint(GLFW_ALPHA_BITS, 0);  // nothing here needs destination alpha, and a window without it cannot be composed with see-through areas
#endif
        window = glfwCreateWindow(width, height, about::AppName, nullptr, nullptr);
        if (window) {
            glsl = choices[i].glsl;
            chosen = i;
        }
    }
    if (!window) {
        std::fputs("Could not create the window (no OpenGL 2.1 or newer is available).\n", stderr);
        glfwTerminate();
        return 1;
    }
    {
        // the icon of the window, in the taskbar and the title bar (macOS takes it from the app bundle, where the system expects it)
        std::vector<unsigned char> pixels[3];
        GLFWimage images[3];
        int count = 0;
        for (int size : {256, 48, 32}) {
            LogoImage logo = logoPng(size);
            if (decodePng(logo.data, logo.size, pixels[count], images[count].width, images[count].height)) {
                images[count].pixels = pixels[count].data();
                ++count;
            }
        }
        if (count > 0) glfwSetWindowIcon(window, count, images);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(options.safeGraphics ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!options.singleWindow) io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    // Fonts are rasterised at the framebuffer scale so text stays sharp on Retina displays, while layout follows the content scale.
    int windowWidth = 1, windowHeight = 1, framebufferWidth = 1, framebufferHeight = 1;
    glfwGetWindowSize(window, &windowWidth, &windowHeight);
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window, &xscale, &yscale);
    float framebufferScale = static_cast<float>(framebufferWidth) / static_cast<float>(windowWidth);
    float uiScale = xscale / framebufferScale;
    if (uiScale < 1.0f) uiScale = 1.0f;
    if (forcedScale > 0.0f) uiScale = forcedScale;  // --scale: size of text and controls, for screenshots and screens that report no scale
    const float fontSize = 15.0f;
    if (const char* font = firstExistingFont()) {
        io.Fonts->AddFontFromFileTTF(font, fontSize * uiScale * framebufferScale);
        Fonts::heading = io.Fonts->AddFontFromFileTTF(font, fontSize * 2.0f * uiScale * framebufferScale);  // headings: made at their own size
    } else {
        ImFontConfig config;
        config.SizePixels = fontSize * uiScale * framebufferScale;
        io.Fonts->AddFontDefault(&config);
        ImFontConfig big;
        big.SizePixels = fontSize * 2.0f * uiScale * framebufferScale;
        Fonts::heading = io.Fonts->AddFontDefault(&big);
    }
    io.FontGlobalScale = 1.0f / framebufferScale;
    ImGuiStyle& style = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.WindowRounding = 4.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.ScaleAllSizes(uiScale);
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl);

    Editor editor(options);
    {
        auto text = [](GLenum name) {
            const char* value = reinterpret_cast<const char*>(glGetString(name));
            return std::string(value ? value : "?");
        };
        std::string context = std::to_string(choices[chosen].major) + "." + std::to_string(choices[chosen].minor) + (choices[chosen].core ? " core" : "");
        editor.setGraphicsInfo(text(GL_VENDOR) + " | " + text(GL_RENDERER) + " | OpenGL " + text(GL_VERSION) + " | context asked: " + context + " | " + choices[chosen].glsl);
    }
    std::string lastTitle;
    int frame = 0;
    while (!editor.wantsQuit()) {
        glfwPollEvents();
        if (glfwWindowShouldClose(window)) {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            editor.askQuit();
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        editor.draw();
        ImGui::Render();

        std::string title = editor.title();
        if (title != lastTitle) {
            glfwSetWindowTitle(window, title.c_str());
            lastTitle = title;
        }
        int displayWidth = 0, displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* mainContext = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(mainContext);
        }
        ++frame;
        if (screenshot && frame >= frames) {
            bool ok = saveScreenshot(screenshot, displayWidth, displayHeight);
            std::printf("screenshot %s (%dx%d): %s\n", screenshot, displayWidth, displayHeight, ok ? "written" : "FAILED");
            break;
        }
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
