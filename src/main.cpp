#include <GLFW/glfw3.h>
#include <iostream>

// Include Dear ImGui core and backend headers
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// Include our oscilloscope manager header
#include "headers/HP54602B_manager.h"

//include imgui headers for context and IO


OscilloscopeManager* g_oscilloscopeManager = nullptr;

int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Scomesh Custom Workspace", NULL, NULL);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable VSync to lock at 60 FPS cleanly

    // -------------------------------------------------------------------------
    // INITIALIZE IMGUI CONTEXT
    // -------------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // ENABLE DOCKING BRANCH CAPABILITIES

    // Setup sleek dark style out of the box
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");


    g_oscilloscopeManager = new OscilloscopeManager(); 


    // -------------------------------------------------------------------------
    // MAIN APPLICATION RENDERING LOOP
    // -------------------------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            g_oscilloscopeManager->drawConnectionControls();
            g_oscilloscopeManager->drawTriggerControl();
            std::vector<uint8_t> latestData;
            g_oscilloscopeManager->GetLatestData(latestData);
            g_oscilloscopeManager->drawBasicPloter(latestData);
           
        }

        //DEMO WINDOW (REMOVE LATER)
        ImGui::ShowDemoWindow();
        // Step 3: Rendering calculations
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        
        // Clear screen with a flat backdrop color before rendering the interface UI
        glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Step 4: Pass ImGui's compiled draw data directly to modern OpenGL
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // -------------------------------------------------------------------------
    // CLEANUP RESOURCES
    // -------------------------------------------------------------------------
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    delete g_oscilloscopeManager;
    return 0;
}