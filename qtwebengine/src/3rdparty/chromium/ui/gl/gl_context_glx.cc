// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/gl_context_glx.h"

#include <memory>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/numerics/safe_conversions.h"
#include "base/trace_event/trace_event.h"
#include "ui/gfx/x/connection.h"
#include "ui/gl/GL/glextchromium.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_implementation.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_surface_glx.h"

#ifndef GLX_NV_robustness_video_memory_purge
#define GLX_NV_robustness_video_memory_purge 1
#define GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV 0x20F7
#endif /* GLX_NV_robustness_video_memory_purge */

namespace gl {

namespace {

using GLVersion = std::pair<int, int>;

GLXContext CreateContextAttribs(x11::Connection* connection,
                                GLXFBConfig config,
                                GLXContext share,
                                GLVersion version,
                                int profile_mask) {
  std::vector<int> attribs;

  if (GLSurfaceGLX::IsCreateContextRobustnessSupported()) {
    attribs.push_back(GLX_CONTEXT_RESET_NOTIFICATION_STRATEGY_ARB);
    attribs.push_back(GLX_LOSE_CONTEXT_ON_RESET_ARB);

    if (GLSurfaceGLX::IsRobustnessVideoMemoryPurgeSupported()) {
      attribs.push_back(GLX_GENERATE_RESET_ON_VIDEO_MEMORY_PURGE_NV);
      attribs.push_back(GL_TRUE);
    }
  }

  if (version.first != 0 || version.second != 0) {
    attribs.push_back(GLX_CONTEXT_MAJOR_VERSION_ARB);
    attribs.push_back(version.first);

    attribs.push_back(GLX_CONTEXT_MINOR_VERSION_ARB);
    attribs.push_back(version.second);
  }

  if (profile_mask != 0 && GLSurfaceGLX::IsCreateContextProfileSupported()) {
    attribs.push_back(GLX_CONTEXT_PROFILE_MASK_ARB);
    attribs.push_back(profile_mask);
  }

  attribs.push_back(0);

  GLXContext context = glXCreateContextAttribsARB(
      connection->GetXlibDisplay(), config, share,
      true, attribs.data());

  return context;
}

GLXContext CreateHighestVersionContext(x11::Connection* connection,
                                       GLXFBConfig config,
                                       GLXContext share) {
  // The only way to get a core profile context of the highest version using
  // glXCreateContextAttrib is to try creationg contexts in decreasing version
  // numbers. It might look that asking for a core context of version (0, 0)
  // works on some driver but it create a _compatibility_ context of the highest
  // version instead. The cost of failing a context creation is small (< 0.1 ms)
  // on Mesa but is unfortunately a bit expensive on the Nvidia driver (~3ms).

  // Also try to get any Desktop GL context, but if that fails fallback to
  // asking for OpenGL ES contexts.

  std::string client_vendor =
      glXGetClientString(connection->GetXlibDisplay(), GLX_VENDOR);
  bool is_mesa = client_vendor.find("Mesa") != std::string::npos;

  struct ContextCreationInfo {
    ContextCreationInfo(int profileFlag, GLVersion version)
        : profileFlag(profileFlag), version(version) {}
    int profileFlag;
    GLVersion version;
  };

  std::vector<ContextCreationInfo> contexts_to_try;
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 5));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 4));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 3));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 2));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 1));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(4, 0));
  contexts_to_try.emplace_back(GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                               GLVersion(3, 3));

  // On Mesa, do not try to create OpenGL context versions between 3.0 and
  // 3.2 because of compatibility problems. See crbug.com/659030
  if (!is_mesa) {
    contexts_to_try.emplace_back(0, GLVersion(3, 2));
    contexts_to_try.emplace_back(0, GLVersion(3, 1));
    contexts_to_try.emplace_back(0, GLVersion(3, 0));
  }

  contexts_to_try.emplace_back(0, GLVersion(2, 1));
  contexts_to_try.emplace_back(0, GLVersion(2, 0));
  contexts_to_try.emplace_back(0, GLVersion(1, 5));
  contexts_to_try.emplace_back(0, GLVersion(1, 4));
  contexts_to_try.emplace_back(0, GLVersion(1, 3));
  contexts_to_try.emplace_back(0, GLVersion(1, 2));
  contexts_to_try.emplace_back(0, GLVersion(1, 1));
  contexts_to_try.emplace_back(0, GLVersion(1, 0));
  contexts_to_try.emplace_back(GLX_CONTEXT_ES2_PROFILE_BIT_EXT,
                               GLVersion(3, 2));
  contexts_to_try.emplace_back(GLX_CONTEXT_ES2_PROFILE_BIT_EXT,
                               GLVersion(3, 1));
  contexts_to_try.emplace_back(GLX_CONTEXT_ES2_PROFILE_BIT_EXT,
                               GLVersion(3, 0));
  contexts_to_try.emplace_back(GLX_CONTEXT_ES2_PROFILE_BIT_EXT,
                               GLVersion(2, 0));

  for (const auto& info : contexts_to_try) {
    if (!GLSurfaceGLX::IsCreateContextES2ProfileSupported() &&
        info.profileFlag == GLX_CONTEXT_ES2_PROFILE_BIT_EXT) {
      continue;
    }
    GLXContext context = CreateContextAttribs(connection, config, share,
                                              info.version, info.profileFlag);
    if (context != nullptr) {
      return context;
    }
  }

  return nullptr;
}

}  // namespace

