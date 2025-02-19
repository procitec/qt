// Copyright 2013 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/browser_autofill_manager.h"

#include <stddef.h>
#include <stdint.h>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "base/check_deref.h"
#include "base/check_op.h"
#include "base/command_line.h"
#include "base/containers/adapters.h"
#include "base/containers/contains.h"
#include "base/containers/flat_map.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/hash/hash.h"
#include "base/i18n/rtl.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/ranges/algorithm.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "base/threading/thread_restrictions.h"
#include "base/uuid.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#if !BUILDFLAG(IS_QTWEBENGINE)
#include "components/autofill/core/browser/autocomplete_history_manager.h"
#endif
#include "components/autofill/core/browser/autofill_browser_util.h"
#include "components/autofill/core/browser/autofill_client.h"
#include "components/autofill/core/browser/autofill_compose_delegate.h"
#include "components/autofill/core/browser/autofill_data_util.h"
#include "components/autofill/core/browser/autofill_experiments.h"
#include "components/autofill/core/browser/autofill_external_delegate.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/autofill_granular_filling_utils.h"
#include "components/autofill/core/browser/autofill_optimization_guide.h"
#include "components/autofill/core/browser/autofill_suggestion_generator.h"
#include "components/autofill/core/browser/autofill_trigger_details.h"
#include "components/autofill/core/browser/autofill_type.h"
#include "components/autofill/core/browser/crowdsourcing/autofill_crowdsourcing_encoding.h"
#include "components/autofill/core/browser/data_model/autofill_data_model.h"
#include "components/autofill/core/browser/data_model/autofill_profile.h"
#include "components/autofill/core/browser/data_model/autofill_profile_comparator.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/data_model/phone_number.h"
#include "components/autofill/core/browser/field_filling_address_util.h"
#include "components/autofill/core/browser/field_filling_payments_util.h"
#include "components/autofill/core/browser/field_type_utils.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/filling_product.h"
#include "components/autofill/core/browser/form_autofill_history.h"
#if !BUILDFLAG(IS_QTWEBENGINE)
#include "components/autofill/core/browser/form_data_importer.h"
#include "components/autofill/core/browser/form_structure.h"
#include "components/autofill/core/browser/geo/phone_number_i18n.h"
#include "components/autofill/core/browser/logging/log_manager.h"
#include "components/autofill/core/browser/metrics/autofill_metrics.h"
#include "components/autofill/core/browser/metrics/fallback_autocomplete_unrecognized_metrics.h"
#include "components/autofill/core/browser/metrics/form_events/form_events.h"
#include "components/autofill/core/browser/metrics/log_event.h"
#include "components/autofill/core/browser/metrics/payments/card_metadata_metrics.h"
#include "components/autofill/core/browser/metrics/quality_metrics.h"
#include "components/autofill/core/browser/payments/autofill_offer_manager.h"
#include "components/autofill/core/browser/payments/credit_card_access_manager.h"
#include "components/autofill/core/browser/personal_data_manager.h"
#include "components/autofill/core/browser/profile_token_quality.h"
#include "components/autofill/core/browser/randomized_encoder.h"
#include "components/autofill/core/browser/suggestions_context.h"
#include "components/autofill/core/browser/ui/payments/bubble_show_options.h"
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
#include "components/autofill/core/browser/ui/popup_item_ids.h"
#include "components/autofill/core/browser/ui/popup_types.h"
#include "components/autofill/core/browser/ui/suggestion.h"
#include "components/autofill/core/browser/validation.h"
#include "components/autofill/core/common/aliases.h"
#include "components/autofill/core/common/autocomplete_parsing_util.h"
#include "components/autofill/core/common/autofill_clock.h"
#include "components/autofill/core/common/autofill_constants.h"
#include "components/autofill/core/common/autofill_data_validation.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_internals/log_message.h"
#include "components/autofill/core/common/autofill_internals/logging_scope.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/autofill_regex_constants.h"
#include "components/autofill/core/common/autofill_regexes.h"
#include "components/autofill/core/common/autofill_util.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/form_data_predictions.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom-shared.h"
#include "components/autofill/core/common/password_form_fill_data.h"
#include "components/autofill/core/common/signatures.h"
#include "components/autofill/core/common/unique_ids.h"
#include "components/plus_addresses/plus_address_metrics.h"
#include "components/plus_addresses/plus_address_service.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"
#include "components/security_interstitials/core/pref_names.h"
#include "components/security_state/core/security_state.h"
#include "components/strings/grit/components_strings.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace autofill {

using base::TimeTicks;
using mojom::SubmissionSource;

#if !BUILDFLAG(IS_QTWEBENGINE)
namespace {

constexpr size_t kMaxRecentFormSignaturesToRemember = 3;

// The minimum required number of fields for an user perception survey to be
// triggered. This makes sure that for example forms that only contain a single
// email field do not prompt a survey. Such survey answer would likely taint
// our analysis.
constexpr size_t kMinFormSizeToTriggerUserPerceptionSurvey = 4;

// Time to wait after a dynamic form change before triggering a refill.
// This is used for sites that change multiple things consecutively.
constexpr base::TimeDelta kWaitTimeForDynamicForms = base::Milliseconds(200);

std::string_view GetSkipFieldFillLogMessage(
    FieldFillingSkipReason skip_reason) {
  switch (skip_reason) {
    case FieldFillingSkipReason::kNotInFilledSection:
      return "Skipped: Not part of filled section";
    case FieldFillingSkipReason::kNotFocused:
      return "Skipped: Only fill when focused";
    case FieldFillingSkipReason::kUnrecognizedAutocompleteAttribute:
      return "Skipped: Unrecognized autocomplete attribute";
    case FieldFillingSkipReason::kFormChanged:
      return "Skipped: Form has changed";
    case FieldFillingSkipReason::kInvisibleField:
      return "Skipped: Invisible field";
    case FieldFillingSkipReason::kValuePrefilled:
      return "Skipped: Value is prefilled";
    case FieldFillingSkipReason::kUserFilledFields:
      return "Skipped: User filled the field";
    case FieldFillingSkipReason::kAutofilledFieldsNotRefill:
      return "Skipped: Previously autofilled field and not a refill";
    case FieldFillingSkipReason::kNoFillableGroup:
      return "Skipped: Field type has no fillable group";
    case FieldFillingSkipReason::kRefillNotInInitialFill:
      return "Skipped: Refill field group different from initial filling group";
    case FieldFillingSkipReason::kExpiredCards:
      return "Skipped: Expired expiration date for credit card";
    case FieldFillingSkipReason::kFillingLimitReachedType:
      return "Skipped: Field type filling limit reached";
    case FieldFillingSkipReason::kFieldDoesNotMatchTargetFieldsSet:
      return "Skipped: The field type does not match the targeted fields.";
    case FieldFillingSkipReason::kFieldTypeUnrelated:
      return "Skipped: The field type is not related to the data used for "
             "filling.";
    case FieldFillingSkipReason::kNotSkipped:
      return "Fillable";
    case FieldFillingSkipReason::kUnknown:
      NOTREACHED_NORETURN();
  }
}

// Converts `filling_stats` to a key-value representation, where the key
// is the "stats category" and the value is the number of fields that match
// such category. This is used to show users a survey that will measure the
// perception of Autofill.
std::map<std::string, std::string> AddressFormFillingStatsToSurveyStringData(
    autofill_metrics::FormGroupFillingStats& filling_stats) {
  return {
      {"Accepted fields", base::NumberToString(filling_stats.num_accepted)},
      {"Corrected to same type",
       base::NumberToString(filling_stats.num_corrected_to_same_type)},
      {"Corrected to a different type",
       base::NumberToString(filling_stats.num_corrected_to_different_type)},
      {"Corrected to an unknown type",
       base::NumberToString(filling_stats.num_corrected_to_unknown_type)},
      {"Corrected to empty",
       base::NumberToString(filling_stats.num_corrected_to_empty)},
      {"Manually filled to same type",
       base::NumberToString(filling_stats.num_manually_filled_to_same_type)},
      {"Manually filled to a different type",
       base::NumberToString(filling_stats.num_manually_filled_to_differt_type)},
      {"Manually filled to an unknown type",
       base::NumberToString(filling_stats.num_manually_filled_to_unknown_type)},
      {"Total corrected", base::NumberToString(filling_stats.TotalCorrected())},
      {"Total filled", base::NumberToString(filling_stats.TotalFilled())},
      {"Total unfilled", base::NumberToString(filling_stats.TotalUnfilled())},
      {"Total manually filled",
       base::NumberToString(filling_stats.TotalManuallyFilled())},
      {"Total number of fields", base::NumberToString(filling_stats.Total())}};
}

// Returns whether the |field| is predicted as being any kind of name.
bool IsNameType(const AutofillField& field) {
  return field.Type().group() == FieldTypeGroup::kName ||
         field.Type().GetStorableType() == CREDIT_CARD_NAME_FULL ||
         field.Type().GetStorableType() == CREDIT_CARD_NAME_FIRST ||
         field.Type().GetStorableType() == CREDIT_CARD_NAME_LAST;
}

// Selects the right name type from the |old_types| to insert into the
// |types_to_keep| based on |is_credit_card|. This is called when we have
// multiple possible types.
void SelectRightNameType(AutofillField* field, bool is_credit_card) {
  DCHECK(field);
  // There should be at least two possible field types.
  DCHECK_LE(2U, field->possible_types().size());

  FieldTypeSet types_to_keep;
  const auto& old_types = field->possible_types();

  for (FieldType type : old_types) {
    FieldTypeGroup group = GroupTypeOfFieldType(type);
    if ((is_credit_card && group == FieldTypeGroup::kCreditCard) ||
        (!is_credit_card && group == FieldTypeGroup::kName)) {
      types_to_keep.insert(type);
    }
  }

  FieldTypeValidityStatesMap new_types_validities;
  // Since the disambiguation takes place when we up to four possible types,
  // here we can add up to three remaining types when only one is removed.
  for (auto type_to_keep : types_to_keep) {
    new_types_validities[type_to_keep] =
        field->get_validities_for_possible_type(type_to_keep);
  }
  field->set_possible_types(types_to_keep);
  field->set_possible_types_validities(new_types_validities);
}

void LogDeveloperEngagementUkm(ukm::UkmRecorder* ukm_recorder,
                               ukm::SourceId source_id,
                               const FormStructure& form_structure) {
  if (form_structure.developer_engagement_metrics()) {
    AutofillMetrics::LogDeveloperEngagementUkm(
        ukm_recorder, source_id, form_structure.main_frame_origin().GetURL(),
        form_structure.IsCompleteCreditCardForm(),
        form_structure.GetFormTypes(),
        form_structure.developer_engagement_metrics(),
        form_structure.form_signature());
  }
}

ValuePatternsMetric GetValuePattern(const std::u16string& value) {
  if (IsUPIVirtualPaymentAddress(value))
    return ValuePatternsMetric::kUpiVpa;
  if (IsInternationalBankAccountNumber(value))
    return ValuePatternsMetric::kIban;
  return ValuePatternsMetric::kNoPatternFound;
}

void LogValuePatternsMetric(const FormData& form) {
  for (const FormFieldData& field : form.fields) {
    if (!field.IsFocusable())
      continue;
    std::u16string value;
    base::TrimWhitespace(field.value, base::TRIM_ALL, &value);
    if (value.empty())
      continue;
    base::UmaHistogramEnumeration("Autofill.SubmittedValuePatterns",
                                  GetValuePattern(value));
  }
}

bool IsSingleFieldFormFillerFillingProduct(FillingProduct filling_product) {
  switch (filling_product) {
    case FillingProduct::kAutocomplete:
    case FillingProduct::kIban:
    case FillingProduct::kMerchantPromoCode:
      return true;
    case FillingProduct::kPlusAddresses:
    case FillingProduct::kCompose:
    case FillingProduct::kPassword:
    case FillingProduct::kCreditCard:
    case FillingProduct::kAddress:
    case FillingProduct::kNone:
      return false;
  }
}

FillDataType GetEventTypeFromSingleFieldSuggestionPopupItemId(
    PopupItemId popup_item_id) {
  switch (popup_item_id) {
    case PopupItemId::kAutocompleteEntry:
      return FillDataType::kSingleFieldFormFillerAutocomplete;
    case PopupItemId::kMerchantPromoCodeEntry:
      return FillDataType::kSingleFieldFormFillerPromoCode;
    case PopupItemId::kIbanEntry:
      return FillDataType::kSingleFieldFormFillerIban;
    case PopupItemId::kAccountStoragePasswordEntry:
    case PopupItemId::kAccountStorageUsernameEntry:
    case PopupItemId::kAddressEntry:
    case PopupItemId::kAllSavedPasswordsEntry:
    case PopupItemId::kAutofillOptions:
    case PopupItemId::kClearForm:
    case PopupItemId::kCompose:
    case PopupItemId::kCreateNewPlusAddress:
    case PopupItemId::kCreditCardEntry:
    case PopupItemId::kDatalistEntry:
    case PopupItemId::kDeleteAddressProfile:
    case PopupItemId::kEditAddressProfile:
    case PopupItemId::kAddressFieldByFieldFilling:
    case PopupItemId::kCreditCardFieldByFieldFilling:
    case PopupItemId::kFillEverythingFromAddressProfile:
    case PopupItemId::kFillExistingPlusAddress:
    case PopupItemId::kFillFullAddress:
    case PopupItemId::kFillFullName:
    case PopupItemId::kFillFullPhoneNumber:
    case PopupItemId::kFillFullEmail:
    case PopupItemId::kGeneratePasswordEntry:
    case PopupItemId::kInsecureContextPaymentDisabledMessage:
    case PopupItemId::kMixedFormMessage:
    case PopupItemId::kPasswordAccountStorageEmpty:
    case PopupItemId::kPasswordAccountStorageOptIn:
    case PopupItemId::kPasswordAccountStorageOptInAndGenerate:
    case PopupItemId::kPasswordAccountStorageReSignin:
    case PopupItemId::kPasswordEntry:
    case PopupItemId::kScanCreditCard:
    case PopupItemId::kSeePromoCodeDetails:
    case PopupItemId::kSeparator:
    case PopupItemId::kShowAccountCards:
    case PopupItemId::kUsernameEntry:
    case PopupItemId::kVirtualCreditCardEntry:
    case PopupItemId::kWebauthnCredential:
    case PopupItemId::kWebauthnSignInWithAnotherDevice:
    case PopupItemId::kDevtoolsTestAddresses:
    case PopupItemId::kDevtoolsTestAddressEntry:
      NOTREACHED();
  }
  NOTREACHED();
  return FillDataType::kUndefined;
}

std::string FetchCountryCodeFromProfile(const AutofillProfile* profile) {
  return base::UTF16ToUTF8(profile->GetRawInfo(autofill::ADDRESS_HOME_COUNTRY));
}

void LogLanguageMetrics(const translate::LanguageState* language_state) {
  if (language_state) {
    AutofillMetrics::LogFieldParsingTranslatedFormLanguageMetric(
        language_state->current_language());
    AutofillMetrics::LogFieldParsingPageTranslationStatusMetric(
        language_state->IsPageTranslated());
  }
}

AutofillMetrics::AutocompleteState AutocompleteStateForSubmittedField(
    const AutofillField& field) {
  // An unparsable autocomplete attribute is treated like kNone.
  auto autocomplete_state = AutofillMetrics::AutocompleteState::kNone;
  // autocomplete=on is ignored as well. But for the purposes of metrics we care
  // about cases where the developer tries to disable autocomplete.
  if (field.autocomplete_attribute != "on" &&
      ShouldIgnoreAutocompleteAttribute(field.autocomplete_attribute)) {
    autocomplete_state = AutofillMetrics::AutocompleteState::kOff;
  } else if (field.parsed_autocomplete) {
    autocomplete_state =
        field.parsed_autocomplete->field_type != HtmlFieldType::kUnrecognized
            ? AutofillMetrics::AutocompleteState::kValid
            : AutofillMetrics::AutocompleteState::kGarbage;

    if (field.autocomplete_attribute == "new-password" ||
        field.autocomplete_attribute == "current-password") {
      autocomplete_state = AutofillMetrics::AutocompleteState::kPassword;
    }
  }

  return autocomplete_state;
}

void LogAutocompletePredictionCollisionTypeMetrics(
    const FormStructure& form_structure) {
  for (size_t i = 0; i < form_structure.field_count(); i++) {
    const AutofillField* field = form_structure.field(i);
    auto heuristic_type = field->heuristic_type();
    auto server_type = field->server_type();

    auto prediction_state = AutofillMetrics::PredictionState::kNone;
    if (IsFillableFieldType(heuristic_type)) {
      prediction_state = IsFillableFieldType(server_type)
                             ? AutofillMetrics::PredictionState::kBoth
                             : AutofillMetrics::PredictionState::kHeuristics;
    } else if (IsFillableFieldType(server_type)) {
      prediction_state = AutofillMetrics::PredictionState::kServer;
    }

    auto autocomplete_state = AutocompleteStateForSubmittedField(*field);
    AutofillMetrics::LogAutocompletePredictionCollisionState(
        prediction_state, autocomplete_state);
    AutofillMetrics::LogAutocompletePredictionCollisionTypes(
        autocomplete_state, server_type, heuristic_type);
  }
}

void LogContextMenuImpressionsForSubmittedField(const AutofillField& field) {
  auto autocomplete_state = AutocompleteStateForSubmittedField(field);
  AutofillMetrics::LogContextMenuImpressionsForField(
      field.Type().GetStorableType(), autocomplete_state);
}

// Finds the first field in |form_structure| with |field.value|=|value|.
AutofillField* FindFirstFieldWithValue(const FormStructure& form_structure,
                                       const std::u16string& value) {
  for (const auto& field : form_structure) {
    std::u16string trimmed_value;
    base::TrimWhitespace(field->value, base::TRIM_ALL, &trimmed_value);
    if (trimmed_value == value)
      return field.get();
  }
  return nullptr;
}

// Heuristically identifies all possible credit card verification fields.
AutofillField* HeuristicallyFindCVCFieldForUpload(
    const FormStructure& form_structure) {
  // Stores a pointer to the explicitly found expiration year.
  bool found_explicit_expiration_year_field = false;

  // The first pass checks the existence of an explicitly marked field for the
  // credit card expiration year.
  for (const auto& field : form_structure) {
    const FieldTypeSet& type_set = field->possible_types();
    if (type_set.find(CREDIT_CARD_EXP_2_DIGIT_YEAR) != type_set.end() ||
        type_set.find(CREDIT_CARD_EXP_4_DIGIT_YEAR) != type_set.end()) {
      found_explicit_expiration_year_field = true;
      break;
    }
  }

  // Keeps track if a credit card number field was found.
  bool credit_card_number_found = false;

  // In the second pass, the CVC field is heuristically searched for.
  // A field is considered a CVC field, iff:
  // * it appears after the credit card number field;
  // * it has the |UNKNOWN_TYPE| prediction;
  // * it does not look like an expiration year or an expiration year was
  //   already found;
  // * it is filled with a 3-4 digit number;
  for (const auto& field : form_structure) {
    const FieldTypeSet& type_set = field->possible_types();

    // Checks if the field is of |CREDIT_CARD_NUMBER| type.
    if (type_set.find(CREDIT_CARD_NUMBER) != type_set.end()) {
      credit_card_number_found = true;
      continue;
    }
    // Skip the field if no credit card number was found yet.
    if (!credit_card_number_found) {
      continue;
    }

    // Don't consider fields that already have any prediction.
    if (type_set.find(UNKNOWN_TYPE) == type_set.end())
      continue;
    // |UNKNOWN_TYPE| should come alone.
    DCHECK_EQ(1u, type_set.size());

    std::u16string trimmed_value;
    base::TrimWhitespace(field->value, base::TRIM_ALL, &trimmed_value);

    // Skip the field if it can be confused with a expiration year.
    if (!found_explicit_expiration_year_field &&
        IsPlausible4DigitExpirationYear(trimmed_value)) {
      continue;
    }

    // Skip the field if its value does not like a CVC value.
    if (!IsPlausibleCreditCardCVCNumber(trimmed_value))
      continue;

    return field.get();
  }
  return nullptr;
}

// Iff the CVC of the credit card is known, find the first field with this
// value (also set |properties_mask| to |kKnownValue|). Otherwise, heuristically
// search for the CVC field if any.
AutofillField* GetBestPossibleCVCFieldForUpload(
    const FormStructure& form_structure,
    std::u16string last_unlocked_credit_card_cvc) {
  if (!last_unlocked_credit_card_cvc.empty()) {
    AutofillField* result =
        FindFirstFieldWithValue(form_structure, last_unlocked_credit_card_cvc);
    if (result)
      result->properties_mask = FieldPropertiesFlags::kKnownValue;
    return result;
  }

  return HeuristicallyFindCVCFieldForUpload(form_structure);
}

const char* SubmissionSourceToString(SubmissionSource source) {
  switch (source) {
    case SubmissionSource::NONE:
      return "NONE";
    case SubmissionSource::SAME_DOCUMENT_NAVIGATION:
      return "SAME_DOCUMENT_NAVIGATION";
    case SubmissionSource::XHR_SUCCEEDED:
      return "XHR_SUCCEEDED";
    case SubmissionSource::FRAME_DETACHED:
      return "FRAME_DETACHED";
    case SubmissionSource::PROBABLY_FORM_SUBMITTED:
      return "PROBABLY_FORM_SUBMITTED";
    case SubmissionSource::FORM_SUBMISSION:
      return "FORM_SUBMISSION";
    case SubmissionSource::DOM_MUTATION_AFTER_AUTOFILL:
      return "DOM_MUTATION_AFTER_AUTOFILL";
  }
  return "Unknown";
}

// Returns how many fields with type |field_type| may be filled in a form at
// maximum.
size_t TypeValueFormFillingLimit(FieldType field_type) {
  switch (field_type) {
    case CREDIT_CARD_NUMBER:
      return kCreditCardTypeValueFormFillingLimit;
    case ADDRESS_HOME_STATE:
      return kStateTypeValueFormFillingLimit;
    default:
      return kTypeValueFormFillingLimit;
  }
}

std::string_view ActionPersistenceToString(
    mojom::ActionPersistence action_persistence) {
  switch (action_persistence) {
    case mojom::ActionPersistence::kFill:
      return "fill";
    case mojom::ActionPersistence::kPreview:
      return "preview";
  }
}

// Returns true if autocomplete=unrecognized (address) fields should receive
// suggestions. On desktop, suggestion can only be triggered for them through
// manual fallbacks. On mobile, suggestions are always shown.
bool ShouldShowSuggestionsForAutocompleteUnrecognizedFields(
    AutofillSuggestionTriggerSource trigger_source) {
#if BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
  return true;
#else
  return IsAutofillManuallyTriggered(trigger_source);
#endif  // BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_IOS)
}

// Returns `true` if `autofill_field`'s pre-filled value is classified as
// meaningful (guarded by `features::kAutofillOverwritePlaceholdersOnly`) and
// Autofill's behavior for filling pre-filled fields is overwriting them by
// default.
bool IsMeaningfullyPreFilled(const autofill::AutofillField* autofill_field) {
  return autofill_field->may_use_prefilled_placeholder().has_value() &&
         !autofill_field->may_use_prefilled_placeholder().value() &&
         !base::FeatureList::IsEnabled(
             features::kAutofillSkipPreFilledFields) &&
         base::FeatureList::IsEnabled(
             features::kAutofillOverwritePlaceholdersOnly);
}

// Returns `true` if `autofill_field`'s pre-filled value is classified as a
// placeholder (guarded by `features::kAutofillOverwritePlaceholdersOnly`) and
// Autofill's behavior for filling pre-filled fields is skipping them by
// default.
bool IsNotAPlaceholder(const autofill::AutofillField* autofill_field) {
  return (!autofill_field->may_use_prefilled_placeholder().has_value() ||
          !autofill_field->may_use_prefilled_placeholder().value() ||
          !base::FeatureList::IsEnabled(
              features::kAutofillOverwritePlaceholdersOnly)) &&
         base::FeatureList::IsEnabled(features::kAutofillSkipPreFilledFields);
}

}  // namespace

