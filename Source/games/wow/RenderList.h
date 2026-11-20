/*
 * RenderList.h
 *
 * What a frame draws, without a graphics API. The model code (WoWModel, its render passes, particles, ribbons,
 * attachments) appends draws here exactly where it used to issue OpenGL calls; the viewport then hands the list
 * to its renderer (Vulkan). The list keeps the one piece of GL state that code relied on implicitly -- the
 * modelview matrix stack -- so a draw records the transform it was made under.
 *
 * Like the GL context it replaces, there is one current list, used from the UI thread only: the viewport makes
 * its list current (RenderList::Scope), calls the scene's draw(), and renders what was recorded.
 */

#ifndef _RENDERLIST_H_
#define _RENDERLIST_H_

#include <cstdint>
#include <vector>

#include "glm/glm.hpp"

#include "TextureSource.h" // TextureID

#ifdef _WIN32
#    ifdef BUILDING_WOW_DLL
#        define _RENDERLIST_API_ __declspec(dllexport)
#    else
#        define _RENDERLIST_API_ __declspec(dllimport)
#    endif
#else
#    define _RENDERLIST_API_
#endif

namespace wmv
{
  // M2 blend modes (wow_enums.h BM_*), in the same numbering.
  enum BlendMode : uint8_t
  {
    BLEND_OPAQUE = 0,         // writes colour; alpha only matters through ocol.w < 1 (then src-alpha blend)
    BLEND_ALPHA_KEY = 1,      // alpha test, no blend
    BLEND_ALPHA = 2,          // src alpha, 1 - src alpha
    BLEND_ADD = 3,            // one, one
    BLEND_ADD_ALPHA = 4,      // src alpha, one
    BLEND_MODULATE = 5,       // dst colour, zero
    BLEND_MODULATE_2X = 6,    // dst colour, src colour
    BLEND_7 = 7,              // one, 1 - src alpha (premultiplied)
    BLEND_ADD_COLOR = 8,      // src colour, one (particles' additive mode)
    BLEND_COUNT
  };

  enum UvSource : int8_t
  {
    UV_SET0 = 0,
    UV_SET1 = 1,
    UV_SPHERE_MAP = 2, // what GL's GL_SPHERE_MAP texgen made, from the eye-space normal
    UV_SET2 = 3
  };

  enum Primitive : uint8_t
  {
    PRIM_TRIANGLES = 0,
    PRIM_LINES = 1
  };

  struct Vertex
  {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 uv0;
    glm::vec2 uv1;
    glm::vec4 color; // per-vertex colour (particles, ribbons, lines); white for meshes
  };

  // Everything one draw needs, by value. Defaults are the plain lit, textured, opaque, depth-tested case.
  struct Material
  {
    TextureID tex[4] = { 0, 0, 0, 0 };   // 0 = no texture (samples white)
    int8_t uvSource[4] = { UV_SET0, UV_SET1, UV_SET2, UV_SET2 };
    glm::mat4 texMatrix[2] = { glm::mat4(1.0f), glm::mat4(1.0f) }; // texture animation for units 0 and 1
    bool wrapS = true, wrapT = true;     // repeat (true) or clamp to edge (false)

    BlendMode blend = BLEND_OPAQUE;
    bool blendEnabled = false;           // BLEND_OPAQUE / BLEND_ALPHA_KEY blend only when the colour's alpha < 1
    bool alphaTest = false;
    float alphaRef = 0.5f;
    bool cull = false;
    bool frontFaceCW = false;            // a mirrored model is drawn with its winding flipped
    bool depthTest = true;
    bool depthWrite = true;
    bool unlit = false;
    bool textured = true;                // false: colour only (GL_TEXTURE_2D off)
    bool wireframe = false;
    bool twoSided = true;                // light a back face with its flipped normal (cloth, leaves, capes)
    bool screenSpace = false;            // the draw's transform is already in clip space (backgrounds, overlays)

    glm::vec4 color = glm::vec4(1.0f);   // glColor / material diffuse; .w is the pass opacity
    glm::vec4 emissive = glm::vec4(0.0f); // GL_EMISSION

    // The M2 pixel combiner (ModelRenderPass, M2SV_COMBINER*). pixelShader < 0 is the fixed-function path.
    int pixelShader = -1;
    // Particles with several textures: colour x tex0 x tex1 x tex2 x 4, as GL's GL_COMBINE with RGB_SCALE 4 made.
    bool particleMultiTexture = false;
    int textureCount = 1;
    int egxBlendMode = 0;
    float combinerAlphaTest = 0.501960814f;
    float specularWeight = 0.0f;
    float envBoost = 0.0f;
    float metalSpec = 0.0f;
    float metalTight = 55.0f;
  };

  struct Draw
  {
    Primitive primitive = PRIM_TRIANGLES;
    uint32_t firstIndex = 0;   // into RenderList::indices
    uint32_t indexCount = 0;
    glm::mat4 model = glm::mat4(1.0f); // the modelview stack's top (without the camera) when recorded
    Material material;
    int layer = 0;             // drawn in increasing layer order, then in recording order (0 = scene)
  };

  class _RENDERLIST_API_ RenderList
  {
  public:
    RenderList();

    void clear();

    // The camera. The model code reads these (billboards face the camera; sphere maps use the eye space).
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);

    // The modelview stack, without the camera (glPushMatrix / glPopMatrix / glMultMatrixf ...).
    void push();
    void pop();
    void multiply(const glm::mat4 & m);
    void translate(const glm::vec3 & v);
    void rotate(float degrees, const glm::vec3 & axis);
    void scale(const glm::vec3 & s);
    void loadIdentity();
    const glm::mat4 & top() const { return m_stack.back(); }
    glm::mat4 modelView() const { return view * m_stack.back(); }

    // Geometry. addVertices returns the index of the first vertex added, for the indices that follow.
    uint32_t addVertices(const Vertex * v, size_t count);
    uint32_t addVertex(const Vertex & v);
    void addIndex(uint32_t i) { indices.push_back(i); }

    // A draw of the indices added since 'firstIndex', under the current transform.
    void draw(const Material & material, uint32_t firstIndex, Primitive primitive = PRIM_TRIANGLES, int layer = 0);
    uint32_t indexMark() const { return (uint32_t)indices.size(); }

    // Copies another list's geometry and draws into this one, under the current transform (what calling a GL
    // display list did). A recorded list is how geometry built once at load is drawn every frame.
    void append(const RenderList & other);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Draw> draws;

    // The list the model code is recording into, or nullptr outside a frame.
    static RenderList * current();

    // Makes a list current for its lifetime.
    class _RENDERLIST_API_ Scope
    {
    public:
      explicit Scope(RenderList & list);
      ~Scope();
    private:
      RenderList * m_previous;
    };

  private:
    std::vector<glm::mat4> m_stack;
  };
}

#endif