GLContextGLX::GLContextGLX(GLShareGroup* share_group)
    : GLContextReal(share_group) {}

bool GLContextGLX::InitializeImpl(GLSurface* compatible_surface,
                                  const GLContextAttribs& attribs) {
  // webgl_compatibility_context and disabling bind_generates_resource are not
  // supported.
  DCHECK(!attribs.webgl_compatibility_context &&
         attribs.bind_generates_resource);

  connection_ = x11::Connection::Get();

  GLXContext share_handle = static_cast<GLXContext>(
      share_group() ? share_group()->GetHandle() : nullptr);

  if (GLSurfaceGLX::IsCreateContextSupported()) {
    DVLOG(1) << "GLX_ARB_create_context supported.";
    if (base::CommandLine::ForCurrentProcess()->HasSwitch(switches::kCreateDefaultGLContext)) {
      context_ = CreateContextAttribs(
          connection_, static_cast<GLXFBConfig>(compatible_surface->GetConfig()),
          share_handle, GLVersion(0, 0), 0);
    } else {
      context_ = CreateHighestVersionContext(
          connection_, static_cast<GLXFBConfig>(compatible_surface->GetConfig()),
          share_handle);
    }
    if (!context_) {
      LOG(ERROR) << "Failed to create GL context with "
                 << "glXCreateContextAttribsARB.";
      return false;
    }
  } else {
    DVLOG(1) << "GLX_ARB_create_context not supported.";
    context_ = glXCreateNewContext(
        connection_->GetXlibDisplay(),
        static_cast<GLXFBConfig>(compatible_surface->GetConfig()),
        GLX_RGBA_TYPE, share_handle, true);
    if (!context_) {
      LOG(ERROR) << "Failed to create GL context with glXCreateNewContext.";
      return false;
    }
  }
  DCHECK(context_);
  DVLOG(1) << "  Successfully allocated "
           << (compatible_surface->IsOffscreen() ? "offscreen" : "onscreen")
           << " GL context with LOSE_CONTEXT_ON_RESET_ARB";

  DVLOG(1) << (compatible_surface->IsOffscreen() ? "Offscreen" : "Onscreen")
           << " context was "
           << (glXIsDirect(connection_->GetXlibDisplay(),
                           static_cast<GLXContext>(context_))
                   ? "direct"
                   : "indirect")
           << ".";

  return true;
}

void GLContextGLX::Destroy() {
  OnContextWillDestroy();
  if (context_) {
    glXDestroyContext(
        connection_->GetXlibDisplay(),
        static_cast<GLXContext>(context_));
    context_ = nullptr;
  }
}

bool GLContextGLX::MakeCurrentImpl(GLSurface* surface) {
  DCHECK(context_);
  if (IsCurrent(surface))
    return true;

  ScopedReleaseCurrent release_current;
  TRACE_EVENT0("gpu", "GLContextGLX::MakeCurrent");
  if (!glXMakeContextCurrent(
          connection_->GetXlibDisplay(),
          reinterpret_cast<GLXDrawable>(surface->GetHandle()),
          reinterpret_cast<GLXDrawable>(surface->GetHandle()),
          static_cast<GLXContext>(context_))) {
    LOG(ERROR) << "Couldn't make context current with X drawable.";
    return false;
  }

  // Set this as soon as the context is current, since we might call into GL.
  BindGLApi();

  SetCurrent(surface);
  InitializeDynamicBindings();

  if (!surface->OnMakeCurrent(this)) {
    LOG(ERROR) << "Could not make current.";
    return false;
  }

  release_current.Cancel();
  return true;
}

void GLContextGLX::ReleaseCurrent(GLSurface* surface) {
  if (!IsCurrent(surface))
    return;

  SetCurrent(nullptr);
  if (!glXMakeContextCurrent(
          connection_->GetXlibDisplay(), 0, 0,
          nullptr)) {
    LOG(ERROR) << "glXMakeCurrent failed in ReleaseCurrent";
  }
}

bool GLContextGLX::IsCurrent(GLSurface* surface) {
  bool native_context_is_current =
      glXGetCurrentContext() == static_cast<GLXContext>(context_);

  // If our context is current then our notion of which GLContext is
  // current must be correct. On the other hand, third-party code
  // using OpenGL might change the current context.
  DCHECK(!native_context_is_current || (GetRealCurrent() == this));

  if (!native_context_is_current)
    return false;

  if (surface) {
    if (glXGetCurrentDrawable() !=
        reinterpret_cast<GLXDrawable>(surface->GetHandle())) {
      return false;
    }
  }

  return true;
}

void* GLContextGLX::GetHandle() {
  return context_;
}

unsigned int GLContextGLX::CheckStickyGraphicsResetStatusImpl() {
  DCHECK(IsCurrent(nullptr));
  DCHECK(g_current_gl_driver);
  const ExtensionsGL& ext = g_current_gl_driver->ext;
  if ((graphics_reset_status_ == GL_NO_ERROR) &&
      GLSurfaceGLX::IsCreateContextRobustnessSupported() &&
      (ext.b_GL_KHR_robustness || ext.b_GL_EXT_robustness ||
       ext.b_GL_ARB_robustness)) {
    graphics_reset_status_ = glGetGraphicsResetStatusARB();
  }
  return graphics_reset_status_;
}

GLContextGLX::~GLContextGLX() {
  Destroy();
}

}  // namespace gl
