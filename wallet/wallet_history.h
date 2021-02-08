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
          rpl::producer<std::optional<SelectedAsset>> selectedAsset);
  ~History();

  void updateGeometry(QPoint position, int width);
  [[nodiscard]] rpl::producer<int> heightValue() const;
  void setVisible(bool visible);
  void setVisibleTopBottom(int top, int bottom);

  [[nodiscard]] rpl::producer<std::pair<Ton::Symbol, Ton::TransactionId>> preloadRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> viewRequests() const;
  [[nodiscard]] rpl::producer<Ton::Transaction> decryptRequests() const;
  [[nodiscard]] rpl::producer<QSet<QString>> ownerResolutionRequests() const;

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
  bool mergePendingChanged(std::vector<Ton::PendingTransaction> &&list);
  bool mergeListChanged(std::map<Ton::Symbol, Ton::TransactionsSlice> &&data);
  void refreshRows();
  void refreshPending();
  void paint(Painter &p, QRect clip);
  void repaintRow(not_null<HistoryRow *> row);
  void repaintShadow(not_null<HistoryRow *> row);
  void checkPreload() const;

  void selectRow(int selected, const ClickHandlerPtr &handler);
  void selectRowByMouse();
  void pressRow();
  void releaseRow();
  void decryptById(const Ton::TransactionId &id);

  void computeInitTransactionId();
  void refreshShowDates();
  void setRowShowDate(const std::unique_ptr<HistoryRow> &row, bool show = true);
  bool takeDecrypted(int index, const std::vector<Ton::Transaction> &decrypted);
  [[nodiscard]] std::unique_ptr<HistoryRow> makeRow(const Ton::Transaction &data, bool isInit);

  struct TransactionsState {
    std::vector<Ton::Transaction> list;
    Ton::TransactionId previousId;
    Ton::TransactionId initTransactionId;
    int64 leastScannedTransactionLt = std::numeric_limits<int64>::max();
  };

  Ui::RpWidget _widget;

  std::vector<Ton::PendingTransaction> _pendingData;
  std::map<Ton::Symbol, TransactionsState> _transactions;

  rpl::variable<SelectedAsset> _selectedAsset;
  std::vector<std::unique_ptr<HistoryRow>> _pendingRows;
  std::map<Ton::Symbol, std::vector<std::unique_ptr<HistoryRow>>> _rows;
  std::map<QString, QString> _tokenOwners;
  int _visibleTop = 0;
  int _visibleBottom = 0;
  int _selected = -1;
  int _pressed = -1;

  rpl::event_stream<std::pair<Ton::Symbol, Ton::TransactionId>> _preloadRequests;
  rpl::event_stream<Ton::Transaction> _viewRequests;
  rpl::event_stream<Ton::Transaction> _decryptRequests;
  rpl::event_stream<QSet<QString>> _ownerResolutionRequests;
};

[[nodiscard]] rpl::producer<HistoryState> MakeHistoryState(rpl::producer<Ton::WalletViewerState> state);

}  // namespace Wallet
