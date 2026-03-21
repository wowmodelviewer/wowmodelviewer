#include "SceneRenderer.h"
#include "OrbitCamera.h"
#include "ViewportFBO.h"
#include "Attachment.h"

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace SceneRenderer
{

// ---- Module state ---------------------------------------------------------
static State    s_state;
static Renderer s_renderer;

State& state() noexcept { return s_state; }
Renderer& renderer() noexcept { return s_renderer; }

// ---- GPU resource creation ------------------------------------------------
void initResources()
{
    s_renderer.init();
}

void shutdown()
{
    s_renderer.shutdown();
}

// ---- Lighting -------------------------------------------------------------
void setupLighting()
{
    s_renderer.applyLighting(s_state.light);
}

// ---- Checkerboard background ----------------------------------------------
void renderCheckerboard(int w, int h)
{
    s_renderer.drawCheckerboard(w, h);
}

// ---- Ground grid ----------------------------------------------------------
void renderGrid()
{
    s_renderer.drawGrid();
}

// ---- Scene objects --------------------------------------------------------
void renderObjects(Attachment* root)
{
    if (!root)
        return;

    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    root->draw();

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    root->drawParticles();

    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
}

// ---- Full FBO render pass -------------------------------------------------
void renderToFBO(ViewportFBO& fbo, int w, int h,
                 const OrbitCamera& camera, Attachment* root,
                 float fov, const glm::vec3& clearColor, bool drawGridFlag)
{
    if (w <= 0 || h <= 0)
        return;

    s_renderer.beginPass(fbo, w, h, clearColor);

    // Background (screen-space, drawn before 3D scene)
    if (s_state.drawGradientBg)
    {
        s_renderer.drawGradient(w, h, s_state.gradientTop, s_state.gradientBottom);
    }
    else if (s_state.drawCheckerBg && s_renderer.checkerTexture())
    {
        s_renderer.drawCheckerboard(w, h);
    }
    glClear(GL_DEPTH_BUFFER_BIT);

    // Projection & View
    s_renderer.applyProjection(w, h, fov);
    s_renderer.applyView(camera.getViewMatrix());

    // Lighting
    setupLighting();

    // Grid
    if (drawGridFlag)
        s_renderer.drawGrid();

    // Model
    glEnable(GL_NORMALIZE);
    renderObjects(root);
    glDisable(GL_NORMALIZE);

    s_renderer.endPass(fbo);
}

} // namespace SceneRenderer
