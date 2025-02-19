// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/safe_browsing/core/browser/safe_browsing_url_checker_impl.h"

#include <memory>

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/histogram_macros_local.h"
#include "base/task/sequenced_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "components/safe_browsing/core/browser/database_manager_mechanism.h"
#include "components/safe_browsing/core/browser/db/database_manager.h"
#include "components/safe_browsing/core/browser/db/v4_protocol_manager_util.h"
#include "components/safe_browsing/core/browser/hash_realtime_mechanism.h"
#include "components/safe_browsing/core/browser/hashprefix_realtime/hash_realtime_utils.h"
#include "components/safe_browsing/core/browser/realtime/policy_engine.h"
#include "components/safe_browsing/core/browser/realtime/url_lookup_service_base.h"
#include "components/safe_browsing/core/browser/safe_browsing_lookup_mechanism.h"
#include "components/safe_browsing/core/browser/url_checker_delegate.h"
#include "components/safe_browsing/core/browser/utils/scheme_logger.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/safe_browsing/core/common/web_ui_constants.h"
#include "components/security_interstitials/core/unsafe_resource.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "net/base/load_flags.h"
#include "net/http/http_request_headers.h"
#include "services/network/public/cpp/request_destination.h"

using security_interstitials::UnsafeResource;

namespace safe_browsing {
using CompleteCheckResult = SafeBrowsingLookupMechanism::CompleteCheckResult;
using hash_realtime_utils::HashRealTimeSelection;

namespace {
void RecordCheckUrlTimeout(bool timed_out) {
  UMA_HISTOGRAM_BOOLEAN("SafeBrowsing.CheckUrl.Timeout", timed_out);
}
}  // namespace

SafeBrowsingUrlCheckerImpl::Notifier::Notifier(CheckUrlCallback callback)
    : callback_(std::move(callback)) {}

SafeBrowsingUrlCheckerImpl::Notifier::Notifier(
    NativeCheckUrlCallback native_callback)
    : native_callback_(std::move(native_callback)) {}

SafeBrowsingUrlCheckerImpl::Notifier::~Notifier() = default;

SafeBrowsingUrlCheckerImpl::Notifier::Notifier(Notifier&& other) = default;

SafeBrowsingUrlCheckerImpl::Notifier&
SafeBrowsingUrlCheckerImpl::Notifier::operator=(Notifier&& other) = default;

void SafeBrowsingUrlCheckerImpl::Notifier::OnCompleteCheck(
    bool proceed,
    bool showed_interstitial,
    bool has_post_commit_interstitial_skipped,
    PerformedCheck performed_check) {
  DCHECK(performed_check != PerformedCheck::kUnknown);
  if (callback_) {
    std::move(callback_).Run(mojo::NullReceiver(), proceed,
                             showed_interstitial);
    return;
  }

  if (native_callback_) {
    std::move(native_callback_)
        .Run(nullptr, proceed, showed_interstitial,
             has_post_commit_interstitial_skipped, performed_check);
    return;
  }

  if (slow_check_notifier_) {
    slow_check_notifier_->OnCompleteCheck(proceed, showed_interstitial);
    slow_check_notifier_.reset();
    return;
  }

  std::move(native_slow_check_notifier_)
      .Run(proceed, showed_interstitial, has_post_commit_interstitial_skipped,
           performed_check);
}

SafeBrowsingUrlCheckerImpl::UrlInfo::UrlInfo(const GURL& in_url,
                                             const std::string& in_method,
                                             Notifier in_notifier)
    : url(in_url), method(in_method), notifier(std::move(in_notifier)) {}

SafeBrowsingUrlCheckerImpl::UrlInfo::UrlInfo(UrlInfo&& other) = default;

SafeBrowsingUrlCheckerImpl::UrlInfo::~UrlInfo() = default;

SafeBrowsingUrlCheckerImpl::KickOffLookupMechanismResult::
    KickOffLookupMechanismResult(
        SafeBrowsingLookupMechanism::StartCheckResult start_check_result,
        PerformedCheck performed_check)
    : start_check_result(start_check_result),
      performed_check(performed_check) {}
SafeBrowsingUrlCheckerImpl::KickOffLookupMechanismResult::
    ~KickOffLookupMechanismResult() = default;

SafeBrowsingUrlCheckerImpl::SafeBrowsingUrlCheckerImpl(
    const net::HttpRequestHeaders& headers,
    int load_flags,
    network::mojom::RequestDestination request_destination,
    bool has_user_gesture,
    scoped_refptr<UrlCheckerDelegate> url_checker_delegate,
    const base::RepeatingCallback<content::WebContents*()>& web_contents_getter,
    base::WeakPtr<web::WebState> weak_web_state,
    UnsafeResource::RenderProcessId render_process_id,
    const UnsafeResource::RenderFrameToken& render_frame_token,
    UnsafeResource::FrameTreeNodeId frame_tree_node_id,
    absl::optional<int64_t> navigation_id,
    bool url_real_time_lookup_enabled,
    bool can_urt_check_subresource_url,
    bool can_check_db,
    bool can_check_high_confidence_allowlist,
    std::string url_lookup_service_metric_suffix,
    GURL last_committed_url,
    scoped_refptr<base::SequencedTaskRunner> ui_task_runner,
    base::WeakPtr<RealTimeUrlLookupServiceBase> url_lookup_service_on_ui,
    base::WeakPtr<HashRealTimeService> hash_realtime_service_on_ui,
    HashRealTimeSelection hash_realtime_selection)
    : headers_(headers),
      load_flags_(load_flags),
      request_destination_(request_destination),
      has_user_gesture_(has_user_gesture),
      web_contents_getter_(web_contents_getter),
      render_process_id_(render_process_id),
      render_frame_token_(render_frame_token),
      frame_tree_node_id_(frame_tree_node_id),
      navigation_id_(navigation_id),
      weak_web_state_(weak_web_state),
      url_checker_delegate_(std::move(url_checker_delegate)),
      database_manager_(url_checker_delegate_->GetDatabaseManager()),
      url_real_time_lookup_enabled_(url_real_time_lookup_enabled),
      can_urt_check_subresource_url_(can_urt_check_subresource_url),
      can_check_db_(can_check_db),
      can_check_high_confidence_allowlist_(can_check_high_confidence_allowlist),
      url_lookup_service_metric_suffix_(url_lookup_service_metric_suffix),
      last_committed_url_(last_committed_url),
      ui_task_runner_(ui_task_runner),
      url_lookup_service_on_ui_(url_lookup_service_on_ui),
      hash_realtime_service_on_ui_(hash_realtime_service_on_ui),
      hash_realtime_selection_(hash_realtime_selection) {
  DCHECK(!can_urt_check_subresource_url_ || url_real_time_lookup_enabled_);
  DCHECK(url_real_time_lookup_enabled_ || can_check_db_);

  // This object is used exclusively on the IO thread but may be constructed on
  // the UI thread.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

SafeBrowsingUrlCheckerImpl::~SafeBrowsingUrlCheckerImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (state_ == STATE_CHECKING_URL) {
    const GURL& url = urls_[next_index_].url;
    TRACE_EVENT_NESTABLE_ASYNC_END1("safe_browsing", "CheckUrl",
                                    TRACE_ID_LOCAL(this), "url", url.spec());
  }
}

void SafeBrowsingUrlCheckerImpl::CheckUrl(const GURL& url,
                                          const std::string& method,
                                          CheckUrlCallback callback) {
  CheckUrlImplAndMaybeDeleteSelf(url, method, Notifier(std::move(callback)));
}

void SafeBrowsingUrlCheckerImpl::CheckUrl(const GURL& url,
                                          const std::string& method,
                                          NativeCheckUrlCallback callback) {
  CheckUrlImplAndMaybeDeleteSelf(url, method, Notifier(std::move(callback)));
}

base::WeakPtr<SafeBrowsingUrlCheckerImpl>
SafeBrowsingUrlCheckerImpl::WeakPtr() {
  return weak_factory_.GetWeakPtr();
}

UnsafeResource SafeBrowsingUrlCheckerImpl::MakeUnsafeResource(
    const GURL& url,
    SBThreatType threat_type,
    const ThreatMetadata& metadata,
    ThreatSource threat_source,
    std::unique_ptr<RTLookupResponse> rt_lookup_response,
    PerformedCheck performed_check) {
  UnsafeResource resource;
  resource.url = url;
  resource.original_url = urls_[0].url;
  if (urls_.size() > 1) {
    resource.redirect_urls.reserve(urls_.size() - 1);
    for (size_t i = 1; i < urls_.size(); ++i) {
      resource.redirect_urls.push_back(urls_[i].url);
    }
  }
  resource.is_subresource =
      request_destination_ != network::mojom::RequestDestination::kDocument;
  resource.is_subframe =
      network::IsRequestDestinationEmbeddedFrame(request_destination_);
  resource.threat_type = threat_type;
  resource.threat_metadata = metadata;
  resource.request_destination = request_destination_;
  resource.callback = base::BindRepeating(
      &SafeBrowsingUrlCheckerImpl::OnBlockingPageCompleteAndMaybeDeleteSelf,
      weak_factory_.GetWeakPtr(), performed_check);
  resource.callback_sequence = base::SequencedTaskRunner::GetCurrentDefault();
  resource.render_process_id = render_process_id_;
  resource.render_frame_token = render_frame_token_;
  resource.frame_tree_node_id = frame_tree_node_id_;
  resource.navigation_id = navigation_id_;
  resource.weak_web_state = weak_web_state_;
  resource.threat_source = threat_source;
  if (rt_lookup_response) {
    resource.rt_lookup_response = *rt_lookup_response;
  }
  return resource;
}

void SafeBrowsingUrlCheckerImpl::OnUrlResultAndMaybeDeleteSelf(
    PerformedCheck performed_check,
    bool timed_out,
    absl::optional<std::unique_ptr<CompleteCheckResult>> result) {
  DCHECK_EQ(result.has_value(), !timed_out);
  lookup_mechanism_runner_.reset();
  if (timed_out) {
    // Any pending callbacks on this URL check should be skipped.
    weak_factory_.InvalidateWeakPtrs();
    OnUrlResultInternalAndMaybeDeleteSelf(urls_[next_index_].url,
                                          safe_browsing::SB_THREAT_TYPE_SAFE,
                                          ThreatMetadata(),
                                          /*threat_source=*/absl::nullopt,
                                          /*rt_lookup_response=*/nullptr,
                                          /*timed_out=*/true, performed_check);
  } else {
    OnUrlResultInternalAndMaybeDeleteSelf(
        result.value()->url, result.value()->threat_type,
        result.value()->metadata, result.value()->threat_source,
        std::move(result.value()->url_real_time_lookup_response),
        /*timed_out=*/false, performed_check);
  }
}

void SafeBrowsingUrlCheckerImpl::OnUrlResultInternalAndMaybeDeleteSelf(
    const GURL& url,
    SBThreatType threat_type,
    const ThreatMetadata& metadata,
    absl::optional<ThreatSource> threat_source,
    std::unique_ptr<RTLookupResponse> rt_lookup_response,
    bool timed_out,
    PerformedCheck performed_check) {
  DCHECK_EQ(STATE_CHECKING_URL, state_);
  DCHECK_LT(next_index_, urls_.size());
  DCHECK_EQ(urls_[next_index_].url, url);
  DCHECK(threat_source.has_value() || threat_type == SB_THREAT_TYPE_SAFE);

  RecordCheckUrlTimeout(timed_out);
  TRACE_EVENT_NESTABLE_ASYNC_END1("safe_browsing", "CheckUrl",
                                  TRACE_ID_LOCAL(this), "url", url.spec());

  const bool is_prefetch = (load_flags_ & net::LOAD_PREFETCH);
  if (request_destination_ == network::mojom::RequestDestination::kDocument) {
    base::UmaHistogramBoolean("SafeBrowsing.CheckUrl.IsDocumentCheckPrefetch",
                              is_prefetch);
  }

  // Handle main frame and subresources. We do this to catch resources flagged
  // as phishing even if the top frame isn't flagged.
  if (!is_prefetch && threat_type == SB_THREAT_TYPE_URL_PHISHING &&
      base::FeatureList::IsEnabled(kDelayedWarnings)) {
    if (state_ != STATE_DELAYED_BLOCKING_PAGE) {
      // Delayed warnings experiment delays the warning until a user interaction
      // happens. Create an interaction observer and continue like there wasn't
      // a warning. The observer will create the interstitial when necessary.
      UnsafeResource unsafe_resource =
          MakeUnsafeResource(url, threat_type, metadata, threat_source.value(),
                             std::move(rt_lookup_response), performed_check);
      unsafe_resource.is_delayed_warning = true;
      url_checker_delegate_
          ->StartObservingInteractionsForDelayedBlockingPageHelper(
              unsafe_resource,
              request_destination_ ==
                  network::mojom::RequestDestination::kDocument);
      state_ = STATE_DELAYED_BLOCKING_PAGE;
    }
    // Let the navigation continue in case of delayed warnings.
    // No need to call ProcessUrls here, it'll return early.
    RunNextCallbackAndMaybeDeleteSelf(
        /*proceed=*/true,
        /*showed_interstitial=*/false,
        /*has_post_commit_interstitial_skipped=*/false, performed_check);
    return;
  }

  if (threat_type == SB_THREAT_TYPE_SAFE ||
      threat_type == SB_THREAT_TYPE_SUSPICIOUS_SITE) {
    state_ = STATE_NONE;

    if (threat_type == SB_THREAT_TYPE_SUSPICIOUS_SITE) {
      url_checker_delegate_->NotifySuspiciousSiteDetected(web_contents_getter_);
    }

    if (!RunNextCallbackAndMaybeDeleteSelf(
            /*proceed=*/true,
            /*showed_interstitial=*/false,
            /*has_post_commit_interstitial_skipped=*/false, performed_check)) {
      return;
    }

    ProcessUrlsAndMaybeDeleteSelf();
    return;
  }

  if (is_prefetch) {
    // Destroy the prefetch with FINAL_STATUS_SAFE_BROWSING.
    if (request_destination_ == network::mojom::RequestDestination::kDocument) {
      url_checker_delegate_->MaybeDestroyNoStatePrefetchContents(
          web_contents_getter_);
    }
    // Record the result of canceled unsafe prefetch. This is used as a signal
    // for testing.
    LOCAL_HISTOGRAM_ENUMERATION(
        "SB2Test.RequestDestination.UnsafePrefetchCanceled",
        request_destination_);

    BlockAndProcessUrlsAndMaybeDeleteSelf(
        /*showed_interstitial=*/false,
        /*has_post_commit_interstitial_skipped=*/false, performed_check);
    return;
  }

  UMA_HISTOGRAM_ENUMERATION("SB2.RequestDestination.Unsafe",
                            request_destination_);

  UnsafeResource resource =
      MakeUnsafeResource(url, threat_type, metadata, threat_source.value(),
                         std::move(rt_lookup_response), performed_check);

  state_ = STATE_DISPLAYING_BLOCKING_PAGE;

  url_checker_delegate_->StartDisplayingBlockingPageHelper(
      resource, urls_[next_index_].method, headers_,
      request_destination_ == network::mojom::RequestDestination::kDocument,
      has_user_gesture_);
}

void SafeBrowsingUrlCheckerImpl::CheckUrlImplAndMaybeDeleteSelf(
    const GURL& url,
    const std::string& method,
    Notifier notifier) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DVLOG(1) << "SafeBrowsingUrlCheckerImpl checks URL: " << url;
  urls_.emplace_back(url, method, std::move(notifier));

