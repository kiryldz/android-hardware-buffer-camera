#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
// needed for glEGLImageTargetTexture2DOES
#include <GLES2/gl2ext.h>

#include "base_renderer.hpp"

namespace engine {
namespace android {

class OpenGLRenderer : public BaseRenderer {
protected:

    const char *renderingModeName() override {
        return (const char *) "OpenGL ES";
    }

    bool onWindowCreated() override {
        return prepareEgl();
    }

    void onWindowSizeUpdated(int width, int height) override {
        glClearColor(0.5, 0.5, 0.5, 0.5);
        glViewport(0, 0, width, height);
    }

    void onWindowDestroyed() override {
        destroyEgl();
    }

    void hwBufferToTexture(AHardwareBuffer *buffer) override;

    bool couldRender() const override {
        return eglPrepared && viewportHeight > 0 && viewportHeight > 0;
    }

    void render() override {
        renderImpl();
    }

    void postChoreographerCallback() override {
        // posting next frame callback, no need to explicitly wake the looper afterwards
        // as AChoreographer seems to operate with it's own fd and callbacks
        AChoreographer_postFrameCallback(aChoreographer, doFrame, this);
    }

private:
    ///////// OpenGL
    const GLchar *vertexShaderSource = "#version 320 es\n"
                                       "precision highp float;"
                                       "uniform mat4 uMvpMatrix;"
                                       "layout (location = 0) in vec2 aPosition;"
                                       "layout (location = 1) in vec2 aTexCoord;"
                                       "out vec2 vCoordinate;"
                                       "void main() {"
                                       " vCoordinate = aTexCoord;"
                                       " gl_Position = uMvpMatrix * vec4(aPosition, 0.0, 1.0);"
                                       "}";
    const GLchar *fragmentShaderSource = "#version 320 es\n"
                                         "#extension GL_OES_EGL_image_external_essl3 : require\n"
                                         "precision mediump float;"
                                         "in vec2 vCoordinate;"
                                         "out vec4 FragColor;"
                                         "uniform samplerExternalOES sExtSampler;"
                                         "void main() {"
                                         " FragColor = texture(sExtSampler, vCoordinate);"
                                         "}";

    /**
     * Store all the verticies in one array and operate with strides instead of storing 2 VBOs:
     * one for positions coordinates and another for texture coordinates.
     */
    float vertexArray[16] = {
            // positions    // texture coordinates
            -1.0f, -1.0f, 0.0f, 0.0f,
            -1.0f, 1.0f, 0.0f, 1.0f,
            1.0f, -1.0f, 1.0f, 0.0f,
            1.0f, 1.0f, 1.0f, 1.0f
    };
    GLuint program = 0;
    GLuint vertexShader = 0;
    GLuint fragmentShader = 0;
    GLuint vbo[1];
    GLint uniformMvp = 0;
    GLint externalSampler = 0;
    GLuint cameraExternalTex = 0;

    ///////// EGL

    EGLDisplay eglDisplay;
    EGLContext eglContext;
    EGLSurface eglSurface;

    ///////// Variables

    volatile bool hardwareBufferDescribed = false;
    volatile bool eglPrepared = false;

    ///////// Functions

    bool prepareEgl();

    void destroyEgl();

    void renderImpl();

    ///////// Callbacks for AChoreographer and ALooper stored as private static functions

    static void doFrame(long timeStampNanos, void *data);
};

} // namespace android
} // namespace engine