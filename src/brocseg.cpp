#include <cstdio>
#include <vector>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <cmath>

// broc
#include "broccommon.h"
#include "brocmath.h"
#include "brocprof.h"
#include "brocrender.h"

// imgui
#include <imgui.h>

// openmesh
#include <OpenMesh/Core/Mesh/TriMesh_ArrayKernelT.hh>
#include <OpenMesh/Core/IO/MeshIO.hh>

using u32 = uint32_t;

namespace brocseg {
typedef OpenMesh::TriMesh_ArrayKernelT<> OpenMeshT;

void debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                          const GLchar *message, const void *userParam) {
  std::printf("got dbg message\n");
}

void colorByCurvature(broc::Mesh &brocMesh, OpenMeshT &omMesh, float percentile) {
  prof::watch w{};
  std::vector<float> curvatures;

  auto omMeshGaussianCurvature = [&omMesh](OpenMeshT::VertexHandle vh) -> float {
    std::vector<glm::vec3> adjacent =
        omMesh.vv_range(vh).to_vector([&omMesh](const OpenMeshT::VertexHandle &vv) {
          OpenMeshT::Point p = omMesh.point(vv);
          return glm::vec3(p[0], p[1], p[2]);
        });
    OpenMeshT::Point omPoint = omMesh.point(vh);
    return math::gaussianCurvature(glm::vec3(omPoint[0], omPoint[1], omPoint[2]), adjacent);
  };

  for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
    curvatures.push_back(omMeshGaussianCurvature(vh));
  }
  std::cout << w.report("curvature") << "\n";

  prof::watch percentileWatch{};
  auto [m, M] = math::percentileThreshold(curvatures, percentile);
  std::cout << percentile << " percentile: [" << m << ", " << M << "]\n";
  for (int vIdx = 0; vIdx < brocMesh.vertices.size(); ++vIdx) {
    broc::Mesh::Vertex &v = brocMesh.vertices[vIdx];
    float normCurv = std::clamp(curvatures[vIdx], m, M);
    normCurv = math::remap(normCurv, m, M, 0.0f, 1.0f);
    v.color = math::colorFromNormalized(normCurv);
  }
  std::cout << percentileWatch.report("percentile calculation") << "\n";

  brocMesh.sendGl();
}

void colorByBorders(broc::Mesh &mesh, size_t sIdx, size_t tIdx) {
  /*
  math::flownet g = {.nVertices = vertices.size()};
  g.adj_.resize(vertices.size());
  g.capacity_.resize(vertices.size());
  for (OpenMeshT::VertexHandle vh : mesh.vertices()) {
    g.capacity_[vh.idx()].resize(vertices.size());
    for (OpenMeshT::VertexHandle vv : mesh.vv_range(vh)) {
      g.adj_[vh.idx()].push_back(vv.idx());
    }
  }
  for (int borderVertexIdx : borderVertexIndices) {
    vertices[borderVertexIdx].color = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  sendGl();
  */
}

void translateToOrigin(broc::Mesh &brocMesh) {
  math::BBox box;
  for (const auto &v : brocMesh.vertices) {
    box.addPoint(v.pos);
  }
  glm::vec3 translate = -1.0f * (box.maxp + box.minp) / 2.0f;
  for (auto &v : brocMesh.vertices) {
    v.pos += translate;
  }
}

broc::Mesh convert(const OpenMeshT &omMesh, const std::string &name) {
  broc::Mesh brocMesh{name};
  for (OpenMeshT::VertexIter vIt = omMesh.vertices_begin(); vIt != omMesh.vertices_end(); ++vIt) {
    OpenMeshT::Point p = omMesh.point(*vIt);
    OpenMeshT::Normal n = omMesh.normal(*vIt);
    broc::Mesh::Vertex v{.pos = glm::vec3(p[0], p[1], p[2]),
                         .normal = glm::vec3(n[0], n[1], n[2]),
                         .color = glm::vec3(0.5f, 0.0f, 0.5f)};
    brocMesh.vertices.push_back(v);
  }

  for (OpenMeshT::ConstFaceIter fIt = omMesh.faces_begin(); fIt != omMesh.faces_end(); ++fIt) {
    OpenMeshT::Face f = omMesh.face(*fIt);
    for (OpenMeshT::ConstFaceVertexIter fvIt = omMesh.cfv_iter(*fIt); fvIt.is_valid(); ++fvIt) {
      brocMesh.indices.push_back(fvIt->idx());
    }
  }
  return brocMesh;
}

std::optional<OpenMeshT> loadMesh(const std::string &pFile) {
  OpenMeshT mesh;
  mesh.request_vertex_normals();
  if (!mesh.has_vertex_normals()) {
    std::cout << "normals not available\n";
  }
  OpenMesh::IO::Options opt;
  if (!OpenMesh::IO::read_mesh(mesh, pFile.c_str(), opt)) {
    std::cout << "openmesh read error\n";
    return std::nullopt;
  }
  if (!opt.check(OpenMesh::IO::Options::VertexNormal)) {
    mesh.request_face_normals();
    mesh.update_normals();
    mesh.release_face_normals();
  }

  return mesh;
}

