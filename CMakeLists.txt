cmake_minimum_required(VERSION 3.25)
project(BionicaEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_executable(BionicaEditor
    src/main.cpp
)

target_link_libraries(BionicaEditor PRIVATE
    Vulkan::Vulkan
    glfw
    imgui::imgui
    glm::glm
)

if(MSVC)
    target_compile_options(BionicaEditor PRIVATE /W4 /permissive- /utf-8)
endif()

find_program(GLSLC_EXECUTABLE glslc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/VulkanSDK/1.4.357.0/Bin"
    REQUIRED
)
message(STATUS "glslc found: ${GLSLC_EXECUTABLE}")

set(SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BINARY_DIR}")

file(GLOB SHADER_SOURCES
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag"
)

set(SPIRV_BINARIES "")
foreach(SHADER ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT "${SHADER_BINARY_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )
    list(APPEND SPIRV_BINARIES ${SPIRV_OUTPUT})
endforeach()

add_custom_target(BionicaShaders DEPENDS ${SPIRV_BINARIES})
add_dependencies(BionicaEditor BionicaShaders)

target_compile_definitions(BionicaEditor PRIVATE
    BIONICA_SHADER_DIR="${SHADER_BINARY_DIR}"
)
