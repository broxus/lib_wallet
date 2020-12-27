#pragma once

#include "ui/layers/generic_box.h"

namespace Ton {
struct WalletState;
} // namespace Ton

namespace Wallet {

struct StakeInvoice;

enum class StakeInvoiceField {
	Amount,
};

void SendStakeBox(
	not_null<Ui::GenericBox*> box,
	const StakeInvoice &invoice,
	rpl::producer<Ton::WalletState> state,
	const Fn<void(StakeInvoice, Fn<void(StakeInvoiceField)> error)> &done);

} // namespace Wallet
