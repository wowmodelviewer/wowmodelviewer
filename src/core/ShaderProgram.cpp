#include "ShaderProgram.h"

#include <fstream>
#include <sstream>

#include <glm/gtc/type_ptr.hpp>

#include "Logger.h"

ShaderProgram::~ShaderProgram()
{
  glDeleteProgram(handle_);
}

bool ShaderProgram::loadShaders(const std::string& vsFilename, const std::string& fsFilename)
{
  LOG_INFO << "Creating shader program with vertex shader" << vsFilename.c_str() << "and fragment shader" << fsFilename.c_str();
  GLuint vs, fs;
  
  const auto hasVs = compileShader(vsFilename, GL_VERTEX_SHADER, vs);
  const auto hasFs = compileShader(fsFilename, GL_FRAGMENT_SHADER, fs);

  if(!hasVs && !hasFs) // no valid shader program
  {
    LOG_ERROR << "Cannot create shader program. No suitable shader compiled";
    return false;
  }

  handle_ = glCreateProgram();
  if (handle_ == 0)
  {
    LOG_ERROR << "Unable to create shader program!";
    return false;
  }

  // attach shaders (if valid)
  if (hasVs)
    glAttachShader(handle_, vs);

  if (hasFs)
    glAttachShader(handle_, fs);

  // then link program and check for errors
  glLinkProgram(handle_);

  auto status = 0;
  glGetProgramiv(handle_, GL_LINK_STATUS, &status);
  if (status == GL_FALSE)
  {
    auto l = 0;
    glGetProgramiv(handle_, GL_INFO_LOG_LENGTH, &l);
    std::string errorLog(l, ' ');
    glGetProgramInfoLog(handle_, l, &l, &errorLog[0]);
    LOG_ERROR << "Shader program link failed." << errorLog.c_str();
    handle_ = 0;
  }

  // delete shaders
  if (hasVs)
    glDeleteShader(vs);

  if (hasFs)
    glDeleteShader(fs);

  LOG_INFO << "Shader Program final result" << (handle_ != 0);

  return handle_ != 0;
}

void ShaderProgram::use() const
{
  if (handle_ > 0)
    glUseProgram(handle_);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec2& v) const
{
  glUniform2f(glGetUniformLocation(handle_, name), v.x, v.y);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec3& v) const
{
  glUniform3f(glGetUniformLocation(handle_, name), v.x, v.y, v.z);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::vec4& v) const
{
  glUniform4f(glGetUniformLocation(handle_, name), v.x, v.y, v.z, v.w);
}

void ShaderProgram::setUniform(const GLchar* name, const glm::mat4& m) const
{
  glUniformMatrix4fv(glGetUniformLocation(handle_, name), 1, GL_FALSE, glm::value_ptr(m));
}

bool ShaderProgram::compileShader(const std::string& filename, const GLenum shaderType, GLuint& shader)
{
  LOG_INFO << "Compiling shader" << filename.c_str() << "...";
  if (filename.empty())
    return false;

  std::stringstream ss;
  std::ifstream file;

  std::string vsString;

  // open and read shader file
  try
  {
    file.open(filename, std::ios::in);

    if(!file.fail())
    {
      ss << file.rdbuf();
      vsString = ss.str();
      file.close();
    }
    else
    {
      LOG_ERROR << "Error opening file" << filename.c_str();
      return false;
    }
  }
  catch (std::exception&)
  {
    LOG_ERROR << "Error reading file" << filename.c_str();
    return false;
  }

  // compile it
  auto status = 0;
  const auto* vsSourcePtr = vsString.c_str();
  shader = glCreateShader(shaderType);
  glShaderSource(shader, 1, &vsSourcePtr, nullptr);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE)
  {
    auto l = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &l);
    std::string errorLog(l, ' ');
    glGetShaderInfoLog(shader, l, &l, &errorLog[0]);
    LOG_ERROR << "Shader compilation" << filename.c_str() << "failed." << errorLog.c_str();
    shader = 0;
    return false;
  }
  LOG_INFO << "Compilation succesfuly done.";
  return true;
}