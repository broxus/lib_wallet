// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ton/ton_state.h"
#include "ton/ton_result.h"

#include "wallet_common.h"

namespace Ui {
class RpWidget;
class ScrollArea;
}  // namespace Ui

namespace Wallet {

enum class Action;
enum class InfoTransition;

class Info final {
 public:
  struct Data {
    rpl::producer<Ton::WalletViewerState> state;
    rpl::producer<Ton::Result<std::pair<Ton::Symbol, Ton::LoadedSlice>>> loaded;
    rpl::producer<Ton::Update> updates;
    rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted;
    rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted;
    rpl::producer<not_null<std::map<QString, QString> *>> updateWalletOwners;
    rpl::producer<InfoTransition> transitionEvents;
    Fn<void(QImage, QString)> share;
    Fn<void()> openGate;
    bool justCreated = false;
    bool useTestNetwork = false;
  };
  Info(not_null<QWidget *> parent, Data data);
  ~Info();

  void setGeometry(QRect geometry);

  [[nodiscard]] rpl::producer<std::optional<SelectedAsset>> selectedAsset() const;

  [[nodiscard]] rpl::producer<Action> actionRequests() const;
  [[nodiscard]] rpl::producer<CustomAsset> removeAssetRequests() const;
  [[nodiscard]] rpl::producer<std::pair<Ton::Symbol, Ton::TransactionId>> preloadRequests() const;
  [[nodiscard]] rpl::producer<std::pair<int, int>> assetsReorderRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> viewRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> decryptRequests() const;
  [[nodiscard]] rpl::producer<std::pair<const Ton::Symbol *, const QSet<QString> *>> ownerResolutionRequests() const;
  [[nodiscard]] rpl::producer<const QString *> collectTokenRequests() const;
  [[nodiscard]] rpl::producer<const QString *> executeSwapBackRequests() const;

  [[nodiscard]] rpl::lifetime &lifetime();

 private:
  void setupControls(Data &&data);

  const std::unique_ptr<Ui::RpWidget> _widget;
  const not_null<Ui::ScrollArea *> _scroll;
  const not_null<Ui::RpWidget *> _inner;

  rpl::variable<std::optional<SelectedAsset>> _selectedAsset;

  rpl::event_stream<Action> _actionRequests;
  rpl::event_stream<CustomAsset> _removeAssetRequests;
  rpl::event_stream<std::pair<int, int>> _assetsReorderRequests;
  rpl::event_stream<std::pair<Ton::Symbol, Ton::TransactionId>> _preloadRequests;
  rpl::event_stream<Ton::Transaction> _viewRequests;
  rpl::event_stream<Ton::Transaction> _decryptRequests;
  rpl::event_stream<std::pair<const Ton::Symbol *, const QSet<QString> *>> _ownerResolutionRequests;
  rpl::event_stream<const QString *> _collectTokenRequests;
  rpl::event_stream<const QString *> _executeSwapBackRequests;
};

}  // namespace Wallet
