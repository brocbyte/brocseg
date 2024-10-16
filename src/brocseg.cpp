#include <cstdio>
#include <vector>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <cmath>
#include <unordered_set>

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
using OpenMeshT = OpenMesh::TriMesh_ArrayKernelT<>;

struct Scene {
  OpenMeshT omMesh;
  broc::Mesh brocMesh;
  std::vector<size_t> selectedVertexIndices;
  float percentile;
};

inline float curvatureToQuality(float curvature) {
  return 1.0f / std::exp(curvature);
}

void normalize(std::vector<float> &arr, float percentile) {
  prof::watch percentileWatch;
  auto [m, M] = math::percentileThreshold(arr, percentile);
  std::cout << percentile << " percentile: [" << m << ", " << M << "]\n";
  std::cout << percentileWatch.report("percentile calculation") << "\n";

  for (auto &c : arr) {
    c = std::clamp(c, m, M);
    c = math::remap(c, m, M, 0.0f, 1.0f);
    c = std::clamp(c, 0.0f, 1.0f);
  }
}

void colorBy(broc::Mesh &brocMesh, const std::vector<float> &source, float percentile) {
  return;
  std::vector<float> normalizedSource{source};
  normalize(normalizedSource, percentile);
  for (int vIdx = 0; vIdx < brocMesh.vertices.size(); ++vIdx) {
    brocMesh.vertices[vIdx].color = math::colorFromNormalized(normalizedSource[vIdx]);
  }
  brocMesh.sendGl();
}

std::vector<glm::vec3> adjacentVertices(OpenMeshT &omMesh, OpenMeshT::VertexHandle vh) {
  return omMesh.vv_range(vh).to_vector([&omMesh](const OpenMeshT::VertexHandle &vv) {
    OpenMeshT::Point p = omMesh.point(vv);
    return glm::vec3(p[0], p[1], p[2]);
  });
}

std::vector<float> computePerVertexMeanCurvature(broc::Mesh &brocMesh, OpenMeshT &omMesh) {
  prof::watch w;
  std::vector<float> rawCurvatures(omMesh.n_vertices());
  for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
    std::vector<glm::vec3> adjacent = adjacentVertices(omMesh, vh);
    OpenMeshT::Point point = omMesh.point(vh);
    OpenMeshT::Normal normal = omMesh.normal(vh);
    glm::vec p = glm::vec3(point[0], point[1], point[2]);
    glm::vec n = glm::vec3(normal[0], normal[1], normal[2]);
    float meanCurvature = math::meanCurvature(p, adjacent, n);
    float gaussianCurvature = math::gaussianCurvature(p, adjacent);
    float k1 = meanCurvature + std::sqrt(meanCurvature * meanCurvature - gaussianCurvature);
    float k2 = meanCurvature - std::sqrt(meanCurvature * meanCurvature - gaussianCurvature);
    rawCurvatures[vh.idx()] = meanCurvature;
  }
  std::cout << w.report("curvature") << "\n";
  return rawCurvatures;
}

std::vector<float> energyFromCurvatures(const std::vector<float> &rawCurvatures, float percentile) {
  std::vector<float> energy = rawCurvatures;
  auto [minCurvature, maxCurvature] = math::percentileThreshold(energy, percentile);

  float infiniteWeight = 1e8 * energy.size();
  float minQuality = curvatureToQuality(maxCurvature);
  float maxQuality = curvatureToQuality(minCurvature);
  for (size_t i = 0; i < energy.size(); ++i) {
    float w = curvatureToQuality(energy[i]);
    energy[i] = w;
  }
  return energy;
}

