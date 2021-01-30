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
}  // namespace Ton

namespace Wallet {

enum class AddressLabelType { YourAddress, TokenAddress, DePoolAddress };

struct EmptyHistoryState {
  QString address;
  AddressLabelType addressType = AddressLabelType::YourAddress;
  bool justCreated = false;
};

class EmptyHistory final {
 public:
  EmptyHistory(not_null<Ui::RpWidget *> parent, rpl::producer<EmptyHistoryState> state,
               Fn<void(QImage, QString)> share);

  void setGeometry(QRect geometry);
  void setVisible(bool visible);

  [[nodiscard]] rpl::lifetime &lifetime();

 private:
  void setupControls(rpl::producer<EmptyHistoryState> &&state);

  Ui::RpWidget _widget;
  Fn<void(QImage, QString)> _share;
};

[[nodiscard]] rpl::producer<EmptyHistoryState> MakeEmptyHistoryState(
    rpl::producer<Ton::WalletViewerState> state, rpl::producer<std::optional<SelectedAsset>> selectedAsset,
    bool justCreated);

}  // namespace Wallet
