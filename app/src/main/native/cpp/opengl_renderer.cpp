#include "opengl_renderer.hpp"

PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = nullptr;
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

namespace engine {
namespace android {

// OPENGL HELPER METHODS START

const char* stringFromError(GLenum err) {
  switch (err) {
    case GL_INVALID_ENUM:
      return "GL_INVALID_ENUM";
    case GL_INVALID_VALUE:
      return "GL_INVALID_VALUE";
    case GL_INVALID_OPERATION:
      return "GL_INVALID_OPERATION";
    case GL_INVALID_FRAMEBUFFER_OPERATION:
      return "GL_INVALID_FRAMEBUFFER_OPERATION";
    case GL_OUT_OF_MEMORY:
      return "GL_OUT_OF_MEMORY";
#ifdef GL_TABLE_TOO_LARGE
      case GL_TABLE_TOO_LARGE:
        return "GL_TABLE_TOO_LARGE";
#endif
#ifdef GL_STACK_OVERFLOW
      case GL_STACK_OVERFLOW:
        return "GL_STACK_OVERFLOW";
#endif
#ifdef GL_STACK_UNDERFLOW
      case GL_STACK_UNDERFLOW:
        return "GL_STACK_UNDERFLOW";
#endif
#ifdef GL_CONTEXT_LOST
      case GL_CONTEXT_LOST:
        return "GL_CONTEXT_LOST";
#endif
    default:
      return "ALL_OK";
  }
}

void checkLinkStatus(GLuint program) {
  GLint isLinked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &isLinked);
  if (isLinked == GL_FALSE) {
    GLint maxLength = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
    GLchar infoLog[maxLength];
    glGetProgramInfoLog(program, maxLength, &maxLength, &infoLog[0]);
    LOGE("GL shader link status: %s", &infoLog[0]);
  }
}

void checkCompileStatus(GLuint shader) {
  GLint isCompiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
  if (isCompiled == GL_FALSE) {
    GLint maxLength = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &maxLength);
    // The maxLength includes the NULL character
    GLchar errorLog[maxLength];
    glGetShaderInfoLog(shader, maxLength, &maxLength, &errorLog[0]);
    LOGE("GL shader link status: %s", &errorLog[0]);
  }
}

// OPENGL HELPER METHODS END

void OpenGLRenderer::doFrame(long, void* data) {
  auto * renderer = reinterpret_cast<engine::android::OpenGLRenderer*>(data);
  if (renderer->couldRender()) {
    renderer->render();
    // perform the check if aHwBufferQueue is not empty - then we need to catch up
    AHardwareBuffer * aHardwareBuffer;
    if (renderer->aHwBufferQueue.try_pop(aHardwareBuffer)) {
      LOGI("Catching up as some more buffers could be consumed!");
      renderer->hwBufferToExternalTexture(aHardwareBuffer);
    }
  }
}

bool OpenGLRenderer::couldRender() const {
  return eglPrepared && viewportHeight > 0 && viewportHeight > 0;
}

