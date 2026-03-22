#pragma once

#include <glad/gl.h>

/// @brief Simple OpenGL framebuffer object wrapper for off-screen rendering.
///
/// Manages the lifetime of an FBO, colour texture, and depth renderbuffer.
/// Used by the viewport to render the 3D scene into an ImGui image.
struct ViewportFBO
{
    GLuint fbo       = 0;   ///< Framebuffer object handle.
    GLuint colorTex  = 0;   ///< Colour attachment (GL_RGBA8 texture).
    GLuint depthRbo  = 0;   ///< Depth attachment (GL_DEPTH_COMPONENT24 renderbuffer).
    int    width     = 0;   ///< Current width in pixels.
    int    height    = 0;   ///< Current height in pixels.

    /// @brief Allocate GPU resources at the given resolution.
    void create(int w, int h)
    {
        width  = w;
        height = h;
        glGenFramebuffers(1, &fbo);
        glGenTextures(1, &colorTex);
        glGenRenderbuffers(1, &depthRbo);

        glBindTexture(GL_TEXTURE_2D, colorTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glBindRenderbuffer(GL_RENDERBUFFER, depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    /// @brief Resize the FBO; destroys and recreates if dimensions changed.
    void resize(int w, int h)
    {
        if (w == width && h == height)
            return;
        destroy();
        if (w > 0 && h > 0)
            create(w, h);
    }

    /// @brief Bind this FBO as the current render target.
    void bind()   const { glBindFramebuffer(GL_FRAMEBUFFER, fbo); }

    /// @brief Unbind (revert to the default framebuffer).
    void unbind() const { glBindFramebuffer(GL_FRAMEBUFFER, 0);   }

    /// @brief Release all GPU resources.
    void destroy()
    {
        if (fbo)       { glDeleteFramebuffers(1, &fbo);       fbo       = 0; }
        if (colorTex)  { glDeleteTextures(1, &colorTex);      colorTex  = 0; }
        if (depthRbo)  { glDeleteRenderbuffers(1, &depthRbo); depthRbo  = 0; }
        width = height = 0;
    }
};
