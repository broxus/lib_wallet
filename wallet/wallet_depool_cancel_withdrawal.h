#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
}  // namespace Ton

namespace Wallet {

struct CancelWithdrawalInvoice;

void DePoolCancelWithdrawalBox(not_null<Ui::GenericBox *> box, const CancelWithdrawalInvoice &invoice,
                               const Fn<void(CancelWithdrawalInvoice)> &done);

}  // namespace Wallet
