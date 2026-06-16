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


    g_oscilloscopeManager = new OscilloscopeManager("COM7"); // Replace "COM3" with your actual COM port
    if (!g_oscilloscopeManager->Connect()) {
        std::cerr << "Failed to connect to oscilloscope on COM3" << std::endl;
        delete g_oscilloscopeManager;
        return -1;
    }


    // -------------------------------------------------------------------------
    // MAIN APPLICATION RENDERING LOOP
    // -------------------------------------------------------------------------
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Step 1: Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Step 2: Define your UI Components (This is where your layout code lives)
        {
            // A simple control panel window
            ImGui::Begin("Oscilloscope Control Panel");
            ImGui::Text("Welcome to Scomesh Stage 1 sandbox.");
            
            if (ImGui::Button("Trigger Capture")) {
                // Placeholder for capture logic
                std::cout << "Triggering waveform capture..." << std::endl;
                g_oscilloscopeManager->SendCommand(":DIG:TRIG"); // Example SCPI command to trigger capture (replace with actual command as needed)
            }
            ImGui::End();

            std::vector<uint8_t> displayBuffer;
            g_oscilloscopeManager->GetLatestData(displayBuffer);

            std::vector<float> plotBuffer(displayBuffer.size());
            for (size_t i = 0; i < displayBuffer.size(); ++i) {
                // Cast the raw byte (0-255) to a float (0.0f - 255.0f)
                plotBuffer[i] = static_cast<float>(displayBuffer[i]);
            }

            // 3. Build your UI and draw the plot
            ImGui::Begin("Live Oscilloscope Feed");

            ImGui::Text("Data Points Received: %zu", displayBuffer.size());

            if (!plotBuffer.empty()) {
                // ImGui::PlotLines parameters:
                // 1. Label
                // 2. Pointer to the float array data (.data())
                // 3. Number of points to draw
                // 4. Value offset (0)
                // 5. Overlay text (NULL)
                // 6. Scale Min (0.0f for an 8-bit ADC)
                // 7. Scale Max (255.0f for an 8-bit ADC)
                // 8. Graph Size (ImVec2(0, 150) makes it dynamically stretch to window width, 150px high)
                
                ImGui::PlotLines(
                    "Raw ADC Stream", 
                    plotBuffer.data(), 
                    plotBuffer.size(), 
                    0, 
                    NULL, 
                    0.0f, 
                    255.0f, 
                    ImVec2(0, 150)
                );
            }

            ImGui::End();
        }

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