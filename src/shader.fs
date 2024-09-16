R""(
#version 400
uniform vec3 lightPos;
uniform vec3 meshColor;
in vec3 Normal;
in vec3 FragPos;
in vec3 color;
out vec4 frag_colour;
void main() {
  float ambientStrength = 0.1;
  vec3 ambient = ambientStrength * vec3(1.0, 1.0, 1.0);
  vec3 norm = normalize(Normal);
  vec3 lightDir = normalize(lightPos - FragPos);
  float diff = max(dot(norm, lightDir), 0.0);
  vec3 diffuse = diff * vec3(1.0, 1.0, 1.0);
  vec3 result = (ambient + diffuse) * color; //meshColor;
  frag_colour = vec4(result, 1.0);
}
)""
