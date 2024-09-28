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

struct Scene {
  OpenMeshT omMesh;
  broc::Mesh brocMesh;
  std::vector<size_t> selectedVertexIndices;
  float percentile;
};

float curvatureToQuality(float curvature) { return 1.0f / std::exp(curvature); }

void normalize(std::vector<float>& arr, float percentile) {
  prof::watch percentileWatch{};
  auto [m, M] = math::percentileThreshold(arr, percentile);
  std::cout << percentile << " percentile: [" << m << ", " << M << "]\n";
  std::cout << percentileWatch.report("percentile calculation") << "\n";

  for (auto& c : arr) {
    c = std::clamp(c, m, M);
    c = math::remap(c, m, M, 0.0f, 1.0f);
    c = std::clamp(c, 0.0f, 1.0f);
  }
}

void colorBy(broc::Mesh &brocMesh, std::vector<float> source, float percentile) {
  normalize(source, percentile);
  for (int vIdx = 0; vIdx < brocMesh.vertices.size(); ++vIdx) {
    brocMesh.vertices[vIdx].color = math::colorFromNormalized(source[vIdx]);
  }
  brocMesh.sendGl();
}

std::vector<float> colorByCurvature(broc::Mesh &brocMesh, OpenMeshT &omMesh, float percentile) {
  auto omMeshGaussianCurvature = [&omMesh](OpenMeshT::VertexHandle vh) -> float {
    std::vector<glm::vec3> adjacent =
        omMesh.vv_range(vh).to_vector([&omMesh](const OpenMeshT::VertexHandle &vv) {
          OpenMeshT::Point p = omMesh.point(vv);
          return glm::vec3(p[0], p[1], p[2]);
        });
    OpenMeshT::Point omPoint = omMesh.point(vh);
    return math::gaussianCurvature(glm::vec3(omPoint[0], omPoint[1], omPoint[2]), adjacent);
  };

  prof::watch w{};
  std::vector<float> curvatures;
  for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
    curvatures.push_back(omMeshGaussianCurvature(vh));
  }
  std::cout << w.report("curvature") << "\n";
  for (size_t vIdx = 0; vIdx < omMesh.n_vertices(); ++vIdx) {
    //curvatures[vIdx] = vIdx < omMesh.n_vertices() / 2 ? 0.5f : 0.0f;
  }

  colorBy(brocMesh, curvatures, percentile);
  auto [minCurvature, maxCurvature] = math::percentileThreshold(curvatures, percentile);

  std::vector<float> weights(curvatures.size(), 0.0f);
  float infiniteWeight = 1e5 * omMesh.n_faces();
  float minQuality = curvatureToQuality(maxCurvature);
  float maxQuality = curvatureToQuality(minCurvature);
  std::transform(curvatures.begin(), curvatures.end(), weights.begin(),
      [&infiniteWeight, &maxQuality, &minQuality](float c) {
    float w = curvatureToQuality(c);
    if (w > maxQuality) {
      w = infiniteWeight;
    } else if (w < minQuality) {
      w = 0.0f;
    }
    return w;
  });
  return weights;
}

std::vector<size_t> colorByBorders(broc::Mesh &brocMesh, OpenMeshT &omMesh, size_t sIdx, size_t tIdx,
                    const std::vector<float> &energy) {
  size_t nVertices = brocMesh.vertices.size();
  math::flownet g = {.nVertices = nVertices};
  g.adj_.resize(nVertices);
  g.capacity_.resize(nVertices);
  for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
    g.capacity_[vh.idx()].resize(nVertices);
    for (OpenMeshT::HalfedgeHandle he : omMesh.voh_range(vh)) {
      OpenMeshT::VertexHandle to = omMesh.to_vertex_handle(he);
      if (glm::length(brocMesh.vertices[sIdx].pos - brocMesh.vertices[vh.idx()].pos) > 7.0f) {
        continue;
      }
      g.adj_[vh.idx()].push_back(to.idx());
      float e1 = energy[vh.idx()];
      float e2 = energy[to.idx()];
      float diff = std::abs(e1 - e2);
      g.capacity_[vh.idx()][to.idx()] = (diff > math::EPS) ? (1.0 / diff) : std::numeric_limits<i32>::max();
    }
  }
  std::vector<size_t> result = g.mincut(sIdx, tIdx);
  return result;
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

OpenMeshT loadMesh(const std::string &pFile) {
  OpenMeshT mesh;
  mesh.request_vertex_normals();
  mesh.request_edge_colors();
  if (!mesh.has_vertex_normals()) {
    std::cout << "normals not available\n";
  }
  OpenMesh::IO::Options opt;
  if (!OpenMesh::IO::read_mesh(mesh, pFile.c_str(), opt)) {
    std::cout << "openmesh read error\n";
    exit(-1);
  }
  if (!opt.check(OpenMesh::IO::Options::VertexNormal)) {
    mesh.request_face_normals();
    mesh.update_normals();
    mesh.release_face_normals();
  }

  return mesh;
}

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

