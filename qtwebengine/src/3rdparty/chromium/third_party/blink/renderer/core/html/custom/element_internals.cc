// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/html/custom/element_internals.h"

#include "third_party/blink/renderer/bindings/core/v8/v8_union_file_formdata_usvstring.h"
#include "third_party/blink/renderer/bindings/core/v8/v8_validity_state_flags.h"
#include "third_party/blink/renderer/core/accessibility/ax_object_cache.h"
#include "third_party/blink/renderer/core/dom/node_lists_node_data.h"
#include "third_party/blink/renderer/core/execution_context/execution_context.h"
#include "third_party/blink/renderer/core/fileapi/file.h"
#include "third_party/blink/renderer/core/html/custom/custom_element.h"
#include "third_party/blink/renderer/core/html/custom/custom_element_registry.h"
#include "third_party/blink/renderer/core/html/custom/custom_state_set.h"
#include "third_party/blink/renderer/core/html/forms/form_controller.h"
#include "third_party/blink/renderer/core/html/forms/form_data.h"
#include "third_party/blink/renderer/core/html/forms/html_form_element.h"
#include "third_party/blink/renderer/core/html/forms/validity_state.h"
#include "third_party/blink/renderer/core/html/html_element.h"

namespace blink {

namespace {

bool IsValidityStateFlagsValid(const ValidityStateFlags* flags) {
  if (!flags)
    return true;
  if (flags->badInput() || flags->customError() || flags->patternMismatch() ||
      flags->rangeOverflow() || flags->rangeUnderflow() ||
      flags->stepMismatch() || flags->tooLong() || flags->tooShort() ||
      flags->typeMismatch() || flags->valueMissing())
    return false;
  return true;
}

void AppendToFormControlState(const V8ControlValue& value,
                              FormControlState& state) {
  switch (value.GetContentType()) {
    case V8ControlValue::ContentType::kFile: {
      state.Append("File");
      value.GetAsFile()->AppendToControlState(state);
      break;
    }
    case V8ControlValue::ContentType::kFormData: {
      state.Append("FormData");
      value.GetAsFormData()->AppendToControlState(state);
      break;
    }
    case V8ControlValue::ContentType::kUSVString: {
      state.Append("USVString");
      state.Append(value.GetAsUSVString());
      break;
    }
  }
}

const V8ControlValue* RestoreFromFormControlState(
    ExecutionContext& execution_context,
    const FormControlState& state,
    const StringView& section_title,
    wtf_size_t& index) {
  if (state.ValueSize() < index + 3) {
    return nullptr;
  }
  if (state[index] != section_title) {
    return nullptr;
  }
  const V8ControlValue* restored_value = nullptr;
  const String& entry_type = state[index + 1];
  index += 2;
  if (entry_type == "USVString") {
    restored_value = MakeGarbageCollected<V8ControlValue>(state[index++]);
  } else if (entry_type == "File") {
    if (auto* file =
            File::CreateFromControlState(&execution_context, state, index)) {
      restored_value = MakeGarbageCollected<V8ControlValue>(file);
    }
  } else if (entry_type == "FormData") {
    if (auto* form_data =
            FormData::CreateFromControlState(execution_context, state, index)) {
      restored_value = MakeGarbageCollected<V8ControlValue>(form_data);
    }
  } else {
    NOTREACHED();
  }
  return restored_value;
}

}  // namespace

ElementInternals::ElementInternals(HTMLElement& target) : target_(target) {
}

void ElementInternals::Trace(Visitor* visitor) const {
  visitor->Trace(target_);
  visitor->Trace(value_);
  visitor->Trace(state_);
  visitor->Trace(validity_flags_);
  visitor->Trace(validation_anchor_);
  visitor->Trace(custom_states_);
  visitor->Trace(explicitly_set_attr_elements_map_);
  ListedElement::Trace(visitor);
  ScriptWrappable::Trace(visitor);
  ElementRareDataField::Trace(visitor);
}

void ElementInternals::setFormValue(const V8ControlValue* value,
                                    ExceptionState& exception_state) {
  setFormValue(value, value, exception_state);
}

void ElementInternals::setFormValue(const V8ControlValue* value,
                                    const V8ControlValue* state,
                                    ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return;
  }

