// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_
#define MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_

#include <stdint.h>

#include <map>
#include <memory>

#include <optional>
#include "base/check.h"
#include "base/component_export.h"
#include "base/containers/circular_deque.h"
#include "base/containers/small_map.h"
#include "base/memory/scoped_refptr.h"
#include "base/sequence_checker.h"
#include "base/synchronization/lock.h"
#include "base/types/pass_key.h"
#include "mojo/public/cpp/bindings/associated_group_controller.h"
#include "mojo/public/cpp/bindings/connection_group.h"
#include "mojo/public/cpp/bindings/connector.h"
#include "mojo/public/cpp/bindings/interface_id.h"
#include "mojo/public/cpp/bindings/message_dispatcher.h"
#include "mojo/public/cpp/bindings/pending_flush.h"
#include "mojo/public/cpp/bindings/pipe_control_message_handler.h"
#include "mojo/public/cpp/bindings/pipe_control_message_handler_delegate.h"
#include "mojo/public/cpp/bindings/pipe_control_message_proxy.h"
#include "mojo/public/cpp/bindings/scoped_interface_endpoint_handle.h"

namespace base {
class SequencedTaskRunner;
}

namespace mojo {

class AsyncFlusher;
class PendingFlush;

namespace internal {

// MultiplexRouter supports routing messages for multiple interfaces over a
// single message pipe.
//
// It is partially sequence-affine with several public methods that must be
// called on the sequence to which the MultiplexRouter is bound. See the
// constructor for details.
class COMPONENT_EXPORT(MOJO_CPP_BINDINGS) MultiplexRouter
    : public MessageReceiver,
      public AssociatedGroupController,
      public PipeControlMessageHandlerDelegate {
 public:
  enum Config {
    // There is only the primary interface running on this router. Please note
    // that because of interface versioning, the other side of the message pipe
    // may use a newer primary interface definition which passes associated
    // interfaces. In that case, this router may still receive pipe control
    // messages or messages targetting associated interfaces.
    SINGLE_INTERFACE,
    // Similar to the mode above, there is only the primary interface running on
    // this router. Besides, the primary interface has sync methods.
    SINGLE_INTERFACE_WITH_SYNC_METHODS,
    // There may be associated interfaces running on this router.
    MULTI_INTERFACE
  };

  // Constructs a new MultiplexRouter whose primary bound sequence is determined
  // by `runner`. See below for public methods which are safe to call from any
  // sequence. Other methods must be called from the same sequence used by
  // `runner`.
  //
  // If `set_interface_id_namespace_bit` is true, the interface IDs generated by
  // this router will have the highest bit set.
  //
  // Note that the MultiplexRouter will not initially receive any messages or
  // disconnect events until StartReceiving() is explicitly called. To create a
  // MultiplexRouter which calls this automatically at construction time, use
  // CreateAndStartReceiving().
  static scoped_refptr<MultiplexRouter> Create(
      ScopedMessagePipeHandle message_pipe,
      Config config,
      bool set_interface_id_namespace_bit,
      scoped_refptr<base::SequencedTaskRunner> runner,
      const char* primary_interface_name = "unknown interface");

  // Same as above, but automatically calls StartReceiving() before returning.
  // If `runner` does not run tasks in sequence with the caller, the returned
  // MultiplexRouter may already begin receiving messages and events on `runner`
  // before this call returns.
  static scoped_refptr<MultiplexRouter> CreateAndStartReceiving(
      ScopedMessagePipeHandle message_pipe,
      Config config,
      bool set_interface_id_namespace_bit,
      scoped_refptr<base::SequencedTaskRunner> runner,
      const char* primary_interface_name = "unknown interface");

  // Starts receiving messages on the MultiplexRouter. Once this is called,
  // CloseMessagePipe() or PassMessagePipe() MUST be called in sequence with
  // the MultiplexRouter's `task_runner_` prior to destroying the
  // MultiplexRouter.
  void StartReceiving();

  MultiplexRouter(base::PassKey<MultiplexRouter>,
                  ScopedMessagePipeHandle message_pipe,
                  Config config,
                  bool set_interface_id_namespace_bit,
                  scoped_refptr<base::SequencedTaskRunner> runner,
                  const char* primary_interface_name = "unknown interface");

  MultiplexRouter(const MultiplexRouter&) = delete;
  MultiplexRouter& operator=(const MultiplexRouter&) = delete;

  // Sets a MessageReceiver which can filter a message after validation but
  // before dispatch.
  void SetIncomingMessageFilter(std::unique_ptr<MessageFilter> filter);

  // Adds this object to a ConnectionGroup identified by |ref|. All receiving
  // pipe endpoints decoded from inbound messages on this MultiplexRouter will
  // be added to the same group.
  void SetConnectionGroup(ConnectionGroup::Ref ref);

  // ---------------------------------------------------------------------------
  // The following public methods are safe to call from any sequence.

  // AssociatedGroupController implementation:
  InterfaceId AssociateInterface(
      ScopedInterfaceEndpointHandle handle_to_send) override;
  ScopedInterfaceEndpointHandle CreateLocalEndpointHandle(
      InterfaceId id) override;
  void CloseEndpointHandle(
      InterfaceId id,
      const std::optional<DisconnectReason>& reason) override;
  InterfaceEndpointController* AttachEndpointClient(
      const ScopedInterfaceEndpointHandle& handle,
      InterfaceEndpointClient* endpoint_client,
      scoped_refptr<base::SequencedTaskRunner> runner) override;
  void DetachEndpointClient(
      const ScopedInterfaceEndpointHandle& handle) override;
  void RaiseError() override;
  bool PrefersSerializedMessages() override;

  // ---------------------------------------------------------------------------
  // The following public methods are called on the creating sequence.

  // Please note that this method shouldn't be called unless it results from an
  // explicit request of the user of bindings (e.g., the user sets an
  // InterfacePtr to null or closes a Binding).
  void CloseMessagePipe();

  // Extracts the underlying message pipe.
  ScopedMessagePipeHandle PassMessagePipe() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    DCHECK(!HasAssociatedEndpoints());
    return connector_.PassMessagePipe();
  }