std::vector<size_t> handleMouseClickLeft(const glm::ivec2 &mouse, const broc::Camera &camera, Scene &scene) {
  // construct ray from camera to clicked point
  glm::vec3 rayWorld = mouseToWorldDir(mouse, camera);
  std::cout << rayWorld.x << " " << rayWorld.y << " " << rayWorld.z << "\n";

  broc::Mesh &brocMesh = scene.brocMesh;
  OpenMeshT &omMesh = scene.omMesh;
  std::vector<float> vertexDistances(brocMesh.vertices.size(),
      std::numeric_limits<float>::max());
  for (size_t i = 0; i < brocMesh.vertices.size(); ++i) {
    float dist = glm::length(glm::cross(rayWorld, brocMesh.vertices[i].pos - camera.cameraPos));
    if (dist <= 0.5) {
      vertexDistances[i] = glm::length(brocMesh.vertices[i].pos - camera.cameraPos);
    }
  }
  size_t minDistIdx = std::distance(vertexDistances.begin(), std::min_element(
        vertexDistances.begin(), vertexDistances.end()));

  brocMesh.vertices[minDistIdx].color = glm::vec3(0.5, 0.0, 0.5);
  scene.selectedVertexIndices.push_back(minDistIdx);
  if (scene.selectedVertexIndices.size() >= 2) {
    size_t sIdx = scene.selectedVertexIndices[0];
    size_t tIdx = scene.selectedVertexIndices[1];
    std::vector<float> curvatures = colorByCurvature(brocMesh, omMesh, scene.percentile);
    auto result = colorByBorders(brocMesh, omMesh, sIdx, tIdx, curvatures);
    scene.selectedVertexIndices.clear();
    for (size_t vIdx : result) {
      scene.brocMesh.vertices[vIdx].color = glm::vec3(0.5f, 0.0f, 0.5f);
    }
    brocMesh.sendGl();
    return result;
  }
  brocMesh.sendGl();
  return {};
}

} // namespace brocseg

void debugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                          const GLchar *message, const void *userParam) {
  std::printf("got dbg message\n");
}

int main(int argc, char *argv[]) {
  using namespace brocseg;
  int screenWidth = 1000;
  int screenHeight = 1000;
  broc::OpenGLRenderer renderer{"brocseg", screenWidth, screenHeight};

  prof::watch meshesWatch;
  const char *meshName =
      "stl/L_Ioscan.stl";
      //"stl/Sphere.stl";
      //"stl/Skull_And_Bones.stl";
      //"stl/528766.00_L_Gingiva.stl";
  //"stl/Suzanne.stl";

  Scene scene = {.omMesh = loadMesh(meshName),
                 .brocMesh = convert(scene.omMesh, meshName),
                 .percentile = 0.9f};
  translateToOrigin(scene.brocMesh);
  std::cout << meshesWatch.report("mesh loading") << "\n";
  scene.brocMesh.sendGl();

  // https://julie-jiang.github.io/image-segmentation/
  colorByCurvature(scene.brocMesh, scene.omMesh, scene.percentile);

  const char *vertex_shader =
#include "shader.vs"
      ;
  const char *fragment_shader =
#include "shader.fs"
      ;

  broc::ShaderProgram shader{vertex_shader, fragment_shader};
  shader.useProgram();

  broc::Camera camera{
      .phaseY = 0.0f,
      .phaseX = 0.0f,
        .amp = 100.0f, .screenWidth = screenWidth, .screenHeight = screenHeight};
  camera.updateMatrices();

  bool running = true;
  size_t replayIdx = 0;
  std::vector<size_t> replay;
  while (running) {
    ImGuiIO &io = ImGui::GetIO();
    running = renderer.begFrame();

    ImGui::ShowDemoWindow();

    if (ImGui::SliderFloat("cameraPhaseY", &camera.phaseY, -math::pi, math::pi) ||
        ImGui::SliderFloat("cameraPhaseX", &camera.phaseX, -math::pi, math::pi) ||
        ImGui::SliderFloat("cameraAmp", &camera.amp, 1.0f, 200.0)) {
      camera.updateMatrices();
    }
    if (io.MouseWheel != 0.0f) {
      camera.amp += -io.MouseWheel * 1.0f;
      camera.updateMatrices();
    }

    if (!io.WantCaptureMouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      glm::ivec2 mouse = glm::ivec2(io.MousePos.x, io.MousePos.y);
      handleMouseClickLeft(mouse, camera, scene);
    }

    /*
    if (replayIdx < replay.size()) {
      for (size_t vIdx : replay[replayIdx]) {
        scene.brocMesh.vertices[vIdx].color = glm::vec3(0.5f, 0.0f, 0.5f);
      }
      scene.brocMesh.sendGl();
      ++replayIdx;
      if (replayIdx == replay.size()) {
        replayIdx = 0;
        colorByCurvature(scene.brocMesh, scene.omMesh, scene.percentile);
      }
    }
    */

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      scene.selectedVertexIndices.clear();
      colorByCurvature(scene.brocMesh, scene.omMesh, scene.percentile);
    }

    if (ImGui::SliderFloat("curvature percentile", &scene.percentile, 0.1f, 1.0f)) {
      colorByCurvature(scene.brocMesh, scene.omMesh, scene.percentile);
    }

    for (size_t vIdx : scene.selectedVertexIndices) {
      ImGui::Text("sIdx: %llu", vIdx);
    }

    shader.useProgram();

    ImGui::Text(scene.brocMesh.getName());

    glm::mat4 modelM =
        glm::mat4(1.0f); // glm::rotate(glm::mat4(1.0f), -math::halfpi, glm::vec3(1.0, 0.0, 0.0));
    glm::vec3 lightPos = camera.cameraPos;

    shader.uniformMatrix4fv("model", modelM);
    shader.uniformMatrix4fv("view", camera.viewM);
    shader.uniformMatrix4fv("projection", camera.projM);
    shader.uniform3fv("lightPos", lightPos);

    scene.brocMesh.draw();

    renderer.endFrame();
  }
  return 0;
}