  ProcessUrlsAndMaybeDeleteSelf();
}

void SafeBrowsingUrlCheckerImpl::ProcessUrlsAndMaybeDeleteSelf() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(STATE_BLOCKED, state_);
  if (!base::FeatureList::IsEnabled(kDelayedWarnings)) {
    DCHECK_NE(STATE_DELAYED_BLOCKING_PAGE, state_);
  }

  if (state_ == STATE_CHECKING_URL ||
      state_ == STATE_DISPLAYING_BLOCKING_PAGE ||
      state_ == STATE_DELAYED_BLOCKING_PAGE) {
    return;
  }

  while (next_index_ < urls_.size()) {
    DCHECK_EQ(STATE_NONE, state_);

    const GURL& url = urls_[next_index_].url;
    if (url_checker_delegate_->IsUrlAllowlisted(url)) {
      if (!RunNextCallbackAndMaybeDeleteSelf(
              /*proceed=*/true, /*showed_interstitial=*/false,
              /*has_post_commit_interstitial_skipped=*/false,
              PerformedCheck::kCheckSkipped)) {
        return;
      }

      continue;
    }

    // TODO(crbug.com/1486144): Remove this check when
    // kSafeBrowsingSkipSubresources is launched.
    if (!database_manager_->CanCheckRequestDestination(request_destination_)) {
      UMA_HISTOGRAM_ENUMERATION("SB2.RequestDestination.Skipped",
                                request_destination_);

      if (!RunNextCallbackAndMaybeDeleteSelf(
              /*proceed=*/true, /*showed_interstitial=*/false,
              /*has_post_commit_interstitial_skipped=*/false,
              PerformedCheck::kCheckSkipped)) {
        return;
      }

      continue;
    }

    UMA_HISTOGRAM_ENUMERATION("SB2.RequestDestination.Checked",
                              request_destination_);

    SBThreatType threat_type = CheckWebUIUrls(url);
    if (threat_type != safe_browsing::SB_THREAT_TYPE_SAFE) {
      state_ = STATE_CHECKING_URL;
      TRACE_EVENT_NESTABLE_ASYNC_BEGIN1(
          "safe_browsing", "CheckUrl", TRACE_ID_LOCAL(this), "url", url.spec());

      base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(&SafeBrowsingUrlCheckerImpl::
                             OnUrlResultInternalAndMaybeDeleteSelf,
                         weak_factory_.GetWeakPtr(), url, threat_type,
                         ThreatMetadata(),
                         database_manager_->GetBrowseUrlThreatSource(
                             CheckBrowseUrlType::kHashDatabase),
                         /*rt_lookup_response=*/nullptr, /*timed_out=*/false,
                         PerformedCheck::kCheckSkipped));
      break;
    }

    TRACE_EVENT_NESTABLE_ASYNC_BEGIN1("safe_browsing", "CheckUrl",
                                      TRACE_ID_LOCAL(this), "url", url.spec());
    KickOffLookupMechanismResult result = KickOffLookupMechanism(url);

    if (result.start_check_result.is_safe_synchronously) {
      lookup_mechanism_runner_.reset();
      RecordCheckUrlTimeout(/*timed_out=*/false);

      TRACE_EVENT_NESTABLE_ASYNC_END1("safe_browsing", "CheckUrl",
                                      TRACE_ID_LOCAL(this), "url", url.spec());

      if (!RunNextCallbackAndMaybeDeleteSelf(
              /*proceed=*/true,
              /*showed_interstitial=*/false,
              /*has_post_commit_interstitial_skipped=*/false,
              result.performed_check)) {
        return;
      }

      continue;
    }

    state_ = STATE_CHECKING_URL;

    break;
  }
}