std::vector<size_t> colorByBorders(broc::Mesh &brocMesh, OpenMeshT &omMesh, size_t sIdx,
                                   size_t tIdx, const std::vector<float> &energy) {
  size_t nVertices = brocMesh.vertices.size();
  math::flownet g;
  g.adj_.resize(nVertices);
  g.capacity_.resize(nVertices);
  for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
    g.capacity_[vh.idx()].resize(nVertices);
    for (OpenMeshT::HalfedgeHandle he : omMesh.voh_range(vh)) {
      OpenMeshT::VertexHandle to = omMesh.to_vertex_handle(he);
      g.adj_[vh.idx()].push_back(to.idx());
      float infiniteWeight = 1e8 * omMesh.n_vertices();
      float e1 = energy[vh.idx()];
      float e2 = energy[to.idx()];
      float diff = std::abs(e1 - e2);
      float weight = (diff > math::EPS) ? (1.0 / diff) : std::numeric_limits<i32>::max();
      if (std::abs(e1) > 100 || std::abs(e2) > 100) {
        weight = 0.0f;
      }
      g.capacity_[vh.idx()][to.idx()] = static_cast<i32>(weight);
    }
  }
  std::vector<size_t> result = g.mincut(sIdx, tIdx);

  {
    std::unordered_set<size_t> setResult(result.begin(), result.end());
    for (OpenMeshT::VertexHandle vh : omMesh.vertices()) {
      bool allNeighborsSelected = true;
      for (OpenMeshT::VertexHandle vv : omMesh.vv_range(vh)) {
        if (setResult.find(vv.idx()) == setResult.end()) {
          allNeighborsSelected = false;
        }
      }
      if (allNeighborsSelected && setResult.find(vh.idx()) == setResult.end()) {
        result.push_back(vh.idx());
      }
    }
  }

  return result;
}

void translateToOrigin(broc::Mesh &brocMesh) {
  math::BBox box;
  for (const auto &v : brocMesh.vertices) {
    box.addPoint(v.pos);
  }
  glm::vec3 translate = -1.0f * (box.maxp + box.minp) / 2.0f;
  glm::vec3 diag = box.maxp - box.minp;
  float scale = std::max(diag[0], std::max(diag[1], diag[2]));
  glm::mat4 normalizer = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / scale)) *
                         glm::translate(glm::mat4(1.0f), translate);
  for (auto &v : brocMesh.vertices) {
    v.pos = glm::vec3(normalizer * glm::vec4(v.pos, 1.0f));
  }
}

