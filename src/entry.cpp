#include "cli/cli.hpp"
#include "telemetry/log.hpp"

int main(int argc, char *argv[]) {
  mokai::PerfScope total_runtime("Total process execution");
  mokai::Cli app;
  return app.Run(argc, argv);
}
