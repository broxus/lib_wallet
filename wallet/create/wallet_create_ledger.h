// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "wallet/create/wallet_create_step.h"

namespace Ton {
struct TonLedgerKey;
} // namespace Ton

namespace Wallet::Create {

class Ledger final : public Step {
 public:
  explicit Ledger(const std::vector<Ton::TonLedgerKey> &ledgerKeys);

  int desiredHeight() const override;

  std::vector<Ton::TonLedgerKey> passLedgerKeys() const;

 private:
  void initControls(const std::vector<Ton::TonLedgerKey> &ledgerKeys);
  void showFinishedHook() override;

  int _desiredHeight = 0;

  Fn<std::vector<Ton::TonLedgerKey>()> _passLedgerKeys;
};

}  // namespace Wallet::Create
