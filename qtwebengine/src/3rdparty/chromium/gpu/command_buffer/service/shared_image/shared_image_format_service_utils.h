// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_SHARED_IMAGE_FORMAT_SERVICE_UTILS_H_
#define GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_SHARED_IMAGE_FORMAT_SERVICE_UTILS_H_

#include "build/build_config.h"
#include "components/viz/common/resources/shared_image_format.h"
#include "gpu/command_buffer/common/gl2_types.h"
#include "gpu/config/gpu_preferences.h"
#include "gpu/gpu_gles2_export.h"
#include "gpu/vulkan/buildflags.h"
#include "skia/buildflags.h"
#include "third_party/dawn/include/dawn/webgpu.h"
#include "third_party/dawn/include/dawn/webgpu_cpp.h"
#include "third_party/skia/include/core/SkYUVAInfo.h"
#include "third_party/skia/include/gpu/graphite/TextureInfo.h"
#include "ui/gfx/buffer_types.h"

#if BUILDFLAG(ENABLE_VULKAN)
#include <vulkan/vulkan_core.h>
#endif

namespace skgpu::graphite {
class TextureInfo;
}

namespace gpu {

namespace gles2 {
class FeatureInfo;
}  // namespace gles2

// GLFormatDesc is a struct containing the GL data type, data format, internal
// format used by the image, internal format used for storage and GL target.
struct GLFormatDesc {
  GLenum data_type = 0;
  GLenum data_format = 0;
  GLenum image_internal_format = 0;
  GLenum storage_internal_format = 0;
  GLenum target = 0;
};

// A set of utility functions to get the equivalent GPU API (GL, Vulkan, Dawn,
// Metal) type/format information for a given SharedImageFormat. These functions
// should ideally only be called from the GPU service and viz.

// BufferFormat is being transitioned out of SharedImage code (to use
// SharedImageFormat instead). Refrain from using this function or preferably
// use with single planar SharedImageFormats. Returns BufferFormat for given
// `format`.
GPU_GLES2_EXPORT gfx::BufferFormat ToBufferFormat(
    viz::SharedImageFormat format);

// Returns SkYUVAInfo::PlaneConfig equivalent of
// SharedImageFormat::PlaneConfig.
GPU_GLES2_EXPORT SkYUVAInfo::PlaneConfig ToSkYUVAPlaneConfig(
    viz::SharedImageFormat format);

// Returns SkYUVAInfo::Subsampling equivalent of
// SharedImageFormat::Subsampling.
GPU_GLES2_EXPORT SkYUVAInfo::Subsampling ToSkYUVASubsampling(
    viz::SharedImageFormat format);

// Returns the closest SkColorType for a given multiplanar `format` with
// external sampler.
GPU_GLES2_EXPORT SkColorType
ToClosestSkColorTypeExternalSampler(viz::SharedImageFormat format);

// Holds capabilities and provides accessors for getting appropriate GL formats
// for shared images.
class GPU_GLES2_EXPORT GLFormatCaps {
 public:
  // For default values of feature info capabilities.
  GLFormatCaps() = default;
  explicit GLFormatCaps(const gles2::FeatureInfo* feature_info);

  // Following functions return the appropriate GL type/format for a
  // SharedImageFormat.
  // Return the GLFormatDesc when using external sampler for a given `format`.
  GLFormatDesc ToGLFormatDescExternalSampler(
      viz::SharedImageFormat format) const;
  // Return the GLFormatDesc for a given `format`.
  GLFormatDesc ToGLFormatDesc(viz::SharedImageFormat format,
                              int plane_index) const;
  // Same as above with an additional param to control whether to use
  // HALF_FLOAT_OES extension types or core types for F16 format.
  GLFormatDesc ToGLFormatDescOverrideHalfFloatType(
      viz::SharedImageFormat format,
      int plane_index) const;

  bool ext_texture_rg() const { return ext_texture_rg_; }
  bool ext_texture_norm16() const { return ext_texture_norm16_; }
  bool disable_r8_shared_images() const { return disable_r8_shared_images_; }

 private:
  // Return fallback gl format if the GL data, internal, tex storage format is
  // not supported.
  GLenum GetFallbackFormatIfNotSupported(GLenum gl_format) const;

