#pragma once
#include <cstdio>

#include "broccommon.h"
#include "brocmath.h"
// sdl + opengl
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
// imgui
#include <imgui.h>
#include "imgui_bindings/imgui_impl_sdl2.h"
#include "imgui_bindings/imgui_impl_opengl3.h"

namespace {
void checkeq(int actual, int line, int expected = 0) {
  if (actual != expected) {
    std::printf("%d: sdl error: %s\n", line, SDL_GetError());
    SDL_Quit();
    exit(1);
  }
}
template <typename T> void checknz(T actual) {
  if (actual == 0) {
    std::printf("sdl error: %s\n", SDL_GetError());
    SDL_Quit();
    exit(1);
  }
}
} // namespace

namespace broc {

class ShaderProgram {
public:
  ShaderProgram(const char *vertex_shader, const char *fragment_shader) {
    char logBuffer[256];
    GLint status;

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertex_shader, NULL);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      glGetShaderInfoLog(vs, sizeof(logBuffer), 0, logBuffer);
      std::printf("vertex shaderlog: %s\n", logBuffer);
      throw std::runtime_error("vertex shader error");
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragment_shader, NULL);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
      glGetShaderInfoLog(fs, sizeof(logBuffer), 0, logBuffer);
      std::printf("fragment shaderlog: %s\n", logBuffer);
      throw std::runtime_error("fragment shader error");
    }

    id_ = glCreateProgram();
    glAttachShader(id_, fs);
    glAttachShader(id_, vs);
    glLinkProgram(id_);
    glGetProgramiv(id_, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
      glGetProgramInfoLog(id_, sizeof(logBuffer), 0, logBuffer);
      std::printf("shader program log: %s\n", logBuffer);
      throw std::runtime_error("shader program error");
    }
  }

  void uniformMatrix4fv(const char *name, const glm::mat4 &m) {
    glUniformMatrix4fv(glGetUniformLocation(id_, name), 1, GL_FALSE, glm::value_ptr(m));
  }
  void uniform3fv(const char *name, const glm::vec3 &v) {
    glUniform3fv(glGetUniformLocation(id_, name), 1, &v[0]);
  }
  void useProgram() const { glUseProgram(id_); }

private:
  GLuint id_;
};

glm::vec3 sphericalToCartesian(float phi, float theta, float radius) {
  return glm::vec3(
    radius * std::sin(phi) * std::sin(theta),
    radius * std::cos(phi),
    radius * std::sin(phi) * std::cos(theta)
  );
}

class Camera {
public:
  float theta;
  float phi;
  float amp;
  int screenWidth;
  int screenHeight;
  glm::mat4 projM;
  glm::mat4 viewM;
  glm::vec3 cameraPos;
  void updateMatrices() {
    projM = glm::perspective(brocseg::math::pi * 0.25f, (float)screenWidth / (float)screenHeight,
                             0.1f, 1000.f);
    cameraPos = sphericalToCartesian(phi, theta, amp);
    viewM = glm::lookAt(cameraPos, glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0));
  }
};

class OpenGLRenderer {
private:
  SDL_Window *win = nullptr;
  SDL_GLContext glContext = 0;

public:
  SDL_Window *getSDLWindow() { return win; }
  SDL_GLContext getGLContext() { return glContext; }
  ~OpenGLRenderer() {
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(win);
    SDL_Quit();
  }
  OpenGLRenderer(const char *windowName, i32 windowWidth, i32 windowHeight) {
    checkeq(SDL_Init(SDL_INIT_VIDEO), __LINE__);
    int gl_context_major_version = 4;
    int gl_context_minor_version = 6;
    int gl_doublebuffer = 1;
    int gl_depth_size = 24;
    int gl_context_flags = SDL_GL_CONTEXT_DEBUG_FLAG;

    checkeq(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_context_major_version), __LINE__);
    checkeq(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_context_minor_version), __LINE__);
    checkeq(SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, gl_doublebuffer), __LINE__);
    checkeq(SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, gl_depth_size), __LINE__);
    checkeq(SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, gl_context_flags), __LINE__);

    win = SDL_CreateWindow(windowName, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowWidth,
                           windowHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    checknz(win);

    glContext = SDL_GL_CreateContext(win);
    checknz(glContext);

    checkeq(SDL_GL_MakeCurrent(win, glContext), __LINE__);

    checkeq(SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &gl_context_major_version), __LINE__);
    checkeq(SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &gl_context_minor_version), __LINE__);
    checkeq(SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &gl_doublebuffer), __LINE__);
    checkeq(SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &gl_depth_size), __LINE__);

    // load gl functions
    if (!gladLoadGL()) {
      std::printf("Failed to initialize OpenGL context\n");
      exit(1);
    }

    // gl debug
    checkeq(SDL_GL_GetAttribute(SDL_GL_CONTEXT_FLAGS, &gl_context_flags), __LINE__);
    if (gl_context_flags & SDL_GL_CONTEXT_DEBUG_FLAG == 0) {
      std::printf("Failed to create debug gl context\n");
      exit(1);
    }
    glEnable(GL_DEBUG_OUTPUT);
    // glDebugMessageCallback(debugMessageCallback, 0);
    // gl debug

    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *sdlGLversion = glGetString(GL_VERSION);
    std::printf("Renderer: %s, version: %s\n", renderer, sdlGLversion);

    checkeq(SDL_GL_SetSwapInterval(1), __LINE__);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    ImGui_ImplSDL2_InitForOpenGL(win, glContext);
    ImGui_ImplOpenGL3_Init();

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
  }

  bool begFrame() {
    bool running = true;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        running = false;
      }
    }
    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    return running;
  }

  void endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(win);
  }
};

class Mesh {
public:
  class Vertex {
  public:
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
  };

  Mesh(const std::string &name) : name_(name) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);
  }

  void draw() const {
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<u32>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
  }

  void sendGl() {
    glBindVertexArray(vao);

    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(indices[0]), &indices[0],
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)offsetof(Vertex, color));
  }

  const char *getName() const { return name_.c_str(); }

  GLuint vao, vbo, ebo;
  std::vector<Vertex> vertices;
  std::vector<u32> indices;
  std::string name_;
};

} // namespace broc
