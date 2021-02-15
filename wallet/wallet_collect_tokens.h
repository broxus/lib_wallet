#pragma once

#include "ui/layers/generic_box.h"
#include "ton/ton_result.h"
#include "ton/ton_state.h"

namespace Wallet {

struct CollectTokensInvoice;

void CollectTokensBox(not_null<Ui::GenericBox *> box, const CollectTokensInvoice &invoice,
                      rpl::producer<Ton::Result<Ton::EthEventDetails>> loadedEventDetails,
                      rpl::producer<Ton::Symbol> loadedSymbol, const Fn<void(QImage, QString)> &share,
                      const Fn<void(CollectTokensInvoice)> &done);

}  // namespace Wallet
