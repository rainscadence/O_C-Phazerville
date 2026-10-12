#include <Arduino.h>

#include "TxiRaw.h"
#include "../ii/InterruptLock.h"

namespace txi2c2midi {
DMAMEM TxiRawQueries<phz::ii::InterruptLock> txi_queries;
}  // namespace txi2c2midi
