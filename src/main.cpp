#include "driver/driver.hpp"

int main(int argc, char **argv) {
  zap::driver zapcDriver;

  if (!zapcDriver.parseArgs(argc, argv)) {
    return 0;
  }

  if (zapcDriver.splitInputs()) {
    return 1;
  }

  if (zapcDriver.verifySources()) {
    return 1;
  }

  if (zapcDriver.verifyOutput()) {
    return 1;
  }

  if (zapcDriver.compile()) {
    return 1;
  }

  int err = 0;
  if (zapcDriver.link()) {
    err = 1;
  }

  if (zapcDriver.cleanup()) {
    return 1;
  }

  return err;
}
