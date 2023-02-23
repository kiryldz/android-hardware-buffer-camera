#pragma once

#include <android/choreographer.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
// needed for glEGLImageTargetTexture2DOES
#include <GLES2/gl2ext.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <tbb/concurrent_queue.h>

#include "base_renderer.hpp"
#include "looper_thread.hpp"
#include "util.hpp"

namespace engine {
namespace android {

class OpenGLRenderer : public BaseRenderer {
public:
  OpenGLRenderer();
  ~OpenGLRenderer();

  void setWindow(ANativeWindow *window) override;
  void updateWindowSize(int width, int height) override;
  void resetWindow() override;
  void feedHardwareBuffer(AHardwareBuffer *aHardwareBuffer) override;

private:
  ///////// OpenGL
  const GLchar * vertexShaderSource = "#version 320 es\n"
                                      "precision highp float;"
                                      "uniform mat4 uMvpMatrix;"
                                      "layout (location = 0) in vec2 aPosition;"
                                      "layout (location = 1) in vec2 aTexCoord;"
                                      "out vec2 vCoordinate;"
                                      "void main() {"
                                      " vCoordinate = aTexCoord;"
                                      " gl_Position = uMvpMatrix * vec4(aPosition, 0.0, 1.0);"
                                      "}";
  const GLchar * fragmentShaderSource = "#version 320 es\n"
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
    -1.0f, -1.0f,   0.0f, 0.0f,
    -1.0f,  1.0f,   0.0f, 1.0f,
    1.0f, -1.0f,   1.0f, 0.0f,
    1.0f,  1.0f,   1.0f, 1.0f
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

  ///////// Threads and threading

  std::unique_ptr<LooperThread> renderThread;
  std::mutex eglMutex;
  std::condition_variable eglInitialized;
  std::condition_variable eglDestroyed;
  /**
   * Concurrent queue needed as worker camera thread produces buffers while render thread consumes them.
   */
  tbb::concurrent_queue<AHardwareBuffer*> aHwBufferQueue;

  ///////// Variables

  AChoreographer * aChoreographer = nullptr;
  ANativeWindow * aNativeWindow = nullptr;

  int viewportWidth = -1;
  int viewportHeight = -1;
  volatile bool hardwareBufferDescribed = false;
  volatile bool eglPrepared = false;
  float bufferImageRatio = 1.0f;

  ///////// Functions

  bool couldRender() const;
  void render();
  bool prepareEgl();
  void destroyEgl();

  /**
   * Always called from render thread - converting hardware buffer to an OpenGL external texture.
   * @param aHardwareBuffer
   */
  void hwBufferToExternalTexture(AHardwareBuffer * aHardwareBuffer);

  ///////// Callbacks for AChoreographer and ALooper stored as private static functions

  static void doFrame(long timeStampNanos, void* data);
};

} // namespace android
} // namespace engine