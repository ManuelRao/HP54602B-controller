cmake_minimum_required(VERSION 3.14)
project(HP54602B_controller CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 1. FETCH GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.3.8
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# 2. FETCH DEAR IMGUI (DOCKING BRANCH)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking # This targets the live docking branch explicitly
)
FetchContent_MakeAvailable(imgui)

# 3. DEFINE OUR SOURCES
# ImGui doesn't have a default CMakeLists, so we manually compile its core files 
# along with the specific backends for GLFW and OpenGL3.
add_executable(${PROJECT_NAME} 
    src/main.cpp 
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# 4. LINK LIBRARIES AND INCLUDE DIRECTORIES
find_package(OpenGL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE 
    glfw 
    OpenGL::GL
)

# Tell our compiler where to find both ImGui's core headers and its backend headers
target_include_directories(${PROJECT_NAME} PRIVATE 
    include
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)