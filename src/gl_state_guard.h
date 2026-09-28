#pragma once

// Captures a handful of GL state bits a callee outside our control (mpv's
// render call; a hardware-rendered libretro core's retro_run()) may mutate,
// and restores them afterward -- neither API makes any guarantee about
// preserving caller GL state, and our own rendering runs right after in the
// same frame. Not exhaustive (it doesn't cover every texture unit, blend
// func/equation, cull/depth-func state, ...): our own draw calls always set
// everything they need explicitly rather than trust what capture() didn't
// re-check, so this only has to get us back to a *sane* baseline (the right
// framebuffer/program/VAO bound, states either wallpaper on or off), not a
// bit-exact one. See PLAN.md Phase 2 "Known Risks".

#include "gl.h"

struct GLStateGuard {
    GLboolean blendEnabled = GL_FALSE;
    GLboolean depthEnabled = GL_FALSE;
    GLboolean scissorEnabled = GL_FALSE;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint program = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texBinding2D = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint framebuffer = 0;

    void capture() {
        blendEnabled = glIsEnabled(GL_BLEND);
        depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBinding2D);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    }

    void restore() const {
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glUseProgram(program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texBinding2D);
        glActiveTexture(static_cast<GLenum>(activeTexture));
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        (blendEnabled ? glEnable : glDisable)(GL_BLEND);
        (depthEnabled ? glEnable : glDisable)(GL_DEPTH_TEST);
        (scissorEnabled ? glEnable : glDisable)(GL_SCISSOR_TEST);
    }
};
