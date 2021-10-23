#version 330 core

layout (location = 0) in vec3 pos;
layout (location = 2) in vec2 inTexCoords;
layout (location = 3) in vec3 color;


uniform mat4 model; // model matrix
uniform mat4 view; // view matrix
uniform mat4 projection; // projection matrix

out vec2 texCoords;

void main()
{
  texCoords = inTexCoords;
  gl_Position = projection * view * model * vec4(pos, 1.0f);
}