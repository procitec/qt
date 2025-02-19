// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_UI_PAYMENTS_AUTOFILL_ERROR_DIALOG_CONTROLLER_IMPL_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_UI_PAYMENTS_AUTOFILL_ERROR_DIALOG_CONTROLLER_IMPL_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "components/autofill/core/browser/payments/autofill_error_dialog_context.h"
#include "components/autofill/core/browser/ui/payments/autofill_error_dialog_controller.h"

namespace autofill {

class AutofillErrorDialogView;

// Implementation of the AutofillErrorDialogController. This class allows error
// dialog to be shown or dismissed.
// The controller is destroyed once the view is dismissed.
class AutofillErrorDialogControllerImpl : public AutofillErrorDialogController {
 public:
  AutofillErrorDialogControllerImpl();
  ~AutofillErrorDialogControllerImpl() override;

  AutofillErrorDialogControllerImpl(const AutofillErrorDialogControllerImpl&) =
      delete;
  AutofillErrorDialogControllerImpl& operator=(
      const AutofillErrorDialogControllerImpl&) = delete;

  // Show the error dialog for the given `autofill_error_dialog_context` and the
  // `view_creation_callback`.
  void Show(
      const AutofillErrorDialogContext& autofill_error_dialog_context,
      base::OnceCallback<AutofillErrorDialogView*()> view_creation_callback);

  // AutofillErrorDialogController.
  void OnDismissed() override;
  const std::u16string GetTitle() override;
  const std::u16string GetDescription() override;
  const std::u16string GetButtonLabel() override;

  AutofillErrorDialogView* autofill_error_dialog_view() {
    return autofill_error_dialog_view_;
  }

 private:
  // Dismiss the error dialog if showing.
  void Dismiss();

  // The context of the error dialog that is being displayed. Contains
  // information such as the type of the error dialog that is being displayed.
  // |error_dialog_context_| may also contain extra information such as a
  // detailed title and description returned from the server. If
  // |error_dialog_context_| contains this information, the fields in
  // |error_dialog_context_| should be preferred when displaying the error
  // dialog.
  AutofillErrorDialogContext error_dialog_context_;
  // View that displays the error dialog.
  raw_ptr<AutofillErrorDialogView> autofill_error_dialog_view_ = nullptr;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_UI_PAYMENTS_AUTOFILL_ERROR_DIALOG_CONTROLLER_IMPL_H_
