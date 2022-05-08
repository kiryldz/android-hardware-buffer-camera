#pragma once

#include <android/looper.h>
#include <android/choreographer.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
// needed for glEGLImageTargetTexture2DOES
#include <GLES2/gl2ext.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <tbb/concurrent_queue.h>

#include <fcntl.h>
#include <thread>
#include <unistd.h>

#include "util.hpp"

namespace engine {
namespace android {

#define PIPE_OUT 0
#define PIPE_IN  1

class OpenGLRenderer {
public:

  AChoreographer * aChoreographer = nullptr;
  ALooper * aLooper = nullptr;
  ANativeWindow * aNativeWindow = nullptr;

  int fds[2];
  int viewportWidth = -1;
  int viewportHeight = -1;

  volatile bool hardwareBufferDescribed = false;
  volatile bool eglPrepared = false;

  EGLDisplay eglDisplay;
  EGLContext eglContext;
  EGLSurface eglSurface;

  GLuint cameraBufTex;
  // concurrent queue needed as worker camera thread produces buffers while render thread consumes them
  tbb::concurrent_queue<AHardwareBuffer*> aHwBufferQueue;

  OpenGLRenderer();
  ~OpenGLRenderer();

  void setWindow(ANativeWindow * window);
  void updateWindowSize(int width, int height);
  void resetWindow();
  void render();
  bool couldRender() const;
  bool prepareEgl();
  void destroyEgl();

  // called from render thread
  void hwBufferToExternalTexture(AHardwareBuffer * aHardwareBuffer);
  // called from camera worker thread
  void feedHardwareBuffer(AHardwareBuffer * aHardwareBuffer);

private:
  std::thread renderThread;
  std::mutex eglMutex;
  std::condition_variable eglInitialized;
  std::condition_variable eglDestroyed;

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

  float bufferImageRatio = 1.0f;

  void eventLoop();
};

} // namespace android
} // namespace engine