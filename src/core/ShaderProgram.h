#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include <map>
#include <string>

#include "GL/glew.h"
#include "glm/glm.hpp"

class ShaderProgram
{
public:
  ShaderProgram() = default;
  ~ShaderProgram();

  bool loadShaders(const std::string& vsFilename, const std::string& fsFilename);
  void use() const;

  GLuint get() const { return handle_; }

  void setUniform(const GLchar* name, const glm::vec2& v) const;
  void setUniform(const GLchar* name, const glm::vec3& v) const;
  void setUniform(const GLchar* name, const glm::vec4& v) const;
  void setUniform(const GLchar* name, const glm::mat4& m) const;

private:
  static bool compileShader(const std::string& filename, const GLenum shaderType, GLuint& shader);

  GLuint handle_ = 0;


};

#endif // SHADERPROGRAM_H
