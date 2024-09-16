cmake_minimum_required(VERSION 3.15)
project(brocseg CXX)

set(CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})
set(CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
message(${CMAKE_BINARY_DIR})

find_package(SDL2 REQUIRED)
find_package(GLEW REQUIRED)
find_package(GLAD REQUIRED)
find_package(GLM REQUIRED)
find_package(assimp REQUIRED)
find_package(imgui REQUIRED CONFIG)
find_package(OpenMesh REQUIRED)

add_executable(${PROJECT_NAME}
                src/brocseg.cpp
                src/imgui_bindings/imgui_impl_opengl3.cpp
                src/imgui_bindings/imgui_impl_opengl3.h
                src/imgui_bindings/imgui_impl_sdl2.cpp
                src/imgui_bindings/imgui_impl_sdl2.h
                )
target_link_libraries(${PROJECT_NAME} PRIVATE
  SDL2::SDL2main
  glad::glad
  GLEW::GLEW
  glm::glm
  assimp::assimp
  imgui::imgui
  openmesh::openmesh
)
