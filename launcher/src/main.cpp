#include "app/launcher_app.h"

int main(int argc, char** argv) {
  comet::launcher::LauncherApp app(argc, argv);
  return app.Run();
}