  // Blocks the current sequence until the first incoming message.
  bool WaitForIncomingMessage() {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return connector_.WaitForIncomingMessage();
  }

  // See Binding for details of pause/resume.
  void PauseIncomingMethodCallProcessing();
  void ResumeIncomingMethodCallProcessing();

  // Initiates an async flush operation. |flusher| signals its corresponding
  // PendingFlush when the flush is actually complete.
  void FlushAsync(AsyncFlusher flusher);

  // Pauses the peer endpoint's message processing until a (potentially remote)
  // flush operation corresponding to |flush| is completed.
  void PausePeerUntilFlushCompletes(PendingFlush flush);

  // Whether there are any associated interfaces running currently.
  bool HasAssociatedEndpoints() const;

  // See comments on Binding::EnableBatchDispatch().
  void EnableBatchDispatch();

  // Sets this object to testing mode.
  // In testing mode, the object doesn't disconnect the underlying message pipe
  // when it receives unexpected or invalid messages.
  void EnableTestingMode();

  // Is the router bound to a message pipe handle?
  bool is_valid() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return connector_.is_valid();
  }

  // TODO(yzshen): consider removing this getter.
  MessagePipeHandle handle() const {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
    return connector_.handle();
  }

  bool SimulateReceivingMessageForTesting(ScopedMessageHandle handle) {
    return connector_.SimulateReadMessage(std::move(handle));
  }

 private:
  class InterfaceEndpoint;
  class MessageWrapper;
  struct Task;

  ~MultiplexRouter() override;

  // Indicates whether `message` can unblock any active external sync waiter.
  bool CanUnblockExternalSyncWait(const Message& message);

  // Indicates whether `message` can unblock the current exclusive same-thread
  // sync wait.
  bool CanUnblockExclusiveSameThreadSyncWait(const Message& message);

  // MessageReceiver implementation:
  bool Accept(Message* message) override;

  // PipeControlMessageHandlerDelegate implementation:
  bool OnPeerAssociatedEndpointClosed(
      InterfaceId id,
      const std::optional<DisconnectReason>& reason) override;
  bool WaitForFlushToComplete(ScopedMessagePipeHandle flush_pipe) override;

  void OnPipeConnectionError(bool force_async_dispatch);
  void OnFlushPipeSignaled(MojoResult result, const HandleSignalsState& state);
  void PauseInternal(bool must_resume_manually);

  // Waits for a specific incoming message to be received and dispatched,
  // deferring all other messages (including sync messages) until later.
  bool ExclusiveSyncWaitForReply(InterfaceId interface_id, uint64_t request_id);

  // Specifies whether we are allowed to directly call into
  // InterfaceEndpointClient (given that we are already on the same sequence as
  // the client).
  enum ClientCallBehavior {
    // Don't call any InterfaceEndpointClient methods directly.
    NO_DIRECT_CLIENT_CALLS,
    // Only call InterfaceEndpointClient::HandleIncomingMessage directly to
    // handle sync messages.
    ALLOW_DIRECT_CLIENT_CALLS_FOR_SYNC_MESSAGES,
    // Allow to call any InterfaceEndpointClient methods directly.
    ALLOW_DIRECT_CLIENT_CALLS
  };