  if (value && value->IsFormData()) {
    value_ = MakeGarbageCollected<V8ControlValue>(
        MakeGarbageCollected<FormData>(*value->GetAsFormData()));
  } else {
    value_ = value;
  }

  if (value == state) {
    state_ = value_;
  } else if (state && state->IsFormData()) {
    state_ = MakeGarbageCollected<V8ControlValue>(
        MakeGarbageCollected<FormData>(*state->GetAsFormData()));
  } else {
    state_ = state;
  }
  NotifyFormStateChanged();
}

HTMLFormElement* ElementInternals::form(ExceptionState& exception_state) const {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return nullptr;
  }
  return ListedElement::Form();
}

void ElementInternals::setValidity(ValidityStateFlags* flags,
                                   ExceptionState& exception_state) {
  setValidity(flags, String(), nullptr, exception_state);
}

void ElementInternals::setValidity(ValidityStateFlags* flags,
                                   const String& message,
                                   ExceptionState& exception_state) {
  setValidity(flags, message, nullptr, exception_state);
}

void ElementInternals::setValidity(ValidityStateFlags* flags,
                                   const String& message,
                                   HTMLElement* anchor,
                                   ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return;
  }
  // Custom element authors should provide a message. They can omit the message
  // argument only if nothing if | flags| is true.
  if (!IsValidityStateFlagsValid(flags) && message.empty()) {
    exception_state.ThrowTypeError(
        "The second argument should not be empty if one or more flags in the "
        "first argument are true.");
    return;
  }
  if (anchor && !Target().IsShadowIncludingAncestorOf(*anchor)) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotFoundError,
        "The Element argument should be a shadow-including descendant of the "
        "target element.");
    return;
  }

  if (validation_anchor_ && validation_anchor_ != anchor) {
    HideVisibleValidationMessage();
  }
  validity_flags_ = flags;
  validation_anchor_ = anchor;
  SetCustomValidationMessage(message);
  SetNeedsValidityCheck();
}

bool ElementInternals::willValidate(ExceptionState& exception_state) const {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return false;
  }
  return WillValidate();
}

ValidityState* ElementInternals::validity(ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return nullptr;
  }
  return ListedElement::validity();
}

String ElementInternals::ValidationMessageForBinding(
    ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return String();
  }
  return validationMessage();
}

String ElementInternals::validationMessage() const {
  if (IsValidityStateFlagsValid(validity_flags_))
    return String();
  return CustomValidationMessage();
}

String ElementInternals::ValidationSubMessage() const {
  if (PatternMismatch())
    return Target().FastGetAttribute(html_names::kTitleAttr).GetString();
  return String();
}

Element& ElementInternals::ValidationAnchor() const {
  return validation_anchor_ ? *validation_anchor_ : Target();
}

bool ElementInternals::checkValidity(ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return false;
  }
  return ListedElement::checkValidity();
}

bool ElementInternals::reportValidity(ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return false;
  }
  return ListedElement::reportValidity();
}

LabelsNodeList* ElementInternals::labels(ExceptionState& exception_state) {
  if (!IsTargetFormAssociated()) {
    exception_state.ThrowDOMException(
        DOMExceptionCode::kNotSupportedError,
        "The target element is not a form-associated custom element.");
    return nullptr;
  }
  return Target().labels();
}

CustomStateSet* ElementInternals::states() {
  if (!custom_states_)
    custom_states_ = MakeGarbageCollected<CustomStateSet>(Target());
  return custom_states_.Get();
}

bool ElementInternals::HasState(const AtomicString& state) const {
  return custom_states_ && custom_states_->Has(state);
}

ShadowRoot* ElementInternals::shadowRoot() const {
  if (ShadowRoot* shadow_root = Target().AuthorShadowRoot()) {
    return shadow_root->IsAvailableToElementInternals() ? shadow_root : nullptr;
  }
  return nullptr;
}

const AtomicString& ElementInternals::FastGetAttribute(
    const QualifiedName& attribute) const {
  const auto it = accessibility_semantics_map_.find(attribute);
  if (it == accessibility_semantics_map_.end())
    return g_null_atom;
  return it->value;
}

const HashMap<QualifiedName, AtomicString>& ElementInternals::GetAttributes()
    const {
  return accessibility_semantics_map_;
}

void ElementInternals::setAttribute(const QualifiedName& attribute,
                                    const AtomicString& value) {
  accessibility_semantics_map_.Set(attribute, value);
  if (AXObjectCache* cache = Target().GetDocument().ExistingAXObjectCache())
    cache->HandleAttributeChanged(attribute, &Target());
}

