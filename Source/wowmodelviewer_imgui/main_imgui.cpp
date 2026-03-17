#include <glad/gl.h>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int /*argc*/, char* /*argv*/[])
{
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // Request OpenGL 4.6 compatibility context (matches the glad loader)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "WoW Model Viewer (ImGui)", nullptr, nullptr);
    if (!window)
    {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    // Load OpenGL functions via glad
    if (!gladLoadGL(glfwGetProcAddress))
    {
        fprintf(stderr, "Failed to initialise OpenGL loader (glad)\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    // Setup platform/renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    bool show_demo_window = true;

    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Full-window dockspace
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

        // Placeholder panels -- will be replaced with real controls during migration
        if (ImGui::Begin("3D Viewport"))
        {
            ImGui::Text("OpenGL render target will go here.");
            ImGui::Text("GL_VERSION: %s", glGetString(GL_VERSION));
        }
        ImGui::End();

        if (ImGui::Begin("File Browser"))
        {
            ImGui::Text("FileControl tree will go here.");
        }
        ImGui::End();

        if (ImGui::Begin("Animation"))
        {
            ImGui::Text("AnimControl will go here.");
        }
        ImGui::End();

        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // Render
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
