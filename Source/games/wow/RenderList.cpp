#include "RenderList.h"

#include "glm/gtc/matrix_transform.hpp"

namespace
{
  wmv::RenderList * g_current = nullptr;
}

wmv::RenderList::RenderList()
{
  clear();
}

void wmv::RenderList::clear()
{
  vertices.clear();
  indices.clear();
  draws.clear();
  m_stack.assign(1, glm::mat4(1.0f));
}

void wmv::RenderList::push()
{
  m_stack.push_back(m_stack.back());
}

void wmv::RenderList::pop()
{
  // Unbalanced pops kept GL's stack at its base, with an error; keep the base matrix the same way.
  if (m_stack.size() > 1)
    m_stack.pop_back();
}

void wmv::RenderList::multiply(const glm::mat4 & m)
{
  m_stack.back() = m_stack.back() * m;
}

void wmv::RenderList::translate(const glm::vec3 & v)
{
  m_stack.back() = glm::translate(m_stack.back(), v);
}

void wmv::RenderList::rotate(float degrees, const glm::vec3 & axis)
{
  m_stack.back() = glm::rotate(m_stack.back(), glm::radians(degrees), axis);
}

void wmv::RenderList::scale(const glm::vec3 & s)
{
  m_stack.back() = glm::scale(m_stack.back(), s);
}

void wmv::RenderList::loadIdentity()
{
  m_stack.back() = glm::mat4(1.0f);
}

uint32_t wmv::RenderList::addVertices(const Vertex * v, size_t count)
{
  const uint32_t first = (uint32_t)vertices.size();
  vertices.insert(vertices.end(), v, v + count);
  return first;
}

uint32_t wmv::RenderList::addVertex(const Vertex & v)
{
  vertices.push_back(v);
  return (uint32_t)vertices.size() - 1;
}

void wmv::RenderList::draw(const Material & material, uint32_t firstIndex, Primitive primitive, int layer)
{
  if (firstIndex >= indices.size())
    return;
  Draw d;
  d.primitive = primitive;
  d.firstIndex = firstIndex;
  d.indexCount = (uint32_t)indices.size() - firstIndex;
  d.model = m_stack.back();
  d.material = material;
  d.layer = layer;
  draws.push_back(d);
}

void wmv::RenderList::append(const RenderList & other)
{
  const uint32_t vbase = (uint32_t)vertices.size();
  const uint32_t ibase = (uint32_t)indices.size();
  vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
  indices.reserve(indices.size() + other.indices.size());
  for (uint32_t i : other.indices)
    indices.push_back(vbase + i);
  for (const Draw & d : other.draws)
  {
    Draw c = d;
    c.firstIndex += ibase;
    c.model = m_stack.back() * d.model;
    draws.push_back(c);
  }
}

wmv::RenderList * wmv::RenderList::current()
{
  return g_current;
}

wmv::RenderList::Scope::Scope(RenderList & list)
: m_previous(g_current)
{
  g_current = &list;
}

wmv::RenderList::Scope::~Scope()
{
  g_current = m_previous;
}