SafeBrowsingUrlCheckerImpl::KickOffLookupMechanismResult
SafeBrowsingUrlCheckerImpl::KickOffLookupMechanism(const GURL& url) {
  base::UmaHistogramBoolean("SafeBrowsing.RT.CanCheckDatabase", can_check_db_);
  scheme_logger::LogScheme(url, "SafeBrowsing.CheckUrl.UrlScheme");
  std::unique_ptr<SafeBrowsingLookupMechanism> lookup_mechanism;
  PerformedCheck performed_check = PerformedCheck::kUnknown;
  DCHECK(!lookup_mechanism_runner_);
  if (CanPerformFullURLLookup(url)) {
    performed_check = PerformedCheck::kUrlRealTimeCheck;
    lookup_mechanism = std::make_unique<UrlRealTimeMechanism>(
        url, url_checker_delegate_->GetThreatTypes(), request_destination_,
        database_manager_, can_check_db_, can_check_high_confidence_allowlist_,
        url_lookup_service_metric_suffix_, last_committed_url_, ui_task_runner_,
        url_lookup_service_on_ui_, url_checker_delegate_, web_contents_getter_);
  } else if (!can_check_db_) {
    return KickOffLookupMechanismResult(
        SafeBrowsingLookupMechanism::StartCheckResult(
            /*is_safe_synchronously=*/true),
        PerformedCheck::kCheckSkipped);
  } else if (hash_realtime_selection_ ==
                 HashRealTimeSelection::kHashRealTimeService &&
             HashRealTimeService::CanCheckUrl(url, request_destination_)) {
    performed_check = PerformedCheck::kHashRealTimeCheck;
    lookup_mechanism = std::make_unique<HashRealTimeMechanism>(
        url, url_checker_delegate_->GetThreatTypes(), database_manager_,
        ui_task_runner_, hash_realtime_service_on_ui_);
  } else if (hash_realtime_selection_ ==
                 HashRealTimeSelection::kDatabaseManager &&
             hash_realtime_utils::CanCheckUrl(url, request_destination_)) {
    performed_check = PerformedCheck::kHashRealTimeCheck;
    lookup_mechanism = std::make_unique<DatabaseManagerMechanism>(
        url, url_checker_delegate_->GetThreatTypes(), database_manager_,
        CheckBrowseUrlType::kHashRealTime);
  } else {
    performed_check = PerformedCheck::kHashDatabaseCheck;
    lookup_mechanism = std::make_unique<DatabaseManagerMechanism>(
        url, url_checker_delegate_->GetThreatTypes(), database_manager_,
        CheckBrowseUrlType::kHashDatabase);
  }
  DCHECK(performed_check != PerformedCheck::kUnknown);
  lookup_mechanism_runner_ =
      std::make_unique<SafeBrowsingLookupMechanismRunner>(
          std::move(lookup_mechanism),
          base::BindOnce(
              &SafeBrowsingUrlCheckerImpl::OnUrlResultAndMaybeDeleteSelf,
              weak_factory_.GetWeakPtr(), performed_check));
  return KickOffLookupMechanismResult(lookup_mechanism_runner_->Run(),
                                      performed_check);
}

