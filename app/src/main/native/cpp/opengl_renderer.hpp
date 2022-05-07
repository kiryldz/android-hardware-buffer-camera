#pragma once

#include <android/looper.h>
#include <android/choreographer.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

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
  AHardwareBuffer * aHardwareBuffer = nullptr;

  int fds[2];
  int viewportWidth = -1;
  int viewportHeight = -1;

  GLuint cameraBufTex;

  OpenGLRenderer();
  ~OpenGLRenderer();

  void setWindow(ANativeWindow * window);
  void updateWindowSize(int width, int height);
  void resetWindow();
  void render();
  bool couldRender() const;
  bool prepareEgl();
  void destroyEgl();

  void feedHardwareBuffer(AHardwareBuffer * buffer);

private:
  std::thread renderThread;
  std::mutex mutex;
  std::condition_variable eglInitialized;
  std::condition_variable eglDestroyed;
  std::condition_variable bufferAcquired;

  const GLchar * vertexShaderSource = "precision highp float; uniform mat4 uMvpMatrix; attribute vec2 aPosition; attribute vec2 aTexCoord; varying vec2 vCoordinate; void main() { vCoordinate = aTexCoord; gl_Position = uMvpMatrix * vec4(aPosition, 0.0, 1.0); }";
  const GLchar * fragmentShaderSource = "#extension GL_OES_EGL_image_external : require\nprecision mediump float; varying vec2 vCoordinate; uniform samplerExternalOES sExtSampler; void main() { gl_FragColor = texture2D(sExtSampler, vCoordinate); }";
  float rectVertex[8] = {
    -1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, -1.0f,
    1.0f, 1.0f
  };
  float rectTex[8] = {
    0.0f, 0.0f,
    0.0f, 1.0f,
    1.0f, 0.0f,
    1.0f, 1.0f
  };
  int coordsPerVertex = 2;
  int coordsPerTex = 2;
  int vertexStride = coordsPerVertex * sizeof(float);
  int texStride = coordsPerTex * sizeof(float);
  int vertexCount = (sizeof(rectVertex) / sizeof(*rectVertex)) / coordsPerVertex;
  GLuint program = 0;
  GLuint vertexShader = 0;
  GLuint fragmentShader = 0;
  GLuint vbo[2];
  GLuint attributePosition = 0;
  GLuint attributeTextureCoord = 0;
  GLint uniformMvp = 0;
  GLint uniformSampler = 0;

  float bufferImageRatio = 1.0f;
  // egl
  volatile bool eglPrepared = false;

  void eventLoop();
};

} // namespace android
} // namespace engine