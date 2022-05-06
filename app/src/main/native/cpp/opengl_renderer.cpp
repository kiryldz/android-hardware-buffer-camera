#include "opengl_renderer.hpp"

PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC eglGetNativeClientBufferANDROID = nullptr;
PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = nullptr;
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;

namespace {

void doFrame(long, void* data) {
  auto * renderer = reinterpret_cast<engine::android::OpenGLRenderer*>(data);
  if (renderer->couldRender()) {
    renderer->render();
    // posting next frame callback, no need to explicitly wake the looper afterwards
    // as AChoreographer seems to operate with it's own fd and callbacks
    // TODO no need to actually post the callback if there's nothing to draw (new buffer did not arrive)
    AChoreographer_postFrameCallback(renderer->aChoreographer, doFrame, renderer);
  }
}

int looperCallback(int fd, int events, void* data) {
  char buffer[9];
  while (read(fd, buffer, sizeof(buffer)) > 0) {}
//  LOGE("looperCallback fd=%i, events=%i, buffer=%s", fd, events, buffer);
  auto * renderer = reinterpret_cast<engine::android::OpenGLRenderer*>(data);
  if (std::string(buffer) == "setWindow") {
    renderer->prepareEgl();
    renderer->aChoreographer = AChoreographer_getInstance();
    AChoreographer_postFrameCallback(renderer->aChoreographer, doFrame, renderer);
  } else if (std::string(buffer) == "destroy__") {
    // TODO validate that fd is the correct one here
    ALooper_removeFd(renderer->aLooper, fd);
    if (close(renderer->fds[PIPE_IN]) || close(renderer->fds[PIPE_OUT])) {
      throw std::runtime_error("Failed to close file descriptor!");
    }
    // explicit wake here in order to unblock ALooper_pollAll
    ALooper_wake(renderer->aLooper);
    ALooper_release(renderer->aLooper);
    // returning 0 to have this file descriptor and callback unregistered from the looper
    return 0;
  } else if (std::string(buffer) == "updWinSiz") {
    LOGE("update window size, width=%i, height=%i", renderer->viewportWidth, renderer->viewportHeight);
    glClearColor(0.5, 0.5, 0.5, 0.5);
    glViewport(0, 0, renderer->viewportWidth, renderer->viewportHeight);
  } else if (std::string(buffer) == "resetWind") {
    renderer->destroyEgl();
    renderer->aNativeWindow = nullptr;
  } else if (std::string(buffer) == "buffReady") {
    // TODO check what are the arguments
    static EGLint attrs[] = { EGL_NONE };
    EGLImageKHR image = eglCreateImageKHR(
      renderer->eglDisplay,
      renderer->eglContext,
      EGL_NATIVE_BUFFER_ANDROID,
      eglGetNativeClientBufferANDROID(renderer->aHardwareBuffer),
      attrs);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, renderer->cameraBufTex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, image);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
  }
  // returning 1 to continue receiving callbacks
  return 1;
}

}

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
    LOGE("%s", &infoLog[0]);
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
    LOGE("%s", &errorLog[0]);
  }
}



// OPENGL HELPER METHODS END

OpenGLRenderer::OpenGLRenderer(): renderThread(std::thread(&OpenGLRenderer::eventLoop, this)) {
}

OpenGLRenderer::~OpenGLRenderer() {
  LOGE("OpenGLRenderer destructor");
  write(fds[PIPE_IN], "destroy__", 9);
  LOGE("OpenGLRenderer renderThread.join waiting");
  renderThread.join();
  LOGE("OpenGLRenderer render thread killed");
}

void OpenGLRenderer::setWindow(ANativeWindow * window) {
  // TODO for sure refactor for something more elegant, perhaps move thread with looper to another class

  std::unique_lock<std::mutex> lock(mutex);
  aNativeWindow = window;
  // schedule an event to the render thread
  write(fds[PIPE_IN], "setWindow", 9);
  LOGE("setWindow waiting for context creation...");
  eglInitialized.wait(lock);
  LOGE("setWindow resume main thread");
}

void OpenGLRenderer::updateWindowSize(int width, int height) {
  // schedule an event to the render thread
  write(fds[PIPE_IN], "updWinSiz", 9);
  // TODO not safe as those are also assigned from render thread - pass them to render thread somehow
  viewportWidth = width;
  viewportHeight = height;
}

