// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_USER_EDUCATION_COMMON_FEATURE_PROMO_SPECIFICATION_H_
#define COMPONENTS_USER_EDUCATION_COMMON_FEATURE_PROMO_SPECIFICATION_H_

#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/containers/flat_tree.h"
#include "base/feature_list.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "components/user_education/common/help_bubble_params.h"
#include "components/user_education/common/tutorial_identifier.h"
#include "third_party/abseil-cpp/absl/types/optional.h"
#include "third_party/abseil-cpp/absl/types/variant.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/interaction/element_identifier.h"
#include "ui/base/interaction/element_tracker.h"

namespace gfx {
struct VectorIcon;
}

namespace user_education {

class FeaturePromoHandle;

// Specifies the parameters for a feature promo and its associated bubble.
class FeaturePromoSpecification {
 public:
  // Represents additional conditions that can affect when a promo can show.
  class AdditionalConditions {
   public:
    AdditionalConditions();
    AdditionalConditions(AdditionalConditions&&) noexcept;
    AdditionalConditions& operator=(AdditionalConditions&&) noexcept;
    ~AdditionalConditions();

    // Provides constraints on when the promo can show based on some other
    // Feautre Engagement event.
    enum class Constraint { kAtMost, kAtLeast, kExactly };

    // Represents an additional condition for the promo to show.
    struct AdditionalCondition {
      // The associated event name.
      std::string event_name;
      // How `count` should be interpreted.
      Constraint constraint = Constraint::kAtMost;
      // The required count for `event_name`, interpreted by `constraint`.
      uint32_t count = 0;
      // The window in which to evaluate `count` using `constraint`.
      absl::optional<uint32_t> in_days;
    };

    // Sets the number of days in which "used" and other events should be
    // collected before deciding whether to show a promo.
    //
    // Default is zero unless there are additional conditions, in which case it
    // is a week.
    void set_initial_delay_days(uint32_t initial_delay_days) {
      this->initial_delay_days_ = initial_delay_days;
    }
    absl::optional<uint32_t> initial_delay_days() const {
      return initial_delay_days_;
    }

    // Sets the number of times a promoted feature can be used before the
    // associated promo stops showing. Default is zero - i.e. if the feature is
    // used at all, the promo won't show.
    void set_used_limit(uint32_t used_limit) { this->used_limit_ = used_limit; }
    absl::optional<uint32_t> used_limit() const { return used_limit_; }

    // Adds an additional constraint on when the promo can show. `event_name` is
    // arbitrary and can be shared between promos.
    //
    // Will only allow the promo to show if `event_name` has been seen
    // `constraint` `count` times in `in_days` days. If `in_days` isn't
    // specified, the period is effectively unlimited.
    void AddAdditionalCondition(
        const char* event_name,
        Constraint constraint,
        uint32_t count,
        absl::optional<uint32_t> in_days = absl::nullopt);
    void AddAdditionalCondition(
        const AdditionalCondition& additional_condition);
    const std::vector<AdditionalCondition>& additional_conditions() const {
      return additional_conditions_;
    }

   private:
    absl::optional<uint32_t> initial_delay_days_;
    absl::optional<uint32_t> used_limit_;
    std::vector<AdditionalCondition> additional_conditions_;
  };

  // Provides metadata about an IPH. Metadata will be shown and used on the
  // tester page, and also provides a information as to when an IPH was added to
  // Chrome and by whom.
  struct Metadata {
    // The platform the IPH can be shown on.
    //
    // These are a subset of variations::Study::Platform.
    //
    // TODO(dfried): figure out how to unify a single list of platforms for all
    // use cases; enums like this are scattered all over the codebase.
    enum class Platforms {
      kWindows = 0,
      kMac = 1,
      kLinux = 2,
      kChromeOSAsh = 3,
      kChromeOSLacros = 9,
    };

    // All desktop platforms.
    static constexpr std::initializer_list<Platforms> kAllDesktopPlatforms{
        Platforms::kWindows, Platforms::kMac, Platforms::kLinux,
        Platforms::kChromeOSAsh, Platforms::kChromeOSLacros};