bool ElementInternals::HasAttribute(const QualifiedName& attribute) const {
  return accessibility_semantics_map_.Contains(attribute);
}

void ElementInternals::DidUpgrade() {
  ContainerNode* parent = Target().parentNode();
  if (!parent)
    return;
  InsertedInto(*parent);
  if (auto* owner_form = Form()) {
    if (auto* lists = owner_form->NodeLists())
      lists->InvalidateCaches(nullptr);
  }
  for (ContainerNode* node = parent; node; node = node->parentNode()) {
    if (IsA<HTMLFieldSetElement>(node)) {
      // TODO(tkent): Invalidate only HTMLFormControlsCollections.
      if (auto* lists = node->NodeLists())
        lists->InvalidateCaches(nullptr);
    }
  }
  Target().GetDocument().GetFormController().RestoreControlStateOnUpgrade(
      *this);
}

void ElementInternals::SetElementAttribute(const QualifiedName& name,
                                           Element* element) {
  if (!element) {
    explicitly_set_attr_elements_map_.erase(name);
    return;
  }
  auto result = explicitly_set_attr_elements_map_.insert(name, nullptr);
  if (result.is_new_entry) {
    result.stored_value->value =
        MakeGarbageCollected<HeapLinkedHashSet<WeakMember<Element>>>();
  } else {
    result.stored_value->value->clear();
  }
  result.stored_value->value->insert(element);
}

Element* ElementInternals::GetElementAttribute(const QualifiedName& name) {
  const auto& iter = explicitly_set_attr_elements_map_.find(name);
  if (iter == explicitly_set_attr_elements_map_.end())
    return nullptr;
  HeapLinkedHashSet<WeakMember<Element>>* stored_elements = iter->value;
  DCHECK_EQ(stored_elements->size(), 1u);
  return stored_elements->begin()->Get();
}

HeapVector<Member<Element>>* ElementInternals::GetAttrAssociatedElements(
    const QualifiedName& name) const {
  const auto& iter = explicitly_set_attr_elements_map_.find(name);
  if (iter == explicitly_set_attr_elements_map_.end())
    return nullptr;
  HeapLinkedHashSet<WeakMember<Element>>* stored_elements = iter->value;

  // Convert from our internal HeapLinkedHashSet of weak references to a
  // HeapVector of strong references so that V8 can implicitly convert to a
  // FrozenArray.
  // TODO(crbug.com/1326808): Use FrozenArray for
  // explicitly_set_attr_elements_map_ once crrev.com/c/5149923 lands, so that
  // the same v8 value is returned each time.
  HeapVector<Member<Element>>* results =
      MakeGarbageCollected<HeapVector<Member<Element>>>();
  results->ReserveInitialCapacity(stored_elements->size());
  for (auto item : *stored_elements) {
    results->push_back(item);
  }

  return results;
}

ScriptValue ElementInternals::GetElementArrayAttribute(
    ScriptState* script_state,
    const QualifiedName& name) {
  HeapVector<Member<Element>>* elements = GetAttrAssociatedElements(name);
  v8::Isolate* isolate = script_state->GetIsolate();

  v8::Local<v8::Value> v8_elements_as_frozen_array =
      ToV8Traits<IDLNullable<IDLArray<Element>>>::ToV8(script_state, elements);
  if (v8_elements_as_frozen_array->IsNull()) {
    return ScriptValue::CreateNull(isolate);
  }
  return ScriptValue(isolate, v8_elements_as_frozen_array);
}

void ElementInternals::SetElementArrayAttribute(
    ScriptState* script_state,
    const QualifiedName& name,
    const ScriptValue given_value,
    const char* const property_name) {
  v8::Isolate* isolate = script_state->GetIsolate();
  ExceptionState exception_state(isolate, ExceptionContextType::kAttributeSet,
                                 "ElementInternals", property_name);

  HeapVector<Member<Element>>* given_elements =
      NativeValueTraits<IDLNullable<IDLArray<Element>>>::NativeValue(
          isolate, given_value.V8Value(), exception_state);
  if (UNLIKELY(exception_state.HadException())) {
    LOG(ERROR) << "Had exception when trying to convert given value for "
               << name.LocalName() << ": " << exception_state.Message();
    return;
  }

  if (!given_elements) {
    explicitly_set_attr_elements_map_.erase(name);
    return;
  }

  // Otherwise convert from our external strong references to our internal weak
  // references.
  auto stored_elements =
      explicitly_set_attr_elements_map_.insert(name, nullptr);
  if (stored_elements.is_new_entry) {
    stored_elements.stored_value->value =
        MakeGarbageCollected<HeapLinkedHashSet<WeakMember<Element>>>();
  } else {
    stored_elements.stored_value->value->clear();
  }

  for (auto element : *given_elements) {
    stored_elements.stored_value->value->insert(element);
  }
}

