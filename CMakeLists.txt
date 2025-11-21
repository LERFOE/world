cmake_minimum_required(VERSION 3.21)
project(mycraft LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(MYCRAFT_WARNINGS "-Wall" "-Wextra" "-Wshadow" "-Wconversion" "-Wpedantic")

find_package(OpenGL REQUIRED)

include(FetchContent)

FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.3.8.zip
)

FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.zip
)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)

FetchContent_Declare(glad
    URL https://github.com/Dav1dde/glad/archive/refs/heads/c.zip
)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)

FetchContent_MakeAvailable(glfw imgui glm)

FetchContent_GetProperties(glad)
if(NOT glad_POPULATED)
    FetchContent_Populate(glad)
endif()

FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

add_library(glad STATIC ${glad_SOURCE_DIR}/src/glad.c)
target_include_directories(glad PUBLIC ${glad_SOURCE_DIR}/include)

set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

set(IMGUI_HEADERS
    ${imgui_SOURCE_DIR}/imgui.h
    ${imgui_SOURCE_DIR}/imgui_internal.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.h
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3_loader.h
)

set(MYCRAFT_SOURCES
    src/main.cpp
    src/shader.cpp
    src/camera.cpp
    src/block.cpp
    src/texture_atlas.cpp
    src/world.cpp
    src/chunk.cpp
    src/raycast.cpp
)

add_executable(mycraft
    ${MYCRAFT_SOURCES}
    ${IMGUI_SOURCES}
)

target_include_directories(mycraft PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${stb_SOURCE_DIR}
    ${glad_SOURCE_DIR}/include
)

if(APPLE)
    target_compile_definitions(mycraft PRIVATE GL_SILENCE_DEPRECATION)
endif()

target_compile_definitions(mycraft PRIVATE
    GLFW_INCLUDE_NONE
    IMGUI_IMPL_OPENGL_LOADER_GLAD
    MYCRAFT_PROJECT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
)

foreach(w ${MYCRAFT_WARNINGS})
    target_compile_options(mycraft PRIVATE ${w})
endforeach()

target_link_libraries(mycraft PRIVATE
    OpenGL::GL
    glfw
    glad
    glm::glm
)

install(TARGETS mycraft RUNTIME DESTINATION bin)