  bool angle_rgbx_internal_format_ = false;
  bool oes_texture_float_available_ = false;
  bool ext_texture_rg_ = false;
  bool ext_texture_norm16_ = false;
  bool disable_r8_shared_images_ = false;
};

// Following functions return the appropriate Vulkan format for a
// SharedImageFormat.
#if BUILDFLAG(ENABLE_VULKAN)
// Returns true if given `format` is supported by Vulkan.
GPU_GLES2_EXPORT bool HasVkFormat(viz::SharedImageFormat format);
// Returns vulkan format for given `format` with external sampler.
GPU_GLES2_EXPORT VkFormat
ToVkFormatExternalSampler(viz::SharedImageFormat format);
// Returns vulkan format for given `format`, which must be single-planar.
GPU_GLES2_EXPORT VkFormat ToVkFormatSinglePlanar(viz::SharedImageFormat format);
// Returns vulkan format for given `format`, which can be single-planar or
// multiplanar with per-plane sampling.
GPU_GLES2_EXPORT VkFormat ToVkFormat(viz::SharedImageFormat format,
                                     int plane_index);
#endif

// Following functions return the appropriate Dawn format for a
// SharedImageFormat. Returns wgpu::TextureFormat format for given `format`.
// Note that this will return a multi-planar Dawn format for multi-planar
// SharedImageFormat.
GPU_GLES2_EXPORT wgpu::TextureFormat ToDawnFormat(
    viz::SharedImageFormat format);
// Returns wgpu::TextureFormat format for given `format` and `plane_index`. Note
// that this returns a single plane Dawn format i.e the TextureView format and
// not a multi-planar format.
GPU_GLES2_EXPORT wgpu::TextureFormat ToDawnTextureViewFormat(
    viz::SharedImageFormat format,
    int plane_index);

// Returns the supported Dawn texture usage. `is_yuv_plane` indicates if the
// texture corresponds to a plane of a multi-planar image and `is_dcomp_surface`
// indicates if the texture corresponds to a direct composition surface.
// `supports_multiplanar_rendering` indicates if the dawn texture supports
// drawing to multiplanar render targets.
wgpu::TextureUsage SupportedDawnTextureUsage(
    bool is_yuv_plane = false,
    bool is_dcomp_surface = false,
    bool supports_multiplanar_rendering = false,
    bool supports_multiplanar_copy = false);

// Returns wgpu::TextureAspect corresponding to `plane_index`. `is_yuv_plane`
// indicates if the aspect corresponds to a plane of a multi-planar
// wgpu::Texture.
wgpu::TextureAspect ToDawnTextureAspect(bool is_yuv_plane, int plane_index);

// Following function return the appropriate Metal format for a
// SharedImageFormat.
#if BUILDFLAG(IS_APPLE)
// Returns MtlPixelFormat format for given `format`.
GPU_GLES2_EXPORT unsigned int ToMTLPixelFormat(viz::SharedImageFormat format,
                                               int plane_index = 0);
#endif

// Returns the graphite::TextureInfo for a given `format` and `plane_index`.
// `is_yuv_plane` indicates if the texture corresponds to a plane of a
// multi-planar image. `mipmapped` indicates if the texture has mipmaps.
// `scanout_dcomp_surface` indicates if the texture corresponds to a Windows
// direct composition surface. `supports_multiplanar_rendering` indicates if the
// dawn texture supports drawing to multiplanar render targets.
// `supports_multiplanar_copy` indicates if the dawn backend supports copy
// operations for multiplanar textures.
GPU_GLES2_EXPORT skgpu::graphite::TextureInfo GraphiteBackendTextureInfo(
    GrContextType gr_context_type,
    viz::SharedImageFormat format,
    int plane_index = 0,
    bool is_yuv_plane = false,
    bool mipmapped = false,
    bool scanout_dcomp_surface = false,
    bool supports_multiplanar_rendering = false,
    bool supports_multiplanar_copy = false);

GPU_GLES2_EXPORT skgpu::graphite::TextureInfo GraphitePromiseTextureInfo(
    GrContextType gr_context_type,
    viz::SharedImageFormat format,
    int plane_index = 0,
    bool mipmapped = false,
    bool scanout_dcomp_surface = false);

#if BUILDFLAG(SKIA_USE_DAWN)
GPU_GLES2_EXPORT skgpu::graphite::DawnTextureInfo DawnBackendTextureInfo(
    viz::SharedImageFormat format,
    bool is_yuv_plane = false,
    int plane_index = 0,
    bool mipmapped = false,
    bool scanout_dcomp_surface = false,
    bool supports_multiplanar_rendering = false,
    bool support_multiplanar_copy = false);
#endif

#if BUILDFLAG(SKIA_USE_METAL)
GPU_GLES2_EXPORT skgpu::graphite::MtlTextureInfo GraphiteMetalTextureInfo(
    viz::SharedImageFormat format,
    int plane_index = 0,
    bool is_yuv_plane = false,
    bool mipmapped = false);
#endif

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_SHARED_IMAGE_SHARED_IMAGE_FORMAT_SERVICE_UTILS_H_