BrowserAutofillManager::FillingContext::FillingContext(
    const AutofillField& field,
    absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card,
    const std::u16string* optional_cvc)
    : filled_field_id(field.global_id()),
      filled_field_signature(field.GetFieldSignature()),
      filled_origin(field.origin),
      original_fill_time(base::TimeTicks::Now()) {
  DCHECK(absl::holds_alternative<const CreditCard*>(profile_or_credit_card) ||
         !optional_cvc);

  if (absl::holds_alternative<const AutofillProfile*>(profile_or_credit_card)) {
    profile_or_credit_card_with_cvc =
        *absl::get<const AutofillProfile*>(profile_or_credit_card);
  } else if (absl::holds_alternative<const CreditCard*>(
                 profile_or_credit_card)) {
    profile_or_credit_card_with_cvc =
        std::make_pair(*absl::get<const CreditCard*>(profile_or_credit_card),
                       optional_cvc ? *optional_cvc : std::u16string());
  }
}

BrowserAutofillManager::FillingContext::~FillingContext() = default;
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

#if BUILDFLAG(IS_QTWEBENGINE)
BrowserAutofillManager::BrowserAutofillManager(AutofillDriver* driver,
                                               AutofillClient* client,
                                               const std::string& app_locale)
    : AutofillManager(driver, client),
      external_delegate_(
          std::make_unique<AutofillExternalDelegate>(this)) {
}

#else
BrowserAutofillManager::BrowserAutofillManager(AutofillDriver* driver,
                                               AutofillClient* client,
                                               const std::string& app_locale)
    : AutofillManager(driver, client),
      external_delegate_(std::make_unique<AutofillExternalDelegate>(this)),
      app_locale_(app_locale),
      suggestion_generator_(std::make_unique<AutofillSuggestionGenerator>(
          *client,
          *client->GetPersonalDataManager())) {
  address_form_event_logger_ =
      std::make_unique<autofill_metrics::AddressFormEventLogger>(
          driver->IsInAnyMainFrame(), form_interactions_ukm_logger(), client);
  credit_card_form_event_logger_ =
      std::make_unique<autofill_metrics::CreditCardFormEventLogger>(
          driver->IsInAnyMainFrame(), form_interactions_ukm_logger(),
          client->GetPersonalDataManager(), client);
  autocomplete_unrecognized_fallback_logger_ = std::make_unique<
      autofill_metrics::AutocompleteUnrecognizedFallbackEventLogger>();

  credit_card_access_manager_ = std::make_unique<CreditCardAccessManager>(
      driver, client, client->GetPersonalDataManager(),
      credit_card_form_event_logger_.get());
}

BrowserAutofillManager::~BrowserAutofillManager() {
  if (has_parsed_forms_) {
    base::UmaHistogramBoolean(
        "Autofill.WebOTP.PhoneNumberCollection.ParseResult",
        has_observed_phone_number_field_);
    base::UmaHistogramBoolean("Autofill.WebOTP.OneTimeCode.FromAutocomplete",
                              has_observed_one_time_code_field_);
  }

  // Process log events and record into UKM when the form is destroyed or
  // removed.
  for (const auto& [form_id, form_structure] : form_structures()) {
    ProcessFieldLogEventsInForm(*form_structure);
  }

  single_field_form_fill_router_->CancelPendingQueries();

  address_form_event_logger_->OnDestroyed();
  credit_card_form_event_logger_->OnDestroyed();

  // We don't flush the `queued_vote_uploads_` here because that would trigger
  // network requests in the AutofillCrowdsourcingManager, which are managed
  // with by SimpleURLLoaders owned by the AutofillCrowdsourcingManager.
  // Destroying the BrowserAutofillManager destroys the
  // AutofillCrowdsourcingManager and its SimpleURLLoaders, which would
  // immediately cancel the uploads. As a consequence of this, votes are lost if
  // the user generates blur votes and closes the tab before the votes are sent
  // (due to a navigation).
}
#endif  // BUILDFLAG(IS_QTWEBENGINE)

base::WeakPtr<AutofillManager> BrowserAutofillManager::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

#if !BUILDFLAG(IS_QTWEBENGINE)
CreditCardAccessManager& BrowserAutofillManager::GetCreditCardAccessManager() {
  return *credit_card_access_manager_;
}

const CreditCardAccessManager&
BrowserAutofillManager::GetCreditCardAccessManager() const {
  return const_cast<BrowserAutofillManager*>(this)
      ->GetCreditCardAccessManager();
}

bool BrowserAutofillManager::ShouldShowScanCreditCard(
    const FormData& form,
    const FormFieldData& field) const {
  if (!client().HasCreditCardScanFeature() ||
      !IsAutofillPaymentMethodsEnabled()) {
    return false;
  }

  AutofillField* autofill_field = GetAutofillField(form, field);
  if (!autofill_field)
    return false;

  bool is_card_number_field =
      autofill_field->Type().GetStorableType() == CREDIT_CARD_NUMBER &&
      base::ContainsOnlyChars(CreditCard::StripSeparators(field.value),
                              u"0123456789");

  if (!is_card_number_field)
    return false;

  if (IsFormNonSecure(form))
    return false;

  static const int kShowScanCreditCardMaxValueLength = 6;
  return field.value.size() <= kShowScanCreditCardMaxValueLength;
}

bool BrowserAutofillManager::ShouldShowCardsFromAccountOption(
    const FormData& form,
    const FormFieldData& field,
    AutofillSuggestionTriggerSource trigger_source) const {
  // If `trigger_source` is equal to `kShowCardsFromAccount`, that means that
  // the user accepted "Show cards from account" suggestiona and it should not
  // be shown again.
  if (trigger_source ==
      AutofillSuggestionTriggerSource::kShowCardsFromAccount) {
    return false;
  }
  // Check whether we are dealing with a credit card field.
  AutofillField* autofill_field = GetAutofillField(form, field);
  if (!autofill_field ||
      autofill_field->Type().group() != FieldTypeGroup::kCreditCard ||
      // Exclude CVC and card type fields, because these will not have
      // suggestions available after the user opts in.
      autofill_field->Type().GetStorableType() ==
          CREDIT_CARD_VERIFICATION_CODE ||
      autofill_field->Type().GetStorableType() == CREDIT_CARD_TYPE) {
    return false;
  }

  if (IsFormNonSecure(form))
    return false;

  return client().GetPersonalDataManager()->ShouldShowCardsFromAccountOption();
}

void BrowserAutofillManager::OnUserAcceptedCardsFromAccountOption() {
  client().GetPersonalDataManager()->OnUserAcceptedCardsFromAccountOption();
}

void BrowserAutofillManager::RefetchCardsAndUpdatePopup(
    const FormData& form,
    const FormFieldData& field_data,
    const gfx::RectF& element_bounds) {
  external_delegate_->OnQuery(
      form, field_data, element_bounds,
      AutofillSuggestionTriggerSource::kShowCardsFromAccount);
  AutofillField* autofill_field = GetAutofillField(form, field_data);
  FieldType field_type = autofill_field
                             ? autofill_field->Type().GetStorableType()
                             : CREDIT_CARD_NUMBER;
  DCHECK_EQ(FieldTypeGroup::kCreditCard, GroupTypeOfFieldType(field_type));

  bool should_display_gpay_logo = false;
  auto cards = GetCreditCardSuggestions(
      form, field_data, field_type,
      AutofillSuggestionTriggerSource::kShowCardsFromAccount,
      should_display_gpay_logo);
  DCHECK(!cards.empty());
  external_delegate_->OnSuggestionsReturned(
      field_data.global_id(), cards,
      should_display_gpay_logo);
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

bool BrowserAutofillManager::ShouldParseForms() {
#if !BUILDFLAG(IS_QTWEBENGINE)
  bool autofill_enabled = IsAutofillEnabled();
  // If autofill is disabled but the password manager is enabled, we still
  // need to parse the forms and query the server as the password manager
  // depends on server classifications.
  bool password_manager_enabled = client().IsPasswordManagerEnabled();
  signin_state_for_metrics_ =
      client().GetPersonalDataManager()
          ? client()
                .GetPersonalDataManager()
                ->GetPaymentsSigninStateForMetrics()
          : AutofillMetrics::PaymentsSigninState::kUnknown;
  if (!has_logged_autofill_enabled_) {
    AutofillMetrics::LogIsAutofillEnabledAtPageLoad(autofill_enabled,
                                                    signin_state_for_metrics_);
    AutofillMetrics::LogIsAutofillProfileEnabledAtPageLoad(
        IsAutofillProfileEnabled(), signin_state_for_metrics_);
    AutofillMetrics::LogIsAutofillCreditCardEnabledAtPageLoad(
        IsAutofillPaymentMethodsEnabled(), signin_state_for_metrics_);
    has_logged_autofill_enabled_ = true;
  }

  // Enable the parsing also for the password manager, so that we fetch server
  // classifications if the password manager is enabled but autofill is
  // disabled.
  return autofill_enabled || password_manager_enabled;
#else
  // FIXME: Breaks typing in datalist when enabled.
  return false;
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnFormSubmittedImpl(const FormData& form,
                                                 bool known_success,
                                                 SubmissionSource source) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  base::UmaHistogramEnumeration("Autofill.FormSubmission.PerProfileType",
                                client().GetProfileType());
  LOG_AF(log_manager()) << LoggingScope::kSubmission
                        << LogMessage::kFormSubmissionDetected << Br{}
                        << "known_success: " << known_success << Br{}
                        << "source: " << SubmissionSourceToString(source)
                        << Br{} << form;

  // Always upload page language metrics.
  LogLanguageMetrics(client().GetLanguageState());

  // Always let the value patterns metric upload data.
  LogValuePatternsMetric(form);

  // Note that `ValidateSubmittedForm()` returns nullptr in incognito mode.
  // Consequently, in incognito mode Autofill doesn't:
  // - Import
  // - Vote
  // - Collect any key metrics (since they are conditioned form submission - see
  //  `FormEventLoggerBase::OnWillSubmitForm()`)
  // - Collect profile token quality observations
  std::unique_ptr<FormStructure> submitted_form = ValidateSubmittedForm(form);
  CHECK(!client().IsOffTheRecord() || !submitted_form);
  if (!submitted_form) {
    // We always give Autocomplete a chance to save the data.
    // TODO(crbug.com/1467623): Verify frequency of plus address (or the other
    // type(s) checked for below, for that matter) slipping through in this code
    // path.
    single_field_form_fill_router_->OnWillSubmitForm(
        form, submitted_form.get(), client().IsAutocompleteEnabled());
    return;
  }

  form_submitted_timestamp_ = base::TimeTicks::Now();

  // Log metrics about the autocomplete attribute usage in the submitted form.
  LogAutocompletePredictionCollisionTypeMetrics(*submitted_form);

  // Log interaction time metrics for the ablation study.
  if (!initial_interaction_timestamp_.is_null()) {
    base::TimeDelta time_from_interaction_to_submission =
        base::TimeTicks::Now() - initial_interaction_timestamp_;
    DenseSet<FormType> form_types = submitted_form->GetFormTypes();
    bool card_form = base::Contains(form_types, FormType::kCreditCardForm);
    bool address_form = base::Contains(form_types, FormType::kAddressForm);
    if (card_form) {
      credit_card_form_event_logger_->SetTimeFromInteractionToSubmission(
          time_from_interaction_to_submission);
    }
    if (address_form) {
      address_form_event_logger_->SetTimeFromInteractionToSubmission(
          time_from_interaction_to_submission);
    }
  }

  plus_addresses::PlusAddressService* plus_address_service =
      client().GetPlusAddressService();

  FormData form_for_autocomplete = submitted_form->ToFormData();
  int num_fields_where_context_menu_was_shown = 0;
  for (size_t i = 0; i < submitted_form->field_count(); ++i) {
    if (submitted_form->field(i)->Type().GetStorableType() ==
        CREDIT_CARD_VERIFICATION_CODE) {
      // However, if Autofill has recognized a field as CVC, that shouldn't be
      // saved.
      form_for_autocomplete.fields[i].should_autocomplete = false;
    }
    if (plus_address_service &&
        plus_address_service->IsPlusAddress(
            base::UTF16ToUTF8(submitted_form->field(i)->value))) {
      // Similarly to CVC, any plus addresses needn't be saved to autocomplete.
      // Note that the feature is experimental, and `plus_address_service` will
      // be null if the feature is not enabled (it's disabled by default).
      form_for_autocomplete.fields[i].should_autocomplete = false;
    }

    // The context menu was shown in this field, log the metrics by
    // autocomplete type, form type and autofill type prediction of the field.
    if (submitted_form->field(i)->was_context_menu_shown()) {
      num_fields_where_context_menu_was_shown++;
      LogContextMenuImpressionsForSubmittedField(*submitted_form->field(i));
    }
  }

  AutofillMetrics::LogContextMenuImpressionsForForm(
      num_fields_where_context_menu_was_shown);

  single_field_form_fill_router_->OnWillSubmitForm(
      form_for_autocomplete, submitted_form.get(),
      client().IsAutocompleteEnabled());

  if (IsAutofillProfileEnabled()) {
    address_form_event_logger_->OnWillSubmitForm(signin_state_for_metrics_,
                                                 *submitted_form);
  }
  if (IsAutofillPaymentMethodsEnabled()) {
    credit_card_form_event_logger_->OnWillSubmitForm(signin_state_for_metrics_,
                                                     *submitted_form);
  }

  submitted_form->set_submission_source(source);

  // Update Personal Data with the form's submitted data.
  // Also triggers offering local/upload credit card save, if applicable.
  if (submitted_form->IsAutofillable()) {
    FormDataImporter* form_data_importer = client().GetFormDataImporter();
    form_data_importer->ImportAndProcessFormData(
        *submitted_form, IsAutofillProfileEnabled(),
        IsAutofillPaymentMethodsEnabled());
    // Associate the form signatures of recently submitted address/credit card
    // forms to `submitted_form`, if it is an address/credit card form itself.
    // This information is attached to the vote.
    if (base::FeatureList::IsEnabled(features::kAutofillAssociateForms)) {
      if (std::optional<FormStructure::FormAssociations> associations =
              form_data_importer->GetFormAssociations(
                  submitted_form->form_signature())) {
        submitted_form->set_form_associations(*associations);
      }
    }
  }

  MaybeStartVoteUploadProcess(std::move(submitted_form),
                              /*observed_submission=*/true);

  // TODO(crbug.com/803334): Add FormStructure::Clone() method.
  // Create another FormStructure instance.
  submitted_form = ValidateSubmittedForm(form);
  DCHECK(submitted_form);
  if (!submitted_form)
    return;

  submitted_form->set_submission_source(source);

  if (IsAutofillProfileEnabled()) {
    address_form_event_logger_->OnFormSubmitted(signin_state_for_metrics_,
                                                *submitted_form);
  }
  if (IsAutofillPaymentMethodsEnabled()) {
    credit_card_form_event_logger_->OnFormSubmitted(signin_state_for_metrics_,
                                                    *submitted_form);
    if (touch_to_fill_delegate_) {
      touch_to_fill_delegate_->LogMetricsAfterSubmission(*submitted_form);
    }
  }

  ProfileTokenQuality::SaveObservationsForFilledFormForAllSubmittedProfiles(
      *submitted_form, form, *client().GetPersonalDataManager());
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

#if !BUILDFLAG(IS_QTWEBENGINE)
bool BrowserAutofillManager::MaybeStartVoteUploadProcess(
    std::unique_ptr<FormStructure> form_structure,
    bool observed_submission) {
  // It is possible for |client().GetPersonalDataManager()| to be null, such as
  // when used in the Android webview.
  if (!client().GetPersonalDataManager()) {
    return false;
  }

  // Only upload server statistics and UMA metrics if at least some local data
  // is available to use as a baseline.
  std::vector<AutofillProfile*> profiles =
      client().GetPersonalDataManager()->GetProfiles();
  if (observed_submission && form_structure->IsAutofillable()) {
    AutofillMetrics::LogNumberOfProfilesAtAutofillableFormSubmission(
        client().GetPersonalDataManager()->GetProfiles().size());
  }

  const std::vector<CreditCard*>& credit_cards =
      client().GetPersonalDataManager()->GetCreditCards();

  if (profiles.empty() && credit_cards.empty())
    return false;

  if (form_structure->field_count() * (profiles.size() + credit_cards.size()) >=
      kMaxTypeMatchingCalls)
    return false;

  // Copy the profile and credit card data, so that it can be accessed on a
  // separate thread.
  std::vector<AutofillProfile> copied_profiles;
  copied_profiles.reserve(profiles.size());
  for (const AutofillProfile* profile : profiles)
    copied_profiles.push_back(*profile);

  std::vector<CreditCard> copied_credit_cards;
  copied_credit_cards.reserve(credit_cards.size());
  for (const CreditCard* card : credit_cards)
    copied_credit_cards.push_back(*card);

  // Annotate the form with the source language of the page.
  form_structure->set_current_page_language(GetCurrentPageLanguage());

  // Attach the Randomized Encoder.
  form_structure->set_randomized_encoder(
      RandomizedEncoder::Create(client().GetPrefs()));

  // Determine |ADDRESS_HOME_STATE| as a possible types for the fields in the
  // |form_structure| with the help of |AlternativeStateNameMap|.
  // |AlternativeStateNameMap| can only be accessed on the main UI thread.
  PreProcessStateMatchingTypes(copied_profiles, form_structure.get());

  // Ownership of |form_structure| is passed to the
  // BrowserAutofillManager::OnSubmissionFieldTypesDetermined() call.
  FormStructure* raw_form = form_structure.get();

  base::OnceClosure call_after_determine_field_types =
      base::BindOnce(&BrowserAutofillManager::OnSubmissionFieldTypesDetermined,
                     weak_ptr_factory_.GetWeakPtr(), std::move(form_structure),
                     initial_interaction_timestamp_, base::TimeTicks::Now(),
                     observed_submission, client().GetUkmSourceId());

  // If the form was not submitted (e.g. the user just removed the focus from
  // the form), it's possible that later modifications lead to more accurate
  // votes. In this case we just want to cache the upload and have a chance to
  // override it with better data.
  if (!observed_submission) {
    call_after_determine_field_types = base::BindOnce(
        &BrowserAutofillManager::StoreUploadVotesAndLogQualityCallback,
        weak_ptr_factory_.GetWeakPtr(), raw_form->form_signature(),
        std::move(call_after_determine_field_types));
  }

  if (!vote_upload_task_runner_) {
    // If the priority is BEST_EFFORT, the task can be preempted, which is
    // thought to cause high memory usage (as memory is retained by the task
    // while it is preempted), https://crbug.com/974249
    vote_upload_task_runner_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE});
  }

  vote_upload_task_runner_->PostTaskAndReply(
      FROM_HERE,
      base::BindOnce(
          &BrowserAutofillManager::DeterminePossibleFieldTypesForUpload,
          std::move(copied_profiles), std::move(copied_credit_cards),
          last_unlocked_credit_card_cvc_, app_locale_, observed_submission,
          raw_form),
      std::move(call_after_determine_field_types));

  return true;
}