using TopoMesh = std::pair<broc::Mesh, OpenMeshT>;

glm::vec3 mouseToWorldDir(const glm::ivec2 &mouse, const broc::Camera &camera) {
  float x = (2.0f * mouse.x) / camera.screenWidth - 1.0f;
  float y = 1.0f - (2.0f * mouse.y) / camera.screenHeight;
  float z = 1.0f;
  glm::vec4 rayClip = glm::vec4(x, y, -1.0, 1.0);
  glm::vec4 rayEye = glm::inverse(camera.projM) * rayClip;
  rayEye.w = 0.0;

  glm::vec4 tmp = glm::inverse(camera.viewM) * rayEye;
  glm::vec3 rayWorld = glm::vec3(tmp.x, tmp.y, tmp.z);
  rayWorld = glm::normalize(rayWorld);
  return rayWorld;
}
void handleMouseClickLeft(const glm::ivec2 &mouse, const broc::Camera &camera,
                          std::vector<TopoMesh> &meshes) {
  glm::vec3 rayWorld = mouseToWorldDir(mouse, camera);
  std::cout << rayWorld.x << " " << rayWorld.y << " " << rayWorld.z << "\n";
  for (auto &mesh : meshes) {
    broc::Mesh &brocMesh = mesh.first;
    float minDist = std::numeric_limits<float>::max();
    size_t idx = 0;
    for (size_t i = 0; i < brocMesh.vertices.size(); ++i) {
      float dist = glm::length(glm::cross(rayWorld, brocMesh.vertices[i].pos - camera.cameraPos));
      if (dist < minDist) {
        idx = i;
        minDist = dist;
      }
    }
    static size_t sIdx = 0, tIdx = 1;
    brocMesh.vertices[idx].color = glm::vec3(0.5, 0.0, 0.5);
    sIdx = tIdx;
    tIdx = idx;
    colorByBorders(brocMesh, sIdx, tIdx);
    brocMesh.sendGl();
  }
}

} // namespace brocseg

int main(int argc, char *argv[]) {
  using namespace brocseg;
  int screenWidth = 1000;
  int screenHeight = 1000;
  broc::OpenGLRenderer renderer{"brocseg", screenWidth, screenHeight};

  prof::watch meshesWatch;
  std::array<const char *, 1> meshNames = {
      "stl/L_Ioscan.stl",
      //"stl/Sphere.stl"
  };
  std::vector<TopoMesh> topoMeshes;
  for (const char *meshName : meshNames) {
    std::optional<OpenMeshT> maybeMesh = loadMesh(meshName);
    if (!maybeMesh) {
      std::printf("Failed to import mesh: %s\n", meshName);
    } else {
      broc::Mesh brocMesh = convert(*maybeMesh, meshName);
      translateToOrigin(brocMesh);
      topoMeshes.push_back({brocMesh, *maybeMesh});
    }
  }
  std::cout << meshesWatch.report("mesh loading") << "\n";

  // https://julie-jiang.github.io/image-segmentation/
  static float curvaturePercentile = 0.9f;
  for (auto &mesh : topoMeshes) {
    colorByCurvature(mesh.first, mesh.second, curvaturePercentile);
  }

  const char *vertex_shader =
#include "shader.vs"
      ;
  const char *fragment_shader =
#include "shader.fs"
      ;

  broc::ShaderProgram shader{vertex_shader, fragment_shader};
  shader.useProgram();

  broc::Camera camera{
      .phase = 0.0f, .amp = 100.0f, .screenWidth = screenWidth, .screenHeight = screenHeight};
  camera.updateMatrices();

  bool running = true;
  while (running) {
    ImGuiIO &io = ImGui::GetIO();
    running = renderer.begFrame();

    ImGui::ShowDemoWindow();

    if (ImGui::SliderFloat("cameraPhase", &camera.phase, -math::pi, math::pi) ||
        ImGui::SliderFloat("cameraAmp", &camera.amp, 1.0f, 200.0)) {
      camera.updateMatrices();
    }
    if (io.MouseWheel != 0.0f) {
      camera.amp += -io.MouseWheel * 10.0f;
      camera.updateMatrices();
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      glm::ivec2 mouse = glm::ivec2(io.MousePos.x, io.MousePos.y);
      handleMouseClickLeft(mouse, camera, topoMeshes);
    }

    if (ImGui::SliderFloat("curvature percentile", &curvaturePercentile, 0.1f, 1.0f)) {
      for (auto &mesh : topoMeshes) {
        colorByCurvature(mesh.first, mesh.second, curvaturePercentile);
      }
    }

    shader.useProgram();

    for (TopoMesh &mesh : topoMeshes) {
      ImGui::Text(mesh.first.getName());

      glm::mat4 modelM = glm::mat4(1.0f);
      glm::vec3 lightPos = camera.cameraPos;

      shader.uniformMatrix4fv("model", modelM);
      shader.uniformMatrix4fv("view", camera.viewM);
      shader.uniformMatrix4fv("projection", camera.projM);
      shader.uniform3fv("lightPos", lightPos);

      mesh.first.draw();
    }

    renderer.endFrame();
  }
}
