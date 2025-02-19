// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/accessibility/platform/ax_platform_node.h"

#include "base/debug/crash_logging.h"
#include "base/lazy_instance.h"
#include "build/build_config.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/accessibility/platform/ax_platform.h"
#include "ui/accessibility/platform/ax_platform_node_delegate.h"
#include "ui/base/buildflags.h"

namespace ui {

// static
base::LazyInstance<AXPlatformNode::NativeWindowHandlerCallback>::Leaky
    AXPlatformNode::native_window_handler_ = LAZY_INSTANCE_INITIALIZER;

// static
bool AXPlatformNode::disallow_ax_mode_changes_;

// static
gfx::NativeViewAccessible AXPlatformNode::popup_focus_override_ = nullptr;

// static
AXPlatformNode* AXPlatformNode::FromNativeWindow(
    gfx::NativeWindow native_window) {
  if (native_window_handler_.Get())
    return native_window_handler_.Get().Run(native_window);
  return nullptr;
}

#if !BUILDFLAG_INTERNAL_HAS_NATIVE_ACCESSIBILITY()
// static
AXPlatformNode* AXPlatformNode::FromNativeViewAccessible(
    gfx::NativeViewAccessible accessible) {
  return nullptr;
}
#endif  // !BUILDFLAG_INTERNAL_HAS_NATIVE_ACCESSIBILITY()

// static
void AXPlatformNode::RegisterNativeWindowHandler(
    AXPlatformNode::NativeWindowHandlerCallback handler) {
  native_window_handler_.Get() = handler;
}

// static
void AXPlatformNode::DisallowAXModeChanges() {
  disallow_ax_mode_changes_ = true;
}

AXPlatformNode::AXPlatformNode() = default;

AXPlatformNode::~AXPlatformNode() = default;

void AXPlatformNode::Destroy() {
}

int32_t AXPlatformNode::GetUniqueId() const {
  DCHECK(GetDelegate()) << "|GetUniqueId| must be called after |Init|.";
  return GetDelegate() ? GetDelegate()->GetUniqueId().Get()
                       : kInvalidAXUniqueId;
}

std::string AXPlatformNode::ToString() {
  return GetDelegate() ? GetDelegate()->ToString() : "No delegate";
}

std::string AXPlatformNode::SubtreeToString() {
  return GetDelegate() ? GetDelegate()->SubtreeToString() : "No delegate";
}

std::ostream& operator<<(std::ostream& stream, AXPlatformNode& node) {
  return stream << node.ToString();
}

// static
AXMode AXPlatformNode::GetAccessibilityMode() {
  return AXPlatform::GetInstance().GetMode();
}

// static
void AXPlatformNode::NotifyAddAXModeFlags(AXMode mode_flags) {
  if (disallow_ax_mode_changes_) {
    return;
  }

  auto& ax_platform = AXPlatform::GetInstance();
  const AXMode old_ax_mode = ax_platform.GetMode();
  const AXMode new_ax_mode = old_ax_mode | mode_flags;
  if (new_ax_mode == old_ax_mode) {
    return;  // No change.
  }

  ax_platform.SetMode(new_ax_mode);
}

// static
void AXPlatformNode::SetAXMode(AXMode new_mode) {
  AXPlatform::GetInstance().SetMode(new_mode);
}

// static
void AXPlatformNode::SetPopupFocusOverride(
    gfx::NativeViewAccessible popup_focus_override) {
  popup_focus_override_ = popup_focus_override;
}

// static
gfx::NativeViewAccessible AXPlatformNode::GetPopupFocusOverride() {
  return popup_focus_override_;
}

}  // namespace ui