void OpenGLRenderer::resetWindow() {
  std::unique_lock<std::mutex> lock(mutex);
  LOGE("resetWindow");
  write(fds[PIPE_IN], "resetWind", 9);
  LOGE("resetWindow waiting for destroying EGL...");
  eglDestroyed.wait(lock);
  LOGE("resetWindow resume main thread");
}

bool OpenGLRenderer::couldRender() const {
  return eglPrepared && viewportHeight > 0 && viewportHeight > 0;
}

bool OpenGLRenderer::prepareEgl()
{
  LOGE("Configuring EGL");

  EGLDisplay display;
  EGLConfig config;
  EGLint numConfigs;
  EGLint format;
  EGLSurface surface;
  EGLContext context;
  EGLint width;
  EGLint height;

  // TODO think of better config, now using one for Samsung S20 Ultra 5G
  const int attr[] = {
    EGL_CONFIG_CAVEAT, EGL_NONE,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_BUFFER_SIZE, 24,
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 0,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };

  if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY)
  {
    LOGE("eglGetDisplay() returned error %d", eglGetError());
    return false;
  }
  if (!eglInitialize(display, 0, 0))
  {
    LOGE("eglInitialize() returned error %d", eglGetError());
    return false;
  }

  if (!eglChooseConfig(display, attr, &config, 1, &numConfigs))
  {
    LOGE("eglChooseConfig() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format))
  {
    LOGE("eglGetConfigAttrib() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!aNativeWindow)
  {
    LOGW("ANativeWindow was destroyed, could not prepare EGL");
    return false;
  }

  ANativeWindow_setBuffersGeometry(aNativeWindow, 0, 0, format);

  if (!(surface = eglCreateWindowSurface(display, config, aNativeWindow, 0)))
  {
    LOGE("eglCreateWindowSurface() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }
  const int attribute_list[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  if (!(context = eglCreateContext(display, config, 0, attribute_list)))
  {
    LOGE("eglCreateContext() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  if (!eglMakeCurrent(display, surface, surface, context))
  {
    LOGE("eglMakeCurrent() returned error %d", eglGetError());
    destroyEgl();
    return false;
  }

  eglDisplay = display;
  eglContext = context;
  eglSurface = surface;

  LOGE("EGL version: %s", eglQueryString(eglDisplay, EGL_VERSION));

  // initialize extensions

  eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
  if (!eglCreateImageKHR)
  {
    LOGE("Couldn't get function pointer to eglCreateImageKHR!");
    return false;
  }
  glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
  if (!glEGLImageTargetTexture2DOES)
  {
    LOGE("Couldn't get function pointer to glEGLImageTargetTexture2DOES!");
    return false;
  }
  eglGetNativeClientBufferANDROID = (PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC) eglGetProcAddress("eglGetNativeClientBufferANDROID");
  if (!eglGetNativeClientBufferANDROID)
  {
    LOGE("Couldn't get function pointer to eglGetNativeClientBufferANDROID!");
    return false;
  }
  // texture
  glGenTextures(1, &cameraBufTex);

  // initial setup

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

  glGenBuffers(2, vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(rectVertex),
    rectVertex,
    GL_STATIC_DRAW
  );
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glBufferData(
    GL_ARRAY_BUFFER,
    sizeof(rectTex),
    rectTex,
    GL_STATIC_DRAW
  );
  glBindBuffer(GL_ARRAY_BUFFER, 0);

  attributePosition = glGetAttribLocation(program, "aPosition");
  attributeTextureCoord = glGetAttribLocation(program, "aTexCoord");
  uniformMvp = glGetUniformLocation(program, "uMvpMatrix");
  uniformSampler = glGetUniformLocation(program, "sExtSampler");

  eglPrepared = true;
  LOGE("EGL initialized, GPU is %s", (const char*)glGetString(GL_RENDERER));
  LOGE("GL error, initial config: %s", stringFromError(glGetError()));
  eglInitialized.notify_one();

  return true;
}

void OpenGLRenderer::destroyEgl() {
  LOGE("Destroying EGL");

  eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(eglDisplay, eglContext);
  eglDestroySurface(eglDisplay, eglSurface);
  eglReleaseThread();
  eglTerminate(eglDisplay);

  eglDisplay = EGL_NO_DISPLAY;
  eglContext = EGL_NO_CONTEXT;
  eglSurface = EGL_NO_SURFACE;

  eglPrepared = false;
  LOGE("EGL destroyed!");
  eglDestroyed.notify_one();
}

void OpenGLRenderer::render() {
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(program);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
  glVertexAttribPointer(
    attributePosition,
    coordsPerVertex,
    GL_FLOAT,
    GL_FALSE,
    vertexStride,
    nullptr
  );
  glEnableVertexAttribArray(attributePosition);
  glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
  glVertexAttribPointer(
    attributeTextureCoord,
    coordsPerTex,
    GL_FLOAT,
    GL_FALSE,
    texStride,
    nullptr
  );
  glEnableVertexAttribArray(attributeTextureCoord);
  // TODO matrix must be calculated once
  const float viewportRatio = static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight);
  const float ratio = viewportRatio * bufferImageRatio;
  auto proj = glm::frustum(-ratio, ratio, -1.f, 1.f, 3.f, 7.f);
  auto view = glm::lookAt(
    glm::vec3(0.f, 0.f, 3.f),
    glm::vec3(0.f, 0.f, 0.f),
    glm::vec3(1.f, 0.f, 0.f)
    );
  auto mvp = proj * view;
  glUniformMatrix4fv(uniformMvp, 1, GL_FALSE, glm::value_ptr(mvp));
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_EXTERNAL_OES, cameraBufTex);
  glUniform1i(uniformSampler, 0);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, vertexCount);
  LOGE("GL error, render: %s", stringFromError(glGetError()));
  glDisableVertexAttribArray(attributePosition);
  glDisableVertexAttribArray(attributeTextureCoord);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glUseProgram(0);
  if (!eglSwapBuffers(eglDisplay, eglSurface)) {
    LOGE("eglSwapBuffers returned error %d", eglGetError());
  } else {
    LOGE("Swapped buffers!");
  }
  // TODO releasing leads to null ptr dereference, revisit
//  AHardwareBuffer_release(aHardwareBuffer);
  bufferAcquired.notify_one();
}

void OpenGLRenderer::eventLoop() {
  // in order to use AChoreographer for effective rendering
  // and allow scheduling events in general we prepare and acquire the ALooper
  aLooper = ALooper_prepare(0);
  assert(aLooper);
  ALooper_acquire(aLooper);
  if (pipe2(fds, O_CLOEXEC)) {
    throw std::runtime_error("Failed to create pipe needed for the ALooper");
  }
  // TODO understand if this is really required
  if (fcntl(fds[PIPE_OUT], F_SETFL, O_NONBLOCK)) {
    throw std::runtime_error("Failed to set pipe read end non-blocking.");
  }
  auto ret = ALooper_addFd(aLooper, fds[PIPE_OUT], ALOOPER_POLL_CALLBACK,
                      ALOOPER_EVENT_INPUT, looperCallback, this);
  if (ret != 1) {
    throw std::runtime_error("Failed to add file descriptor to Looper.");
  }

  int outFd, outEvents;
  char *outData = nullptr;

  // using negative timeout and block poll forever
  // we will be using poll callbacks to schedule events and not make use of ALooper_wake() at all unless we want to exit the thread
  ALooper_pollAll(-1, &outFd, &outEvents, reinterpret_cast<void**>(&outData));
}

void OpenGLRenderer::feedHardwareBuffer(AHardwareBuffer * buffer) {
  std::unique_lock<std::mutex> lock(mutex);
  LOGE("feed new hardware buffer");
  // TODO do this code once
  AHardwareBuffer_Desc description;
  AHardwareBuffer_describe(buffer, &description);
  bufferImageRatio = static_cast<float>(description.width) / static_cast<float>(description.height);

  // TODO introduce flag like needsRender, for now simply schedule event that buffer is available
  aHardwareBuffer = buffer;
  // TODO investigate acquire-release better, now it should be fine due to bufferAcquired lock
//  AHardwareBuffer_acquire(aHardwareBuffer);
  write(fds[PIPE_IN], "buffReady", 9);
  // TODO ideally we need buffer queue and not lock camera worker thread but for simplicity add the lock now
  bufferAcquired.wait(lock);
}

} // namespace android
} // namespace engine