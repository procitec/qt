// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/policy/core/common/remote_commands/remote_commands_invalidator.h"

#include <string>

#include "base/logging.h"
#include "base/metrics/histogram_functions.h"
#include "base/scoped_observation.h"
#include "components/invalidation/public/invalidation.h"
#include "components/invalidation/public/invalidation_service.h"
#include "components/invalidation/public/invalidation_util.h"
#include "components/invalidation/public/invalidator_state.h"
#include "components/policy/core/common/cloud/enterprise_metrics.h"
#include "components/policy/core/common/cloud/policy_invalidation_util.h"

namespace policy {

RemoteCommandsInvalidator::RemoteCommandsInvalidator(std::string owner_name)
    : owner_name_(std::move(owner_name)) {}

RemoteCommandsInvalidator::~RemoteCommandsInvalidator() {
  DCHECK_EQ(SHUT_DOWN, state_);
}

void RemoteCommandsInvalidator::Initialize(
    invalidation::InvalidationService* invalidation_service) {
  DCHECK_EQ(SHUT_DOWN, state_);
  DCHECK(thread_checker_.CalledOnValidThread());

  DCHECK(invalidation_service);
  invalidation_service_ = invalidation_service;

  state_ = STOPPED;
  // TODO(crbug.com/1486860): Reset `invalidation_service_` to avoid dangling
  // pointer.
  OnInitialize();
}

void RemoteCommandsInvalidator::Shutdown() {
  DCHECK_NE(SHUT_DOWN, state_);
  DCHECK(thread_checker_.CalledOnValidThread());

  Stop();

  state_ = SHUT_DOWN;
  OnShutdown();
}

void RemoteCommandsInvalidator::Start() {
  DCHECK_EQ(STOPPED, state_);
  DCHECK(thread_checker_.CalledOnValidThread());

  state_ = STARTED;

  OnStart();
}

void RemoteCommandsInvalidator::Stop() {
  DCHECK_NE(SHUT_DOWN, state_);
  DCHECK(thread_checker_.CalledOnValidThread());

  if (state_ == STARTED) {
    Unregister();
    state_ = STOPPED;

    OnStop();
  }
}

void RemoteCommandsInvalidator::OnInvalidatorStateChange(
    invalidation::InvalidatorState state) {
  DCHECK_EQ(STARTED, state_);
  DCHECK(thread_checker_.CalledOnValidThread());
}

void RemoteCommandsInvalidator::OnIncomingInvalidation(
    const invalidation::Invalidation& invalidation) {
  DCHECK_EQ(STARTED, state_);
  DCHECK(thread_checker_.CalledOnValidThread());

  VLOG(2) << "Received remote command invalidation";

  if (!AreInvalidationsEnabled()) {
    LOG(WARNING) << "Unexpected invalidation received.";
  }

  CHECK(invalidation.topic() == topic_);

  invalidation.Acknowledge();

  DoRemoteCommandsFetch(invalidation);
}

std::string RemoteCommandsInvalidator::GetOwnerName() const {
  return owner_name_;
}

bool RemoteCommandsInvalidator::IsPublicTopic(
    const invalidation::Topic& topic) const {
  return IsPublicInvalidationTopic(topic);
}

void RemoteCommandsInvalidator::ReloadPolicyData(
    const enterprise_management::PolicyData* policy) {
  DCHECK(thread_checker_.CalledOnValidThread());

  if (state_ != STARTED) {
    return;
  }

  // Create the Topic based on the policy data.
  // If the policy does not specify the Topic, then unsubscribe and unregister.
  invalidation::Topic topic;
  if (!policy || !GetRemoteCommandTopicFromPolicy(*policy, &topic)) {
    UnsubscribeFromTopics();
    Unregister();
    return;
  }

  // If the policy topic in the policy data is different from the currently
  // registered topic, update the object registration.
  if (!IsRegistered() || topic != topic_) {
    Register(topic);
  }
}

bool RemoteCommandsInvalidator::IsRegistered() const {
  return invalidation_service_ && invalidation_service_->HasObserver(this);
}

bool RemoteCommandsInvalidator::AreInvalidationsEnabled() const {
  return IsRegistered() && invalidation_service_->GetInvalidatorState() ==
                               invalidation::INVALIDATIONS_ENABLED;
}

void RemoteCommandsInvalidator::Register(const invalidation::Topic& topic) {
  // Register this handler with the invalidation service if needed.
  if (!IsRegistered()) {
    OnInvalidatorStateChange(invalidation_service_->GetInvalidatorState());
    invalidation_service_->AddObserver(this);
  }

  topic_ = topic;

  // Update subscription with the invalidation service.
  const bool success =
      invalidation_service_->UpdateInterestedTopics(this, /*topics=*/{topic});
  CHECK(success) << "Could not subscribe to topic: " << topic;
}

void RemoteCommandsInvalidator::Unregister() {
  if (IsRegistered()) {
    invalidation_service_->RemoveObserver(this);
  }
}

void RemoteCommandsInvalidator::UnsubscribeFromTopics() {
  base::ScopedObservation<invalidation::InvalidationService,
                          invalidation::InvalidationHandler>
      temporary_registration(this);

  // Invalidator cannot unset its topics without being registered. Let's quickly
  // register and unregister to do just that.
  if (!IsRegistered()) {
    temporary_registration.Observe(invalidation_service_);
  }

  CHECK(invalidation_service_->UpdateInterestedTopics(
      this, invalidation::TopicSet()));
}

}  // namespace policy