    // Represents a callback that will be triggered when a user launches an IPH
    // from the tester page, before the IPH is shown. This can get the browser
    // into the proper state to demo the IPH.
    //
    // The `launch_context` is provided as a hint in case there are multiple
    // windows. The function should return true on success; false if the
    // required state cannot be set up.
    using TesterPageLaunchCallback =
        base::RepeatingCallback<bool(ui::ElementContext launch_context)>;

    Metadata(int launch_milestone,
             std::string owners,
             std::string triggering_condition_description,
             base::flat_set<const base::Feature*> required_features = {},
             base::flat_set<Platforms> platforms = kAllDesktopPlatforms);
    Metadata();
    Metadata(Metadata&&) noexcept;
    Metadata& operator=(Metadata&&) noexcept;
    ~Metadata();

    // The integer part of the launch milestone. For example, 118.
    int launch_milestone = 0;

    // The email, ldap, group name, team name, etc. of the owner(s) of the IPH.
    // This is a display-only field on an internal page, so the format is up to
    // the implementing team, but it is also metadata to track each IPH's
    // lifecycle so be sure to specify it.
    std::string owners;

    // Description of when the IPH would be triggered. This is a display-only
    // field on an internal page, so the format is up to the implementing team,
    // but a good description will help other people understand how the IPH is
    // implemented and when to expect it to appear.
    std::string triggering_condition_description;

    // The set of non-IPH features that must be enabled in order for the IPH to
    // be displayed.
    using FeatureSet = base::flat_set<const base::Feature*>;
    FeatureSet required_features;

    // The set of platforms the IPH can be displayed on.
    base::flat_set<Platforms> platforms = kAllDesktopPlatforms;
  };

  // Provide different ways to specify parameters for title or body text.
  struct NoSubstitution {};
  using StringSubstitutions = std::vector<std::u16string>;
  using FormatParameters = absl::variant<
      // No substitutions; use the string as-is (default).
      NoSubstitution,
      // Use the following substitutions for the various substitution fields.
      StringSubstitutions,
      // Use a single string substitution. Included for convenience.
      std::u16string,
      // Specify a number of items in a singular/plural string.
      int>;

  // Optional method that filters a set of potential `elements` to choose and
  // return the anchor element, or null if none of the inputs is appropriate.
  // This method can return an element different from the input list, or null
  // if no valid element is found (this will cause the IPH not to run).
  using AnchorElementFilter = base::RepeatingCallback<ui::TrackedElement*(
      const ui::ElementTracker::ElementList& elements)>;

  // The callback type when creating a custom action IPH. The parameters are
  // `context`, which provides the context of the window in which the promo was
  // shown, and `promo_handle`, which holds the promo open until it is
  // destroyed.
  //
  // Typically, if you are taking an additional sequence of actions in response
  // to the custom callback, you will want to move `promo_handle` into longer-
  // term storage until that sequence is complete, to prevent additional IPH or
  // similar promos from being able to trigger in the interim. If you do not
  // care, simply let `promo_handle` expire at the end of the callback.
  using CustomActionCallback =
      base::RepeatingCallback<void(ui::ElementContext context,
                                   FeaturePromoHandle promo_handle)>;

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  //
  // Describes the type of promo. Used to configure defaults for the promo's
  // bubble.
  enum class PromoType {
    // Uninitialized/invalid specification.
    kUnspecified = 0,
    // A toast-style promo.
    kToast = 1,
    // A snooze-style promo.
    kSnooze = 2,
    // A tutorial promo.
    kTutorial = 3,
    // A promo where one button is replaced by a custom action.
    kCustomAction = 4,
    // A simple promo that acts like a toast but without the required
    // accessibility data.
    kLegacy = 5,
    kMaxValue = kLegacy
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused.
  //
  // Specifies the subtype of promo. Almost all promos will be `kNormal`; using
  // some of the other special types requires being on an allowlist.
  enum class PromoSubtype {
    // A normal promo. Follows the default rules for when it can show.
    kNormal = 0,
    // A promo designed to be shown in multiple apps (or webapps). Can show once
    // per app.
    kPerApp = 1,
    // A promo that must be able to be shown until explicitly acknowledged and
    // dismissed by the user. This type requires being on an allowlist.
    kLegalNotice = 2,
    // A promo that must be able to be shown at most times, alerting the user
    // that something important has happened, and offering them an opportunity
    // to address it. This type requires being on an allowlist.
    kActionableAlert = 3,
    kMaxValue = kActionableAlert
  };

