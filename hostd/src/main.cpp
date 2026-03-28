#include "app/hostd_app.h"

int main(int argc, char** argv) {
  comet::hostd::HostdApp app(argc, argv);
  return app.Run();
}