broc::Mesh convert(const OpenMeshT &omMesh, const std::string &name) {
  broc::Mesh brocMesh{name};
  for (OpenMeshT::VertexIter vIt = omMesh.vertices_begin(); vIt != omMesh.vertices_end(); ++vIt) {
    OpenMeshT::Point p = omMesh.point(*vIt);
    OpenMeshT::Normal n = omMesh.normal(*vIt);
    broc::Mesh::Vertex v{.pos = glm::vec3(p[0], p[1], p[2]),
                         .normal = glm::vec3(n[0], n[1], n[2]),
                         .color = glm::vec3(0.3f, 0.3f, 0.3f)};
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
  std::cout << "## n vertices: " << mesh.n_vertices() << "\n";
  std::cout << "## n faces: " << mesh.n_faces() << "\n";

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

glm::vec3 getNextColor() {
  static size_t colorIdx = -1;
  ++colorIdx;
  std::vector<float> hue = {0.0f,  180.0f, 270.0f, 45.0f, 225.0f, 315.0f,
                            15.0f, 195.0f, 285.0f, 30.0f, 210.0f, 300.0f};
  std::vector<glm::vec3> colors;
  colors.reserve(hue.size());
  auto rgbFromHue = [](const float& h) {
    return math::rgbFromHsv(h / 360.0f, 1.0f, 1.0f);
  };
  std::transform(hue.begin(), hue.end(), std::back_inserter(colors), rgbFromHue);
  if (colorIdx >= colors.size()) {
    colorIdx = 0;
  }
  return colors.at(colorIdx);
}

void handleMouseClickLeft(const glm::ivec2 &mouse, const broc::Camera &camera, Scene &scene,
                          const std::vector<float> &rawCurvatures) {
  glm::vec3 rayWorld = mouseToWorldDir(mouse, camera);

  broc::Mesh &brocMesh = scene.brocMesh;
  OpenMeshT &omMesh = scene.omMesh;
  std::vector<float> vertexDistances(brocMesh.vertices.size(), std::numeric_limits<float>::max());
  bool found = false;
  for (size_t i = 0; i < brocMesh.vertices.size(); ++i) {
    float dist = glm::length(glm::cross(rayWorld, brocMesh.vertices[i].pos - camera.cameraPos));
    if (dist <= 0.01) {
      float d = glm::dot(rayWorld, brocMesh.vertices[i].pos - camera.cameraPos);
      vertexDistances[i] = d;
      found = true;
    }
  }
  size_t minDistIdx = std::distance(
      vertexDistances.begin(), std::min_element(vertexDistances.begin(), vertexDistances.end()));
  if (!found) {
    scene.selectedVertexIndices.clear();
    colorBy(scene.brocMesh, rawCurvatures, scene.percentile);
    brocMesh.sendGl();
    return;
  }

  //brocMesh.vertices[minDistIdx].color = glm::vec3(0.5, 0.0, 0.5);
  scene.selectedVertexIndices.push_back(minDistIdx);
  if (scene.selectedVertexIndices.size() >= 2) {
    size_t sIdx = scene.selectedVertexIndices[0];
    size_t tIdx = scene.selectedVertexIndices[1];
    scene.selectedVertexIndices.clear();
    std::vector<float> energy = energyFromCurvatures(rawCurvatures, scene.percentile);
    std::vector<size_t> result = colorByBorders(brocMesh, omMesh, sIdx, tIdx, energy);
    glm::vec3 selectionColor = getNextColor();
    for (size_t vIdx : result) {
      scene.brocMesh.vertices[vIdx].color = selectionColor;
    }
    brocMesh.sendGl();
  }
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
  const char *meshName = "stl/leg.stl";
  //const char *meshName = "stl/bunny.obj";

  Scene scene = {.omMesh = loadMesh(meshName),
                 .brocMesh = convert(scene.omMesh, meshName),
                 .percentile = 0.9f};
  translateToOrigin(scene.brocMesh);
  std::cout << meshesWatch.report("mesh loading") << "\n";
  scene.brocMesh.sendGl();

  // https://julie-jiang.github.io/image-segmentation/
  std::vector<float> rawCurvatures = computePerVertexMeanCurvature(scene.brocMesh, scene.omMesh);
  colorBy(scene.brocMesh, rawCurvatures, scene.percentile);

  const char *vertex_shader =
#include "shader.vs"
      ;
  const char *fragment_shader =
#include "shader.fs"
      ;

  broc::ShaderProgram shader{vertex_shader, fragment_shader};
  shader.useProgram();

  broc::Camera camera{.theta = 0.0f,
                      .phi = math::halfpi,
                      .amp = 3.0f,
                      .screenWidth = screenWidth,
                      .screenHeight = screenHeight};
  camera.updateMatrices();

  bool running = true;
  size_t replayIdx = 0;
  std::vector<size_t> replay;
  while (running) {
    ImGuiIO &io = ImGui::GetIO();
    running = renderer.begFrame();

    ImGui::ShowDemoWindow();

    if (!io.WantCaptureMouse) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
          float dPhi = ((float)(-io.MouseDelta.y) / 300.0f);
          float dTheta = ((float)(-io.MouseDelta.x) / 300.0f);
          camera.phi += dPhi;
          camera.theta += dTheta;
          camera.updateMatrices();
        }
      }

      if (io.MouseWheel != 0.0f) {
        camera.amp += -io.MouseWheel * 0.1f;
        camera.updateMatrices();
      }

      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        glm::ivec2 mouse = glm::ivec2(io.MousePos.x, io.MousePos.y);
        handleMouseClickLeft(mouse, camera, scene, rawCurvatures);
      }
    }

    if (ImGui::SliderFloat("curvature percentile", &scene.percentile, 0.1f, 1.0f)) {
      colorBy(scene.brocMesh, rawCurvatures, scene.percentile);
    }

    for (size_t vIdx : scene.selectedVertexIndices) {
      ImGui::Text("sIdx: %llu", vIdx);
    }

    shader.useProgram();

    ImGui::Text(scene.brocMesh.getName());

    glm::mat4 modelM = glm::mat4(1.0f);
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