ScriptValue ElementInternals::ariaControlsElements(ScriptState* script_state) {
  return GetElementArrayAttribute(script_state, html_names::kAriaControlsAttr);
}
void ElementInternals::setAriaControlsElements(ScriptState* script_state,
                                               ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaControlsAttr,
                           given_elements, "ariaControlsElements");
}

ScriptValue ElementInternals::ariaDescribedByElements(
    ScriptState* script_state) {
  return GetElementArrayAttribute(script_state,
                                  html_names::kAriaDescribedbyAttr);
}
void ElementInternals::setAriaDescribedByElements(ScriptState* script_state,
                                                  ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaDescribedbyAttr,
                           given_elements, "ariaDescribedByElements");
}

ScriptValue ElementInternals::ariaDetailsElements(ScriptState* script_state) {
  return GetElementArrayAttribute(script_state, html_names::kAriaDetailsAttr);
}
void ElementInternals::setAriaDetailsElements(ScriptState* script_state,
                                              ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaDetailsAttr,
                           given_elements, "ariaDetailsElements");
}

ScriptValue ElementInternals::ariaErrorMessageElements(
    ScriptState* script_state) {
  return GetElementArrayAttribute(script_state,
                                  html_names::kAriaErrormessageAttr);
}
void ElementInternals::setAriaErrorMessageElements(ScriptState* script_state,
                                                   ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaErrormessageAttr,
                           given_elements, "ariaErrorMessageElements");
}

ScriptValue ElementInternals::ariaFlowToElements(ScriptState* script_state) {
  return GetElementArrayAttribute(script_state, html_names::kAriaFlowtoAttr);
}
void ElementInternals::setAriaFlowToElements(ScriptState* script_state,
                                             ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaFlowtoAttr,
                           given_elements, "ariaFlowToElements");
}

ScriptValue ElementInternals::ariaLabelledByElements(
    ScriptState* script_state) {
  return GetElementArrayAttribute(script_state,
                                  html_names::kAriaLabelledbyAttr);
}
void ElementInternals::setAriaLabelledByElements(ScriptState* script_state,
                                                 ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaLabelledbyAttr,
                           given_elements, "ariaLabelledByElements");
}

ScriptValue ElementInternals::ariaOwnsElements(ScriptState* script_state) {
  return GetElementArrayAttribute(script_state, html_names::kAriaOwnsAttr);
}
void ElementInternals::setAriaOwnsElements(ScriptState* script_state,
                                           ScriptValue given_elements) {
  SetElementArrayAttribute(script_state, html_names::kAriaOwnsAttr,
                           given_elements, "ariaOwnsElements");
}

bool ElementInternals::IsTargetFormAssociated() const {
  if (Target().IsFormAssociatedCustomElement())
    return true;
  // Custom element could be in the process of upgrading here, during which
  // it will have state kFailed or kPreCustomized according to:
  // https://html.spec.whatwg.org/multipage/custom-elements.html#upgrades
  if (Target().GetCustomElementState() != CustomElementState::kUndefined &&
      Target().GetCustomElementState() != CustomElementState::kFailed &&
      Target().GetCustomElementState() != CustomElementState::kPreCustomized) {
    return false;
  }
  // An element is in "undefined" state in its constructor JavaScript code.
  // ElementInternals needs to handle elements to be form-associated same as
  // form-associated custom elements because web authors want to call
  // form-related operations of ElementInternals in constructors.
  CustomElementRegistry* registry = CustomElement::Registry(Target());
  if (!registry)
    return false;
  auto* definition = registry->DefinitionForName(Target().localName());
  return definition && definition->IsFormAssociated();
}

bool ElementInternals::IsFormControlElement() const {
  return false;
}