bool OpenGLRenderer::prepareEgl()
{
  LOGI("Configuring EGL");

  EGLDisplay display;
  EGLConfig config;
  EGLint numConfigs;
  EGLint format;
  EGLSurface surface;
  EGLContext context;

  // using classic RGBA 8888 config, trust Google Grafika here,
  // see https://github.com/google/grafika/blob/b1df331e89cffeab621f02b102d4c2c25eb6088a/app/src/main/java/com/android/grafika/gles/EglCore.java#L150-L152
  const int attr[] = {
    EGL_CONFIG_CAVEAT, EGL_NONE,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 0,
    EGL_STENCIL_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
    EGL_NONE
  };

  if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
    LOGE("eglGetDisplay() returned error %d", eglGetError());
    return false;
  }
  if (!eglInitialize(display, nullptr, nullptr)) {
    LOGE("eglInitialize() returned error %d", eglGetError());
    return false;
  }

  if (!eglChooseConfig(display, attr, &config, 1, &numConfigs)) {
    LOGE("eglChooseConfig() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
    LOGE("eglGetConfigAttrib() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!aNativeWindow) {
    LOGW("ANativeWindow was destroyed, could not prepare EGL");
    return false;
  }

  ANativeWindow_setBuffersGeometry(aNativeWindow, 0, 0, format);

  if (!(surface = eglCreateWindowSurface(display, config, aNativeWindow, nullptr))) {
    LOGE("eglCreateWindowSurface() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }
  const int attribute_list[] = {
    EGL_CONTEXT_CLIENT_VERSION, 3,
    EGL_NONE
  };

  if (!(context = eglCreateContext(display, config, nullptr, attribute_list))) {
    LOGE("eglCreateContext() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!eglMakeCurrent(display, surface, surface, context)) {
    LOGE("eglMakeCurrent() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  // need to cache all those to properly release them when Android destroys surface
  // we can't rely on eglGetCurrentContext() etc impl's as they will already be accessing bad surface, context, display at that point
  eglDisplay = display;
  eglSurface = surface;
  eglContext = context;

  // initialize extensions

  eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
  if (!eglCreateImageKHR) {
    LOGE("Couldn't get function pointer to eglCreateImageKHR extension!");
    return false;
  }
  glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
  if (!glEGLImageTargetTexture2DOES) {
    LOGE("Couldn't get function pointer to glEGLImageTargetTexture2DOES extension!");
    return false;
  }
  eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
  if (!eglDestroyImageKHR) {
    LOGE("Couldn't get function pointer to eglDestroyImageKHR extension!");
    return false;
  }
  eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC) eglGetProcAddress("eglGetNativeClientBufferANDROID");
  if (!eglGetNativeClientBufferANDROID) {
    LOGE("Couldn't get function pointer to eglGetNativeClientBufferANDROID extension!");
    return false;
  }

  // initial OpenGL ES setup

  program = glCreateProgram();
  vertexShader = glCreateShader(GL_VERTEX_SHADER);
  fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
  glCompileShader(vertexShader);
  checkCompileStatus(vertexShader);
  glAttachShader(program, vertexShader);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
  glCompileShader(fragmentShader);
  checkCompileStatus(fragmentShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  checkLinkStatus(program);

  glGenTextures(1, &cameraExternalTex);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraExternalTex);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

  glGenBuffers(1, vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(vertexArray),
    vertexArray,
    GL_STATIC_DRAW
  );
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  uniformMvp = glGetUniformLocation(program, "uMvpMatrix");
  externalSampler = glGetUniformLocation(program, "sExtSampler");

  LOGI("EGL initialized, version %s, GPU is %s",
       eglQueryString(eglDisplay, EGL_VERSION),
       (const char*)glGetString(GL_RENDERER)
  );
  LOGI("GL initialized, errors on context: %s, version: %s",
       stringFromError(glGetError()),
       glGetString(GL_VERSION)
  );
  // make sure that if we resume - buffer does not contain outdated frames so pop and release them
  AHardwareBuffer * aHardwareBuffer;
  while (aHwBufferQueue.try_pop(aHardwareBuffer)) {
    AHardwareBuffer_release(aHardwareBuffer);
  }
  eglPrepared = true;
  return true;
}

void OpenGLRenderer::destroyEgl() {
  LOGI("Destroying EGL");
  eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(eglDisplay, eglContext);
  eglDestroySurface(eglDisplay, eglSurface);
  eglTerminate(eglDisplay);
  eglDisplay = EGL_NO_DISPLAY;
  eglSurface = EGL_NO_SURFACE;
  eglContext = EGL_NO_CONTEXT;
  eglReleaseThread();
  eglPrepared = false;
  LOGI("EGL destroyed!");
}

void OpenGLRenderer::render() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  // no actual camera drawing to do if first hardware buffer was not described and loaded to ext texture
  if (!hardwareBufferDescribed) {
    if (!eglSwapBuffers(eglDisplay, eglSurface)) {
      LOGE("eglSwapBuffers returned error %d", eglGetError());
    }
    return;
  }
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glVertexAttribPointer(
    0,
    2,
    GL_FLOAT,
    GL_FALSE,
    4 * sizeof(float),
    nullptr
  );
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(
    1,
    2,
    GL_FLOAT,
    GL_FALSE,
    4 * sizeof(float),
    (void*) (2 * sizeof(float))
  );
  glEnableVertexAttribArray(1);
  // calculate MVP matrix only once
  static const float viewportRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
  static const float ratio = viewportRatio * bufferImageRatio;
  static auto proj = glm::frustum(-ratio, ratio, -1.f, 1.f, 3.f, 7.f);
  static auto view = glm::lookAt(
    glm::vec3(0.f, 0.f, 3.f),
    glm::vec3(0.f, 0.f, 0.f),
    glm::vec3(1.f, 0.f, 0.f)
    );
  static auto mvp = proj * view;
  glUniformMatrix4fv(uniformMvp, 1, GL_FALSE, glm::value_ptr(mvp));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraExternalTex);
  glUniform1i(externalSampler, 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  if (!eglSwapBuffers(eglDisplay, eglSurface)) {
    LOGE("eglSwapBuffers returned error %d", eglGetError());
  } else {
    LOGI("Swapped buffers!");
  }
}

void OpenGLRenderer::hwBufferToExternalTexture(AHardwareBuffer * aHardwareBuffer) {
  // EGL could have already be destroyed beforehand
  if (!eglPrepared) return;
  static EGLint attrs[] = { EGL_NONE };
  LOGI("Pop hardware buffer, size %u", aHwBufferQueue.unsafe_size());
  EGLImageKHR image = eglCreateImageKHR(
    eglDisplay,
    // a bit strange - at least Adreno 640 works OK only when EGL_NO_CONTEXT is passed...
    // on Mali G77 passing valid OpenGL context works here but EGL_NO_CONTEXT works as well so
    // leaving EGL_NO_CONTEXT here
    EGL_NO_CONTEXT,
    EGL_NATIVE_BUFFER_ANDROID,
    eglGetNativeClientBufferANDROID(aHardwareBuffer),
    attrs);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraExternalTex);
  glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  // interesting - works OK destroying it here, before actual rendering
  eglDestroyImageKHR(eglDisplay, image);
  // still not precisely clear - if AHardwareBuffer is 'shared' memory -
  // releasing it here should lead to missing texture data when drawing
  // but it works as expected if we do release memory here, so OK
  AHardwareBuffer_release(aHardwareBuffer);
  if (!hardwareBufferDescribed) {
    hardwareBufferDescribed = true;
  }
}

} // namespace android
} // namespace engine