  // Represents a command or command accelerator. Can be valueless (falsy) if
  // neither a command ID nor an explicit accelerator is specified.
  class AcceleratorInfo {
   public:
    // You can assign either an int (command ID) or a ui::Accelerator to an
    // AcceleratorInfo object.
    using ValueType = absl::variant<int, ui::Accelerator>;

    AcceleratorInfo();
    AcceleratorInfo(const AcceleratorInfo& other);
    explicit AcceleratorInfo(ValueType value);
    AcceleratorInfo& operator=(ValueType value);
    AcceleratorInfo& operator=(const AcceleratorInfo& other);
    ~AcceleratorInfo();

    explicit operator bool() const;
    bool operator!() const { return !static_cast<bool>(*this); }

    ui::Accelerator GetAccelerator(
        const ui::AcceleratorProvider* provider) const;

   private:
    ValueType value_;
  };

  FeaturePromoSpecification();
  FeaturePromoSpecification(FeaturePromoSpecification&& other) noexcept;
  FeaturePromoSpecification& operator=(
      FeaturePromoSpecification&& other) noexcept;
  ~FeaturePromoSpecification();

  // Format a localized string with ID `string_id` based on the given
  // `format_params`.
  static std::u16string FormatString(int string_id,
                                     const FormatParameters& format_params);

  // Specifies a standard toast promo.
  //
  // Because toasts are transient and time out after a short period, it can be
  // difficult for screen reader users to navigate to the UI they point to.
  // Because of this, toasts require a screen reader prompt that is different
  // from the bubble text. This prompt should fully describe the UI the toast is
  // pointing to, and may include a single parameter, which is the accelerator
  // that is used to open/access the UI.
  //
  // For example, for a promo for the bookmark star, you might have:
  // Bubble text: "Click here to bookmark the current tab."
  // Accessible text: "Press |<ph name="ACCEL">$1<ex>Ctrl+D</ex></ph>| "
  //                  "to bookmark the current tab"
  // Accelerator: AcceleratorInfo(IDC_BOOKMARK_THIS_TAB)
  //
  // In this case, the system-specific accelerator for IDC_BOOKMARK_THIS_TAB is
  // retrieved and its text representation is injected into the accessible text
  // for screen reader users. An empty `AcceleratorInfo()` can be used for cases
  // where the accessible text does not require an accelerator.
  static FeaturePromoSpecification CreateForToastPromo(
      const base::Feature& feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id,
      int accessible_text_string_id,
      AcceleratorInfo accessible_accelerator);

  // Specifies a promo with snooze buttons.
  static FeaturePromoSpecification CreateForSnoozePromo(
      const base::Feature& feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id);

  // Specifies a promo with snooze buttons, but with accessible text string id.
  // See comments from `FeaturePromoSpecification::CreateForToastPromo()`.
  static FeaturePromoSpecification CreateForSnoozePromo(
      const base::Feature& feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id,
      int accessible_text_string_id,
      AcceleratorInfo accessible_accelerator);

  // Specifies a promo that launches a tutorial.
  static FeaturePromoSpecification CreateForTutorialPromo(
      const base::Feature& feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id,
      TutorialIdentifier tutorial_id);

  // Specifies a promo that triggers a custom action.
  static FeaturePromoSpecification CreateForCustomAction(
      const base::Feature& feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id,
      int custom_action_string_id,
      CustomActionCallback custom_action_callback);

  // Specifies a text-only promo without additional accessibility information.
  // Deprecated. Only included for backwards compatibility with existing
  // promos. This is the only case in which |feature| can be null, and if it is
  // the result can only be used for a critical promo.
  static FeaturePromoSpecification CreateForLegacyPromo(
      const base::Feature* feature,
      ui::ElementIdentifier anchor_element_id,
      int body_text_string_id);

