#include "common/event/signal_impl.h"

#include "common/event/dispatcher_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

SignalEventImpl::SignalEventImpl(DispatcherImpl& dispatcher, signal_t signal_num, SignalCb cb)
    : cb_(cb) {
  #ifndef WIN32
  evsignal_assign(
      &raw_event_, &dispatcher.base(), signal_num,
      [](evutil_socket_t, short, void* arg) -> void { static_cast<SignalEventImpl*>(arg)->cb_(); },
      this);
  evsignal_add(&raw_event_, nullptr);
  #else
  auto CtrlHandler = [](DWORD fdwCtrlType) -> BOOL {
    return 1;
  };
  if (!SetConsoleCtrlHandler(CtrlHandler, 1)) {
    PANIC("Could not set control handler.");
  }
  #endif
}

} // namespace Event
} // namespace Envoy
