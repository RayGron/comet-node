#pragma once

namespace comet::launcher {

class LauncherApp final {
 public:
  LauncherApp(int argc, char** argv);

  LauncherApp(const LauncherApp&) = delete;
  LauncherApp& operator=(const LauncherApp&) = delete;

  int Run();

 private:
  int argc_;
  char** argv_;
};

}  // namespace comet::launcher
