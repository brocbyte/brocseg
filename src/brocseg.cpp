#include <cstdio>
#include <vector>
#include <iostream>
#include <optional>
#include <stdexcept>

// sdl + opengl
#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

// imgui
#include <imgui.h>
#include "imgui_bindings/imgui_impl_sdl2.h"
#include "imgui_bindings/imgui_impl_opengl3.h"

// glm
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// openmesh
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/IO/MeshIO.hh>

using u32 = uint32_t;

namespace brocseg {
namespace sdl {

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
} // namespace sdl

typedef OpenMesh::TriMesh_ArrayKernelT<> OpenMeshT;

void debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                          const GLchar *message, const void *userParam) {
  std::printf("got dbg message\n");
}

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

  GLuint getId() const { return id_; }

private:
  GLuint id_;
};

class BBox {
public:
  glm::vec3 minp = glm::vec3(std::numeric_limits<float>::max());
  glm::vec3 maxp = glm::vec3(std::numeric_limits<float>::lowest());
  void addPoint(const glm::vec3 &p) {
    maxp.x = std::max(maxp.x, p.x);
    maxp.y = std::max(maxp.y, p.y);
    maxp.z = std::max(maxp.z, p.z);
    minp.x = std::min(minp.x, p.x);
    minp.y = std::min(minp.y, p.y);
    minp.z = std::min(minp.z, p.z);
  }
};

class Vertex {
public:
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec3 color;
  Vertex(const glm::vec3 &pos_) : pos(pos_) {}
};

glm::vec3 randColor() {
  return glm::vec3(static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
                   static_cast<float>(rand()) / static_cast<float>(RAND_MAX),
                   static_cast<float>(rand()) / static_cast<float>(RAND_MAX));
}

class Mesh {
public:
  Mesh(aiMesh *mesh, glm::vec3 meshColor_, const std::string &name_) : name(name_) {
    for (u32 i = 0; i < mesh->mNumVertices; ++i) {
      Vertex v{glm::vec3(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z)};
      if (mesh->HasNormals()) {
        v.normal = glm::vec3(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z);
      }
      v.color = randColor();
      vertices.push_back(v);
      box.addPoint(v.pos);
    }
    for (u32 i = 0; i < mesh->mNumFaces; i++) {
      aiFace face = mesh->mFaces[i];
      for (u32 j = 0; j < face.mNumIndices; j++)
        indices.push_back(face.mIndices[j]);
    }
    // translate to origin
    glm::vec3 translate = -1.0f * (box.maxp + box.minp) / 2.0f;
    for (Vertex &v : vertices) {
      v.pos += translate;
    }

    for (u32 i = 0; i < 3; ++i) {
      meshColor[i] = meshColor_[i];
    }

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ebo);

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
  void draw(const ShaderProgram &shaderProgram) const {
    glUseProgram(shaderProgram.getId());
    glm::vec3 glmMeshColor = glm::vec3(meshColor[0], meshColor[1], meshColor[2]);
    glUniform3fv(glGetUniformLocation(shaderProgram.getId(), "meshColor"), 1, &glmMeshColor[0]);

    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, static_cast<u32>(indices.size()), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
  }

  const char *getName() { return name.c_str(); }

public:
  float meshColor[3];

private:
  BBox box;
  GLuint vao, vbo, ebo;
  std::vector<Vertex> vertices;
  std::vector<u32> indices;
  std::string name;
};

class Model {
public:
  Model(const aiScene *scene, glm::vec3 color, const std::string &name) {
    Mesh mesh{scene->mMeshes[0], color, name};
    meshes.push_back(mesh);
  }

  // private:
  std::vector<Mesh> meshes;
};

std::optional<Model> loadModel(const std::string &pFile) {
  Assimp::Importer importer;

  const aiScene *scene =
      importer.ReadFile(pFile, aiProcess_CalcTangentSpace | aiProcess_Triangulate |
                                   aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);

  if (nullptr == scene) {
    std::cout << (importer.GetErrorString());
    return std::nullopt;
  }

  glm::vec3 color = randColor();
  return Model{scene, color, pFile};
}

} // namespace brocseg

int main(int argc, char *argv[]) {
  using namespace brocseg;
  using sdl::checkeq;
  using sdl::checknz;
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

  SDL_Window *win = SDL_CreateWindow("brocseg", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     1000, 1000, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  checknz(win);

  SDL_GLContext glContext = SDL_GL_CreateContext(win);
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
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

  ImGui_ImplSDL2_InitForOpenGL(win, glContext);
  ImGui_ImplOpenGL3_Init();

  std::vector<std::optional<Model>> models = {loadModel("stl/L_Ioscan.stl"),
                                              loadModel("stl/Skull_And_Bones.stl")};
  for (auto &model : models) {
    if (!model) {
      std::printf("Failed to import model");
      exit(1);
    }
  }

  const char *vertex_shader =
#include "shader.vs"
      ;
  const char *fragment_shader =
#include "shader.fs"
      ;

  ShaderProgram shaderProgram{vertex_shader, fragment_shader};
  GLuint shader_programme = shaderProgram.getId();
  glUseProgram(shader_programme);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);

  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT) {
        running = false;
      }
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::ShowDemoWindow();

    float pi = glm::pi<float>();
    static float cameraPhase = 0;
    ImGui::SliderFloat("cameraPhase", &cameraPhase, -pi, pi);
    static float cameraAmp = 100.0f;
    ImGui::SliderFloat("cameraAmp", &cameraAmp, 1.0f, 200.0);
    ImGuiIO &io = ImGui::GetIO();
    if (io.MouseWheel != 0.0f) {
      cameraAmp += -io.MouseWheel * 10.0f;
    }

    glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader_programme);

    for (auto &model : models) {
      for (auto &mesh : model->meshes) {
        glm::mat4 projM = glm::perspective(pi * 0.25f, 4.0f / 3.0f, 0.1f, 1000.f);

        glm::vec3 cameraPos =
            glm::vec3(sin(cameraPhase) * cameraAmp, 0.0, cos(cameraPhase) * cameraAmp);
        glm::mat4 viewM;
        viewM = glm::lookAt(cameraPos, glm::vec3(0.0, 0.0, 0.0), glm::vec3(0.0, 1.0, 0.0));

        glm::mat4 modelM = glm::mat4(1.0f);

        glm::vec3 lightPos = cameraPos;

        glUniformMatrix4fv(glGetUniformLocation(shader_programme, "model"), 1, GL_FALSE,
                           glm::value_ptr(modelM));
        glUniformMatrix4fv(glGetUniformLocation(shader_programme, "view"), 1, GL_FALSE,
                           glm::value_ptr(viewM));
        glUniformMatrix4fv(glGetUniformLocation(shader_programme, "projection"), 1, GL_FALSE,
                           glm::value_ptr(projM));
        glUniform3fv(glGetUniformLocation(shader_programme, "lightPos"), 1, &lightPos[0]);

        ImGui::ColorEdit3(mesh.getName(), mesh.meshColor);

        mesh.draw(shaderProgram);
      }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(win);
  }

  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(win);
  SDL_Quit();
}
