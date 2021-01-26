#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
}  // namespace Ton

namespace Wallet {

struct WithdrawalInvoice;

enum class DePoolWithdrawField {
  Amount,
};

void DePoolWithdrawBox(not_null<Ui::GenericBox *> box, const WithdrawalInvoice &invoice,
                       rpl::producer<Ton::WalletState> state,
                       const Fn<void(WithdrawalInvoice, Fn<void(DePoolWithdrawField)> error)> &done);

}  // namespace Wallet
