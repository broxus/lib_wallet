#pragma once

#include "ui/rp_widget.h"
#include "ui/inline_token_icon.h"
#include "ui/click_handler.h"
#include "ui/widgets/buttons.h"

class Painter;

namespace Ui {
class ScrollArea;
}  // namespace Ui

namespace Ton {
struct WalletViewerState;
}  // namespace Ton

namespace Wallet {

struct TokenItem {
  Ton::Symbol token = Ton::Symbol::ton();
  QString address = "";
  int64_t balance = 0;
};

struct DePoolItem {
  QString address = "";
  int64_t total = 0;
  int64_t reward = 0;
};

using AssetItem = std::variant<TokenItem, DePoolItem>;

bool operator==(const AssetItem &a, const AssetItem &b);
bool operator!=(const AssetItem &a, const AssetItem &b);

struct AssetsListState {
  std::vector<AssetItem> items;
};

class AssetsListRow;

class AssetsList final {
 public:
  AssetsList(not_null<Ui::RpWidget *> parent, rpl::producer<AssetsListState> state, not_null<Ui::ScrollArea *> scroll);
  ~AssetsList();

  void setGeometry(QRect geometry);

  [[nodiscard]] rpl::producer<AssetItem> openRequests() const;
  [[nodiscard]] rpl::producer<> gateOpenRequests() const;
  [[nodiscard]] rpl::producer<int> heightValue() const;

  [[nodiscard]] rpl::lifetime &lifetime();

 private:
  void setupContent(rpl::producer<AssetsListState> &&state);

  void refreshItemValues(const AssetsListState &data);
  bool mergeListChanged(AssetsListState &&data);

  Ui::RpWidget _widget;
  not_null<Ui::ScrollArea *> _scroll;

  std::vector<std::unique_ptr<AssetsListRow>> _rows;
  std::vector<Ui::RoundButton *> _buttons;
  rpl::variable<int> _height;

  rpl::event_stream<AssetItem> _openRequests;
  rpl::event_stream<> _gateOpenRequests;
};

[[nodiscard]] rpl::producer<AssetsListState> MakeTokensListState(rpl::producer<Ton::WalletViewerState> state);

}  // namespace Wallet