  // Set the optional bubble title. This text appears above the body text in a
  // slightly larger font.
  FeaturePromoSpecification& SetBubbleTitleText(int title_text_string_id);

  // Set the optional bubble icon. This is displayed next to the title or body
  // text.
  FeaturePromoSpecification& SetBubbleIcon(const gfx::VectorIcon* bubble_icon);

  // Set the bubble arrow. Default is top-left.
  FeaturePromoSpecification& SetBubbleArrow(HelpBubbleArrow bubble_arrow);

  // Overrides the default focus-on-show behavior for the bubble. By default
  // bubbles with action buttons are focused to aid with accessibility. In
  // unusual circumstances this allows the value to be overridden. However, it
  // is almost always better to e.g. improve the promo trigger logic so it
  // doesn't interrupt user workflow than it is to disable bubble auto-focus.
  //
  // You should document calls to this method with a reason and ideally a bug
  // describing why the default a11y behavior needs to be overridden and what
  // can be done to fix it.
  FeaturePromoSpecification& OverrideFocusOnShow(bool focus_on_show);

  // Set the promo subtype. Setting the subtype to LegalNotice requires being on
  // an allowlist.
  FeaturePromoSpecification& SetPromoSubtype(PromoSubtype promo_subtype);

  // Set the anchor element filter.
  FeaturePromoSpecification& SetAnchorElementFilter(
      AnchorElementFilter anchor_element_filter);

  // Set whether we should look for the anchor element in any context.
  // Default is false. Since usually we only want to create the bubble in the
  // currently active window, this is only really useful for cases where there
  // is a floating window, WebContents, or tab-modal dialog that can become
  // detached from the current active window and therefore requires its own
  // unique context.
  FeaturePromoSpecification& SetInAnyContext(bool in_any_context);

  // Get the anchor element based on `anchor_element_id`,
  // `anchor_element_filter`, and `context`.
  ui::TrackedElement* GetAnchorElement(ui::ElementContext context) const;

  const base::Feature* feature() const { return feature_; }
  PromoType promo_type() const { return promo_type_; }
  PromoSubtype promo_subtype() const { return promo_subtype_; }
  ui::ElementIdentifier anchor_element_id() const { return anchor_element_id_; }
  const AnchorElementFilter& anchor_element_filter() const {
    return anchor_element_filter_;
  }
  bool in_any_context() const { return in_any_context_; }
  int bubble_body_string_id() const { return bubble_body_string_id_; }
  int bubble_title_string_id() const { return bubble_title_string_id_; }
  const gfx::VectorIcon* bubble_icon() const { return bubble_icon_; }
  HelpBubbleArrow bubble_arrow() const { return bubble_arrow_; }
  const absl::optional<bool>& focus_on_show_override() const {
    return focus_on_show_override_;
  }
  int screen_reader_string_id() const { return screen_reader_string_id_; }
  const AcceleratorInfo& screen_reader_accelerator() const {
    return screen_reader_accelerator_;
  }
  const TutorialIdentifier& tutorial_id() const { return tutorial_id_; }
  const std::u16string custom_action_caption() const {
    return custom_action_caption_;
  }

  // Sets whether the custom action button is the default button on the help
  // bubble (default is false). It is an error to call this method for a promo
  // not created with CreateForCustomAction().
  FeaturePromoSpecification& SetCustomActionIsDefault(
      bool custom_action_is_default);
  bool custom_action_is_default() const { return custom_action_is_default_; }

  // Used to claim the callback when creating the bubble.
  CustomActionCallback custom_action_callback() const {
    return custom_action_callback_;
  }
  FeaturePromoSpecification& SetCustomActionDismissText(
      int custom_action_dismiss_string_id);
  int custom_action_dismiss_string_id() const {
    return custom_action_dismiss_string_id_;
  }

  // Set menu item element identifiers that should be highlighted while
  // this FeaturePromo is active.
  FeaturePromoSpecification& SetHighlightedMenuItem(
      const ui::ElementIdentifier highlighted_menu_identifier);
  const ui::ElementIdentifier highlighted_menu_identifier() const {
    return highlighted_menu_identifier_;
  }