void BrowserAutofillManager::UpdatePendingForm(const FormData& form) {
  // Process the current pending form if different than supplied |form|.
  if (pending_form_data_ && !pending_form_data_->SameFormAs(form)) {
    ProcessPendingFormForUpload();
  }
  // A new pending form is assigned.
  pending_form_data_ = std::make_unique<FormData>(form);
}

void BrowserAutofillManager::ProcessPendingFormForUpload() {
  if (!pending_form_data_)
    return;

  // We get the FormStructure corresponding to |pending_form_data_|, used in the
  // upload process. |pending_form_data_| is reset.
  std::unique_ptr<FormStructure> upload_form =
      ValidateSubmittedForm(*pending_form_data_);
  pending_form_data_.reset();
  if (!upload_form)
    return;

  MaybeStartVoteUploadProcess(std::move(upload_form),
                              /*observed_submission=*/false);
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

void BrowserAutofillManager::OnTextFieldDidChangeImpl(
    const FormData& form,
    const FormFieldData& field,
    const gfx::RectF& bounding_box,
    const TimeTicks timestamp) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  // Log events when user edits the field.
  // If the user types into the same field multiple times, repeated
  // TypingFieldLogEvents are coalesced.
  autofill_field->AppendLogEventIfNotRepeated(TypingFieldLogEvent{
      .has_value_after_typing = ToOptionalBoolean(!field.value.empty())});

  UpdatePendingForm(form);

  uint32_t profile_form_bitmask = 0;
  if (!user_did_type_ || autofill_field->is_autofilled) {
    form_interactions_ukm_logger()->LogTextFieldDidChange(*form_structure,
                                                          *autofill_field);
    profile_form_bitmask = data_util::DetermineGroups(*form_structure);
  }

  auto* logger = GetEventFormLogger(*autofill_field);
  if (!autofill_field->is_autofilled) {
    if (logger)
      logger->OnTypedIntoNonFilledField();
  }

  if (!user_did_type_) {
    user_did_type_ = true;
    AutofillMetrics::LogUserHappinessMetric(
        AutofillMetrics::USER_DID_TYPE, autofill_field->Type().group(),
        client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);
  }

  if (autofill_field->is_autofilled) {
    autofill_field->is_autofilled = false;
    autofill_field->set_previously_autofilled(true);
    AutofillMetrics::LogUserHappinessMetric(
        AutofillMetrics::USER_DID_EDIT_AUTOFILLED_FIELD,
        autofill_field->Type().group(),
        client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);

    if (logger)
      logger->OnEditedAutofilledField();

    if (!user_did_edit_autofilled_field_) {
      user_did_edit_autofilled_field_ = true;
      AutofillMetrics::LogUserHappinessMetric(
          AutofillMetrics::USER_DID_EDIT_AUTOFILLED_FIELD_ONCE,
          autofill_field->Type().group(),
          client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);
    }
  }

  UpdateInitialInteractionTimestamp(timestamp);

  if (logger)
    logger->OnTextFieldDidChange(autofill_field->global_id());
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

#if !BUILDFLAG(IS_QTWEBENGINE)
bool BrowserAutofillManager::IsFormNonSecure(const FormData& form) const {
  // Check if testing override applies.
  if (consider_form_as_secure_for_testing_.has_value() &&
      consider_form_as_secure_for_testing_.value()) {
    return false;
  }

  return IsFormOrClientNonSecure(client(), form);
}
#endif

void BrowserAutofillManager::OnAskForValuesToFillImpl(
    const FormData& form,
    const FormFieldData& field,
    const gfx::RectF& transformed_box,
    AutofillSuggestionTriggerSource trigger_source) {
  if (base::FeatureList::IsEnabled(features::kAutofillDisableFilling)) {
    return;
  }

  external_delegate_->SetCurrentDataListValues(field.datalist_options);
  external_delegate_->OnQuery(form, field, transformed_box, trigger_source);

  std::vector<Suggestion> suggestions;
#if !BUILDFLAG(IS_QTWEBENGINE)
  SuggestionsContext context;
  GetAvailableSuggestions(form, field, trigger_source, &suggestions, &context);

  const bool form_element_was_clicked =
      trigger_source ==
      AutofillSuggestionTriggerSource::kFormControlElementClicked;

  if (context.is_autofill_available) {
    switch (context.suppress_reason) {
      case SuppressReason::kNotSuppressed:
        break;

      case SuppressReason::kAblation:
        single_field_form_fill_router_->CancelPendingQueries();
        external_delegate_->OnSuggestionsReturned(field.global_id(),
                                                  suggestions);
        LOG_AF(log_manager())
            << LoggingScope::kFilling << LogMessage::kSuggestionSuppressed
            << " Reason: Ablation experiment";
        return;

      case SuppressReason::kInsecureForm:
        LOG_AF(log_manager())
            << LoggingScope::kFilling << LogMessage::kSuggestionSuppressed
            << " Reason: Insecure form";
        return;
      case SuppressReason::kAutocompleteOff:
        LOG_AF(log_manager())
            << LoggingScope::kFilling << LogMessage::kSuggestionSuppressed
            << " Reason: autocomplete=off";
        return;
      case SuppressReason::kAutocompleteUnrecognized:
        LOG_AF(log_manager())
            << LoggingScope::kFilling << LogMessage::kSuggestionSuppressed
            << " Reason: autocomplete=unrecognized";
        return;
    }

    if (!suggestions.empty()) {
      if (context.filling_product == FillingProduct::kCreditCard) {
        AutofillMetrics::LogIsQueriedCreditCardFormSecure(
            context.is_context_secure);
      }
      if (context.filling_product == FillingProduct::kAddress) {
        AutofillMetrics::LogAddressSuggestionsCount(base::ranges::count_if(
            suggestions, [](const Suggestion& suggestion) {
              return GetFillingProductFromPopupItemId(
                         suggestion.popup_item_id) == FillingProduct::kAddress;
            }));
      }
    }
  }

  if (suggestions.empty() &&
      (field.form_control_type == FormControlType::kTextArea ||
       field.form_control_type == FormControlType::kContentEditable)) {
    if (std::optional<Suggestion> maybe_compose_suggestion =
            MaybeGetComposeSuggestion(field)) {
      suggestions.push_back(*std::move(maybe_compose_suggestion));
    }
  }

  auto ShouldOfferSingleFieldFormFill = [&] {
    // Do not offer single field form fill if there are already suggestions.
    if (!suggestions.empty()) {
      return false;
    }

    if (trigger_source ==
        AutofillSuggestionTriggerSource::kTextareaFocusedWithoutClick) {
      return false;
    }

    // Do not offer single field form fill suggestions for credit card number,
    // cvc, and expiration date related fields. Standalone cvc fields (used to
    // re-authenticate the use of a credit card the website has on file) will be
    // handled separately because those have the field type
    // CREDIT_CARD_STANDALONE_VERIFICATION_CODE.
    FieldType server_type =
        context.focused_field ? context.focused_field->Type().GetStorableType()
                              : UNKNOWN_TYPE;
    if (data_util::IsCreditCardExpirationType(server_type) ||
        server_type == CREDIT_CARD_VERIFICATION_CODE ||
        server_type == CREDIT_CARD_NUMBER) {
      return false;
    }

    // Do not offer single field form fill suggestions if popups are suppressed
    // due to an unrecognized autocomplete attribute. Note that in the context
    // of Autofill, the popup for credit card related fields is not getting
    // suppressed due to an unrecognized autocomplete attribute.
    // TODO(crbug.com/1344590): Revisit here to see whether we should offer IBAN
    // filling for fields with unrecognized autocomplete attribute
    if (context.suppress_reason == SuppressReason::kAutocompleteUnrecognized) {
      return false;
    }

    // Therefore, we check the attribute explicitly.
    if (context.focused_field && context.focused_field->Type().html_type() ==
                                     HtmlFieldType::kUnrecognized) {
      return false;
    }

    // Finally, check that the scheme is secure.
    if (context.suppress_reason == SuppressReason::kInsecureForm) {
      return false;
    }
    return true;
  };

  auto ShouldShowSuggestion = [&] {
    if (fast_checkout_delegate_ &&
        (fast_checkout_delegate_->IsShowingFastCheckoutUI() ||
         (form_element_was_clicked &&
          fast_checkout_delegate_->TryToShowFastCheckout(form, field,
                                                         GetWeakPtr())))) {
      // The Fast Checkout surface is shown, so abort showing regular Autofill
      // UI. Now the flow is controlled by the `FastCheckoutClient` instead of
      // `external_delegate_`.
      // In principle, TTF and Fast Checkout triggering surfaces are different
      // and the two screens should never coincide.
      return false;
    }

    if (ShouldOfferSingleFieldFormFill()) {
      // Suggestions come back asynchronously, so the SingleFieldFormFillRouter
      // will handle sending the results back to the renderer.
      // TODO(crbug.com/1007974): The callback will only be called once.
      bool handled_by_single_field_form_filler =
          single_field_form_fill_router_->OnGetSingleFieldSuggestions(
              field, client(),
              base::BindRepeating(
                  [](base::WeakPtr<BrowserAutofillManager> self,
                     FieldGlobalId field_id,
                     const std::vector<Suggestion>& suggestions) {
                    if (self) {
                      self->external_delegate_->OnSuggestionsReturned(
                          field_id, suggestions);
                    }
                  },
                  weak_ptr_factory_.GetWeakPtr()),
              context);
      if (handled_by_single_field_form_filler) {
        return false;
      }
    }

    single_field_form_fill_router_->CancelPendingQueries();
    if (touch_to_fill_delegate_ &&
        (touch_to_fill_delegate_->IsShowingTouchToFill() ||
         (form_element_was_clicked &&
          touch_to_fill_delegate_->TryToShowTouchToFill(form, field)))) {
      // Touch To Fill surface is shown, so abort showing regular Autofill UI.
      // Now the flow is controlled by the |touch_to_fill_delegate_| instead
      // of |external_delegate_|.
      return false;
    }
    return true;
  };

  bool show_suggestion = ShouldShowSuggestion();
  // When focusing on a field, log whether there is a suggestion for the user
  // and whether the suggestion is shown.
  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (form_element_was_clicked &&
      GetCachedFormAndField(form, field, &form_structure, &autofill_field)) {
    autofill_field->AppendLogEventIfNotRepeated(AskForValuesToFillFieldLogEvent{
        .has_suggestion = ToOptionalBoolean(!suggestions.empty()),
        .suggestion_is_shown = ToOptionalBoolean(show_suggestion),
    });
  }
  if (show_suggestion) {
    // Send Autofill suggestions (could be an empty list).
    external_delegate_->OnSuggestionsReturned(field.global_id(), suggestions,
                                              context.should_display_gpay_logo);
  }
#else
  external_delegate_->OnSuggestionsReturned(field.global_id(), suggestions);
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

#if !BUILDFLAG(IS_QTWEBENGINE)
bool BrowserAutofillManager::ShouldFetchCreditCard(
    const FormData& form,
    const FormFieldData& field,
    const FormStructure& form_structure,
    const AutofillField& autofill_field,
    const CreditCard& credit_card) {
  if (WillFillCreditCardNumber(form.fields, form_structure.fields(),
                               autofill_field)) {
    return true;
  }
  // This happens for web sites which cache all credit card details except for
  // the cvc, which is different every time the virtual credit card is being
  // used.
  return credit_card.record_type() == CreditCard::RecordType::kVirtualCard &&
         autofill_field.Type().GetStorableType() ==
             CREDIT_CARD_STANDALONE_VERIFICATION_CODE;
}

void BrowserAutofillManager::FillOrPreviewCreditCardForm(
    mojom::ActionPersistence action_persistence,
    const FormData& form,
    const FormFieldData& field,
    const CreditCard& credit_card,
    const AutofillTriggerDetails& trigger_details) {
  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  credit_card_ = credit_card;
  bool is_preview = action_persistence != mojom::ActionPersistence::kFill;
  bool should_fetch_card =
      !is_preview && ShouldFetchCreditCard(form, field, *form_structure,
                                           *autofill_field, credit_card_);

  if (should_fetch_card) {
    credit_card_form_event_logger_->OnDidSelectCardSuggestion(
        credit_card_, *form_structure, signin_state_for_metrics_);

    credit_card_form_ = form;
    credit_card_field_ = field;

    // CreditCardAccessManager::FetchCreditCard() will trigger
    // OnCreditCardFetched() in this class after successfully fetching the card.
    fetched_credit_card_trigger_source_ = trigger_details.trigger_source;
    credit_card_access_manager_->FetchCreditCard(
        &credit_card_,
        base::BindOnce(&BrowserAutofillManager::OnCreditCardFetched,
                       weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  FillOrPreviewDataModelForm(action_persistence, form, field, &credit_card_,
                             /*optional_cvc=*/nullptr, form_structure,
                             autofill_field, trigger_details);
}

void BrowserAutofillManager::FillOrPreviewProfileForm(
    mojom::ActionPersistence action_persistence,
    const FormData& form,
    const FormFieldData& field,
    const AutofillProfile& profile,
    const AutofillTriggerDetails& trigger_details) {
  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;
  FillOrPreviewDataModelForm(action_persistence, form, field, &profile,
                             /*optional_cvc=*/nullptr, form_structure,
                             autofill_field, trigger_details);
}

void BrowserAutofillManager::FillOrPreviewField(
    mojom::ActionPersistence action_persistence,
    mojom::TextReplacement text_replacement,
    const FormData& form,
    const FormFieldData& field,
    const std::u16string& value,
    PopupItemId popup_item_id) {
  if (AutofillField* autofill_field = GetAutofillField(form, field);
      autofill_field && action_persistence == mojom::ActionPersistence::kFill &&
      (popup_item_id == PopupItemId::kCreditCardFieldByFieldFilling ||
       popup_item_id == PopupItemId::kAddressFieldByFieldFilling)) {
    // TODO(crbug.com/1345089): Only use AutofillField.
    const FormFieldData* const filled_field = &field;
    form_autofill_history_.AddFormFillEntry(
        base::make_span(&filled_field, 1u),
        base::make_span(&autofill_field, 1u),
        GetFillingProductFromPopupItemId(popup_item_id),
        /*is_refill=*/false);
    autofill_field->is_autofilled = true;
    autofill_field->AppendLogEventIfNotRepeated(FillFieldLogEvent{
        .fill_event_id = GetNextFillEventId(),
        .had_value_before_filling = ToOptionalBoolean(!field.value.empty()),
        .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
        .was_autofilled_before_security_policy = ToOptionalBoolean(true),
        .had_value_after_filling = ToOptionalBoolean(true),
        .filling_method = AutofillFillingMethod::kFieldByFieldFilling});
  }
  driver().ApplyFieldAction(action_persistence, text_replacement,
                            field.global_id(), value);
}

void BrowserAutofillManager::UndoAutofill(
    mojom::ActionPersistence action_persistence,
    const FormData& form,
    const FormFieldData& trigger_field) {
  FormStructure* form_structure = FindCachedFormById(form.global_id());
  if (!form_structure) {
    return;
  }
  // This will apply the undo operation and return information about the
  // operation being undone, for metric purposes.
  FillingProduct filling_product = UndoAutofillImpl(
      action_persistence, form, *form_structure, trigger_field);

  // The remaining logic is only relevant for filling.
  if (action_persistence != mojom::ActionPersistence::kPreview) {
    if (filling_product == FillingProduct::kAddress) {
      address_form_event_logger_->OnDidUndoAutofill();
    } else if (filling_product == FillingProduct::kCreditCard) {
      credit_card_form_event_logger_->OnDidUndoAutofill();
    }
  }
}

void BrowserAutofillManager::FillCreditCardForm(
    const FormData& form,
    const FormFieldData& field,
    const CreditCard& credit_card,
    const std::u16string& cvc,
    const AutofillTriggerDetails& trigger_details) {
  if (!IsValidFormData(form) || !IsValidFormFieldData(field)) {
    return;
  }

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form, field,
                             &credit_card, &cvc, form_structure, autofill_field,
                             trigger_details,
                             /*is_refill=*/false);
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

void BrowserAutofillManager::OnFocusNoLongerOnFormImpl(
    bool had_interacted_form) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  // For historical reasons, Chrome takes action on this message only if focus
  // was previously on a form with which the user had interacted.
  // TODO(crbug.com/1140473): Remove need for this short-circuit.
  if (!had_interacted_form)
    return;

  ProcessPendingFormForUpload();

#if BUILDFLAG(IS_CHROMEOS_ASH)
  // There is no way of determining whether ChromeVox is in use, so assume it's
  // being used.
  external_delegate_->OnAutofillAvailabilityEvent(
      mojom::AutofillSuggestionAvailability::kNoSuggestions);
#else
  if (external_delegate_->HasActiveScreenReader()) {
    external_delegate_->OnAutofillAvailabilityEvent(
        mojom::AutofillSuggestionAvailability::kNoSuggestions);
  }
#endif
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnFocusOnFormFieldImpl(
    const FormData& form,
    const FormFieldData& field,
    const gfx::RectF& bounding_box) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  // Notify installed screen readers if the focus is on a field for which there
  // are suggestions to present. Ignore if a screen reader is not present. If
  // the platform is ChromeOS, then assume ChromeVox is in use as there is no
  // way of determining whether it's being used from this point in the code.
#if !BUILDFLAG(IS_CHROMEOS_ASH)
  if (!external_delegate_->HasActiveScreenReader())
    return;
#endif

  // TODO(https://crbug.com/848427): Add metrics for performance impact.
  std::vector<Suggestion> suggestions;
  SuggestionsContext context;
  // This code path checks if suggestions to be announced to a screen reader are
  // available when the focus on a form field changes. This cannot happen in
  // `OnAskForValuesToFillImpl()`, since the `AutofillSuggestionAvailability` is
  // a sticky flag and needs to be reset when a non-autofillable field is
  // focused. The suggestion trigger source doesn't influence the set of
  // suggestions generated, but only the way suggestions behave when they are
  // accepted. For this reason, checking whether suggestions are available can
  // be done with the `kUnspecified` suggestion trigger source.
  GetAvailableSuggestions(form, field,
                          AutofillSuggestionTriggerSource::kUnspecified,
                          &suggestions, &context);
  external_delegate_->OnAutofillAvailabilityEvent(
      (context.suppress_reason == SuppressReason::kNotSuppressed &&
       !suggestions.empty())
          ? mojom::AutofillSuggestionAvailability::kAutofillAvailable
          : mojom::AutofillSuggestionAvailability::kNoSuggestions);
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnSelectControlDidChangeImpl(
    const FormData& form,
    const FormFieldData& field,
    const gfx::RectF& bounding_box) {
  // TODO(crbug.com/814961): Handle select control change.
}

void BrowserAutofillManager::OnDidFillAutofillFormDataImpl(
    const FormData& form,
    const TimeTicks timestamp) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  UpdatePendingForm(form);

  // Find the FormStructure that corresponds to |form|. Use default form type if
  // form is not present in our cache, which will happen rarely.

  FormStructure* form_structure = FindCachedFormById(form.global_id());
  DenseSet<FormType> form_types;
  if (form_structure) {
    form_types = form_structure->GetFormTypes();
  }

  uint32_t profile_form_bitmask =
      form_structure ? data_util::DetermineGroups(*form_structure) : 0;

  AutofillMetrics::LogUserHappinessMetric(
      AutofillMetrics::USER_DID_AUTOFILL, form_types,
      client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);
  if (!user_did_autofill_) {
    user_did_autofill_ = true;
    AutofillMetrics::LogUserHappinessMetric(
        AutofillMetrics::USER_DID_AUTOFILL_ONCE, form_types,
        client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);
  }

  UpdateInitialInteractionTimestamp(timestamp);
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

#if !BUILDFLAG(IS_QTWEBENGINE)
void BrowserAutofillManager::DidShowSuggestions(
    base::span<const PopupItemId> shown_suggestions_types,
    const FormData& form,
    const FormFieldData& field) {
  NotifyObservers(&Observer::OnSuggestionsShown);

  bool has_autofill_suggestions = base::ranges::any_of(
      shown_suggestions_types,
      AutofillExternalDelegate::IsAutofillAndFirstLayerSuggestionId);
  if (!has_autofill_suggestions) {
    return;
  }

  if (base::Contains(shown_suggestions_types, FillingProduct::kCreditCard,
                     GetFillingProductFromPopupItemId) &&
      IsCreditCardFidoAuthenticationEnabled()) {
    credit_card_access_manager_->PrepareToFetchCreditCard();
  }

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  // TODO(crbug.com/1493361): Adapt for the unclassified forms.
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  uint32_t profile_form_bitmask = data_util::DetermineGroups(*form_structure);
  AutofillMetrics::LogUserHappinessMetric(
      AutofillMetrics::SUGGESTIONS_SHOWN, autofill_field->Type().group(),
      client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);

  if (!did_show_suggestions_) {
    // TODO(crbug.com/1493361): Take suggestions for unclassified forms into
    // account.
    did_show_suggestions_ = true;
    AutofillMetrics::LogUserHappinessMetric(
        AutofillMetrics::SUGGESTIONS_SHOWN_ONCE, autofill_field->Type().group(),
        client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);
  }

  auto* logger = GetEventFormLogger(*autofill_field);
  if (logger) {
    logger->OnDidShowSuggestions(*form_structure, *autofill_field,
                                 form_structure->form_parsed_timestamp(),
                                 signin_state_for_metrics_,
                                 client().IsOffTheRecord());
  } else if (autofill_field->ShouldSuppressSuggestionsAndFillingByDefault()) {
    // Suggestions were triggered on an ac=unrecognized address field.
    autocomplete_unrecognized_fallback_logger_->OnDidShowSuggestions();
  }
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

void BrowserAutofillManager::OnHidePopupImpl() {
#if !BUILDFLAG(IS_QTWEBENGINE)
  single_field_form_fill_router_->CancelPendingQueries();
  client().HideAutofillPopup(PopupHidingReason::kRendererEvent);
  if (fast_checkout_delegate_) {
    fast_checkout_delegate_->HideFastCheckout(/*allow_further_runs=*/false);
  }
  if (touch_to_fill_delegate_) {
    touch_to_fill_delegate_->HideTouchToFill();
  }
#else
  client().HideAutofillPopup(PopupHidingReason::kRendererEvent);
#endif
}

#if !BUILDFLAG(IS_QTWEBENGINE)
bool BrowserAutofillManager::RemoveAutofillProfileOrCreditCard(
    Suggestion::BackendId backend_id) {
  if (const CreditCard* credit_card = GetCreditCard(backend_id)) {
    // Server cards cannot be deleted from within Chrome.
    bool allowed_to_delete = CreditCard::IsLocalCard(credit_card);

    if (allowed_to_delete) {
      client().GetPersonalDataManager()->DeleteLocalCreditCards({*credit_card});
    }

    return allowed_to_delete;
  }

  if (const AutofillProfile* profile = GetProfile(backend_id)) {
    client().GetPersonalDataManager()->RemoveByGUID(profile->guid());
    return true;
  }

  return false;  // The ID was valid. The entry may have been deleted in a race.
}

void BrowserAutofillManager::RemoveCurrentSingleFieldSuggestion(
    const std::u16string& name,
    const std::u16string& value,
    PopupItemId popup_item_id) {
  single_field_form_fill_router_->OnRemoveCurrentSingleFieldSuggestion(
      name, value, popup_item_id);
}

void BrowserAutofillManager::OnSingleFieldSuggestionSelected(
    const std::u16string& value,
    PopupItemId popup_item_id,
    const FormData& form,
    const FormFieldData& field) {
  single_field_form_fill_router_->OnSingleFieldSuggestionSelected(
      value, popup_item_id);

  AutofillField* autofill_trigger_field = GetAutofillField(form, field);
  if (!autofill_trigger_field) {
    return;
  }
  if (IsSingleFieldFormFillerFillingProduct(
          GetFillingProductFromPopupItemId(popup_item_id))) {
    autofill_trigger_field->AppendLogEventIfNotRepeated(
        TriggerFillFieldLogEvent{
            .data_type =
                GetEventTypeFromSingleFieldSuggestionPopupItemId(popup_item_id),
            .associated_country_code = "",
            .timestamp = AutofillClock::Now()});
  }
}

void BrowserAutofillManager::OnUserHideSuggestions(const FormData& form,
                                                   const FormFieldData& field) {
  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  auto* logger = GetEventFormLogger(*autofill_field);
  if (logger)
    logger->OnUserHideSuggestions(*form_structure, *autofill_field);
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

bool BrowserAutofillManager::ShouldClearPreviewedForm() {
#if !BUILDFLAG(IS_QTWEBENGINE)
  return credit_card_access_manager_->ShouldClearPreviewedForm();
#else
  return false;
#endif
}

void BrowserAutofillManager::OnSelectOrSelectListFieldOptionsDidChangeImpl(
    const FormData& form) {
  FormStructure* form_structure = FindCachedFormById(form.global_id());
  if (!form_structure)
    return;

  driver().SendAutofillTypePredictionsToRenderer({form_structure});

#if !BUILDFLAG(IS_QTWEBENGINE)
  if (ShouldTriggerRefill(*form_structure,
                          RefillTriggerReason::kSelectOptionsChanged)) {
    TriggerRefill(
        form, {.trigger_source = AutofillTriggerSource::kSelectOptionsChanged});
  }
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnJavaScriptChangedAutofilledValueImpl(
    const FormData& form,
    const FormFieldData& field,
    const std::u16string& old_value) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  // Log to chrome://autofill-internals that a field's value was set by
  // JavaScript.
  auto StructureOfString = [](std::u16string str) {
    for (auto& c : str) {
      if (base::IsAsciiAlpha(c)) {
        c = 'a';
      } else if (base::IsAsciiDigit(c)) {
        c = '0';
      } else if (base::IsAsciiWhitespace(c)) {
        c = ' ';
      } else {
        c = '$';
      }
    }
    return str;
  };
  auto GetFieldNumber = [&]() {
    for (size_t i = 0; i < form.fields.size(); ++i) {
      if (form.fields[i].global_id() == field.global_id())
        return base::StringPrintf("Field %zu", i);
    }
    return std::string("unknown");
  };
  LogBuffer change(IsLoggingActive(log_manager()));
  LOG_AF(change) << Tag{"div"} << Attrib{"class", "form"};
  LOG_AF(change) << field << Br{};
  LOG_AF(change) << "Old value structure: '"
                 << StructureOfString(old_value.substr(0, 80)) << "'" << Br{};
  LOG_AF(change) << "New value structure: '"
                 << StructureOfString(field.value.substr(0, 80)) << "'";
  LOG_AF(log_manager()) << LoggingScope::kWebsiteModifiedFieldValue
                        << LogMessage::kJavaScriptChangedAutofilledValue << Br{}
                        << Tag{"table"} << Tr{} << GetFieldNumber()
                        << std::move(change);

  AnalyzeJavaScriptChangedAutofilledValue(form, field);
  if (FormStructure* form_structure = FindCachedFormById(form.global_id())) {
    MaybeTriggerRefillForExpirationDate(
        form, field, *form_structure, old_value,
        {.trigger_source =
             AutofillTriggerSource::kJavaScriptChangedAutofilledValue});
  }
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

#if !BUILDFLAG(IS_QTWEBENGINE)
void BrowserAutofillManager::MaybeTriggerRefillForExpirationDate(
    const FormData& form,
    const FormFieldData& field,
    const FormStructure& form_structure,
    const std::u16string& old_value,
    const AutofillTriggerDetails& trigger_details) {
  // We currently support a single case of refilling credit card expiration
  // dates: If we filled the expiration date in a format "05/2023" and the
  // website turned it into "05 / 20" (i.e. it broke the year by cutting the
  // last two digits instead of stripping the first two digits).
  constexpr size_t kSupportedLength = std::string_view("MM/YYYY").size();
  if (old_value.length() != kSupportedLength) {
    return;
  }
  if (old_value == field.value) {
    return;
  }
  static constexpr char16_t kFormatRegEx[] =
      uR"(^(\d\d)(\s?[/-]?\s?)?(\d\d|\d\d\d\d)$)";
  std::vector<std::u16string> old_groups;
  if (!MatchesRegex<kFormatRegEx>(old_value, &old_groups)) {
    return;
  }
  DCHECK_EQ(old_groups.size(), 4u);

  std::vector<std::u16string> new_groups;
  if (!MatchesRegex<kFormatRegEx>(field.value, &new_groups)) {
    return;
  }
  DCHECK_EQ(new_groups.size(), 4u);

  int old_month, old_year, new_month, new_year;
  if (!base::StringToInt(old_groups[1], &old_month) ||
      !base::StringToInt(old_groups[3], &old_year) ||
      !base::StringToInt(new_groups[1], &new_month) ||
      !base::StringToInt(new_groups[3], &new_year) ||
      old_groups[3].size() != 4 || new_groups[3].size() != 2 ||
      old_month != new_month ||
      // We need to refill if the first two digits of the year were preserved.
      old_year / 100 != new_year) {
    return;
  }
  std::u16string refill_value = field.value;
  CHECK(refill_value.size() >= 2);
  refill_value[refill_value.size() - 1] = '0' + (old_year % 10);
  refill_value[refill_value.size() - 2] = '0' + ((old_year % 100) / 10);

  if (ShouldTriggerRefill(form_structure,
                          RefillTriggerReason::kExpirationDateFormatted)) {
    FillingContext* filling_context =
        GetFillingContext(form_structure.global_id());
    DCHECK(filling_context);  // This is enforced by ShouldTriggerRefill.
    filling_context->forced_fill_values[field.global_id()] = refill_value;
    ScheduleRefill(form, form_structure, trigger_details);
  }
}

void BrowserAutofillManager::AnalyzeJavaScriptChangedAutofilledValue(
    const FormData& form,
    const FormFieldData& field) {
  // We are interested in reporting the events where JavaScript resets an
  // autofilled value immediately after filling. For a reset, the value
  // needs to be empty.
  if (!field.value.empty())
    return;

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return;

  FillingContext* filling_context =
      GetFillingContext(form_structure->global_id());
  if (!filling_context)
    return;

  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeDelta delta = now - filling_context->original_fill_time;

  // If the filling happened too long ago, maybe this is just an effect of
  // the user pressing a "reset form" button.
  if (delta >= limit_before_refill_) {
    return;
  }

  auto* logger = GetEventFormLogger(*autofill_field);
  if (logger) {
    logger->OnAutofilledFieldWasClearedByJavaScriptShortlyAfterFill(
        *form_structure);
  }
}

void BrowserAutofillManager::OnCreditCardFetched(
    CreditCardFetchResult result,
    const CreditCard* credit_card) {
  if (result != CreditCardFetchResult::kSuccess) {
    driver().RendererShouldClearPreviewedForm();
    return;
  }
  // In the failure case, `credit_card` can be `nullptr`, but in the success
  // case it is non-null.
  CHECK(credit_card);
  OnCreditCardFetchedSuccessfully(*credit_card);

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(credit_card_form_, credit_card_field_,
                             &form_structure, &autofill_field)) {
    return;
  }

  FillCreditCardForm(
      credit_card_form_, credit_card_field_, *credit_card, credit_card->cvc(),
      {.trigger_source = fetched_credit_card_trigger_source_.value_or(
           AutofillTriggerSource::kCreditCardCvcPopup)});
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

void BrowserAutofillManager::OnDidEndTextFieldEditingImpl() {
  external_delegate_->DidEndTextFieldEditing();
  // Should not hide the Touch To Fill surface, since it is an overlay UI
  // which ends editing.
}

bool BrowserAutofillManager::IsAutofillEnabled() const {
  return IsAutofillProfileEnabled() || IsAutofillPaymentMethodsEnabled();
}

bool BrowserAutofillManager::IsAutofillProfileEnabled() const {
  return prefs::IsAutofillProfileEnabled(client().GetPrefs());
}

bool BrowserAutofillManager::IsAutofillPaymentMethodsEnabled() const {
  return prefs::IsAutofillPaymentMethodsEnabled(client().GetPrefs());
}

#if !BUILDFLAG(IS_QTWEBENGINE)
const FormData& BrowserAutofillManager::last_query_form() const {
  return external_delegate_->query_form();
}

bool BrowserAutofillManager::ShouldUploadForm(const FormStructure& form) {
  return IsAutofillEnabled() && !client().IsOffTheRecord() &&
         form.ShouldBeUploaded();
}

void BrowserAutofillManager::
    FetchPotentialCardLastFourDigitsCombinationFromDOM() {
  driver().GetFourDigitCombinationsFromDOM(base::BindOnce(
      [](base::WeakPtr<BrowserAutofillManager> self,
         const std::vector<std::string>& four_digit_combinations_in_dom) {
        if (!self) {
          return;
        }
        self->four_digit_combinations_in_dom_ = four_digit_combinations_in_dom;
      },
      weak_ptr_factory_.GetWeakPtr()));
}

void BrowserAutofillManager::StoreUploadVotesAndLogQualityCallback(
    FormSignature form_signature,
    base::OnceClosure callback) {
  // Remove entries with the same FormSignature to replace them.
  WipeLogQualityAndVotesUploadCallback(form_signature);

  // Entries in queued_vote_uploads_ are submitted after navigations or form
  // submissions. To reduce the risk of collecting too much data that is not
  // send, we allow only `kMaxEntriesInQueue` entries. Anything in excess will
  // be sent when the queue becomes to long.
  constexpr int kMaxEntriesInQueue = 10;
  while (queued_vote_uploads_.size() >= kMaxEntriesInQueue) {
    base::OnceCallback oldest_callback =
        std::move(queued_vote_uploads_.back().second);
    queued_vote_uploads_.pop_back();
    std::move(oldest_callback).Run();
  }

  queued_vote_uploads_.emplace_front(form_signature, std::move(callback));
}

void BrowserAutofillManager::WipeLogQualityAndVotesUploadCallback(
    FormSignature form_signature) {
  std::erase_if(queued_vote_uploads_, [form_signature](const auto& entry) {
    return entry.first == form_signature;
  });
}

void BrowserAutofillManager::FlushPendingLogQualityAndVotesUploadCallbacks() {
  std::list<std::pair<FormSignature, base::OnceClosure>> queued_vote_uploads =
      std::exchange(queued_vote_uploads_, {});
  for (auto& i : queued_vote_uploads)
    std::move(i.second).Run();
}

// We explicitly pass in all the time stamps of interest, as the cached ones
// might get reset before this method executes.
void BrowserAutofillManager::UploadVotesAndLogQuality(
    std::unique_ptr<FormStructure> submitted_form,
    base::TimeTicks interaction_time,
    base::TimeTicks submission_time,
    bool observed_submission,
    ukm::SourceId source_id) {
  // If the form is submitted, we don't need to send pending votes from blur
  // (un-focus) events.
  if (observed_submission)
    WipeLogQualityAndVotesUploadCallback(submitted_form->form_signature());

  if (submitted_form->ShouldRunHeuristics() ||
      submitted_form->ShouldRunHeuristicsForSingleFieldForms() ||
      submitted_form->ShouldBeQueried()) {
    FormInteractionCounts form_interaction_counts = {};
    if (submitted_form->field_count() > 0) {
      const AutofillField* autofill_field = submitted_form->field(0);
      auto* logger = GetEventFormLogger(*autofill_field);
      if (logger) {
        form_interaction_counts = logger->form_interaction_counts();
      }
    }

    autofill_metrics::LogQualityMetrics(
        *submitted_form, submitted_form->form_parsed_timestamp(),
        interaction_time, submission_time, form_interactions_ukm_logger(),
        did_show_suggestions_, observed_submission, form_interaction_counts);

    if (observed_submission) {
      // Ensure that callbacks for blur votes get sent as well here because
      // we are not sure whether a full navigation with a Reset() call follows.
      FlushPendingLogQualityAndVotesUploadCallbacks();
    }
  }

  if (!submitted_form->ShouldBeUploaded())
    return;

  if (base::FeatureList::IsEnabled(
          features::kAutofillLogUKMEventsWithSampleRate) &&
      ShouldUploadUkm(*submitted_form)) {
    AutofillMetrics::LogAutofillFieldInfoAfterSubmission(
        client().GetUkmRecorder(), source_id, *submitted_form, submission_time);
  }

  if (!client().GetCrowdsourcingManager()) {
    return;
  }

  // Check if the form is among the forms that were recently auto-filled.
  bool was_autofilled = base::Contains(autofilled_form_signatures_,
                                       submitted_form->form_signature());

  FieldTypeSet non_empty_types;
  client().GetPersonalDataManager()->GetNonEmptyTypes(&non_empty_types);
  // As CVC is not stored, treat it separately.
  if (!last_unlocked_credit_card_cvc_.empty() ||
      non_empty_types.contains(CREDIT_CARD_NUMBER)) {
    non_empty_types.insert(CREDIT_CARD_VERIFICATION_CODE);
  }

  client().GetCrowdsourcingManager()->StartUploadRequest(
      /*upload_contents=*/EncodeUploadRequest(
          *submitted_form, non_empty_types, was_autofilled,
          /*login_form_signature=*/{}, observed_submission),
      submitted_form->submission_source(), submitted_form->active_field_count(),
      client().GetPrefs());
}

const gfx::Image& BrowserAutofillManager::GetCardImage(
    const CreditCard& credit_card) {
  gfx::Image* card_art_image =
      client().GetPersonalDataManager()->GetCreditCardArtImageForUrl(
          credit_card.card_art_url());
  return card_art_image
             ? *card_art_image
             : ui::ResourceBundle::GetSharedInstance().GetImageNamed(
                   CreditCard::IconResourceId(credit_card.network()));
}

void BrowserAutofillManager::OnSubmissionFieldTypesDetermined(
    std::unique_ptr<FormStructure> submitted_form,
    base::TimeTicks interaction_time,
    base::TimeTicks submission_time,
    bool observed_submission,
    ukm::SourceId source_id) {
  size_t address_fields_count = base::ranges::count_if(
      submitted_form->fields(),
      [](const std::unique_ptr<AutofillField>& field) {
        return FieldTypeGroupToFormType(field->Type().group()) ==
               FormType::kAddressForm;
      });

  if (address_fields_count >= kMinFormSizeToTriggerUserPerceptionSurvey &&
      base::FeatureList::IsEnabled(
          features::kAutofillAddressUserPerceptionSurvey)) {
    autofill_metrics::FormGroupFillingStats filling_stats =
        autofill_metrics::GetAddressFormFillingStats(*submitted_form);
    if (filling_stats.TotalFilled() > 0) {
      client().TriggerUserPerceptionOfAutofillSurvey(
          AddressFormFillingStatsToSurveyStringData(filling_stats));
    }
  }
  UploadVotesAndLogQuality(std::move(submitted_form), interaction_time,
                           submission_time, observed_submission, source_id);
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

void BrowserAutofillManager::Reset() {
#if !BUILDFLAG(IS_QTWEBENGINE)
  // Process log events and record into UKM when the form is destroyed or
  // removed.
  for (const auto& [form_id, form_structure] : form_structures()) {
    ProcessFieldLogEventsInForm(*form_structure);
  }

  // Note that upload_request_ is not reset here because the prompt to
  // save a card is shown after page navigation.
  ProcessPendingFormForUpload();
  FlushPendingLogQualityAndVotesUploadCallbacks();
  DCHECK(!pending_form_data_);
  // `credit_card_access_manager_` needs to be reset before resetting
  // `credit_card_form_event_logger_`, since it keeps a raw pointer to it.
  credit_card_access_manager_.reset();
  // {address, credit_card}_form_event_logger_ need to be reset before
  // AutofillManager::Reset() because ~FormEventLoggerBase() uses
  // form_interactions_ukm_logger_ that is created and assigned in
  // AutofillManager::Reset(). The new form_interactions_ukm_logger_ instance
  // is needed for constructing the new *form_event_logger_ instances which is
  // why calling AutofillManager::Reset() after constructing *form_event_logger_
  // instances is not an option.
  address_form_event_logger_->OnDestroyed();
  address_form_event_logger_.reset();
  credit_card_form_event_logger_->OnDestroyed();
  credit_card_form_event_logger_.reset();
  AutofillManager::Reset();
  address_form_event_logger_ =
      std::make_unique<autofill_metrics::AddressFormEventLogger>(
          driver().IsInAnyMainFrame(), form_interactions_ukm_logger(),
          &unsafe_client());
  credit_card_form_event_logger_ =
      std::make_unique<autofill_metrics::CreditCardFormEventLogger>(
          driver().IsInAnyMainFrame(), form_interactions_ukm_logger(),
          unsafe_client().GetPersonalDataManager(), &unsafe_client());
  credit_card_access_manager_ = std::make_unique<CreditCardAccessManager>(
      &driver(), &unsafe_client(), unsafe_client().GetPersonalDataManager(),
      credit_card_form_event_logger_.get());
  autocomplete_unrecognized_fallback_logger_ = std::make_unique<
      autofill_metrics::AutocompleteUnrecognizedFallbackEventLogger>();

  has_logged_autofill_enabled_ = false;
  did_show_suggestions_ = false;
  user_did_type_ = false;
  user_did_autofill_ = false;
  user_did_edit_autofilled_field_ = false;
  credit_card_ = CreditCard();
  credit_card_form_ = FormData();
  credit_card_field_ = FormFieldData();
  last_unlocked_credit_card_cvc_.clear();
  initial_interaction_timestamp_ = TimeTicks();
  fetched_credit_card_trigger_source_ = std::nullopt;
  if (touch_to_fill_delegate_) {
    touch_to_fill_delegate_->Reset();
  }
  filling_context_.clear();
  form_autofill_history_.Reset();
  form_submitted_timestamp_ = TimeTicks();
  four_digit_combinations_in_dom_.clear();
#else
  AutofillManager::Reset();
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnContextMenuShownInField(
    const FormGlobalId& form_global_id,
    const FieldGlobalId& field_global_id) {
  FormStructure* form = FindCachedFormById(form_global_id);
  if (!form)
    return;
  auto field =
      base::ranges::find_if(*form, [&field_global_id](const auto& field) {
        return field->global_id() == field_global_id;
      });

  if (field != form->end())
    (*field)->set_was_context_menu_shown(true);
}

#if !BUILDFLAG(IS_QTWEBENGINE)
bool BrowserAutofillManager::RefreshDataModels() {
  if (!IsAutofillEnabled())
    return false;

  credit_card_access_manager_->UpdateCreditCardFormEventLogger();

  const std::vector<AutofillProfile*>& profiles =
      client().GetPersonalDataManager()->GetProfiles();
  address_form_event_logger_->set_record_type_count(profiles.size());

  return !profiles.empty() ||
         !client().GetPersonalDataManager()->GetCreditCards().empty();
}

CreditCard* BrowserAutofillManager::GetCreditCard(
    Suggestion::BackendId unique_id) {
  return client().GetPersonalDataManager()->GetCreditCardByGUID(
      absl::get<Suggestion::Guid>(unique_id).value());
}

AutofillProfile* BrowserAutofillManager::GetProfile(
    Suggestion::BackendId unique_id) {
  std::string guid = absl::get<Suggestion::Guid>(unique_id).value();
  if (base::Uuid::ParseCaseInsensitive(guid).is_valid()) {
    return client().GetPersonalDataManager()->GetProfileByGUID(guid);
  }
  return nullptr;
}

base::flat_map<FieldGlobalId, FieldFillingSkipReason>
BrowserAutofillManager::GetFieldFillingSkipReasons(
    const FormData& form,
    const FormStructure& form_structure,
    const FormFieldData& trigger_field,
    const Section& filling_section,
    const FieldTypeSet& field_types_to_fill,
    const DenseSet<FieldTypeGroup>* optional_type_groups_originally_filled,
    FillingProduct filling_product,
    bool skip_unrecognized_autocomplete_fields,
    bool is_refill,
    bool is_expired_credit_card) const {
  // Counts the number of times a type was seen in the section to be filled.
  // This is used to limit the maximum number of fills per value.
  base::flat_map<FieldType, size_t> type_count;
  type_count.reserve(form_structure.field_count());

  CHECK_EQ(form.fields.size(), form_structure.field_count());
  base::flat_map<FieldGlobalId, FieldFillingSkipReason> skip_reasons =
      base::MakeFlatMap<FieldGlobalId, FieldFillingSkipReason>(
          form_structure, {}, [](const std::unique_ptr<AutofillField>& field) {
            return std::make_pair(field->global_id(),
                                  FieldFillingSkipReason::kNotSkipped);
          });
  for (size_t i = 0; i < form_structure.field_count(); ++i) {
    // Log events when the fields on the form are filled by autofill suggestion.
    const AutofillField* autofill_field = form_structure.field(i);
    const FieldGlobalId field_id = autofill_field->global_id();
    const bool is_triggering_field =
        FormFieldData::DeepEqual(*autofill_field, trigger_field);

    if (autofill_field->section != filling_section) {
      skip_reasons[field_id] = FieldFillingSkipReason::kNotInFilledSection;
      continue;
    }

    if (autofill_field->only_fill_when_focused() && !is_triggering_field) {
      skip_reasons[field_id] = FieldFillingSkipReason::kNotFocused;
      continue;
    }

    // Address fields with unrecognized autocomplete attribute are only filled
    // when triggered through manual fallbacks.
    if (!is_triggering_field && skip_unrecognized_autocomplete_fields &&
        autofill_field->ShouldSuppressSuggestionsAndFillingByDefault()) {
      skip_reasons[field_id] =
          FieldFillingSkipReason::kUnrecognizedAutocompleteAttribute;
      continue;
    }

    // TODO(crbug/1203667#c9): Skip if the form has changed in the meantime,
    // which may happen with refills.
    if (field_id != form.fields[i].global_id()) {
      skip_reasons[field_id] = FieldFillingSkipReason::kFormChanged;
      continue;
    }

    // Don't fill unfocusable fields, with the exception of <select> fields, for
    // the sake of filling the synthetic fields.
    if (!autofill_field->IsFocusable() && !autofill_field->IsSelectElement()) {
      skip_reasons[field_id] = FieldFillingSkipReason::kInvisibleField;
      continue;
    }

    // Do not fill fields that have been edited by the user, except if the field
    // is empty and its initial value (= cached value) was empty as well. A
    // similar check is done in ForEachMatchingFormFieldCommon(), which
    // frequently has false negatives.
    if ((form.fields[i].properties_mask & kUserTyped) &&
        (!form.fields[i].value.empty() || !autofill_field->value.empty()) &&
        !is_triggering_field) {
      skip_reasons[field_id] = FieldFillingSkipReason::kUserFilledFields;
      continue;
    }

    // Don't fill previously autofilled fields except the initiating field or
    // when it's a refill.
    if (form.fields[i].is_autofilled && !is_triggering_field && !is_refill) {
      skip_reasons[field_id] =
          FieldFillingSkipReason::kAutofilledFieldsNotRefill;
      continue;
    }

    FieldTypeGroup field_group_type = autofill_field->Type().group();
    if (field_group_type == FieldTypeGroup::kNoGroup) {
      skip_reasons[field_id] = FieldFillingSkipReason::kNoFillableGroup;
      continue;
    }

    // On a refill, only fill fields from type groups that were present during
    // the initial fill.
    if (is_refill && optional_type_groups_originally_filled &&
        !base::Contains(*optional_type_groups_originally_filled,
                        field_group_type)) {
      skip_reasons[field_id] = FieldFillingSkipReason::kRefillNotInInitialFill;
      continue;
    }

    FieldType field_type = autofill_field->Type().GetStorableType();
    // Don't fill expired cards expiration date.
    if (data_util::IsCreditCardExpirationType(field_type) &&
        is_expired_credit_card) {
      skip_reasons[field_id] = FieldFillingSkipReason::kExpiredCards;
      continue;
    }

    if (base::FeatureList::IsEnabled(
            features::kAutofillGranularFillingAvailable)) {
      if (!field_types_to_fill.contains(field_type)) {
        skip_reasons[field_id] =
            FieldFillingSkipReason::kFieldDoesNotMatchTargetFieldsSet;
        continue;
      }
    }

    // A field with a specific type is only allowed to be filled a limited
    // number of times given by |TypeValueFormFillingLimit(field_type)|.
    if (++type_count[field_type] > TypeValueFormFillingLimit(field_type)) {
      skip_reasons[field_id] = FieldFillingSkipReason::kFillingLimitReachedType;
      continue;
    }

    // Don't fill meaningfully pre-filled fields but do fill placeholders.
    if (!is_triggering_field && !form.fields[i].value.empty() &&
        (IsNotAPlaceholder(autofill_field) ||
         IsMeaningfullyPreFilled(autofill_field))) {
      skip_reasons[field_id] = FieldFillingSkipReason::kValuePrefilled;
      continue;
    }

    // Usually, this should not happen because Autofill sectioning logic
    // separates address fields from credit card fields. However, autofill
    // respects the HTML `autocomplete` attribute when it is used to specify a
    // section, and so in some rare cases it might happen that a credit card
    // field is included in an address field or vice versa.
    // Note that autofilling using manual fallback does not use this logic flow,
    // otherwise this wouldn't be true.
    if ((filling_product == FillingProduct::kAddress &&
         !IsAddressType(autofill_field->Type().GetStorableType())) ||
        (filling_product == FillingProduct::kCreditCard &&
         autofill_field->Type().group() != FieldTypeGroup::kCreditCard)) {
      skip_reasons[field_id] = FieldFillingSkipReason::kFieldTypeUnrelated;
      continue;
    }
    // Usually, `skip_reasons[field_id] == FieldFillingSkipReason::kNotSkipped`.
    // It may not be the case though because FieldGlobalIds are not unique among
    // FormData::fields, so a previous iteration may have set a skip reason for
    // `field_id`. To err on the side of caution we keep the skip reason.
  }
  return skip_reasons;
}

FillingProduct BrowserAutofillManager::UndoAutofillImpl(
    mojom::ActionPersistence action_persistence,
    FormData form,
    FormStructure& form_structure,
    const FormFieldData& trigger_field) {
  if (!form_autofill_history_.HasHistory(trigger_field.global_id())) {
    LOG_AF(log_manager())
        << "Could not undo the filling operation on field "
        << trigger_field.global_id()
        << " because history was dropped upon reaching history limit of "
        << kMaxStorableFieldFillHistory;
    return FillingProduct::kNone;
  }
  FormAutofillHistory::FillOperation operation =
      form_autofill_history_.GetLastFillingOperationForField(
          trigger_field.global_id());

  base::flat_map<FieldGlobalId, AutofillField*> cached_fields =
      base::MakeFlatMap<FieldGlobalId, AutofillField*>(
          form_structure.fields(), {},
          [](const std::unique_ptr<AutofillField>& field) {
            return std::make_pair(field->global_id(), field.get());
          });
  // Remove the fields to be skipped so that we only pass fields to be modified
  // by the renderer.
  std::erase_if(form.fields, [this, &operation,
                              &cached_fields](const FormFieldData& field) {
    // Skip not-autofilled fields as undo only acts on autofilled fields.
    return !field.is_autofilled ||
           // Skip fields whose last autofill operations is different than
           // the one of the trigger field.
           form_autofill_history_.GetLastFillingOperationForField(
               field.global_id()) != operation ||
           // Skip fields that are not cached to avoid unexpected outcomes.
           !cached_fields.contains(field.global_id());
  });

  for (FormFieldData& field : form.fields) {
    AutofillField& autofill_field =
        CHECK_DEREF(cached_fields[field.global_id()]);
    const FormAutofillHistory::FieldFillingEntry& previous_state =
        operation.GetFieldFillingEntry(field.global_id());
    // Update the FormFieldData to be sent for the renderer.
    field.value = previous_state.value;
    field.is_autofilled = previous_state.is_autofilled;

    // Update the cached AutofillField in the browser.
    // TODO(crbug.com/1345089): Consider updating the value too.
    autofill_field.is_autofilled = previous_state.is_autofilled;
    autofill_field.set_autofill_source_profile_guid(
        previous_state.autofill_source_profile_guid);
    autofill_field.set_autofilled_type(previous_state.autofilled_type);
  }

  // Do not attempt a refill after an Undo operation.
  if (FillingContext* filling_context = GetFillingContext(form.global_id())) {
    SetFillingContext(form.global_id(), nullptr);
  }

  // Since Undo only affects fields that were already filled, and only sets
  // values to fields to something that already existed in it prior to the
  // filling, it is okay to bypass the filling security checks and hence passing
  // dummy values for `triggered_origin` and `field_type_map`.
  driver().ApplyFormAction(mojom::ActionType::kUndo, action_persistence, form,
                           url::Origin(),
                           /*field_type_map=*/{});

  FillingProduct filling_product = operation.get_filling_product();
  if (action_persistence != mojom::ActionPersistence::kPreview) {
    // History is not cleared on previews as it might be used for future
    // previews or for the filling.
    form_autofill_history_.EraseFormFillEntry(std::move(operation));
  }
  return filling_product;
}

void BrowserAutofillManager::FillOrPreviewDataModelForm(
    mojom::ActionPersistence action_persistence,
    const FormData& form,
    const FormFieldData& field,
    absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card,
    const std::u16string* optional_cvc,
    FormStructure* form_structure,
    AutofillField* autofill_trigger_field,
    const AutofillTriggerDetails& trigger_details,
    bool is_refill) {
  bool is_credit_card =
      absl::holds_alternative<const CreditCard*>(profile_or_credit_card);

  DCHECK(is_credit_card || !optional_cvc);
  DCHECK(form_structure);
  DCHECK(autofill_trigger_field);

  LogBuffer buffer(IsLoggingActive(log_manager()));
  LOG_AF(buffer) << "action_persistence: "
                 << ActionPersistenceToString(action_persistence);
  LOG_AF(buffer) << "is credit card section: " << is_credit_card << Br{};
  LOG_AF(buffer) << "is refill: " << is_refill << Br{};
  LOG_AF(buffer) << *form_structure << Br{};
  LOG_AF(buffer) << Tag{"table"};

  form_structure->RationalizePhoneNumbersInSection(
      autofill_trigger_field->section);

  // TODO(crbug/1203667#c9): Skip if the form has changed in the meantime, which
  // may happen with refills.
  if (action_persistence == mojom::ActionPersistence::kFill) {
    base::UmaHistogramBoolean(
        "Autofill.SkippingFormFillDueToChangedFieldCount",
        form_structure->field_count() != form.fields.size());
  }
  if (form_structure->field_count() != form.fields.size()) {
    LOG_AF(buffer)
        << Tr{} << "*"
        << "Skipped filling of form because the number of fields to be "
           "filled differs from the number of fields registered in the form "
           "cache."
        << CTag{"table"};
    LOG_AF(log_manager()) << LoggingScope::kFilling
                          << LogMessage::kSendFillingData << Br{}
                          << std::move(buffer);
    return;
  }

  if (action_persistence == mojom::ActionPersistence::kFill && !is_refill) {
    SetFillingContext(
        form_structure->global_id(),
        std::make_unique<FillingContext>(*autofill_trigger_field,
                                         profile_or_credit_card, optional_cvc));
  }

  // Only record the types that are filled for an eventual refill if all the
  // following are satisfied:
  //  The form is already filled.
  //  A refill has not been attempted for that form yet.
  //  This fill is not a refill attempt.
  FillingContext* filling_context =
      GetFillingContext(form_structure->global_id());
  bool could_attempt_refill = filling_context != nullptr &&
                              !filling_context->attempted_refill && !is_refill;

  // Contains those fields that BrowserAutofillManager can and wants to fill.
  // This is used for logging in CreditCardFormEventLogger.
  base::flat_set<FieldGlobalId> newly_filled_field_ids;
  newly_filled_field_ids.reserve(form_structure->field_count());

  // Log events on the field which triggers the Autofill suggestion.
  std::optional<FillEventId> fill_event_id;
  if (action_persistence == mojom::ActionPersistence::kFill) {
    std::string country_code;
    if (const autofill::AutofillProfile** address =
            absl::get_if<const AutofillProfile*>(&profile_or_credit_card)) {
      country_code = FetchCountryCodeFromProfile(*address);
    }

    TriggerFillFieldLogEvent trigger_fill_field_log_event =
        TriggerFillFieldLogEvent{
            .data_type = is_credit_card ? FillDataType::kCreditCard
                                        : FillDataType::kAutofillProfile,
            .associated_country_code = country_code,
            .timestamp = AutofillClock::Now()};

    autofill_trigger_field->AppendLogEventIfNotRepeated(
        trigger_fill_field_log_event);
    fill_event_id = trigger_fill_field_log_event.fill_event_id;
  }

  // Create a copy of the current form to fill and send to the renderer.
  FormData result_form = form;
  CHECK_EQ(result_form.fields.size(), form_structure->field_count());
  for (size_t i = 0; i < form_structure->field_count(); ++i) {
    // On the renderer, the section is used regardless of the autofill status.
    result_form.fields[i].section = form_structure->field(i)->section;
  }

  base::flat_map<FieldGlobalId, FieldFillingSkipReason> skip_reasons =
      GetFieldFillingSkipReasons(
          result_form, *form_structure, field, autofill_trigger_field->section,
          trigger_details.field_types_to_fill,
          filling_context ? &filling_context->type_groups_originally_filled
                          : nullptr,
          is_credit_card ? FillingProduct::kCreditCard
                         : FillingProduct::kAddress,
          /*skip_unrecognized_autocomplete_fields=*/
          trigger_details.trigger_source !=
              AutofillTriggerSource::kManualFallback,
          is_refill,
          is_credit_card && absl::get<const CreditCard*>(profile_or_credit_card)
                                ->IsExpired(AutofillClock::Now()));

  constexpr DenseSet<FieldFillingSkipReason> pre_ukm_logging_skips{
      FieldFillingSkipReason::kNotInFilledSection,
      FieldFillingSkipReason::kFormChanged,
      FieldFillingSkipReason::kNotFocused};
  for (size_t i = 0; i < result_form.fields.size(); ++i) {
    AutofillField* autofill_field = form_structure->field(i);

    if (!pre_ukm_logging_skips.contains(
            skip_reasons[autofill_field->global_id()]) &&
        !autofill_field->IsFocusable()) {
      form_interactions_ukm_logger()
          ->LogHiddenRepresentationalFieldSkipDecision(
              *form_structure, *autofill_field,
              !autofill_field->IsSelectElement());
    }
    const bool has_value_before = !result_form.fields[i].value.empty();
    // Log when the suggestion is selected and log on non-checkable fields that
    // skip filling.
    if (skip_reasons[autofill_field->global_id()] !=
        FieldFillingSkipReason::kNotSkipped) {
      LOG_AF(buffer) << Tr{} << base::StringPrintf("Field %zu", i)
                     << GetSkipFieldFillLogMessage(
                            skip_reasons[autofill_field->global_id()]);
      if (fill_event_id && !IsCheckable(autofill_field->check_status)) {
        // This lambda calculates a hash of the value Autofill would have used
        // if the field was skipped due to being pre-filled on page load. If the
        // field was not skipped due to being pre-filled, `std::nullopt` is
        // returned.
        const auto value_that_would_have_been_filled_in_a_prefilled_field_hash =
            [&]() -> std::optional<size_t> {
          if (skip_reasons[autofill_field->global_id()] ==
                  FieldFillingSkipReason::kValuePrefilled &&
              action_persistence == mojom::ActionPersistence::kFill &&
              !is_refill) {
            std::string failure_to_fill;
            const std::map<FieldGlobalId, std::u16string>& forced_fill_values =
                filling_context ? filling_context->forced_fill_values
                                : std::map<FieldGlobalId, std::u16string>();
            const FieldFillingData filling_content = GetFieldFillingData(
                *autofill_field, profile_or_credit_card, forced_fill_values,
                result_form.fields[i], optional_cvc ? *optional_cvc : u"",
                action_persistence, &failure_to_fill);
            if (!filling_content.value_to_fill.empty()) {
              return base::FastHash(
                  base::UTF16ToUTF8(filling_content.value_to_fill));
            }
          }
          return std::nullopt;
        };
        autofill_field->AppendLogEventIfNotRepeated(FillFieldLogEvent{
            .fill_event_id = *fill_event_id,
            .had_value_before_filling = ToOptionalBoolean(has_value_before),
            .autofill_skipped_status =
                skip_reasons[autofill_field->global_id()],
            .was_autofilled_before_security_policy = OptionalBoolean::kFalse,
            .had_value_after_filling = ToOptionalBoolean(has_value_before),
            .filling_method = AutofillFillingMethod::kNone,
            .value_that_would_have_been_filled_in_a_prefilled_field_hash =
                value_that_would_have_been_filled_in_a_prefilled_field_hash(),
        });
      }
      continue;
    }

    if (could_attempt_refill) {
      filling_context->type_groups_originally_filled.insert(
          autofill_field->Type().group());
    }

    // Must match ForEachMatchingFormField() in form_autofill_util.cc.
    // Only notify autofilling of empty fields and the field that initiated the
    // filling (note that <select> and <selectlist> controls may not be empty
    // but will still be autofilled).
    const bool should_notify =
        !is_credit_card &&
        (result_form.fields[i].SameFieldAs(field) ||
         result_form.fields[i].IsSelectOrSelectListElement() ||
         !has_value_before);
    std::string failure_to_fill;  // Reason for failing to fill.
    const std::map<FieldGlobalId, std::u16string>& forced_fill_values =
        filling_context ? filling_context->forced_fill_values
                        : std::map<FieldGlobalId, std::u16string>();

    // Fill the non-empty value from `profile_or_credit_card` into the
    // `result_form` form, which will be sent to the renderer.
    // FillField() may also fill a field if it had been autofilled or manually
    // filled before, and also returns true in such a case; however, such fields
    // don't reach this code.
    const bool is_newly_autofilled =
        FillField(*autofill_field, profile_or_credit_card, forced_fill_values,
                  result_form.fields[i], should_notify,
                  optional_cvc ? *optional_cvc : u"",
                  data_util::DetermineGroups(*form_structure),
                  action_persistence, &failure_to_fill);
    if (is_newly_autofilled)
      newly_filled_field_ids.insert(result_form.fields[i].global_id());

    const bool has_value_after = !result_form.fields[i].value.empty();
    const bool is_autofilled_before = form.fields[i].is_autofilled;
    const bool is_autofilled_after = result_form.fields[i].is_autofilled;

    // Log when the suggestion is selected and log on non-checkable fields that
    // have been filled.
    if (fill_event_id && !IsCheckable(autofill_field->check_status)) {
      autofill_field->AppendLogEventIfNotRepeated(FillFieldLogEvent{
          .fill_event_id = *fill_event_id,
          .had_value_before_filling = ToOptionalBoolean(has_value_before),
          .autofill_skipped_status = FieldFillingSkipReason::kNotSkipped,
          .was_autofilled_before_security_policy =
              ToOptionalBoolean(is_autofilled_after),
          .had_value_after_filling = ToOptionalBoolean(has_value_after),
          .filling_method = base::FeatureList::IsEnabled(
                                features::kAutofillGranularFillingAvailable)
                                ? GetFillingMethodFromTargetedFields(
                                      trigger_details.field_types_to_fill)
                                : AutofillFillingMethod::kFullForm,
      });
    }
    LOG_AF(buffer)
        << Tr{}
        << base::StringPrintf(
               "Field %zu Fillable - has value: %d->%d; autofilled: %d->%d. %s",
               i, has_value_before, has_value_after, is_autofilled_before,
               is_autofilled_after, failure_to_fill.c_str());

    if (!autofill_field->IsFocusable() && result_form.fields[i].is_autofilled) {
      AutofillMetrics::LogHiddenOrPresentationalSelectFieldsFilled();
    }
  }
  if (could_attempt_refill) {
    filling_context->filled_form = result_form;
  }
  auto field_types = base::MakeFlatMap<FieldGlobalId, FieldType>(
      *form_structure, {}, [](const auto& field) {
        return std::make_pair(field->global_id(),
                              field->Type().GetStorableType());
      });
  base::flat_set<FieldGlobalId> safe_fields =
      driver().ApplyFormAction(mojom::ActionType::kFill, action_persistence,
                               result_form, field.origin, field_types);

  // This will hold the fields (and autofill_fields) in the intersection of
  // safe_fields and newly_filled_fields_id.
  struct {
    std::vector<const FormFieldData*> old_values;
    std::vector<const FormFieldData*> new_values;
    std::vector<const AutofillField*> cached;
  } safe_newly_filled_fields;

  for (FieldGlobalId newly_filled_field_id : newly_filled_field_ids) {
    if (safe_fields.contains(newly_filled_field_id)) {
      // A safe field was filled. Both functions will not return a nullptr
      // because they passed the `FieldFillingSkipReason::kFormChanged`
      // condition.
      safe_newly_filled_fields.old_values.push_back(
          form.FindFieldByGlobalId(newly_filled_field_id));
      safe_newly_filled_fields.new_values.push_back(
          result_form.FindFieldByGlobalId(newly_filled_field_id));
      AutofillField* newly_filled_field =
          form_structure->GetFieldById(newly_filled_field_id);
      CHECK(newly_filled_field);
      safe_newly_filled_fields.cached.push_back(newly_filled_field);

      if (fill_event_id && !IsCheckable(newly_filled_field->check_status)) {
        // The field's last field log event should be a type of
        // FillFieldLogEvent. Record in this FillFieldLogEvent object that this
        // newly filled field was actually filled after checking the iframe
        // security policy.
        base::optional_ref<AutofillField::FieldLogEventType>
            last_field_log_event = newly_filled_field->last_field_log_event();
        CHECK(last_field_log_event.has_value());
        CHECK(
            absl::holds_alternative<FillFieldLogEvent>(*last_field_log_event));
        absl::get<FillFieldLogEvent>(*last_field_log_event)
            .filling_prevented_by_iframe_security_policy =
            OptionalBoolean::kFalse;
      }
      continue;
    }
    // Find and report index of fields that were not filled due to the iframe
    // security policy.
    auto it = base::ranges::find(result_form.fields, newly_filled_field_id,
                                 &FormFieldData::global_id);
    if (it != result_form.fields.end()) {
      size_t index = it - result_form.fields.begin();
      std::string field_number = base::StringPrintf("Field %zu", index);
      LOG_AF(buffer) << Tr{} << field_number
                     << "Actually did not fill field because of the iframe "
                        "security policy.";

      // Record in this AutofillField object's last FillFieldLogEvent object
      // that this field was actually not filled due to the iframe security
      // policy.
      AutofillField* not_filled_field =
          form_structure->GetFieldById(it->global_id());
      CHECK(not_filled_field);
      if (fill_event_id && !IsCheckable(not_filled_field->check_status)) {
        base::optional_ref<AutofillField::FieldLogEventType>
            last_field_log_event = not_filled_field->last_field_log_event();
        CHECK(last_field_log_event.has_value());
        CHECK(
            absl::holds_alternative<FillFieldLogEvent>(*last_field_log_event));
        absl::get<FillFieldLogEvent>(*last_field_log_event)
            .filling_prevented_by_iframe_security_policy =
            OptionalBoolean::kTrue;
      }
    }
  }

  // Save filling history to support undoing it later if needed.
  if (action_persistence == mojom::ActionPersistence::kFill) {
    form_autofill_history_.AddFormFillEntry(
        safe_newly_filled_fields.old_values, safe_newly_filled_fields.cached,
        is_credit_card ? FillingProduct::kCreditCard : FillingProduct::kAddress,
        is_refill);
  }

  LOG_AF(buffer) << CTag{"table"};
  LOG_AF(log_manager()) << LoggingScope::kFilling
                        << LogMessage::kSendFillingData << Br{}
                        << std::move(buffer);

  if (filling_context) {
    // When a new preview/fill starts, previously forced_fill_values should be
    // ignored the operation could be for a different card or address.
    filling_context->forced_fill_values.clear();
  }

  OnDidFillOrPreviewForm(
      action_persistence, *form_structure, *autofill_trigger_field,
      safe_newly_filled_fields.new_values, newly_filled_field_ids, safe_fields,
      profile_or_credit_card, trigger_details, is_refill);
}

void BrowserAutofillManager::OnDidFillOrPreviewForm(
    mojom::ActionPersistence action_persistence,
    const FormStructure& form_structure,
    const AutofillField& trigger_autofill_field,
    base::span<const FormFieldData*> safe_filled_fields,
    const base::flat_set<FieldGlobalId>& filled_fields,
    const base::flat_set<FieldGlobalId>& safe_fields,
    absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card,
    const AutofillTriggerDetails& trigger_details,
    bool is_refill) {
  client().DidFillOrPreviewForm(action_persistence,
                                trigger_details.trigger_source, is_refill);
  NotifyObservers(&Observer::OnFillOrPreviewDataModelForm,
                  form_structure.global_id(), action_persistence,
                  safe_filled_fields, profile_or_credit_card);
  if (action_persistence == mojom::ActionPersistence::kPreview) {
    return;
  }
  CHECK_EQ(action_persistence, mojom::ActionPersistence::kFill);

  autofilled_form_signatures_.push_front(form_structure.form_signature());
  // Only remember the last few forms that we've seen, both to avoid false
  // positives and to avoid wasting memory.
  if (autofilled_form_signatures_.size() > kMaxRecentFormSignaturesToRemember) {
    autofilled_form_signatures_.pop_back();
  }
  if (absl::holds_alternative<const CreditCard*>(profile_or_credit_card)) {
    // The originally selected masked card is `credit_card_`. So we must log
    // `credit_card_` as opposed to
    // `absl::get<CreditCard*>(profile_or_credit_card)` to correctly indicate
    // whether the user filled the form using a masked card suggestion.
    is_refill ? credit_card_form_event_logger_->OnDidRefill(
                    signin_state_for_metrics_, form_structure)
              : credit_card_form_event_logger_->OnDidFillSuggestion(
                    credit_card_, form_structure, trigger_autofill_field,
                    filled_fields, safe_fields, signin_state_for_metrics_,
                    trigger_details.trigger_source);
  } else {
    CHECK(absl::holds_alternative<const AutofillProfile*>(
        profile_or_credit_card));
    if (!trigger_autofill_field
             .ShouldSuppressSuggestionsAndFillingByDefault()) {
      is_refill
          ? address_form_event_logger_->OnDidRefill(signin_state_for_metrics_,
                                                    form_structure)
          : address_form_event_logger_->OnDidFillSuggestion(
                *absl::get<const AutofillProfile*>(profile_or_credit_card),
                form_structure, trigger_autofill_field,
                signin_state_for_metrics_, trigger_details.trigger_source);
    } else if (!is_refill) {
      autocomplete_unrecognized_fallback_logger_->OnDidFillSuggestion();
    }
  }
  if (!is_refill) {
    // Note that this may invalidate `profile_or_credit_card`.
    client().GetPersonalDataManager()->RecordUseOf(profile_or_credit_card);
  }
}

std::unique_ptr<FormStructure> BrowserAutofillManager::ValidateSubmittedForm(
    const FormData& form) {
  // Ignore forms not present in our cache.  These are typically forms with
  // wonky JavaScript that also makes them not auto-fillable.
  FormStructure* cached_submitted_form = FindCachedFormById(form.global_id());
  if (!cached_submitted_form || !ShouldUploadForm(*cached_submitted_form)) {
    return nullptr;
  }

  auto submitted_form = std::make_unique<FormStructure>(form);
  submitted_form->RetrieveFromCache(
      *cached_submitted_form,
      FormStructure::RetrieveFromCacheReason::kFormImport);

  return submitted_form;
}

AutofillField* BrowserAutofillManager::GetAutofillField(
    const FormData& form,
    const FormFieldData& field) const {
  if (!client().GetPersonalDataManager()) {
    return nullptr;
  }

  FormStructure* form_structure = nullptr;
  AutofillField* autofill_field = nullptr;
  if (!GetCachedFormAndField(form, field, &form_structure, &autofill_field))
    return nullptr;

  if (!form_structure->IsAutofillable())
    return nullptr;

  return autofill_field;
}

void BrowserAutofillManager::OnCreditCardFetchedSuccessfully(
    const CreditCard& credit_card) {
  last_unlocked_credit_card_cvc_ = credit_card.cvc();
  // If the synced down card is a virtual card, let the client know so that it
  // can show the UI to help user to manually fill the form, if needed.
  if (credit_card.record_type() == CreditCard::RecordType::kVirtualCard) {
    DCHECK(!credit_card.cvc().empty());
    client().GetFormDataImporter()->CacheFetchedVirtualCard(
        credit_card.LastFourDigits());

    VirtualCardManualFallbackBubbleOptions options;
    options.masked_card_name = credit_card.CardNameForAutofillDisplay();
    options.masked_card_number_last_four =
        credit_card.ObfuscatedNumberWithVisibleLastFourDigits();
    options.virtual_card = credit_card;
    // TODO(crbug.com/1473481): Remove CVC from
    // VirtualCardManualFallbackBubbleOptions.
    options.virtual_card_cvc = credit_card.cvc();
    options.card_image = GetCardImage(credit_card);
    client().OnVirtualCardDataAvailable(options);
  }

  // After a server card is fetched, save its instrument id.
  client().GetFormDataImporter()->SetFetchedCardInstrumentId(
      credit_card.instrument_id());

  if (credit_card.record_type() == CreditCard::RecordType::kFullServerCard ||
      credit_card.record_type() == CreditCard::RecordType::kVirtualCard) {
    credit_card_access_manager_->CacheUnmaskedCardInfo(credit_card,
                                                       credit_card.cvc());
  }
}

std::vector<Suggestion> BrowserAutofillManager::GetProfileSuggestions(
    const FormData& form,
    const FormStructure* form_structure,
    const FormFieldData& trigger_field,
    const AutofillField* trigger_autofill_field,
    AutofillSuggestionTriggerSource trigger_source) const {
  address_form_event_logger_->OnDidPollSuggestions(trigger_field,
                                                   signin_state_for_metrics_);
  const bool triggering_field_is_not_address_field =
      !form_structure ||
      (trigger_autofill_field &&
       !IsAddressType(trigger_autofill_field->Type().GetStorableType()));

  if (triggering_field_is_not_address_field) {
    // Since Autofill was triggered from a field that is not classified as
    // address, we consider the `field_types` (i.e, the fields found in the
    // "form") to be a single unclassified field. Note that in this flow it is
    // not used and only holds semantic value.
    return suggestion_generator_->GetSuggestionsForProfiles(
        /*field_types=*/{UNKNOWN_TYPE}, trigger_field, UNKNOWN_TYPE,
        /*last_targeted_fields=*/absl::nullopt, trigger_source);
  }
  // If not manual fallback, `form_structure` and `autofill_field` should exist.
  CHECK(form_structure && trigger_autofill_field);
  std::optional<FieldTypeSet> last_address_fields_to_fill_for_section =
      external_delegate_->GetLastFieldTypesToFillForSection(
          trigger_autofill_field->section);
  // Getting the filling-relevant fields so that suggestions are based only on
  // those fields. Function BrowserAutofillManager::GetFieldFillingSkipReasons
  // assumes that the passed FormData and FormStructure have the same size. If
  // it's not the case we just assume as a fallback that all fields are
  // relevant.
  base::flat_map<FieldGlobalId, FieldFillingSkipReason> skip_reasons =
      form.fields.size() == form_structure->field_count()
          ? GetFieldFillingSkipReasons(
                form, *form_structure, trigger_field,
                trigger_autofill_field->section,
                last_address_fields_to_fill_for_section
                    ? GetTargetServerFieldsForTypeAndLastTargetedFields(
                          *last_address_fields_to_fill_for_section,
                          trigger_autofill_field->Type().GetStorableType())
                    : kAllFieldTypes,
                /*optional_type_groups_originally_filled=*/nullptr,
                FillingProduct::kAddress,
                /*skip_unrecognized_autocomplete_fields=*/trigger_source !=
                    AutofillSuggestionTriggerSource::kManualFallbackAddress,
                /*is_refill=*/false, /*is_expired_credit_card=*/false)
          : base::flat_map<FieldGlobalId, FieldFillingSkipReason>();
  FieldTypeSet field_types;
  for (size_t i = 0; i < form_structure->field_count(); ++i) {
    const AutofillField* autofill_field = form_structure->field(i);
    auto it = skip_reasons.find(autofill_field->global_id());
    if (it == skip_reasons.end() ||
        it->second == FieldFillingSkipReason::kNotSkipped) {
      field_types.insert(autofill_field->Type().GetStorableType());
    }
  }
  return suggestion_generator_->GetSuggestionsForProfiles(
      field_types, trigger_field,
      trigger_autofill_field->Type().GetStorableType(),
      last_address_fields_to_fill_for_section, trigger_source);
}

std::vector<Suggestion> BrowserAutofillManager::GetCreditCardSuggestions(
    const FormData& form,
    const FormFieldData& trigger_field,
    FieldType trigger_field_type,
    AutofillSuggestionTriggerSource trigger_source,
    bool& should_display_gpay_logo) const {
  credit_card_form_event_logger_->OnDidPollSuggestions(
      trigger_field, signin_state_for_metrics_);

  std::vector<Suggestion> suggestions;
  bool with_offer = false;
  bool with_cvc = false;
  bool is_virtual_card_standalone_cvc_field = false;
  autofill_metrics::CardMetadataLoggingContext context;
  if (!IsInAutofillSuggestionsDisabledExperiment()) {
    if (trigger_field_type == CREDIT_CARD_STANDALONE_VERIFICATION_CODE &&
        !four_digit_combinations_in_dom_.empty()) {
      base::flat_map<std::string, VirtualCardUsageData::VirtualCardLastFour>
          virtual_card_guid_to_last_four_map =
              GetVirtualCreditCardsForStandaloneCvcField(trigger_field.origin);
      if (!virtual_card_guid_to_last_four_map.empty()) {
        suggestions =
            suggestion_generator_->GetSuggestionsForVirtualCardStandaloneCvc(
                context, virtual_card_guid_to_last_four_map);
        is_virtual_card_standalone_cvc_field = true;
        // Always display GPay logo for virtual card suggestions.
        should_display_gpay_logo = true;
      }
    } else {
      suggestions = suggestion_generator_->GetSuggestionsForCreditCards(
          trigger_field, trigger_field_type,
          ShouldShowScanCreditCard(form, trigger_field),
          ShouldShowCardsFromAccountOption(form, trigger_field, trigger_source),
          should_display_gpay_logo, with_offer, with_cvc, context);
    }
  }

  credit_card_form_event_logger_->OnDidFetchSuggestion(
      suggestions, with_offer, with_cvc, is_virtual_card_standalone_cvc_field,
      context);
  return suggestions;
}

base::flat_map<std::string, VirtualCardUsageData::VirtualCardLastFour>
BrowserAutofillManager::GetVirtualCreditCardsForStandaloneCvcField(
    const url::Origin& origin) const {
  base::flat_map<std::string, VirtualCardUsageData::VirtualCardLastFour>
      virtual_card_guid_to_last_four_map;
  const std::vector<CreditCard*> cards =
      client().GetPersonalDataManager()->GetCreditCards();
  const std::vector<VirtualCardUsageData*> usage_data =
      client().GetPersonalDataManager()->GetVirtualCardUsageData();

  for (const CreditCard* credit_card : cards) {
    // As we only provide virtual card suggestions for standalone CVC fields,
    // check if the card is an enrolled virtual card.
    if (credit_card->virtual_card_enrollment_state() !=
        CreditCard::VirtualCardEnrollmentState::kEnrolled) {
      continue;
    }
    // Check if card has virtual card usage data on the url origin.
    auto usage_data_iter = base::ranges::find_if(
        usage_data,
        [&origin, &credit_card](VirtualCardUsageData* virtual_card_usage_data) {
          return virtual_card_usage_data->instrument_id().value() ==
                     credit_card->instrument_id() &&
                 virtual_card_usage_data->merchant_origin() == origin;
        });

    // If card has eligible usage data, check if last four is in the url DOM.
    if (usage_data_iter != usage_data.end()) {
      VirtualCardUsageData::VirtualCardLastFour virtual_card_last_four =
          (*usage_data_iter)->virtual_card_last_four();
      if (base::Contains(four_digit_combinations_in_dom_,
                         base::UTF16ToUTF8(virtual_card_last_four.value()))) {
        // Card has usage data on webpage and last four is present in DOM.
        virtual_card_guid_to_last_four_map.insert(
            {credit_card->guid(), virtual_card_last_four});
      }
    }
  }
  return virtual_card_guid_to_last_four_map;
}
#endif  // !BUILDFLAG_IS_QTWEBENGINE)

// TODO(crbug.com/1309848) Eliminate and replace with a listener?
// Should we do the same with all the other BrowserAutofillManager events?
void BrowserAutofillManager::OnBeforeProcessParsedForms() {
#if !BUILDFLAG(IS_QTWEBENGINE)
  has_parsed_forms_ = true;

  // Record the current sync state to be used for metrics on this page.
  signin_state_for_metrics_ =
      client().GetPersonalDataManager()->GetPaymentsSigninStateForMetrics();

  // Setup the url for metrics that we will collect for this form.
  form_interactions_ukm_logger()->OnFormsParsed(client().GetUkmSourceId());
#endif
}

void BrowserAutofillManager::OnFormProcessed(
    const FormData& form,
    const FormStructure& form_structure) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  // If a standalone cvc field is found in the form, query the DOM for last four
  // combinations. Used to search for the virtual card last four for a virtual
  // card saved on file of a merchant webpage.
  if (base::FeatureList::IsEnabled(
          features::kAutofillParseVcnCardOnFileStandaloneCvcFields)) {
    auto contains_standalone_cvc_field =
        base::ranges::any_of(form_structure.fields(), [](const auto& field) {
          return field->Type().GetStorableType() ==
                 CREDIT_CARD_STANDALONE_VERIFICATION_CODE;
        });
    if (contains_standalone_cvc_field) {
      FetchPotentialCardLastFourDigitsCombinationFromDOM();
    }
  }
  if (data_util::ContainsPhone(data_util::DetermineGroups(form_structure))) {
    has_observed_phone_number_field_ = true;
  }
  // TODO(crbug.com/869482): avoid logging developer engagement multiple
  // times for a given form if it or other forms on the page are dynamic.
  LogDeveloperEngagementUkm(client().GetUkmRecorder(),
                            client().GetUkmSourceId(), form_structure);

  for (const auto& field : form_structure) {
    if (field->Type().html_type() == HtmlFieldType::kOneTimeCode) {
      has_observed_one_time_code_field_ = true;
      break;
    }
  }
  // Log the type of form that was parsed.
  DenseSet<FormType> form_types = form_structure.GetFormTypes();
  bool card_form = base::Contains(form_types, FormType::kCreditCardForm);
  bool address_form = base::Contains(form_types, FormType::kAddressForm);
  if (card_form) {
    credit_card_form_event_logger_->OnDidParseForm(form_structure);
  }
  if (address_form) {
    address_form_event_logger_->OnDidParseForm(form_structure);
  }
  // `autofill_optimization_guide_` is not present on unsupported platforms.
  if (auto* autofill_optimization_guide =
          client().GetAutofillOptimizationGuide()) {
    // Initiate necessary pre-processing based on the forms and fields that are
    // parsed, as well as the information that the user has saved in the web
    // database based on `client().GetPersonalDataManager()`.
    autofill_optimization_guide->OnDidParseForm(
        form_structure, client().GetPersonalDataManager());
  }
  // If a form with the same FormGlobalId was previously filled, the structure
  // of the form changed, and there has not been a refill attempt on that form
  // yet, start the process of triggering a refill.
  if (ShouldTriggerRefill(form_structure, RefillTriggerReason::kFormChanged)) {
    ScheduleRefill(form, form_structure,
                   {.trigger_source = AutofillTriggerSource::kFormsSeen});
  }
#else
  NOTREACHED();
#endif  // !BUILDFLAG(IS_QTWEBENGINE)
}

void BrowserAutofillManager::OnAfterProcessParsedForms(
    const DenseSet<FormType>& form_types) {
#if !BUILDFLAG(IS_QTWEBENGINE)
  AutofillMetrics::LogUserHappinessMetric(
      AutofillMetrics::FORMS_LOADED, form_types,
      client().GetSecurityLevelForUmaHistograms(),
      /*profile_form_bitmask=*/0);
#else
  NOTREACHED();
#endif
}

#if !BUILDFLAG(IS_QTWEBENGINE)
void BrowserAutofillManager::UpdateInitialInteractionTimestamp(
    const TimeTicks& interaction_timestamp) {
  if (initial_interaction_timestamp_.is_null() ||
      interaction_timestamp < initial_interaction_timestamp_) {
    initial_interaction_timestamp_ = interaction_timestamp;
  }
}

// static
void BrowserAutofillManager::DeterminePossibleFieldTypesForUpload(
    const std::vector<AutofillProfile>& profiles,
    const std::vector<CreditCard>& credit_cards,
    const std::u16string& last_unlocked_credit_card_cvc,
    const std::string& app_locale,
    bool observed_submission,
    FormStructure* form) {
  // Temporary helper structure for measuring the impact of
  // autofill::features::kAutofillVoteForSelectOptionValues.
  // TODO(crbug.com/1395740) Remove this once the feature has settled.
  struct AutofillVoteForSelectOptionValuesMetrics {
    // Whether kAutofillVoteForSelectOptionValues classified more fields
    // than the original version of this function w/o
    // kAutofillVoteForSelectOptionValuesMetrics.
    bool classified_more_field_types = false;
    // Whether any field types were detected and assigned to fields for the
    // current form.
    bool classified_any_field_types = false;
    // Whether any field was classified as a country field.
    bool classified_field_as_country_field = false;
    // Whether any <select> element was reclassified from a country field
    // to a phone country code field due to
    // kAutofillVoteForSelectOptionValuesMetrics.
    bool switched_from_country_to_phone_country_code = false;
  } metrics;

  // For each field in the |form|, extract the value.  Then for each
  // profile or credit card, identify any stored types that match the value.
  for (size_t i = 0; i < form->field_count(); ++i) {
    AutofillField* field = form->field(i);
    if (!field->possible_types().empty() && field->IsEmpty()) {
      // This is a password field in a sign-in form. Skip checking its type
      // since |field->value| is not set.
      DCHECK_EQ(1u, field->possible_types().size());
      DCHECK_EQ(PASSWORD, *field->possible_types().begin());
      continue;
    }

    FieldTypeSet matching_types;
    std::u16string value;
    base::TrimWhitespace(field->value, base::TRIM_ALL, &value);

    // Consider the textual values of <select> element <option>s as well.
    // If a phone country code <select> element looks as follows:
    // <select> <option value="US">+1</option> </select>
    // We want to consider the <option>'s content ("+1") to classify this as a
    // PHONE_HOME_COUNTRY_CODE field. It is insufficient to just consider the
    // <option>'s value ("US").
    std::optional<std::u16string> select_content;
    // TODO(crbug.com/1395740) Remove the flag check once the feature has
    // settled.
    if (field->IsSelectOrSelectListElement() &&
        base::FeatureList::IsEnabled(
            features::kAutofillVoteForSelectOptionValues)) {
      auto it = base::ranges::find(field->options, field->value,
                                   &SelectOption::value);
      if (it != field->options.end()) {
        select_content = it->content;
        base::TrimWhitespace(*select_content, base::TRIM_ALL, &*select_content);
      }
    }

    for (const AutofillProfile& profile : profiles) {
      profile.GetMatchingTypes(value, app_locale, &matching_types);
      if (select_content) {
        FieldTypeSet matching_types_backup = matching_types;
        profile.GetMatchingTypes(*select_content, app_locale, &matching_types);
        if (matching_types_backup != matching_types)
          metrics.classified_more_field_types = true;
      }
    }

    // TODO(crbug/880531) set possible_types_validities for credit card too.
    for (const CreditCard& card : credit_cards) {
      card.GetMatchingTypes(value, app_locale, &matching_types);
      if (select_content) {
        FieldTypeSet matching_types_backup = matching_types;
        card.GetMatchingTypes(*select_content, app_locale, &matching_types);
        if (matching_types_backup != matching_types)
          metrics.classified_more_field_types = true;
      }
    }

    // In case a select element has options like this
    //  <option value="US">+1</option>,
    // meaning that it contains a phone country code, we treat that as
    // sufficient evidence to only vote for phone country code.
    if (matching_types.contains(ADDRESS_HOME_COUNTRY))
      metrics.classified_field_as_country_field = true;
    if (select_content && matching_types.contains(ADDRESS_HOME_COUNTRY) &&
        MatchesRegex<kAugmentedPhoneCountryCodeRe>(*select_content)) {
      matching_types.erase(ADDRESS_HOME_COUNTRY);
      matching_types.insert(PHONE_HOME_COUNTRY_CODE);
      metrics.switched_from_country_to_phone_country_code = true;
    }

    if (field->state_is_a_matching_type())
      matching_types.insert(ADDRESS_HOME_STATE);

    if (!matching_types.empty())
      metrics.classified_any_field_types = true;

    if (matching_types.empty()) {
      matching_types.insert(UNKNOWN_TYPE);
      FieldTypeValidityStateMap matching_types_validities;
      matching_types_validities[UNKNOWN_TYPE] =
          AutofillDataModel::ValidityState::kUnvalidated;
      field->add_possible_types_validities(matching_types_validities);
    }

    field->set_possible_types(matching_types);
  }

  // As CVCs are not stored, run special heuristics to detect CVC-like values.
  AutofillField* cvc_field =
      GetBestPossibleCVCFieldForUpload(*form, last_unlocked_credit_card_cvc);
  if (cvc_field) {
    FieldTypeSet possible_types = cvc_field->possible_types();
    possible_types.erase(UNKNOWN_TYPE);
    possible_types.insert(CREDIT_CARD_VERIFICATION_CODE);
    cvc_field->set_possible_types(possible_types);
  }

  if (observed_submission && metrics.classified_any_field_types) {
    enum class Bucket {
      kClassifiedAnyField = 0,
      kClassifiedMoreFields = 1,
      kClassifiedFieldAsCountryField = 2,
      kSwitchedFromCountryToPhoneCountryCode = 3,
      kMaxValue = 3
    };
    base::UmaHistogramEnumeration("Autofill.VoteForSelecteOptionValues",
                                  Bucket::kClassifiedAnyField);
    if (metrics.classified_more_field_types) {
      base::UmaHistogramEnumeration("Autofill.VoteForSelecteOptionValues",
                                    Bucket::kClassifiedMoreFields);
    }
    if (metrics.classified_field_as_country_field) {
      base::UmaHistogramEnumeration("Autofill.VoteForSelecteOptionValues",
                                    Bucket::kClassifiedFieldAsCountryField);
    }
    if (metrics.switched_from_country_to_phone_country_code) {
      base::UmaHistogramEnumeration(
          "Autofill.VoteForSelecteOptionValues",
          Bucket::kSwitchedFromCountryToPhoneCountryCode);
    }
  }

  DisambiguateUploadTypes(form);
}

// static
void BrowserAutofillManager::DisambiguateUploadTypes(FormStructure* form) {
  for (size_t i = 0; i < form->field_count(); ++i) {
    AutofillField* field = form->field(i);
    const FieldTypeSet& upload_types = field->possible_types();

    // In case for credit cards and names there are many other possibilities
    // because a field can be of type NAME_FULL, NAME_LAST,
    // NAME_LAST_FIRST/SECOND at the same time.
    // Also, a single line street address is ambiguous to address line 1.
    // However, this case is handled on the server and here only the name
    // disambiguation for address and credit card related name fields is
    // performed.

    // Disambiguation is only applicable if there is a mixture of one or more
    // address related name fields and exactly one credit card related name
    // field.
    const size_t credit_card_type_count =
        NumberOfPossibleFieldTypesInGroup(*field, FieldTypeGroup::kCreditCard);
    const size_t name_type_count =
        NumberOfPossibleFieldTypesInGroup(*field, FieldTypeGroup::kName);
    if (upload_types.size() == (credit_card_type_count + name_type_count) &&
        credit_card_type_count == 1 && name_type_count >= 1) {
      DisambiguateNameUploadTypes(form, i, upload_types);
    }
  }
}

// static
void BrowserAutofillManager::DisambiguateNameUploadTypes(
    FormStructure* form,
    size_t current_index,
    const FieldTypeSet& upload_types) {
  // This case happens when both a profile and a credit card have the same
  // name, and when we have exactly two possible types.

  // If the ambiguous field has either a previous or next field that is
  // not name related, use that information to determine whether the field
  // is a name or a credit card name.
  // If the ambiguous field has both a previous or next field that is not
  // name related, if they are both from the same group, use that group to
  // decide this field's type. Otherwise, there is no safe way to
  // disambiguate.

  // Look for a previous non name related field.
  bool has_found_previous_type = false;
  bool is_previous_credit_card = false;
  size_t index = current_index;
  while (index != 0 && !has_found_previous_type) {
    --index;
    AutofillField* prev_field = form->field(index);
    if (!IsNameType(*prev_field)) {
      has_found_previous_type = true;
      is_previous_credit_card =
          prev_field->Type().group() == FieldTypeGroup::kCreditCard;
    }
  }

  // Look for a next non name related field.
  bool has_found_next_type = false;
  bool is_next_credit_card = false;
  index = current_index;
  while (++index < form->field_count() && !has_found_next_type) {
    AutofillField* next_field = form->field(index);
    if (!IsNameType(*next_field)) {
      has_found_next_type = true;
      is_next_credit_card =
          next_field->Type().group() == FieldTypeGroup::kCreditCard;
    }
  }

  // At least a previous or next field type must have been found in order to
  // disambiguate this field.
  if (has_found_previous_type || has_found_next_type) {
    // If both a previous type and a next type are found and not from the same
    // name group there is no sure way to disambiguate.
    if (has_found_previous_type && has_found_next_type &&
        (is_previous_credit_card != is_next_credit_card)) {
      return;
    }

    // Otherwise, use the previous (if it was found) or next field group to
    // decide whether the field is a name or a credit card name.
    if (has_found_previous_type) {
      SelectRightNameType(form->field(current_index), is_previous_credit_card);
    } else {
      SelectRightNameType(form->field(current_index), is_next_credit_card);
    }
  }
}

BrowserAutofillManager::FieldFillingData
BrowserAutofillManager::GetFieldFillingData(
    const AutofillField& autofill_field,
    const absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card,
    const std::map<FieldGlobalId, std::u16string>& forced_fill_values,
    const FormFieldData& field_data,
    const std::u16string& cvc,
    mojom::ActionPersistence action_persistence,
    std::string* failure_to_fill) {
  auto it = forced_fill_values.find(field_data.global_id());
  bool value_is_an_override = it != forced_fill_values.end();
  const auto& [value_to_fill, filling_type] =
      value_is_an_override
          ? std::make_pair(it->second, autofill_field.Type().GetStorableType())
      : absl::holds_alternative<const AutofillProfile*>(profile_or_credit_card)
          ? GetFillingValueAndTypeForProfile(
                *absl::get<const AutofillProfile*>(profile_or_credit_card),
                app_locale_, autofill_field.Type(), field_data,
                client().GetAddressNormalizer(), failure_to_fill)
          : std::make_pair(
                GetFillingValueForCreditCard(
                    *absl::get<const CreditCard*>(profile_or_credit_card), cvc,
                    app_locale_, action_persistence, autofill_field,
                    failure_to_fill),
                autofill_field.Type().GetStorableType());
  return {value_to_fill, filling_type, value_is_an_override};
}

bool BrowserAutofillManager::FillField(
    AutofillField& autofill_field,
    absl::variant<const AutofillProfile*, const CreditCard*>
        profile_or_credit_card,
    const std::map<FieldGlobalId, std::u16string>& forced_fill_values,
    FormFieldData& field_data,
    bool should_notify,
    const std::u16string& cvc,
    uint32_t profile_form_bitmask,
    mojom::ActionPersistence action_persistence,
    std::string* failure_to_fill) {
  const FieldFillingData filling_content = GetFieldFillingData(
      autofill_field, profile_or_credit_card, forced_fill_values, field_data,
      cvc, action_persistence, failure_to_fill);

  // Do not attempt to fill empty values as it would skew the metrics.
  if (filling_content.value_to_fill.empty()) {
    if (failure_to_fill) {
      *failure_to_fill += "No value to fill available. ";
    }
    return false;
  }
  field_data.value = filling_content.value_to_fill;
  field_data.force_override = filling_content.value_is_an_override;

  if (failure_to_fill) {
    *failure_to_fill = "Decided to fill";
  }
  if (action_persistence == mojom::ActionPersistence::kFill) {
    // Mark the cached field as autofilled, so that we can detect when a
    // user edits an autofilled field (for metrics).
    autofill_field.is_autofilled = true;
    if (const AutofillProfile** profile =
            absl::get_if<const AutofillProfile*>(&profile_or_credit_card)) {
      autofill_field.set_autofill_source_profile_guid((*profile)->guid());
    }
    autofill_field.set_autofilled_type(filling_content.field_type);
  }

  // Mark the field as autofilled when a non-empty value is assigned to
  // it. This allows the renderer to distinguish autofilled fields from
  // fields with non-empty values, such as select-one fields.
  field_data.is_autofilled = true;
  AutofillMetrics::LogUserHappinessMetric(
      AutofillMetrics::FIELD_WAS_AUTOFILLED, autofill_field.Type().group(),
      client().GetSecurityLevelForUmaHistograms(), profile_form_bitmask);

  if (should_notify) {
    DCHECK(absl::holds_alternative<const AutofillProfile*>(
        profile_or_credit_card));
    const AutofillProfile* profile =
        absl::get<const AutofillProfile*>(profile_or_credit_card);
    client().DidFillOrPreviewField(
        /*autofilled_value=*/profile->GetInfo(autofill_field.Type(),
                                              app_locale_),
        /*profile_full_name=*/profile->GetInfo(AutofillType(NAME_FULL),
                                               app_locale_));
  }
  return true;
}

void BrowserAutofillManager::SetFillingContext(
    FormGlobalId form_id,
    std::unique_ptr<FillingContext> context) {
  filling_context_[form_id] = std::move(context);
}

BrowserAutofillManager::FillingContext*
BrowserAutofillManager::GetFillingContext(FormGlobalId form_id) {
  auto it = filling_context_.find(form_id);
  return it != filling_context_.end() ? it->second.get() : nullptr;
}

bool BrowserAutofillManager::ShouldTriggerRefill(
    const FormStructure& form_structure,
    RefillTriggerReason refill_trigger_reason) {
  // Should not refill if a form with the same FormGlobalId that has not been
  // filled before.
  FillingContext* filling_context =
      GetFillingContext(form_structure.global_id());
  if (filling_context == nullptr) {
    return false;
  }

  // Confirm that the form changed by running a DeepEqual check on the filled
  // form and the received form. Other trigger reasons do not need this check
  // since they do not depend on the form changing.
  if (refill_trigger_reason == RefillTriggerReason::kFormChanged &&
      filling_context->filled_form &&
      FormData::DeepEqual(form_structure.ToFormData(),
                          *filling_context->filled_form)) {
    return false;
  }

  base::TimeTicks now = base::TimeTicks::Now();
  base::TimeDelta delta = now - filling_context->original_fill_time;

  return !filling_context->attempted_refill && delta < limit_before_refill_;
}

void BrowserAutofillManager::ScheduleRefill(
    const FormData& form,
    const FormStructure& form_structure,
    const AutofillTriggerDetails& trigger_details) {
  FillingContext* filling_context =
      GetFillingContext(form_structure.global_id());
  DCHECK(filling_context != nullptr);
  // If a timer for the refill was already running, it means the form
  // changed again. Stop the timer and start it again.
  if (filling_context->on_refill_timer.IsRunning()) {
    filling_context->on_refill_timer.AbandonAndStop();
  }
  // Start a new timer to trigger refill.
  filling_context->on_refill_timer.Start(
      FROM_HERE, kWaitTimeForDynamicForms,
      base::BindRepeating(&BrowserAutofillManager::TriggerRefill,
                          weak_ptr_factory_.GetWeakPtr(), form,
                          trigger_details));
}

void BrowserAutofillManager::TriggerRefill(
    const FormData& form,
    const AutofillTriggerDetails& trigger_details) {
  FormStructure* form_structure = FindCachedFormById(form.global_id());
  if (!form_structure) {
    return;
  }
  FillingContext* filling_context =
      GetFillingContext(form_structure->global_id());
  DCHECK(filling_context);

  // The refill attempt can happen from different paths, some of which happen
  // after waiting for a while. Therefore, although this condition has been
  // checked prior to calling TriggerRefill, it may not hold, when we get
  // here.
  if (filling_context->attempted_refill) {
    return;
  }
  filling_context->attempted_refill = true;

  // Try to find the field from which the original fill originated.
  // The precedence for the look up is the following:
  //  - focusable `filled_field_id`
  //  - focusable `filled_field_signature`
  //  - non-focusable `filled_field_id`
  //  - non-focusable `filled_field_signature`
  // and prefer newer renderer ids.
  auto comparison_attributes =
      [&](const std::unique_ptr<AutofillField>& field) {
        return std::make_tuple(
            field->origin == filling_context->filled_origin,
            field->IsFocusable(),
            field->global_id() == filling_context->filled_field_id,
            field->GetFieldSignature() ==
                filling_context->filled_field_signature,
            field->unique_renderer_id);
      };
  auto it =
      base::ranges::max_element(*form_structure, {}, comparison_attributes);
  AutofillField* autofill_field =
      it != form_structure->end() ? it->get() : nullptr;
  bool found_matching_element =
      autofill_field &&
      autofill_field->origin == filling_context->filled_origin &&
      (autofill_field->global_id() == filling_context->filled_field_id ||
       autofill_field->GetFieldSignature() ==
           filling_context->filled_field_signature);
  if (!found_matching_element) {
    return;
  }
  FormFieldData field = *autofill_field;
  if (absl::holds_alternative<std::pair<CreditCard, std::u16string>>(
          filling_context->profile_or_credit_card_with_cvc)) {
    const auto& [credit_card, cvc] =
        absl::get<std::pair<CreditCard, std::u16string>>(
            filling_context->profile_or_credit_card_with_cvc);
    FillOrPreviewDataModelForm(mojom::ActionPersistence::kFill, form, field,
                               &credit_card, &cvc, form_structure,
                               autofill_field, trigger_details,
                               /*is_refill=*/true);
  } else if (absl::holds_alternative<AutofillProfile>(
                 filling_context->profile_or_credit_card_with_cvc)) {
    FillOrPreviewDataModelForm(
        mojom::ActionPersistence::kFill, form, field,
        &absl::get<AutofillProfile>(
            filling_context->profile_or_credit_card_with_cvc),
        /*optional_cvc=*/nullptr, form_structure, autofill_field,
        trigger_details,
        /*is_refill=*/true);
  } else {
    NOTREACHED();
  }
}

void BrowserAutofillManager::GetAvailableSuggestions(
    const FormData& form,
    const FormFieldData& field,
    AutofillSuggestionTriggerSource trigger_source,
    std::vector<Suggestion>* suggestions,
    SuggestionsContext* context) {
  DCHECK(suggestions);
  DCHECK(context);

  // This trigger source is only relevant for Compose, for which suggestions
  // are not populated here.
  if (trigger_source ==
      AutofillSuggestionTriggerSource::kTextareaFocusedWithoutClick) {
    return;
  }

  // Need to refresh models before using the form_event_loggers.
  RefreshDataModels();

  bool got_autofillable_form =
      GetCachedFormAndField(form, field, &context->form_structure,
                            &context->focused_field) &&
      // Don't send suggestions or track forms that should not be parsed.
      context->form_structure->ShouldBeParsed();

  if (!ShouldShowSuggestionsForAutocompleteUnrecognizedFields(trigger_source) &&
      got_autofillable_form &&
      context->focused_field->ShouldSuppressSuggestionsAndFillingByDefault()) {
    // Pre-`AutofillPredictionsForAutocompleteUnrecognized`, autocomplete
    // suggestions were shown if all types of the form were suppressed or
    // unknown. If at least a single field had predictions (and the form was
    // thus considered autofillable), autocomplete suggestions were suppressed
    // for fields with a suppressed prediction.
    // To retain this behavior, the `suppress_reason` is only set if the form
    // contains a field that triggers (non-fallback) suggestions.
    // By not setting it, the autocomplete suggestion logic downstream is
    // triggered, since no Autofill `suggestions` are available.
    if (!base::ranges::all_of(*context->form_structure, [](const auto& field) {
          return field->ShouldSuppressSuggestionsAndFillingByDefault() ||
                 field->Type().GetStorableType() == UNKNOWN_TYPE;
        })) {
      context->suppress_reason = SuppressReason::kAutocompleteUnrecognized;
    }
    suggestions->clear();
    return;
  }
  if (got_autofillable_form) {
    auto* logger = GetEventFormLogger(*context->focused_field);
    if (logger) {
      logger->OnDidInteractWithAutofillableForm(*(context->form_structure),
                                                signin_state_for_metrics_);
    }
  }
  context->filling_product = GetPreferredSuggestionFillingProduct(
      got_autofillable_form ? context->focused_field->Type().GetStorableType()
                            : UNKNOWN_TYPE,
      trigger_source);
  // If the feature is enabled and this is a mixed content form, we show a
  // warning message and don't offer autofill. The warning is shown even if
  // there are no autofill suggestions available.
  if (IsFormMixedContent(client(), form) &&
      client().GetPrefs()->FindPreference(
          ::prefs::kMixedFormsWarningsEnabled) &&
      client().GetPrefs()->GetBoolean(::prefs::kMixedFormsWarningsEnabled)) {
    suggestions->clear();
    // If the user begins typing, we interpret that as dismissing the warning.
    // No suggestions are allowed, but the warning is no longer shown.
    if (field.DidUserType()) {
      context->suppress_reason = SuppressReason::kInsecureForm;
    } else {
      Suggestion warning_suggestion(
          l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_MIXED_FORM));
      warning_suggestion.popup_item_id = PopupItemId::kMixedFormMessage;
      suggestions->emplace_back(warning_suggestion);
    }
    return;
  }
  context->is_context_secure = !IsFormNonSecure(form);

  context->is_autofill_available =
      IsAutofillEnabled() &&
      (IsAutofillManuallyTriggered(trigger_source) || got_autofillable_form);
  if (!context->is_autofill_available) {
    return;
  }

  if (context->filling_product == FillingProduct::kCreditCard) {
    FieldType trigger_field_type =
        context->focused_field
            ? context->focused_field->Type().GetStorableType()
            : UNKNOWN_TYPE;
    *suggestions = GetCreditCardSuggestions(form, field, trigger_field_type,
                                            trigger_source,
                                            context->should_display_gpay_logo);
  } else if (context->filling_product == FillingProduct::kAddress) {
    // Profile suggestions fill ac=unrecognized fields only when triggered
    // through manual fallbacks. As such, suggestion labels differ depending on
    // the `trigger_source`.
    *suggestions =
        GetProfileSuggestions(form, context->form_structure, field,
                              context->focused_field, trigger_source);
    if (context->focused_field &&
        context->focused_field->Type().group() == FieldTypeGroup::kEmail) {
      std::optional<Suggestion> maybe_plus_address_suggestion =
          MaybeGetPlusAddressSuggestion();
      if (maybe_plus_address_suggestion.has_value()) {
        suggestions->insert(suggestions->cbegin(),
                            maybe_plus_address_suggestion.value());
      }
    }
  }

  // Ablation experiment
  if (context->filling_product == FillingProduct::kAddress ||
      context->filling_product == FillingProduct::kCreditCard) {
    FormTypeForAblationStudy form_type =
        context->filling_product == FillingProduct::kCreditCard
            ? FormTypeForAblationStudy::kPayment
            : FormTypeForAblationStudy::kAddress;
    // If ablation_group is AblationGroup::kDefault or AblationGroup::kControl,
    // no ablation happens in the following.
    AblationGroup ablation_group = client().GetAblationStudy().GetAblationGroup(
        client().GetLastCommittedPrimaryMainFrameURL(), form_type);
    context->ablation_group = ablation_group;
    // Note that we don't set the ablation group if there are no suggestions.
    // In that case we stick to kDefault.
    context->conditional_ablation_group =
        !suggestions->empty() ? ablation_group : AblationGroup::kDefault;

    // In both cases (credit card and address forms), we inform the other event
    // logger also about the ablation.
    // This prevents for example that for an encountered address form we log a
    // sample Autofill.Funnel.ParsedAsType.CreditCard = 0 (which would be
    // recorded by the credit_card_form_event_logger_). For the complementary
    // event logger, the conditional ablation status is logged as kDefault to
    // not imply that data would be filled without ablation.
    if (context->filling_product == FillingProduct::kCreditCard) {
      credit_card_form_event_logger_->SetAblationStatus(
          context->ablation_group, context->conditional_ablation_group);
      address_form_event_logger_->SetAblationStatus(context->ablation_group,
                                                    AblationGroup::kDefault);
    } else if (context->filling_product == FillingProduct::kAddress) {
      address_form_event_logger_->SetAblationStatus(
          context->ablation_group, context->conditional_ablation_group);
      credit_card_form_event_logger_->SetAblationStatus(
          context->ablation_group, AblationGroup::kDefault);
    }

    if (!suggestions->empty() && ablation_group == AblationGroup::kAblation) {
      // Logic for disabling/ablating autofill.
      context->suppress_reason = SuppressReason::kAblation;
      suggestions->clear();
      return;
    }
  }
  if (suggestions->empty() ||
      context->filling_product != FillingProduct::kCreditCard) {
    return;
  }
  // Don't provide credit card suggestions for non-secure pages, but do
  // provide them for secure pages with passive mixed content (see
  // implementation of IsContextSecure).
  if (!context->is_context_secure) {
    // Replace the suggestion content with a warning message explaining why
    // Autofill is disabled for a website. The string is different if the
    // credit card autofill HTTP warning experiment is enabled.
    Suggestion warning_suggestion(
        l10n_util::GetStringUTF16(IDS_AUTOFILL_WARNING_INSECURE_CONNECTION));
    warning_suggestion.popup_item_id =
        PopupItemId::kInsecureContextPaymentDisabledMessage;
    suggestions->assign(1, warning_suggestion);
  }
}

autofill_metrics::FormEventLoggerBase*
BrowserAutofillManager::GetEventFormLogger(const AutofillField& field) const {
  if (field.ShouldSuppressSuggestionsAndFillingByDefault()) {
    // Ignore ac=unrecognized fields in key metrics.
    return nullptr;
  }
  switch (FieldTypeGroupToFormType(field.Type().group())) {
    case FormType::kAddressForm:
      return address_form_event_logger_.get();
    case FormType::kCreditCardForm:
      return credit_card_form_event_logger_.get();
    case FormType::kPasswordForm:
    case FormType::kUnknownFormType:
      return nullptr;
  }
  NOTREACHED_NORETURN();
}

void BrowserAutofillManager::PreProcessStateMatchingTypes(
    const std::vector<AutofillProfile>& profiles,
    FormStructure* form_structure) {
  for (const auto& profile : profiles) {
    std::optional<AlternativeStateNameMap::CanonicalStateName>
        canonical_state_name_from_profile =
            profile.GetAddress().GetCanonicalizedStateName();

    if (!canonical_state_name_from_profile) {
      continue;
    }

    const std::u16string& country_code =
        profile.GetInfo(AutofillType(HtmlFieldType::kCountryCode), app_locale_);

    for (auto& field : *form_structure) {
      if (field->state_is_a_matching_type())
        continue;

      std::optional<AlternativeStateNameMap::CanonicalStateName>
          canonical_state_name_from_text =
              AlternativeStateNameMap::GetCanonicalStateName(
                  base::UTF16ToUTF8(country_code), field->value);

      if (canonical_state_name_from_text &&
          canonical_state_name_from_text.value() ==
              canonical_state_name_from_profile.value()) {
        field->set_state_is_a_matching_type();
      }
    }
  }
}

void BrowserAutofillManager::ReportAutofillWebOTPMetrics(bool used_web_otp) {
  // It's possible that a frame without any form uses WebOTP. e.g. a server may
  // send the verification code to a phone number that was collected beforehand
  // and uses the WebOTP API for authentication purpose without user manually
  // entering the code.
  if (!has_parsed_forms() && !used_web_otp)
    return;

  if (has_observed_phone_number_field())
    phone_collection_metric_state_ |= phone_collection_metric::kPhoneCollected;
  if (has_observed_one_time_code_field())
    phone_collection_metric_state_ |= phone_collection_metric::kOTCUsed;
  if (used_web_otp)
    phone_collection_metric_state_ |= phone_collection_metric::kWebOTPUsed;

  ukm::UkmRecorder* recorder = client().GetUkmRecorder();
  ukm::SourceId source_id = client().GetUkmSourceId();
  AutofillMetrics::LogWebOTPPhoneCollectionMetricStateUkm(
      recorder, source_id, phone_collection_metric_state_);

  base::UmaHistogramEnumeration(
      "Autofill.WebOTP.PhonePlusWebOTPPlusOTC",
      static_cast<PhoneCollectionMetricState>(phone_collection_metric_state_));
}

void BrowserAutofillManager::ProcessFieldLogEventsInForm(
    const FormStructure& form_structure) {
  // TODO(crbug.com/1325851): Log metrics if at least one field in the form was
  // classified as a certain type.
  LogEventCountsUMAMetric(form_structure);

  // ShouldUploadUkm reduces the UKM load by ignoring e.g. search boxes at best
  // effort.
  bool should_upload_ukm = base::FeatureList::IsEnabled(
                               features::kAutofillLogUKMEventsWithSampleRate) &&
                           ShouldUploadUkm(form_structure);

  for (const auto& autofill_field : form_structure) {
    if (should_upload_ukm) {
      form_interactions_ukm_logger()->LogAutofillFieldInfoAtFormRemove(
          form_structure, *autofill_field,
          AutocompleteStateForSubmittedField(*autofill_field));
    }

    // Clear log events.
    // Not conditions on kAutofillLogUKMEventsWithSampleRate because there may
    // be other reasons to log events.
    autofill_field->ClearLogEvents();
  }

  // Log FormSummary UKM event.
  if (should_upload_ukm) {
    AutofillMetrics::FormEventSet form_events;
    form_events.insert_all(
        address_form_event_logger_->GetFormEvents(form_structure.global_id()));
    form_events.insert_all(credit_card_form_event_logger_->GetFormEvents(
        form_structure.global_id()));
    form_interactions_ukm_logger()->LogAutofillFormSummaryAtFormRemove(
        form_structure, form_events, initial_interaction_timestamp_,
        form_submitted_timestamp_);
  }
}

bool BrowserAutofillManager::ShouldUploadUkm(
    const FormStructure& form_structure) {
  if (!form_structure.ShouldBeParsed()) {
    return false;
  }

  // Return true if the field is a visible text input field which has predicted
  // types from heuristics or the server.
  auto is_focusable_predicted_text_field =
      [](const std::unique_ptr<AutofillField>& field) {
        return field->IsTextInputElement() && field->IsFocusable() &&
               ((field->server_type() != NO_SERVER_DATA &&
                 field->server_type() != UNKNOWN_TYPE) ||
                field->heuristic_type() != UNKNOWN_TYPE ||
                field->html_type() != HtmlFieldType::kUnspecified);
      };

  size_t num_text_fields = base::ranges::count_if(
      form_structure.fields(), is_focusable_predicted_text_field);
  if (num_text_fields == 0) {
    return false;
  }

  // If the form contains a single text field and this contains the string
  // "search" in its name/id/placeholder, the function return false and the form
  // is not recorded into UKM. The form is considered a search box.
  if (num_text_fields == 1) {
    auto it = base::ranges::find_if(form_structure.fields(),
                                    is_focusable_predicted_text_field);
    if (base::ToLowerASCII((*it)->placeholder).find(u"search") !=
            std::string::npos ||
        base::ToLowerASCII((*it)->name).find(u"search") != std::string::npos ||
        base::ToLowerASCII((*it)->label).find(u"search") != std::string::npos ||
        base::ToLowerASCII((*it)->aria_label).find(u"search") !=
            std::string::npos) {
      return false;
    }
  }

  return true;
}

std::optional<Suggestion>
BrowserAutofillManager::MaybeGetPlusAddressSuggestion() {
  plus_addresses::PlusAddressService* plus_address_service =
      client().GetPlusAddressService();
  if (!plus_address_service ||
      !plus_address_service->SupportsPlusAddresses(
          client().GetLastCommittedPrimaryMainFrameOrigin(),
          client().IsOffTheRecord())) {
    return std::nullopt;
  }
  std::optional<std::string> maybe_address =
      plus_address_service->GetPlusAddress(
          client().GetLastCommittedPrimaryMainFrameOrigin());
  if (maybe_address == std::nullopt) {
    Suggestion create_plus_address_suggestion(
        plus_address_service->GetCreateSuggestionLabel());
    create_plus_address_suggestion.popup_item_id =
        PopupItemId::kCreateNewPlusAddress;
    plus_addresses::PlusAddressMetrics::RecordAutofillSuggestionEvent(
        plus_addresses::PlusAddressMetrics::PlusAddressAutofillSuggestionEvent::
            kCreateNewPlusAddressSuggested);
    create_plus_address_suggestion.icon = Suggestion::Icon::kPlusAddress;
    return create_plus_address_suggestion;
  }
  Suggestion existing_plus_address_suggestion(
      base::UTF8ToUTF16(maybe_address.value()));
  existing_plus_address_suggestion.popup_item_id =
      PopupItemId::kFillExistingPlusAddress;
  plus_addresses::PlusAddressMetrics::RecordAutofillSuggestionEvent(
      plus_addresses::PlusAddressMetrics::PlusAddressAutofillSuggestionEvent::
          kExistingPlusAddressSuggested);
  existing_plus_address_suggestion.icon = Suggestion::Icon::kPlusAddress;
  return existing_plus_address_suggestion;
}

std::optional<Suggestion> BrowserAutofillManager::MaybeGetComposeSuggestion(
    const FormFieldData& field) {
  AutofillComposeDelegate* compose_delegate = client().GetComposeDelegate();
  if (!compose_delegate || !compose_delegate->ShouldOfferComposePopup(field)) {
    return std::nullopt;
  }
  std::u16string suggestion_text;
  std::u16string label_text;
  if (compose_delegate->HasSavedState(field.global_id())) {
    // The nudge text indicates that the user can resume where they left off in
    // the Compose dialog.
    suggestion_text =
        l10n_util::GetStringUTF16(IDS_COMPOSE_SUGGESTION_SAVED_TEXT);
    label_text = l10n_util::GetStringUTF16(IDS_COMPOSE_SUGGESTION_SAVED_LABEL);
  } else {
    // Text for a new Compose session.
    suggestion_text =
        l10n_util::GetStringUTF16(IDS_COMPOSE_SUGGESTION_MAIN_TEXT);
    label_text = l10n_util::GetStringUTF16(IDS_COMPOSE_SUGGESTION_LABEL);
  }
  Suggestion suggestion(std::move(suggestion_text));
  suggestion.labels = {{Suggestion::Text(std::move(label_text))}};
  suggestion.popup_item_id = PopupItemId::kCompose;
  suggestion.icon = Suggestion::Icon::kPenSpark;
  return suggestion;
}

void BrowserAutofillManager::LogEventCountsUMAMetric(
    const FormStructure& form_structure) {
  size_t num_ask_for_values_to_fill_event = 0;
  size_t num_trigger_fill_event = 0;
  size_t num_fill_event = 0;
  size_t num_typing_event = 0;
  size_t num_heuristic_prediction_event = 0;
  size_t num_autocomplete_attribute_event = 0;
  size_t num_server_prediction_event = 0;
  size_t num_rationalization_event = 0;

  for (const auto& autofill_field : form_structure) {
    for (const auto& log_event : autofill_field->field_log_events()) {
      static_assert(
          absl::variant_size<AutofillField::FieldLogEventType>() == 9,
          "When adding new variants check that this function does not "
          "need to be updated.");
      if (absl::holds_alternative<AskForValuesToFillFieldLogEvent>(log_event)) {
        ++num_ask_for_values_to_fill_event;
      } else if (absl::holds_alternative<TriggerFillFieldLogEvent>(log_event)) {
        ++num_trigger_fill_event;
      } else if (absl::holds_alternative<FillFieldLogEvent>(log_event)) {
        ++num_fill_event;
      } else if (absl::holds_alternative<TypingFieldLogEvent>(log_event)) {
        ++num_typing_event;
      } else if (absl::holds_alternative<HeuristicPredictionFieldLogEvent>(
                     log_event)) {
        ++num_heuristic_prediction_event;
      } else if (absl::holds_alternative<AutocompleteAttributeFieldLogEvent>(
                     log_event)) {
        ++num_autocomplete_attribute_event;
      } else if (absl::holds_alternative<ServerPredictionFieldLogEvent>(
                     log_event)) {
        ++num_server_prediction_event;
      } else if (absl::holds_alternative<RationalizationFieldLogEvent>(
                     log_event)) {
        ++num_rationalization_event;
      } else {
        NOTREACHED();
      }
    }
  }

  size_t total_num_log_events =
      num_ask_for_values_to_fill_event + num_trigger_fill_event +
      num_fill_event + num_typing_event + num_heuristic_prediction_event +
      num_autocomplete_attribute_event + num_server_prediction_event +
      num_rationalization_event;
  // Record the number of each type of log events into UMA to decide if we need
  // to clear them before the form is submitted or destroyed.
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.AskForValuesToFillEvent",
                             num_ask_for_values_to_fill_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.TriggerFillEvent",
                             num_trigger_fill_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.FillEvent", num_fill_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.TypingEvent", num_typing_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.HeuristicPredictionEvent",
                             num_heuristic_prediction_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.AutocompleteAttributeEvent",
                             num_autocomplete_attribute_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.ServerPredictionEvent",
                             num_server_prediction_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.RationalizationEvent",
                             num_rationalization_event);
  UMA_HISTOGRAM_COUNTS_10000("Autofill.LogEvent.All", total_num_log_events);
}

void BrowserAutofillManager::SetFastCheckoutRunId(
    FieldTypeGroup field_type_group,
    int64_t run_id) {
  switch (FieldTypeGroupToFormType(field_type_group)) {
    case FormType::kAddressForm:
      address_form_event_logger_->SetFastCheckoutRunId(run_id);
      return;
    case FormType::kCreditCardForm:
      credit_card_form_event_logger_->SetFastCheckoutRunId(run_id);
      break;
    case FormType::kPasswordForm:
    case FormType::kUnknownFormType:
      // FastCheckout only supports address and credit card forms.
      NOTREACHED();
  }
}
#endif  // !BUILDFLAG(IS_QTWEBENGINE)

}  // namespace autofill
