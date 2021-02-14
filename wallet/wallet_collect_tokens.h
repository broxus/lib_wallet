#pragma once

#include "ui/layers/generic_box.h"

namespace Wallet {

struct CollectTokensInvoice;

void CollectTokensBox(not_null<Ui::GenericBox *> box, const CollectTokensInvoice &invoice,
                      const Fn<void(CollectTokensInvoice)> &done);

}  // namespace Wallet
