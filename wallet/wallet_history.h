// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "ton/ton_state.h"

#include "ui/rp_widget.h"
#include "ui/click_handler.h"

#include "wallet_common.h"

class Painter;

namespace Wallet {

struct HistoryState {
  std::map<Ton::Symbol, Ton::TransactionsSlice> lastTransactions;
  std::vector<Ton::PendingTransaction> pendingTransactions;
};

class HistoryRow;

class History final {
 public:
  History(not_null<Ui::RpWidget *> parent, rpl::producer<HistoryState> state,
          rpl::producer<std::pair<Ton::Symbol, Ton::LoadedSlice>> loaded,
          rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
          rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted,
          rpl::producer<not_null<std::map<QString, QString> *>> updateWalletOwners,
          rpl::producer<NotificationsHistoryUpdate> updateNotifications,
          rpl::producer<std::optional<SelectedAsset>> selectedAsset);
  ~History();

  void updateGeometry(QPoint position, int width);
  [[nodiscard]] rpl::producer<int> heightValue() const;
  void setVisible(bool visible);
  void setVisibleTopBottom(int top, int bottom);

  [[nodiscard]] rpl::producer<std::pair<Ton::Symbol, Ton::TransactionId>> preloadRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> viewRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> decryptRequests() const;
  [[nodiscard]] rpl::producer<std::pair<const Ton::Symbol *, const QSet<QString> *>> ownerResolutionRequests() const;
  [[nodiscard]] rpl::producer<not_null<const Ton::Transaction *>> notificationDetailsRequests() const;
  [[nodiscard]] rpl::producer<not_null<const QString *>> collectTokenRequests() const;
  [[nodiscard]] rpl::producer<not_null<const QString *>> executeSwapBackRequests() const;

  [[nodiscard]] rpl::lifetime &lifetime();

 private:
  struct ScrollState {
    Ton::TransactionId top;
    int offset = 0;
  };

  void setupContent(rpl::producer<HistoryState> &&state,
                    rpl::producer<std::pair<Ton::Symbol, Ton::LoadedSlice>> &&loaded,
                    rpl::producer<std::optional<SelectedAsset>> &&selectedAsset);
  void resizeToWidth(int width);
  void mergeState(HistoryState &&state);
  void mergePending(std::vector<Ton::PendingTransaction> &&list);
  void mergeNotifications(NotificationsHistoryUpdate &&update);
  bool mergeListChanged(std::map<Ton::Symbol, Ton::TransactionsSlice> &&data);
  void refreshRows(const SelectedAsset &selectedAsset);
  void refreshPending();
  void paint(Painter &p, QRect clip);
  void repaintRow(not_null<HistoryRow *> row);
  void repaintShadow(not_null<HistoryRow *> row);
  void checkPreload() const;

  void selectRow(const std::pair<bool, int> &selected, const ClickHandlerPtr &handler);
  void selectRowByMouse();
  void pressRow();
  void releaseRow();
  void decryptById(const Ton::TransactionId &id);

  void refreshShowDates(const SelectedAsset &selectedAsset);
  void setRowShowDate(not_null<HistoryRow *> row, bool show = true);
  bool takeDecrypted(int index, const std::vector<Ton::Transaction> &decrypted);
  [[nodiscard]] std::unique_ptr<HistoryRow> makeRow(const Ton::Transaction &data);
  [[nodiscard]] Ton::Symbol currentSymbol() const;

  struct TransactionsState {
    std::vector<Ton::Transaction> list;
    Ton::TransactionId previousId;
    int64 latestScannedTransactionLt = 0;
    int64 leastScannedTransactionLt = std::numeric_limits<int64>::max();
  };

  struct RowsState {
    std::vector<std::unique_ptr<HistoryRow>> pending;
    std::vector<std::unique_ptr<HistoryRow>> regular;
  };

  Ui::RpWidget _widget;

  bool _pendingDataChanged{};
  std::vector<Ton::PendingTransaction> _pendingData;
  std::map<Ton::Symbol, TransactionsState> _transactions;

  rpl::variable<SelectedAsset> _selectedAsset;
  std::map<Ton::Symbol, RowsState> _rows;
  std::map<QString, QString> _tokenOwners;
  int _visibleTop = 0;
  int _visibleBottom = 0;

  std::pair<bool, int> _selected = std::make_pair(false, -1);
  std::pair<bool, int> _pressed = std::make_pair(false, -1);

  rpl::event_stream<std::pair<Ton::Symbol, Ton::TransactionId>> _preloadRequests;
  rpl::event_stream<Ton::Transaction> _viewRequests;
  rpl::event_stream<Ton::Transaction> _decryptRequests;
  rpl::event_stream<std::pair<const Ton::Symbol *, const QSet<QString> *>> _ownerResolutionRequests;
  rpl::event_stream<not_null<const Ton::Transaction *>> _notificationDetailsRequests;
  rpl::event_stream<not_null<const QString *>> _collectTokenRequests;
  rpl::event_stream<not_null<const QString *>> _executeSwapBackRequests;
};

[[nodiscard]] rpl::producer<HistoryState> MakeHistoryState(rpl::producer<Ton::WalletViewerState> state);

}  // namespace Wallet