void SafeBrowsingUrlCheckerImpl::BlockAndProcessUrlsAndMaybeDeleteSelf(
    bool showed_interstitial,
    bool has_post_commit_interstitial_skipped,
    PerformedCheck performed_check) {
  DVLOG(1) << "SafeBrowsingUrlCheckerImpl blocks URL: "
           << urls_[next_index_].url;
  state_ = STATE_BLOCKED;

  // If user decided to not proceed through a warning, mark all the remaining
  // redirects as "bad".
  while (next_index_ < urls_.size()) {
    if (!RunNextCallbackAndMaybeDeleteSelf(
            /*proceed=*/false, showed_interstitial,
            has_post_commit_interstitial_skipped, performed_check)) {
      return;
    }
  }
}

void SafeBrowsingUrlCheckerImpl::OnBlockingPageCompleteAndMaybeDeleteSelf(
    PerformedCheck performed_check,
    UnsafeResource::UrlCheckResult result) {
  DCHECK(state_ == STATE_DISPLAYING_BLOCKING_PAGE ||
         state_ == STATE_DELAYED_BLOCKING_PAGE);

  if (result.proceed) {
    state_ = STATE_NONE;
    if (!RunNextCallbackAndMaybeDeleteSelf(
            /*proceed=*/true, result.showed_interstitial,
            result.has_post_commit_interstitial_skipped, performed_check)) {
      return;
    }
    ProcessUrlsAndMaybeDeleteSelf();
  } else {
    BlockAndProcessUrlsAndMaybeDeleteSelf(
        result.showed_interstitial, result.has_post_commit_interstitial_skipped,
        performed_check);
  }
}

