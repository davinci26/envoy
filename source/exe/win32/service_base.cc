#include "exe/service_base.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/thread_impl.h"
#include "common/event/signal_impl.h"
#include "exe/main_common.h"

#include <processenv.h>
#include <shellapi.h>

#include <locale>
#include <codecvt>
#define SVCNAME TEXT("ENVOY")

namespace Envoy {

namespace {
DWORD Win32FromHResult(HRESULT value) { return value & ~0x80070000; }

} // namespace

ServiceBase* ServiceBase::service_static = nullptr;

ServiceBase::ServiceBase(DWORD controlsAccepted) : handle_(0) {
  status_.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
  status_.dwCurrentState = SERVICE_START_PENDING;
  status_.dwControlsAccepted = controlsAccepted;
}

bool ServiceBase::TryRunAsService(ServiceBase& service) {
  RELEASE_ASSERT(service_static != nullptr, "Global pointer to service should not be null");
  service_static = &service;

  SERVICE_TABLE_ENTRYA service_table[] = {// Even though the service name is ignored for own process
                                          // services, it must be a valid string and cannot be 0.
                                          {SVCNAME, (LPSERVICE_MAIN_FUNCTIONA)ServiceMain},
                                          // Designates the end of table.
                                          {0, 0}};

  if (!::StartServiceCtrlDispatcherA(service_table)) {
    auto last_error = ::GetLastError();
    if (last_error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
      return false;
    } else {
      PANIC(
          fmt::format("Could not dispatch Envoy to start as a service with error {}", last_error));
    }
  }
  return true;
}

DWORD ServiceBase::Start(std::vector<std::string> args, DWORD control) {
  // Run the server listener loop outside try/catch blocks, so that unexpected exceptions
  // show up as a core-dumps for easier diagnostics.
  // #ifndef __APPLE__
  //   // absl::Symbolize mostly works without this, but this improves corner case
  //   // handling, such as running in a chroot jail.
  //   absl::InitializeSymbolizer(argv[0]);
  // #endif
  std::shared_ptr<Envoy::MainCommon> main_common;

  // Initialize the server's main context under a try/catch loop and simply return EXIT_FAILURE
  // as needed. Whatever code in the initialization path that fails is expected to log an error
  // message so the user can diagnose.
  try {
    main_common = std::make_shared<Envoy::MainCommon>(args);
    Envoy::Server::Instance* server = main_common->server();
    // if (server != nullptr && hook != nullptr) {
    //   hook(*server);
    // }
  } catch (const Envoy::NoServingException& e) {
    return S_OK;
  } catch (const Envoy::MalformedArgvException& e) {
    ENVOY_LOG_MISC(warn, "Envoy failed to start with {}", e.what());
    return E_INVALIDARG;
  } catch (const Envoy::EnvoyException& e) {
    ENVOY_LOG_MISC(warn, "Envoy failed to start with {}", e.what());
    return E_FAIL;
  }

  main_common->run();
  return main_common->run() ? S_OK : E_FAIL;
}

void ServiceBase::Stop(DWORD control) {
  auto handler = Event::eventBridgeHandlersSingleton::get()[ENVOY_SIGTERM];
  if (!handler) {
    return;
  }

  char data[] = {'a'};
  Buffer::RawSlice buffer{data, 1};
  auto result = handler->writev(&buffer, 1);
  RELEASE_ASSERT(result.rc_ == 1,
                 fmt::format("failed to write 1 byte: {}", result.err_->getErrorDetails()));
}

void ServiceBase::UpdateState(DWORD state, HRESULT errorCode) {
  status_.dwCurrentState = state;
  // TODO: Distinguish between envoy errors and win32 errors.
  if (FAILED(errorCode)) {
    if (FACILITY_WIN32 == HRESULT_FACILITY(errorCode)) {
      status_.dwWin32ExitCode = Win32FromHResult(errorCode);
    } else {
      status_.dwWin32ExitCode = ERROR_SERVICE_SPECIFIC_ERROR;
      status_.dwServiceSpecificExitCode = errorCode;
    }
  }

  SetServiceStatus();
}

void ServiceBase::SetServiceStatus() {
  RELEASE_ASSERT(service_static != nullptr, "Global pointer to service should not be null");
  if (!::SetServiceStatus(handle_, &status_)) {
    PANIC(
        fmt::format("Could not start StartServiceCtrlDispatcher with error {}", ::GetLastError()));
  }
}

void WINAPI ServiceBase::ServiceMain(DWORD argc, LPSTR* argv) {
  RELEASE_ASSERT(service_static != nullptr, "Global pointer to service should not be null");
  if (argc != 1 || argv == 0 || argv[0] == 0) {
    service_static->UpdateState(SERVICE_STOPPED, E_INVALIDARG);
  }

  service_static->handle_ = ::RegisterServiceCtrlHandler(SVCNAME, Handler);
  if (service_static->handle_ == 0) {
    service_static->UpdateState(SERVICE_STOPPED, ::GetLastError());
  }

  auto cli = std::wstring(::GetCommandLineW());
  int envoyArgCount = 0;
  LPWSTR* argvEnvoy = CommandLineToArgvW(cli.c_str(), &envoyArgCount);
  std::wstring_convert<std::codecvt_utf8<wchar_t>, wchar_t> converter;
  std::vector<std::string> args;
  args.reserve(envoyArgCount);
  for (int i = 0; i < envoyArgCount; ++i) {
    args.emplace_back(converter.to_bytes(std::wstring(argvEnvoy[i])));
  }

  service_static->SetServiceStatus();
  service_static->UpdateState(SERVICE_RUNNING);
  DWORD rc = service_static->Start(args, 0);
  service_static->UpdateState(SERVICE_STOPPED, rc);
}

void WINAPI ServiceBase::Handler(DWORD control) {
  // When the service control manager sends a control code to a service, it waits for the handler
  // function to return before sending additional control codes to other services.
  // The control handler should return as quickly as possible; if it does not return within 30
  // seconds, the SCM returns an error. If a service must do lengthy processing when the service is
  // executing the control handler, it should create a secondary thread to perform the lengthy
  // processing, and then return from the control handler.
  switch (control) {
  case SERVICE_CONTROL_SHUTDOWN:
  case SERVICE_CONTROL_PRESHUTDOWN:
  case SERVICE_CONTROL_STOP: {
    ENVOY_BUG(service_static->status_.dwCurrentState == SERVICE_RUNNING,
              "Attempting to stop Envoy service when it is not running");
    service_static->UpdateState(SERVICE_STOP_PENDING);
    service_static->Stop(control);
    break;
  }
  }
}
} // namespace Envoy