// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ui/rp_widget.h"

#include "wallet_common.h"

namespace Ton {
struct WalletViewerState;
enum class Currency;
}  // namespace Ton

namespace Wallet {

struct CoverState {
  SelectedAsset asset;
  int128 unlockedBalance = 0;
  int64 lockedBalance = 0;
  int64 reward = 0;
  bool justCreated = false;
  bool useTestNetwork = false;
  bool reinvest = false;
  bool isDeployed = false;

  [[nodiscard]] auto selectedToken() const -> Ton::Symbol;
};

class Cover final {
 public:
  Cover(not_null<Ui::RpWidget *> parent, rpl::producer<CoverState> state);

  void setGeometry(QRect geometry);
  [[nodiscard]] int height() const;

  [[nodiscard]] rpl::producer<> sendRequests() const;
  [[nodiscard]] rpl::producer<> receiveRequests() const;
  [[nodiscard]] rpl::producer<> deployRequest() const;

  [[nodiscard]] rpl::lifetime &lifetime();

 private:
  void setupControls();
  void setupBalance();

  Ui::RpWidget _widget;

  rpl::variable<CoverState> _state;
  rpl::event_stream<> _sendRequests;
  rpl::event_stream<> _receiveRequests;
  rpl::event_stream<> _deployRequests;
};

[[nodiscard]] rpl::producer<CoverState> MakeCoverState(rpl::producer<Ton::WalletViewerState> state,
                                                       rpl::producer<std::optional<SelectedAsset>> selectedAsset,
                                                       bool justCreated, bool useTestNetwork);

}  // namespace Wallet
