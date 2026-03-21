#include "Renderer.h"
#include "ViewportFBO.h"

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

Renderer::~Renderer()
{
    shutdown();
}

// ---- Lifecycle -------------------------------------------------------------

void Renderer::init()
{
    const unsigned char pixels[2 * 2 * 4] = {
        56, 56, 56, 255,   46, 46, 46, 255,
        46, 46, 46, 255,   56, 56, 56, 255,
    };
    glGenTextures(1, &m_checkerTex);
    glBindTexture(GL_TEXTURE_2D, m_checkerTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::shutdown()
{
    if (m_checkerTex)
    {
        glDeleteTextures(1, &m_checkerTex);
        m_checkerTex = 0;
    }
}

// ---- Lighting --------------------------------------------------------------

void Renderer::applyLighting(const LightSettings& L)
{
    if (!L.enabled)
    {
        glDisable(GL_LIGHTING);
        return;
    }

    glEnable(GL_LIGHTING);

    if (L.type == LightType::AmbientOnly)
    {
        glDisable(GL_LIGHT0);
        GLfloat modelAmb[] = { L.ambient[0], L.ambient[1], L.ambient[2], 1.0f };
        glLightModelfv(GL_LIGHT_MODEL_AMBIENT, modelAmb);
        return;
    }

    glEnable(GL_LIGHT0);

    GLfloat pos[4];
    if (L.type == LightType::Directional)
    {
        pos[0] = L.direction[0];
        pos[1] = L.direction[1];
        pos[2] = L.direction[2];
        pos[3] = 0.0f;
    }
    else
    {
        pos[0] = L.position[0];
        pos[1] = L.position[1];
        pos[2] = L.position[2];
        pos[3] = 1.0f;
    }

    float i = L.intensity;
    GLfloat diffuse[]  = { L.diffuse[0]  * i, L.diffuse[1]  * i,
                           L.diffuse[2]  * i, 1.0f };
    GLfloat ambient[]  = { L.ambient[0], L.ambient[1], L.ambient[2], 1.0f };
    GLfloat specular[] = { L.specular[0] * i, L.specular[1] * i,
                           L.specular[2] * i, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  diffuse);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  ambient);
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);

    if (L.type == LightType::Spot)
    {
        GLfloat spotDir[] = { L.direction[0], L.direction[1], L.direction[2] };
        glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, spotDir);
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, L.spotCutoff);
        glLightf(GL_LIGHT0, GL_SPOT_EXPONENT, L.spotExponent);
    }
    else
    {
        glLightf(GL_LIGHT0, GL_SPOT_CUTOFF, 180.0f);
    }

    GLfloat modelAmb[] = { L.ambient[0], L.ambient[1], L.ambient[2], 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, modelAmb);
}

// ---- Checkerboard background -----------------------------------------------

void Renderer::drawCheckerboard(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, m_checkerTex);
    glColor3f(1.0f, 1.0f, 1.0f);

    const float tileSize = 16.0f;
    float u = static_cast<float>(w) / (tileSize * 2.0f);
    float v = static_cast<float>(h) / (tileSize * 2.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(u, 0); glVertex2f(static_cast<float>(w), 0);
    glTexCoord2f(u, v); glVertex2f(static_cast<float>(w), static_cast<float>(h));
    glTexCoord2f(0, v); glVertex2f(0, static_cast<float>(h));
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ---- Gradient background ---------------------------------------------------

void Renderer::drawGradient(int w, int h,
                            const glm::vec3& top, const glm::vec3& bottom)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);

    glBegin(GL_QUADS);
    glColor3f(bottom.x, bottom.y, bottom.z);
    glVertex2f(0, 0);
    glVertex2f(static_cast<float>(w), 0);
    glColor3f(top.x, top.y, top.z);
    glVertex2f(static_cast<float>(w), static_cast<float>(h));
    glVertex2f(0, static_cast<float>(h));
    glEnd();

    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

// ---- Ground grid -----------------------------------------------------------

void Renderer::drawGrid(float gridSize, float step)
{
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);

    glLineWidth(1.0f);
    glColor4f(0.55f, 0.55f, 0.55f, 0.6f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBegin(GL_LINES);
    for (float i = -gridSize; i <= gridSize; i += step)
    {
        if (i == 0.0f) continue;
        glVertex3f(-gridSize, i, 0.0f);
        glVertex3f( gridSize, i, 0.0f);
        glVertex3f(i, -gridSize, 0.0f);
        glVertex3f(i,  gridSize, 0.0f);
    }
    glEnd();

    glLineWidth(2.0f);
    glColor3f(0.2f, 0.5f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-gridSize, 0.0f, 0.0f);
    glVertex3f( gridSize, 0.0f, 0.0f);
    glVertex3f(0.0f, -gridSize, 0.0f);
    glVertex3f(0.0f,  gridSize, 0.0f);
    glEnd();

    glLineWidth(1.0f);
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
}

// ---- Projection & view -----------------------------------------------------

void Renderer::applyProjection(int w, int h, float fov,
                               float nearPlane, float farPlane)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glm::mat4 proj = glm::perspective(
        fov, static_cast<float>(w) / static_cast<float>(h),
        nearPlane, farPlane);
    glMultMatrixf(glm::value_ptr(proj));
}

void Renderer::applyView(const glm::mat4& viewMatrix)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMultMatrixf(glm::value_ptr(viewMatrix));
}

// ---- FBO pass management ---------------------------------------------------

void Renderer::beginPass(ViewportFBO& fbo, int w, int h,
                         const glm::vec3& clearColor)
{
    fbo.resize(w, h);
    fbo.bind();
    glViewport(0, 0, w, h);
    glClearColor(clearColor.x, clearColor.y, clearColor.z, 1.0f);
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
}

void Renderer::endPass(ViewportFBO& fbo)
{
    fbo.unbind();
}
