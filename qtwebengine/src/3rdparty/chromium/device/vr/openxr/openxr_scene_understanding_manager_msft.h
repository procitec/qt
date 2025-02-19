// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DEVICE_VR_OPENXR_OPENXR_SCENE_UNDERSTANDING_MANAGER_MSFT_H_
#define DEVICE_VR_OPENXR_OPENXR_SCENE_UNDERSTANDING_MANAGER_MSFT_H_

#include "device/vr/openxr/openxr_scene_understanding_manager.h"

#include <memory>
#include <optional>
#include <vector>

#include "base/memory/raw_ref.h"
#include "device/vr/openxr/openxr_scene.h"
#include "device/vr/openxr/openxr_scene_bounds.h"
#include "device/vr/openxr/openxr_scene_observer.h"
#include "device/vr/openxr/openxr_scene_plane.h"
#include "device/vr/public/mojom/vr_service.mojom.h"
#include "third_party/openxr/src/include/openxr/openxr.h"

namespace gfx {
class Point3F;
class Vector3dF;
}  // namespace gfx

namespace device {

class OpenXrExtensionHelper;

// Class for managing planes and hittests using the _MSFT specific extensions.
class OpenXRSceneUnderstandingManagerMSFT
    : public OpenXRSceneUnderstandingManager {
 public:
  OpenXRSceneUnderstandingManagerMSFT(
      const OpenXrExtensionHelper& extension_helper,
      XrSession session,
      XrSpace mojo_space);
  ~OpenXRSceneUnderstandingManagerMSFT() override;

 private:
  // OpenXRSceneUnderstandingManager
  void OnFrameUpdate(XrTime predicted_display_time) override;
  void OnNewHitTestSubscription() override;
  void OnAllHitTestSubscriptionsRemoved() override;
  std::vector<mojom::XRHitResultPtr> RequestHitTest(
      const gfx::Point3F& origin,
      const gfx::Vector3dF& direction) override;

  std::optional<float> GetRayPlaneDistance(const gfx::Point3F& ray_origin,
                                           const gfx::Vector3dF& ray_vector,
                                           const gfx::Point3F& plane_origin,
                                           const gfx::Vector3dF& plane_normal);

  const raw_ref<const OpenXrExtensionHelper> extension_helper_;
  XrSession session_;
  XrSpace mojo_space_;

  std::unique_ptr<OpenXrSceneObserver> scene_observer_;
  std::unique_ptr<OpenXrScene> scene_;
  OpenXrSceneBounds scene_bounds_;
  XrTime next_scene_update_time_;

  // Scene Compute State Machine:
  // - SceneComputeState::Off
  //     There is no hittest subscription and no active scene compute query.
  // - SceneComputeState::Idle
  //     There is active hittest subscription and OnFrameUpdate will try to
  //     start a new scene-compute query every kUpdateInterval (5 seconds).
  //     When a new scene-compute query is successfully submitted, we then
  //     go to SceneComputeState::Waiting state.
  // - SceneComputeState::Waiting
  //     There is an active scene compute query. We need to wait for
  //     IsSceneComputeCompleted() then we can get the scene data
  //     then immediately go back to SceneComputeState::Idle state.
  enum class SceneComputeState { Off, Idle, Waiting };
  SceneComputeState scene_compute_state_{SceneComputeState::Off};

  std::vector<OpenXrScenePlane> planes_;
};

}  // namespace device
#endif  // DEVICE_VR_OPENXR_OPENXR_SCENE_UNDERSTANDING_MANAGER_MSFT_H_