  // Processes enqueued tasks (incoming messages and error notifications).
  // |current_task_runner| is only used when |client_call_behavior| is
  // ALLOW_DIRECT_CLIENT_CALLS to determine whether we are on the right task
  // runner to make client calls for async messages or connection error
  // notifications.
  //
  // Note: Because calling into InterfaceEndpointClient may lead to destruction
  // of this object, if direct calls are allowed, the caller needs to hold on to
  // a ref outside of |lock_| before calling this method.
  void ProcessTasks(ClientCallBehavior client_call_behavior,
                    base::SequencedTaskRunner* current_task_runner);

  // Processes the first queued sync message for the endpoint corresponding to
  // |id|; returns whether there are more sync messages for that endpoint in the
  // queue.
  //
  // This method is only used by enpoints during sync watching. Therefore, not
  // all sync messages are handled by it.
  bool ProcessFirstSyncMessageForEndpoint(InterfaceId id);

  // Returns true to indicate that |task|/|message| has been processed.
  bool ProcessNotifyErrorTask(Task* task,
                              ClientCallBehavior client_call_behavior,
                              base::SequencedTaskRunner* current_task_runner);
  bool ProcessIncomingMessage(MessageWrapper* message_wrapper,
                              ClientCallBehavior client_call_behavior,
                              base::SequencedTaskRunner* current_task_runner);

  void MaybePostToProcessTasks(base::SequencedTaskRunner* task_runner);
  void LockAndCallProcessTasks();

  // Updates the state of |endpoint|. If both the endpoint and its peer have
  // been closed, removes it from |endpoints_|.
  // NOTE: The method may invalidate |endpoint|.
  enum EndpointStateUpdateType { ENDPOINT_CLOSED, PEER_ENDPOINT_CLOSED };
  void UpdateEndpointStateMayRemove(InterfaceEndpoint* endpoint,
                                    EndpointStateUpdateType type);

  void RaiseErrorInNonTestingMode();

  InterfaceEndpoint* FindOrInsertEndpoint(InterfaceId id, bool* inserted);
  InterfaceEndpoint* FindEndpoint(InterfaceId id);

  // Returns false if some interface IDs are invalid or have been used.
  bool InsertEndpointsForMessage(const Message& message);
  void CloseEndpointsForMessage(const Message& message);

  void AssertLockAcquired();

  const Config config_;

  // Whether to set the namespace bit when generating interface IDs. Please see
  // comments of kInterfaceIdNamespaceMask.
  const bool set_interface_id_namespace_bit_;

  scoped_refptr<base::SequencedTaskRunner> task_runner_;

  MessageDispatcher dispatcher_;
  Connector connector_;

  // Active whenever dispatch is blocked by a pending remote flush.
  ScopedMessagePipeHandle active_flush_pipe_;
  std::optional<mojo::SimpleWatcher> flush_pipe_watcher_;

  // Tracks information about the current exclusive sync wait, if any, on the
  // MultiplexRouter's primary thread. Note that exclusive off-thread sync waits
  // are not managed by the MultiplexRouter and thus are not relevant here.
  struct ExclusiveSyncWaitInfo {
    InterfaceId interface_id = kInvalidInterfaceId;
    uint64_t request_id = 0;
    bool finished = false;
  };
  std::optional<ExclusiveSyncWaitInfo> exclusive_sync_wait_;

  SEQUENCE_CHECKER(sequence_checker_);

  // Protects the following members.
  // Not set in Config::SINGLE_INTERFACE* mode.
  mutable std::optional<base::Lock> lock_;
  PipeControlMessageHandler control_message_handler_;

  // NOTE: It is unsafe to call into this object while holding |lock_|.
  PipeControlMessageProxy control_message_proxy_;

  base::small_map<std::map<InterfaceId, scoped_refptr<InterfaceEndpoint>>, 1>
      endpoints_;
  uint32_t next_interface_id_value_ = 1;

  base::circular_deque<std::unique_ptr<Task>> tasks_;
  // It refers to tasks in |tasks_| and doesn't own any of them.
  std::map<InterfaceId, base::circular_deque<Task*>> sync_message_tasks_;

  bool posted_to_process_tasks_ = false;
  scoped_refptr<base::SequencedTaskRunner> posted_to_task_runner_;

  bool encountered_error_ = false;

  // Indicates whether this router is paused, meaning it is not currently
  // listening for or dispatching available inbound messages.
  bool paused_ = false;

  // If this router is paused, this indicates whether the pause is due to an
  // explicit call to |PauseIncomingMethodCallProcessing()| when |true|, or
  // due implicit pause when waiting on an async flush operation when |false|.
  // When |paused_| is |false|, this value is ignored.
  bool must_resume_manually_ = false;

  bool testing_mode_ = false;

  bool being_destructed_ = false;
};

}  // namespace internal
}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_BINDINGS_LIB_MULTIPLEX_ROUTER_H_
