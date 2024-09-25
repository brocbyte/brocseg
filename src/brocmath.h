#pragma once
#include <algorithm>
#include <numeric>
#include <queue>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/scalar_constants.hpp>
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/norm.hpp>

#include "broccommon.h"

namespace brocseg {
namespace math {
const float EPS = 1e-6;
const float pi = glm::pi<float>();
const float halfpi = pi / 2.0f;

inline float len2(const glm::vec3 &a) { return dot(a, a); }

inline float triangleArea(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
  return 0.5f * glm::length(glm::cross(b - a, c - a));
}

inline float remap(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline float angleBetweenVectors(const glm::vec3 &a, const glm::vec3 &b) {
  float d = glm::dot(a, b) / (glm::length(a) * glm::length(b));
  if (std::abs(d) > 1.0f) {
    if (std::abs(d) - 1.0f > 0.1) {
      std::cout << "angleBetweenVectors: d is " << d << "\n";
    }
    d = std::clamp(d, -1.0f, 1.0f);
  }
  return std::abs(std::acos(d));
}

inline float cotAngle(const glm::vec3 &a, const glm::vec3 &b) {
  return glm::dot(a, b) / glm::length(glm::cross(a, b));
}

inline float voronoiRegion(const glm::vec3 &p, const glm::vec3 &q, const glm::vec3 &r) {
  float cotq = cotAngle(p - q, r - q);
  float cotr = cotAngle(p - r, q - r);
  return (1.0f / 8.0f) * (len2(p - r) * cotq + len2(p - q) * cotr);
}

inline float mixedVoronoiCellArea(const glm::vec3 &p, const std::vector<glm::vec3> &adjacent) {
  float result = 0.0f;
  for (int i = 0; i < adjacent.size(); ++i) {
    const glm::vec3 &q = adjacent[i];
    const glm::vec3 &r = adjacent[(i != adjacent.size() - 1) ? (i + 1) : 0];
    const float pa = angleBetweenVectors(q - p, r - p);
    const float qa = angleBetweenVectors(p - q, r - q);
    const float ra = pi - (pa + qa);
    if (pa <= halfpi && qa <= halfpi && ra <= halfpi) {
      result += voronoiRegion(p, q, r);
    } else {
      if (pa > halfpi) {
        result += (1.0f / 2.0f) * triangleArea(p, q, r);
      } else {
        result += (1.0f / 4.0f) * triangleArea(p, q, r);
      }
    }
  }
  return result;
}

// https://rodolphe-vaillant.fr/entry/33/curvature-of-a-triangle-mesh-definition-and-computation
inline float gaussianCurvature(const glm::vec3 &p, const std::vector<glm::vec3> &adjacent) {
  float sumAngles = 0.0f;
  for (int i = 0; i < adjacent.size(); ++i) {
    const glm::vec3 &q = adjacent[i];
    const glm::vec3 &r = adjacent[(i != adjacent.size() - 1) ? (i + 1) : 0];
    sumAngles += angleBetweenVectors(q - p, r - p);
  }
  float Ai = mixedVoronoiCellArea(p, adjacent);
  if (Ai <= EPS) {
    return std::numeric_limits<float>::max();
  }
  float curvature = (2.0f * pi - sumAngles) / Ai;
  if (std::isnan(curvature)) {
    std::cout << "curvature is nan\n";
  }
  return curvature;
}

inline std::pair<float, float> percentileThreshold(std::vector<float> arr, float percentile) {
  size_t windowSize = static_cast<size_t>(percentile * arr.size());
  if (windowSize == 0) {
    throw std::invalid_argument("percentileThreshold: empty window");
  }
  std::sort(arr.begin(), arr.end());
  float mean = std::reduce(arr.begin(), arr.begin() + windowSize) / windowSize;
  size_t windowBeg = 0;
  float variance =
      std::reduce(arr.begin(), arr.begin() + windowSize, 0.0f,
                  [&mean](float acc, float val) { return acc + (val - mean) * (val - mean); }) /
      windowSize;
  float minVariance = variance;

  for (int i = 1; i + windowSize < arr.size(); ++i) {
    float oldval = arr[i - 1];
    float newval = arr[i + windowSize - 1];
    float oldmean = mean;
    float newmean = oldmean + (newval - oldval) / windowSize;
    mean = newmean;
    variance += (newval - oldval) * (newval - newmean + oldval - oldmean) / (windowSize);
    if (variance < minVariance) {
      minVariance = variance;
      windowBeg = i;
    }
  }
  return {arr[windowBeg], arr[windowBeg + windowSize - 1]};
}

class HSV {
public:
  HSV(float H, float S, float V) : H_(H), S_(S), V_(V) {
    if (!(H >= 0 && H <= 1) || !(S >= 0 && S <= 1) || !(V >= 0 && V <= 1)) {
      throw std::invalid_argument("bad hsv values");
    }
  }
  glm::vec3 toRGB() {
    float r, g, b;

    int i = static_cast<int>(H_ * 6);
    float f = H_ * 6.0f - i;
    float p = V_ * (1.0f - S_);
    float q = V_ * (1.0f - f * S_);
    float t = V_ * (1.0f - (1.0f - f) * S_);
    switch (i % 6) {
    case 0:
      r = V_, g = t, b = p;
      break;
    case 1:
      r = q, g = V_, b = p;
      break;
    case 2:
      r = p, g = V_, b = t;
      break;
    case 3:
      r = p, g = q, b = V_;
      break;
    case 4:
      r = t, g = p, b = V_;
      break;
    case 5:
      r = V_, g = p, b = q;
      break;
    }
    return glm::vec3{r, g, b};
  }

private:
  float H_, S_, V_;
};

inline glm::vec3 colorFromNormalized(float val) {
  HSV hsvColor{val * (240.0f / 360.0f), 1.0, 1.0};
  return hsvColor.toRGB();
}

class flownet {
public:
  i32 bfs(size_t s, size_t t, std::vector<size_t> &parent,
          std::vector<std::vector<i32>>& residualCapacity) {
    std::fill(parent.begin(), parent.end(), -1);
    parent[s] = -2;
    std::queue<std::pair<size_t, i32>> q;
    q.push({s, std::numeric_limits<i32>::max()});
    while (!q.empty()) {
      size_t curr = q.front().first;
      i32 flow = q.front().second;
      q.pop();
      for (size_t next : adj_[curr]) {
        if (parent[next] == -1 && residualCapacity[curr][next]) {
          parent[next] = curr;
          i32 newFlow = std::min(flow, residualCapacity[curr][next]);
          if (next == t)
            return newFlow;
          q.push({next, newFlow});
        }
      }
    }
    return 0;
  }
  // returns indices of vertices in S-part of the flow network
  std::vector<size_t> mincut(size_t s, size_t t) {
    std::vector<std::vector<i32>> residualCapacity{capacity_};
    int flow = 0;
    std::vector<size_t> parent(nVertices);
    int newFlow;
    while (newFlow = bfs(s, t, parent, residualCapacity)) {
      flow += newFlow;
      size_t curr = t;
      while (curr != s) {
        size_t prev = parent[curr];
        residualCapacity[prev][curr] -= newFlow;
        residualCapacity[curr][prev] += newFlow;
        curr = prev;
      }
    }
    std::vector<size_t> sVertices;
    bfs(s, -1, parent, residualCapacity);
    for (size_t i = 0; i < nVertices; ++i) {
      if (parent[i] != -1) {
        sVertices.push_back(i);
      }
    }
    return sVertices;
  }
  size_t nVertices;
  std::vector<std::vector<size_t>> adj_;
  std::vector<std::vector<i32>> capacity_;
};
/*!todo test
    math::flownet g = {.nVertices = 6,
                       .adj_ =
                           {
                               {1, 4},       // 0
                               {0, 2, 3, 4}, // 1
                               {1, 3, 5},    // 2
                               {1, 2, 4, 5}, // 3
                               {0, 1, 3},    // 4
                               {2, 3},       // 5
                           },
                       .capacity_ = {
                           // 0  1  2  3  4  5
                           {0, 7, 0, 0, 4, 0}, // 0
                           {0, 0, 5, 3, 0, 0}, // 1
                           {0, 0, 0, 0, 0, 8}, // 2
                           {0, 0, 3, 0, 0, 5}, // 3
                           {0, 3, 0, 2, 0, 0}, // 4
                           {0, 0, 0, 0, 0, 0}, // 5
                       }};

    std::vector<size_t> borderVertexIndices = g.mincut(0, 5);
    for (auto v : borderVertexIndices) {
      std::cout << v << ", ";
    }
 * */

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

} // namespace math
} // namespace brocseg
