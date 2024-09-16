R""(
#version 400
layout (location = 0) in vec3 vp;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec3 aColor;
out vec3 Normal;
out vec3 FragPos;
out vec3 color;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
  gl_Position = projection * view * model * vec4(vp, 1.0);
  Normal = aNormal;
  FragPos = vec3(model * vec4(vp, 1.0));
  color = aColor;
}
)""
