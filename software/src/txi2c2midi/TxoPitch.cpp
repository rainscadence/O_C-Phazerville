#include <Arduino.h>

#include "TxoPitch.h"
#include "../ii/InterruptLock.h"

namespace txi2c2midi {
DMAMEM TxoPitch<phz::ii::InterruptLock> txo_pitch;
} // namespace txi2c2midi
