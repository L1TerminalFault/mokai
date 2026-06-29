#include "cli/cli.hpp"
#include "log/log.h"

int main(int argc, char *argv[]) {
  mokai::PerfScope total_runtime("Total process execution");
  mokai::Cli app;
  return app.Run(argc, argv);
}