  // Sets the additional conditions for the promo to show.
  FeaturePromoSpecification& SetAdditionalConditions(
      AdditionalConditions additional_conditions);
  const AdditionalConditions& additional_conditions() const {
    return additional_conditions_;
  }

  // Sets the metadata for this promotion.
  FeaturePromoSpecification& SetMetadata(Metadata metadata);
  const Metadata& metadata() const { return metadata_; }

  // Argument-forwarding convenience version of SetMetadata() for constructing
  // a Metadata object in-place.
  template <typename... Args>
  FeaturePromoSpecification& SetMetadata(Args&&... args) {
    return SetMetadata(Metadata(std::forward<Args>(args)...));
  }

  // Force the subtype to a particular value, bypassing permission checks.
  void set_promo_subtype_for_testing(PromoSubtype promo_subtype) {
    promo_subtype_ = promo_subtype;
  }

 private:
  static constexpr HelpBubbleArrow kDefaultBubbleArrow =
      HelpBubbleArrow::kTopRight;

  FeaturePromoSpecification(const base::Feature* feature,
                            PromoType promo_type,
                            ui::ElementIdentifier anchor_element_id,
                            int bubble_body_string_id);

  raw_ptr<const base::Feature> feature_ = nullptr;

  // The type of promo. A promo with type kUnspecified is not valid.
  PromoType promo_type_ = PromoType::kUnspecified;

  // The subtype of the promo.
  PromoSubtype promo_subtype_ = PromoSubtype::kNormal;

  // The element identifier of the element to attach the promo to.
  ui::ElementIdentifier anchor_element_id_;

  // Whether we are allowed to search for the anchor element in any context.
  bool in_any_context_ = false;

  // The filter to use if there is more than one matching element, or
  // additional processing is needed (default is to always use the first
  // matching element).
  AnchorElementFilter anchor_element_filter_;

  // Text to be displayed in the promo bubble body. Should not be zero for
  // valid bubbles. We keep the string ID around because we can specify format
  // parameters when we actually create the bubble.
  int bubble_body_string_id_ = 0;

  // Optional text that is displayed at the top of the bubble, in a slightly
  // more prominent font.
  int bubble_title_string_id_ = 0;

  // Optional icon that is displayed next to bubble text.
  raw_ptr<const gfx::VectorIcon> bubble_icon_ = nullptr;

  // Optional arrow pointing to the promo'd element. Defaults to top left.
  HelpBubbleArrow bubble_arrow_ = kDefaultBubbleArrow;

  // Overrides the default focus-on-show behavior for a bubble, which is to
  // focus bubbles with action buttons, but not bubbles that only have a close
  // button.
  absl::optional<bool> focus_on_show_override_;

  // Optional screen reader announcement that replaces bubble text when the
  // bubble is first announced.
  int screen_reader_string_id_ = 0;

  // Accelerator that is used to fill in a parametric field in
  // screen_reader_string_id_.
  AcceleratorInfo screen_reader_accelerator_;

  // Tutorial identifier if the user decides to view a tutorial.
  TutorialIdentifier tutorial_id_;

  // Custom action button text.
  std::u16string custom_action_caption_;

  // Custom action button action.
  CustomActionCallback custom_action_callback_;

  // Whether the custom action is the default button.
  bool custom_action_is_default_ = false;

  // Dismiss string ID for the custom action promo.
  int custom_action_dismiss_string_id_;

  // Identifier of the menu item that should be highlighted while
  // FeaturePromo is active.
  ui::ElementIdentifier highlighted_menu_identifier_;

  // Additional conditions describing when the promo can show.
  AdditionalConditions additional_conditions_;

  // Metadata for this promo.
  Metadata metadata_;
};

std::ostream& operator<<(std::ostream& oss,
                         FeaturePromoSpecification::PromoType promo_type);
std::ostream& operator<<(std::ostream& oss,
                         FeaturePromoSpecification::PromoSubtype promo_subtype);

}  // namespace user_education

#endif  // COMPONENTS_USER_EDUCATION_COMMON_FEATURE_PROMO_SPECIFICATION_H_