bool ElementInternals::IsElementInternals() const {
  return true;
}

bool ElementInternals::IsEnumeratable() const {
  return true;
}

void ElementInternals::AppendToFormData(FormData& form_data) {
  if (Target().IsDisabledFormControl())
    return;

  if (!value_)
    return;

  const AtomicString& name = Target().FastGetAttribute(html_names::kNameAttr);
  if (!value_->IsFormData() && name.empty())
    return;

  switch (value_->GetContentType()) {
    case V8ControlValue::ContentType::kFile: {
      form_data.AppendFromElement(name, value_->GetAsFile());
      break;
    }
    case V8ControlValue::ContentType::kUSVString: {
      form_data.AppendFromElement(name, value_->GetAsUSVString());
      break;
    }
    case V8ControlValue::ContentType::kFormData: {
      for (const auto& entry : value_->GetAsFormData()->Entries()) {
        if (entry->isFile())
          form_data.append(entry->name(), entry->GetFile());
        else
          form_data.append(entry->name(), entry->Value());
      }
      break;
    }
  }
}

void ElementInternals::DidChangeForm() {
  ListedElement::DidChangeForm();
  CustomElement::EnqueueFormAssociatedCallback(Target(), Form());
}

bool ElementInternals::HasBadInput() const {
  return validity_flags_ && validity_flags_->badInput();
}

bool ElementInternals::PatternMismatch() const {
  return validity_flags_ && validity_flags_->patternMismatch();
}

bool ElementInternals::RangeOverflow() const {
  return validity_flags_ && validity_flags_->rangeOverflow();
}

bool ElementInternals::RangeUnderflow() const {
  return validity_flags_ && validity_flags_->rangeUnderflow();
}

bool ElementInternals::StepMismatch() const {
  return validity_flags_ && validity_flags_->stepMismatch();
}

bool ElementInternals::TooLong() const {
  return validity_flags_ && validity_flags_->tooLong();
}

bool ElementInternals::TooShort() const {
  return validity_flags_ && validity_flags_->tooShort();
}

bool ElementInternals::TypeMismatch() const {
  return validity_flags_ && validity_flags_->typeMismatch();
}

bool ElementInternals::ValueMissing() const {
  return validity_flags_ && validity_flags_->valueMissing();
}

bool ElementInternals::CustomError() const {
  return validity_flags_ && validity_flags_->customError();
}

void ElementInternals::DisabledStateMightBeChanged() {
  bool new_disabled = IsActuallyDisabled();
  if (is_disabled_ == new_disabled)
    return;
  is_disabled_ = new_disabled;
  CustomElement::EnqueueFormDisabledCallback(Target(), new_disabled);
}

bool ElementInternals::ClassSupportsStateRestore() const {
  return true;
}

bool ElementInternals::ShouldSaveAndRestoreFormControlState() const {
  // We don't save/restore control state in a form with autocomplete=off.
  return Target().isConnected() && (!Form() || Form()->ShouldAutocomplete());
}

FormControlState ElementInternals::SaveFormControlState() const {
  FormControlState state;
  if (value_) {
    state.Append("Value");
    AppendToFormControlState(*value_, state);
  }
  if (RuntimeEnabledFeatures::FormStateRestoreCallbackCallWithStateEnabled() &&
      state_) {
    state.Append("State");
    AppendToFormControlState(*state_, state);
  }
  return state;
}

void ElementInternals::RestoreFormControlState(const FormControlState& state) {
  ExecutionContext* execution_context = target_->GetExecutionContext();
  wtf_size_t index = 0;

  // Per spec, the submission value shouldn't be automatically restored by the
  // UA, but Blink has been doing that.
  if (const V8ControlValue* restored_value = RestoreFromFormControlState(
          *execution_context, state, "Value", index)) {
    value_ = restored_value;
  }
  if (!RuntimeEnabledFeatures::FormStateRestoreCallbackCallWithStateEnabled()) {
    if (value_) {
      CustomElement::EnqueueFormStateRestoreCallback(Target(), value_,
                                                     "restore");
    }
    return;
  }

  const V8ControlValue* restored_state =
      RestoreFromFormControlState(*execution_context, state, "State", index);
  if (restored_state) {
    CustomElement::EnqueueFormStateRestoreCallback(Target(), restored_state,
                                                   "restore");
  }
}

}  // namespace blink