SBThreatType SafeBrowsingUrlCheckerImpl::CheckWebUIUrls(const GURL& url) {
  if (url == kChromeUISafeBrowsingMatchMalwareUrl) {
    return safe_browsing::SB_THREAT_TYPE_URL_MALWARE;
  }
  if (url == kChromeUISafeBrowsingMatchPhishingUrl) {
    return safe_browsing::SB_THREAT_TYPE_URL_PHISHING;
  }
  if (url == kChromeUISafeBrowsingMatchUnwantedUrl) {
    return safe_browsing::SB_THREAT_TYPE_URL_UNWANTED;
  }
  if (url == kChromeUISafeBrowsingMatchBillingUrl) {
    return safe_browsing::SB_THREAT_TYPE_BILLING;
  }
  return safe_browsing::SB_THREAT_TYPE_SAFE;
}

bool SafeBrowsingUrlCheckerImpl::RunNextCallbackAndMaybeDeleteSelf(
    bool proceed,
    bool showed_interstitial,
    bool has_post_commit_interstitial_skipped,
    PerformedCheck performed_check) {
  DCHECK_LT(next_index_, urls_.size());
  // OnCompleteCheck may delete *this*. Do not access internal members after
  // the call.
  auto weak_self = weak_factory_.GetWeakPtr();
  UrlInfo& url_info = urls_[next_index_++];
  url_info.notifier.OnCompleteCheck(proceed, showed_interstitial,
                                    has_post_commit_interstitial_skipped,
                                    performed_check);

  // Careful; `this` may be destroyed.
  return !!weak_self;
}

bool SafeBrowsingUrlCheckerImpl::CanPerformFullURLLookup(const GURL& url) {
  return url_real_time_lookup_enabled_ &&
         RealTimePolicyEngine::CanPerformFullURLLookupForRequestDestination(
             request_destination_, can_urt_check_subresource_url_) &&
         RealTimeUrlLookupServiceBase::CanCheckUrl(url);
}

}  // namespace safe_browsing
