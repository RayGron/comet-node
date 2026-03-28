#include "app/controller_app.h"

int main(int argc, char** argv) {
  comet::controller::ControllerApp app(argc, argv);
  return app.Run();
}
