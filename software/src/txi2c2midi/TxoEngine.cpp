#include <Arduino.h>

#include "TxoEngine.h"
#include "../ii/InterruptLock.h"

namespace txi2c2midi {
DMAMEM TxoEngine<phz::ii::InterruptLock> txo_engine;
} // namespace txi2c2midi
