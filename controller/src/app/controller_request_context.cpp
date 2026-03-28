#include "app/controller_request_context.h"

namespace comet::controller {

const HttpRequest*& ControllerRequestContext::CurrentSlot() {
  thread_local const HttpRequest* current_request = nullptr;
  return current_request;
}

ControllerRequestContext::Scope::Scope(const HttpRequest& request)
    : previous_(ControllerRequestContext::Current()) {
  ControllerRequestContext::CurrentSlot() = &request;
}

ControllerRequestContext::Scope::~Scope() {
  ControllerRequestContext::CurrentSlot() = previous_;
}

const HttpRequest* ControllerRequestContext::Current() {
  return CurrentSlot();
}

}  // namespace comet::controller
