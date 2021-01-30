#pragma once

#include "ui/layers/generic_box.h"

namespace Wallet {

struct DeployTokenWalletInvoice;

void DeployTokenWalletBox(not_null<Ui::GenericBox *> box, const DeployTokenWalletInvoice &invoice,
                          const Fn<void(DeployTokenWalletInvoice)> &done);

}  // namespace Wallet
