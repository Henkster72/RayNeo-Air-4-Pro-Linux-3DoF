cmake_minimum_required(VERSION 3.10)
project(RayNeoPinnedViewport)

add_executable(RayNeoPinnedViewport main.cpp)
target_include_directories(RayNeoPinnedViewport PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_dependencies(RayNeoPinnedViewport RayNeoSDK)
target_link_libraries(RayNeoPinnedViewport PRIVATE RayNeoSDK)

find_package(OpenGL REQUIRED)
target_link_libraries(RayNeoPinnedViewport PRIVATE OpenGL::GL)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(RayNeoPinnedViewport PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)
target_compile_definitions(RayNeoPinnedViewport PRIVATE SDL_MAIN_HANDLED)

rayneo_deploy_runtime(RayNeoPinnedViewport)

if (APPLE)
    set_target_properties(RayNeoPinnedViewport PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
else()
    set_target_properties(RayNeoPinnedViewport PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
endif()
