// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_history.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "base/unixtime.h"
#include "base/flags.h"
#include "base/object_ptr.h"
#include "ui/address_label.h"
#include "ui/inline_token_icon.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/rp_widget.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/effects/animations.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <iostream>
#include <QtCore/QDateTime>
#include <utility>

namespace Wallet {
namespace {

constexpr auto kPreloadScreens = 3;
constexpr auto kCommentLinesMax = 3;

enum class Flag : uchar {
  Incoming = 0x01,
  Pending = 0x02,
  Encrypted = 0x04,
  Service = 0x08,
  Initialization = 0x10,
  SwapBack = 0x20,
  DePoolReward = 0x40,
  DePoolStake = 0x80,
};
inline constexpr bool is_flag_type(Flag) {
  return true;
};
using Flags = base::flags<Flag>;

struct TransactionLayout {
  TimeId serverTime = 0;
  QDateTime dateTime;
  Ui::Text::String date;
  Ui::Text::String time;
  Ui::Text::String amountGrams;
  Ui::Text::String amountNano;
  Ui::Text::String address;
  Ui::Text::String comment;
  Ui::Text::String fees;
  int addressWidth = 0;
  int addressHeight = 0;
  Flags flags = Flags();
};

enum class EventType {
  EthEvent,
  TonEvent,
};

[[nodiscard]] const style::TextStyle &addressStyle() {
  const static auto result = Ui::ComputeAddressStyle(st::defaultTextStyle);
  return result;
}

void refreshTimeTexts(TransactionLayout &layout, bool forceDateText = false) {
  layout.dateTime = base::unixtime::parse(layout.serverTime);
  layout.time.setText(st::defaultTextStyle, ph::lng_wallet_short_time(layout.dateTime.time())(ph::now));
  if (layout.date.isEmpty() && !forceDateText) {
    return;
  }
  if (layout.flags & Flag::Pending) {
    layout.date.setText(st::semiboldTextStyle, ph::lng_wallet_row_pending_date(ph::now));
  } else {
    layout.date.setText(st::semiboldTextStyle, ph::lng_wallet_short_date(layout.dateTime.date())(ph::now));
  }
}

[[nodiscard]] TransactionLayout prepareLayout(const Ton::Transaction &data, const Fn<void()> &decrypt,
                                              bool isInitTransaction) {
  const auto service = IsServiceTransaction(data);
  const auto encrypted = IsEncryptedMessage(data) && decrypt;
  const auto amount = FormatAmount(service ? (-data.fee) : CalculateValue(data), Ton::Symbol::ton(),
                                   FormatFlag::Signed | FormatFlag::Rounded);
  const auto incoming = !data.incoming.source.isEmpty();
  const auto pending = (data.id.lt == 0);
  const auto address = Ton::Wallet::ConvertIntoRaw(ExtractAddress(data));
  const auto addressPartWidth = [&](int from, int length = -1) {
    return addressStyle().font->width(address.mid(from, length));
  };

  auto result = TransactionLayout();
  result.serverTime = data.time;
  result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
  result.amountNano.setText(st::walletRowNanoStyle, amount.separator + amount.nanoString);
  result.address =
      Ui::Text::String(addressStyle(), service ? QString() : address, _defaultOptions, st::walletAddressWidthMin);
  result.addressWidth = (addressStyle().font->spacew / 2) +
                        std::max(addressPartWidth(0, address.size() / 2), addressPartWidth(address.size() / 2));
  result.addressHeight = addressStyle().font->height * 2;
  result.comment = Ui::Text::String(st::walletAddressWidthMin);
  result.comment.setText(st::defaultTextStyle, (encrypted ? QString() : ExtractMessage(data)), _textPlainOptions);
  const auto fee = FormatAmount(data.fee, Ton::Symbol::ton()).full;
  result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));

  result.flags = Flag(0)                                                 //
                 | (service ? Flag::Service : Flag(0))                   //
                 | (isInitTransaction ? Flag::Initialization : Flag(0))  //
                 | (encrypted ? Flag::Encrypted : Flag(0))               //
                 | (incoming ? Flag::Incoming : Flag(0))                 //
                 | (pending ? Flag::Pending : Flag(0));

  refreshTimeTexts(result);
  return result;
}

[[nodiscard]] TransactionLayout prepareLayout(const Ton::Transaction &data,
                                              const Ton::DePoolTransaction &dePoolTransaction) {
  const auto [value, fee, flags] = v::match(
      dePoolTransaction,
      [&](const Ton::DePoolOrdinaryStakeTransaction &dePoolOrdinaryStakeTransaction) {
        return std::make_tuple(-dePoolOrdinaryStakeTransaction.stake,
                               -CalculateValue(data) - dePoolOrdinaryStakeTransaction.stake + data.otherFee,
                               Flag::DePoolStake);
      },
      [&](const Ton::DePoolOnRoundCompleteTransaction &dePoolOnRoundCompleteTransaction) {
        return std::make_tuple(dePoolOnRoundCompleteTransaction.reward, data.otherFee, Flag::DePoolReward);
      });

  const auto token = Ton::Symbol::ton();

  const auto amount = FormatAmount(value, token, FormatFlag::Signed | FormatFlag::Rounded);

  const auto incoming = !data.incoming.source.isEmpty();
  const auto pending = (data.id.lt == 0);
  const auto address = Ton::Wallet::ConvertIntoRaw(ExtractAddress(data));
  const auto addressPartWidth = [&](int from, int length = -1) {
    return addressStyle().font->width(address.mid(from, length));
  };

  auto result = TransactionLayout();
  result.serverTime = data.time;
  result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
  result.amountNano.setText(st::walletRowNanoStyle, amount.separator + amount.nanoString);
  result.address = Ui::Text::String(addressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
  result.addressWidth = (addressStyle().font->spacew / 2) +
                        std::max(addressPartWidth(0, address.size() / 2), addressPartWidth(address.size() / 2));
  result.addressHeight = addressStyle().font->height * 2;
  result.comment = Ui::Text::String(st::walletAddressWidthMin);
  result.comment.setText(st::defaultTextStyle, {}, _textPlainOptions);

  result.fees.setText(st::defaultTextStyle,
                      ph::lng_wallet_row_fees(ph::now).replace("{amount}", FormatAmount(fee, Ton::Symbol::ton()).full));

  result.flags = Flag(0)                                  //
                 | (incoming ? Flag::Incoming : Flag(0))  //
                 | (pending ? Flag::Pending : Flag(0))    //
                 | flags;

  refreshTimeTexts(result);
  return result;
}

[[nodiscard]] TransactionLayout prepareLayout(const Ton::Symbol &token, const Ton::Transaction &transaction,
                                              const Ton::TokenTransaction &tokenTransaction) {
  const auto [address, value, incoming, flags] = v::match(
      tokenTransaction,
      [](const Ton::TokenTransfer &transfer) {
        return std::make_tuple(transfer.direct ? Ton::kZeroAddress : Ton::Wallet::ConvertIntoRaw(transfer.address),
                               transfer.value, transfer.incoming, Flag(0));
      },
      [](const Ton::TokenMint &tokenMint) {
        return std::make_tuple(QString{}, tokenMint.value, /*incoming*/ true, Flag(0));
      },
      [](const Ton::TokenSwapBack &tokenSwapBack) {
        return std::make_tuple(tokenSwapBack.address, tokenSwapBack.value, /*incoming*/ false, Flag::SwapBack);
      });

  const auto amount = FormatAmount(incoming ? value : -value, token, FormatFlag::Signed | FormatFlag::Rounded);
  const auto addressPartWidth = [address = std::ref(address)](int from, int length = -1) {
    return addressStyle().font->width(address.get().mid(from, length));
  };

  auto result = TransactionLayout();
  result.serverTime = transaction.time;
  result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
  result.amountNano.setText(st::walletRowNanoStyle, amount.separator + amount.nanoString);
  result.address = Ui::Text::String(addressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
  result.addressWidth = (addressStyle().font->spacew / 2) +
                        std::max(addressPartWidth(0, address.size() / 2), addressPartWidth(address.size() / 2));
  result.addressHeight = addressStyle().font->height * 2;
  result.comment = Ui::Text::String(st::walletAddressWidthMin);
  result.comment.setText(st::defaultTextStyle, {}, _textPlainOptions);

  const auto fee = FormatAmount(CalculateValue(transaction), Ton::Symbol::ton()).full;
  result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));

  result.flags = flags | (incoming ? Flag::Incoming : Flag(0));

  refreshTimeTexts(result);
  return result;
}

using AdditionalTransactionInfo = std::variant<Ton::TokenTransaction, Ton::DePoolTransaction>;

}  // namespace

class HistoryRow final {
 public:
  explicit HistoryRow(Ton::Transaction transaction, const Fn<void()> &decrypt = nullptr, bool isInitTransaction = false)
      : _symbol(Ton::Symbol::ton())
      , _layout(prepareLayout(transaction, _decrypt, isInitTransaction))
      , _transaction(std::move(transaction))
      , _decrypt(decrypt)
      , _isInitTransaction(isInitTransaction) {
  }

  HistoryRow(const HistoryRow &) = delete;
  HistoryRow &operator=(const HistoryRow &) = delete;

  [[nodiscard]] const Ton::TransactionId &id() const {
    return _transaction.id;
  }

  [[nodiscard]] const QDateTime &date() const {
    return _layout.dateTime;
  }

  [[nodiscard]] const Ton::Transaction &transaction() const {
    return _transaction;
  }

  void refreshDate() {
    refreshTimeTexts(_layout);
  }

  void setShowDate(bool show, const Fn<void()> &repaintDate) {
    _width = 0;
    if (!show) {
      _layout.date.clear();
    } else {
      _repaintDate = std::move(repaintDate);
      refreshTimeTexts(_layout, true);
    }
  }

  void setDecryptionFailed() {
    _width = 0;
    _decryptionFailed = true;
    _layout.comment.setText(st::defaultTextStyle, ph::lng_wallet_decrypt_failed(ph::now), _textPlainOptions);
  }

  bool showDate() const {
    return !_layout.date.isEmpty();
  }

  [[nodiscard]] int top() const {
    return _top;
  }
  void setTop(int top) {
    _top = top;
  }

  void resizeToWidth(int width) {
    if (_width == width) {
      return;
    }
    _width = width;
    if (!isVisible()) {
      return;
    }

    const auto padding = st::walletRowPadding;
    const auto use = std::min(_width, st::walletRowWidthMax);
    const auto avail = use - padding.left() - padding.right();

    _height = 0;
    if (!_layout.date.isEmpty()) {
      _height += st::walletRowDateSkip;
    }
    _height += padding.top() + _layout.amountGrams.minHeight();
    if (!_layout.address.isEmpty()) {
      _height += st::walletRowAddressTop + _layout.addressHeight;
    }
    if (!_layout.comment.isEmpty()) {
      _commentHeight =
          std::min(_layout.comment.countHeight(avail), st::defaultTextStyle.font->height * kCommentLinesMax);
      _height += st::walletRowCommentTop + _commentHeight;
    }
    if (!_layout.fees.isEmpty()) {
      _height += st::walletRowFeesTop + _layout.fees.minHeight();
    }
    _height += padding.bottom();
  }
  [[nodiscard]] int height() const {
    return _height;
  }
  [[nodiscard]] int bottom() const {
    return _top + _height;
  }

  void setVisible(bool visible) {
    if (visible) {
      _height = 1;
      resizeToWidth(_width);
    } else {
      _height = 0;
    }
    if (_button.has_value()) {
      (*_button)->setVisible(visible);
    }
  }
  [[nodiscard]] bool isVisible() const {
    return _height > 0;
  }

  void clearAdditionalInfo() {
    resetButton();
    _additionalInfo.reset();
    _symbol = Ton::Symbol::ton();
    _layout = prepareLayout(_transaction, _decrypt, _isInitTransaction);
  }
  void setAdditionalInfo(const Ton::Symbol &symbol, Ton::TokenTransaction &&tokenTransaction) {
    resetButton();
    _layout = prepareLayout(symbol, _transaction, tokenTransaction);
    _additionalInfo = std::forward<Ton::TokenTransaction>(tokenTransaction);
    _symbol = symbol;
  }
  void setAdditionalInfo(Ton::DePoolTransaction &&dePoolTransaction) {
    resetButton();
    _layout = prepareLayout(_transaction, dePoolTransaction);
    _additionalInfo = std::forward<Ton::DePoolTransaction>(dePoolTransaction);
    _symbol = Ton::Symbol::ton();
  }
  void setAdditionalInfo(not_null<Ui::RpWidget *> parent, EventType eventType, const Fn<void()> &openRequest) {
    auto button = object_ptr<Ui::RoundButton>(  //
        parent,
        eventType == EventType::EthEvent  //
            ? ph::lng_wallet_history_receive_tokens()
            : ph::lng_wallet_history_execute_callback(),
        st::walletRowButton);
    button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
    button->setVisible(false);
    button->setClickedCallback(openRequest);
    if (_button.has_value()) {
      (*_button)->setParent(nullptr);
    }
    _button = std::move(button);
  }

  void paint(Painter &p, int x, int y) {
    if (!isVisible()) {
      return;
    }

    const auto padding = st::walletRowPadding;
    const auto use = std::min(_width, st::walletRowWidthMax);
    const auto avail = use - padding.left() - padding.right();
    x += (_width - use) / 2 + padding.left();

    if (!_layout.date.isEmpty()) {
      y += st::walletRowDateSkip;
    } else {
      const auto shadowLeft = (use < _width) ? (x - st::walletRowShadowAdd) : x;
      const auto shadowWidth =
          (use < _width) ? (avail + 2 * st::walletRowShadowAdd) : _width - padding.left() - padding.right();
      p.fillRect(shadowLeft, y, shadowWidth, st::lineWidth, st::shadowFg);
    }
    y += padding.top();

    if (_layout.flags & Flag::Service) {
      const auto labelLeft = x;
      const auto labelTop = y + st::walletRowGramsStyle.font->ascent - st::normalFont->ascent;
      p.setPen(st::windowFg);
      p.setFont(st::normalFont);
      p.drawText(labelLeft, labelTop + st::normalFont->ascent,
                 ((_layout.flags & Flag::Initialization) ? ph::lng_wallet_row_init(ph::now)
                                                         : ph::lng_wallet_row_service(ph::now)));
    } else {
      const auto incoming = (_layout.flags & Flag::Incoming);
      const auto swapBack = (_layout.flags & Flag::SwapBack);

      const auto reward = (_layout.flags & Flag::DePoolReward);
      const auto stake = (_layout.flags & Flag::DePoolStake);

      p.setPen(incoming ? st::boxTextFgGood : st::boxTextFgError);
      _layout.amountGrams.draw(p, x, y, avail);

      const auto nanoTop = y + st::walletRowGramsStyle.font->ascent - st::walletRowNanoStyle.font->ascent;
      const auto nanoLeft = x + _layout.amountGrams.maxWidth();
      _layout.amountNano.draw(p, nanoLeft, nanoTop, avail);

      const auto diamondTop = y + st::walletRowGramsStyle.font->ascent - st::normalFont->ascent;
      const auto diamondLeft = nanoLeft + _layout.amountNano.maxWidth() + st::normalFont->spacew;
      Ui::PaintInlineTokenIcon(_symbol, p, diamondLeft, diamondTop, st::normalFont);

      const auto labelTop = diamondTop;
      const auto labelLeft = diamondLeft + st::walletDiamondSize + st::normalFont->spacew;
      p.setPen(st::windowFg);
      p.setFont(st::normalFont);
      p.drawText(labelLeft, labelTop + st::normalFont->ascent,
                 (incoming   ? reward ? ph::lng_wallet_row_reward_from(ph::now) : ph::lng_wallet_row_from(ph::now)
                    : swapBack ? ph::lng_wallet_row_swap_back_to(ph::now)
                  : stake    ? ph::lng_wallet_row_ordinary_stake_to(ph::now)
                             : ph::lng_wallet_row_to(ph::now)));

      const auto timeTop = labelTop;
      const auto timeLeft = x + avail - _layout.time.maxWidth();
      p.setPen(st::windowSubTextFg);
      _layout.time.draw(p, timeLeft, timeTop, avail);
      if (_layout.flags & Flag::Encrypted) {
        const auto iconLeft = x + avail - st::walletCommentIconLeft - st::walletCommentIcon.width();
        const auto iconTop = labelTop + st::walletCommentIconTop;
        st::walletCommentIcon.paint(p, iconLeft, iconTop, avail);
      }
      if (_layout.flags & Flag::Pending) {
        st::walletRowPending.paint(p, (timeLeft - st::walletRowPendingPosition.x() - st::walletRowPending.width()),
                                   timeTop + st::walletRowPendingPosition.y(), avail);
      }
    }
    y += _layout.amountGrams.minHeight();

    if (_button.has_value()) {
      auto &button = *_button;
      const auto buttonWidth = button->width();
      button->setGeometry(x + avail - buttonWidth, y + st::walletRowAddressTop, buttonWidth, _layout.addressHeight);
      button->setVisible(true);
    }

    if (!_layout.address.isEmpty()) {
      p.setPen(st::windowFg);
      y += st::walletRowAddressTop;
      _layout.address.drawElided(p, x, y, _layout.addressWidth, 2, style::al_topleft, 0, -1, 0, true);
      y += _layout.addressHeight;
    }
    if (!_layout.comment.isEmpty()) {
      y += st::walletRowCommentTop;
      if (_decryptionFailed) {
        p.setPen(st::boxTextFgError);
      }
      _layout.comment.drawElided(p, x, y, avail, kCommentLinesMax);
      y += _commentHeight;
    }
    if (!_layout.fees.isEmpty()) {
      p.setPen(st::windowSubTextFg);
      y += st::walletRowFeesTop;
      _layout.fees.draw(p, x, y, avail);
    }
  }
  void paintDate(Painter &p, int x, int y) {
    if (!isVisible()) {
      return;
    }

    Expects(!_layout.date.isEmpty());
    Expects(_repaintDate != nullptr);

    const auto hasShadow = (y != top());
    if (_dateHasShadow != hasShadow) {
      _dateHasShadow = hasShadow;
      _dateShadowShown.start(_repaintDate, hasShadow ? 0. : 1., hasShadow ? 1. : 0., st::widgetFadeDuration);
    }
    const auto line = st::lineWidth;
    const auto noShadowHeight = st::walletRowDateHeight - line;

    if (_dateHasShadow || _dateShadowShown.animating()) {
      p.setOpacity(_dateShadowShown.value(_dateHasShadow ? 1. : 0.));
      p.fillRect(x, y + noShadowHeight, _width, line, st::shadowFg);
    }

    const auto padding = st::walletRowPadding;
    const auto use = std::min(_width, st::walletRowWidthMax);
    x += (_width - use) / 2;

    p.setOpacity(0.9);
    p.fillRect(x, y, use, noShadowHeight, st::windowBg);

    const auto avail = use - padding.left() - padding.right();
    x += padding.left();
    p.setOpacity(1.);
    p.setPen(st::windowFg);
    _layout.date.draw(p, x, y + st::walletRowDateTop, avail);
  }

  [[nodiscard]] bool isUnderCursor(QPoint point) const {
    return isVisible() && computeInnerRect().contains(point);
  }
  [[nodiscard]] ClickHandlerPtr handlerUnderCursor(QPoint point) const {
    return nullptr;
  }

 private:
  [[nodiscard]] QRect computeInnerRect() const {
    const auto padding = st::walletRowPadding;
    const auto use = std::min(_width, st::walletRowWidthMax);
    const auto avail = use - padding.left() - padding.right();
    const auto left = (use < _width) ? ((_width - use) / 2 + padding.left() - st::walletRowShadowAdd) : 0;
    const auto width = (use < _width) ? (avail + 2 * st::walletRowShadowAdd) : _width;
    auto y = top();
    if (!_layout.date.isEmpty()) {
      y += st::walletRowDateSkip;
    }
    return QRect(left, y, width, bottom() - y);
  }

  void resetButton() {
    if (_button.has_value()) {
      (*_button)->setParent(nullptr);
      _button.reset();
    }
  }

  Ton::Symbol _symbol;
  TransactionLayout _layout;

  Ton::Transaction _transaction;
  std::optional<AdditionalTransactionInfo> _additionalInfo{};

  Fn<void()> _decrypt = [] {};

  bool _isInitTransaction = false;
  int _top = 0;
  int _width = 0;
  int _height = 0;
  int _commentHeight = 0;

  Ui::Animations::Simple _dateShadowShown;
  Fn<void()> _repaintDate;
  bool _dateHasShadow = false;
  bool _decryptionFailed = false;
  std::optional<object_ptr<Ui::RoundButton>> _button = std::nullopt;
};

History::History(not_null<Ui::RpWidget *> parent, rpl::producer<HistoryState> state,
                 rpl::producer<std::pair<Ton::Symbol, Ton::LoadedSlice>> loaded,
                 rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
                 rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted,
                 rpl::producer<not_null<std::map<QString, QString> *>> updateWalletOwners,
                 rpl::producer<std::optional<SelectedAsset>> selectedAsset)
    : _widget(parent), _selectedAsset(SelectedToken{.symbol = Ton::Symbol::ton()}) {
  setupContent(std::move(state), std::move(loaded), std::move(selectedAsset));

  base::unixtime::updates()  //
      | rpl::start_with_next(
            [=] {
              for (auto &items : _rows) {
                for (auto &row : items.second.pendingRows) {
                  row->refreshDate();
                }
                for (auto &row : items.second.regular) {
                  row->refreshDate();
                }
              }
              refreshShowDates();
            },
            _widget.lifetime());

  std::move(collectEncrypted)  //
      | rpl::start_with_next(
            [=](not_null<std::vector<Ton::Transaction> *> list) {
              auto it = _transactions.find(Ton::Symbol::ton());
              if (it != end(_transactions)) {
                auto &&encrypted = ranges::views::all(it->second.list) | ranges::views::filter(IsEncryptedMessage);
                list->insert(list->end(), encrypted.begin(), encrypted.end());
              }
            },
            _widget.lifetime());

  std::move(updateDecrypted)  //
      | rpl::start_with_next(
            [=](not_null<const std::vector<Ton::Transaction> *> list) {
              auto it = _transactions.find(Ton::Symbol::ton());
              if (it == end(_transactions)) {
                return;
              }
              auto &transactions = it->second;

              auto changed = false;
              for (auto i = 0, count = static_cast<int>(transactions.list.size()); i != count; ++i) {
                if (IsEncryptedMessage(transactions.list[i])) {
                  if (takeDecrypted(i, *list)) {
                    changed = true;
                  }
                }
              }
              if (changed) {
                refreshShowDates();
              }
            },
            _widget.lifetime());

  std::move(updateWalletOwners)  //
      | rpl::start_with_next(
            [=](not_null<std::map<QString, QString> *> owners) {
              if (owners->empty()) {
                return;
              }

              auto shouldUpdate = false;
              for (auto &&[wallet, owner] : *owners.get()) {
                shouldUpdate |=
                    _tokenOwners
                        .emplace(std::piecewise_construct, std::forward_as_tuple(wallet), std::forward_as_tuple(owner))
                        .second;
              }

              const auto selectedAsset = _selectedAsset.current();
              const auto selectedToken = std::get_if<SelectedToken>(&selectedAsset);
              if (selectedToken != nullptr && selectedToken->symbol.isToken() && shouldUpdate) {
                refreshShowDates();
              }
            },
            _widget.lifetime());
}

History::~History() = default;

void History::updateGeometry(QPoint position, int width) {
  _widget.move(position);
  resizeToWidth(width);
}

void History::resizeToWidth(int width) {
  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  if (!width) {
    return;
  }

  auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  auto &rows = rowsIt->second;

  auto top = (rows.pendingRows.empty() && rows.regular.empty()) ? 0 : st::walletRowsSkip;
  int height = 0;
  for (const auto &row : rows.pendingRows) {
    row->setTop(top + height);
    row->resizeToWidth(width);
    height += row->height();
  }
  for (const auto &row : rows.regular) {
    row->setTop(top + height);
    row->resizeToWidth(width);
    height += row->height();
  }
  _widget.resize(width, (height > 0 ? top * 2 : 0) + height);

  checkPreload();
}

rpl::producer<int> History::heightValue() const {
  return _widget.heightValue();
}

void History::setVisible(bool visible) {
  _widget.setVisible(visible);
}

void History::setVisibleTopBottom(int top, int bottom) {
  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  _visibleTop = top - _widget.y();
  _visibleBottom = bottom - _widget.y();

  auto transactionsIt = _transactions.find(symbol);
  auto rowsIt = _rows.find(symbol);
  if (_visibleBottom <= _visibleTop ||
      (transactionsIt != end(_transactions) && !transactionsIt->second.previousId.lt) ||
      (rowsIt != end(_rows) && rowsIt->second.regular.empty())) {
    return;
  }
  checkPreload();
}

rpl::producer<std::pair<Ton::Symbol, Ton::TransactionId>> History::preloadRequests() const {
  return _preloadRequests.events();
}

rpl::producer<Ton::Transaction> History::viewRequests() const {
  return _viewRequests.events();
}

rpl::producer<Ton::Transaction> History::decryptRequests() const {
  return _decryptRequests.events();
}

rpl::producer<std::pair<const Ton::Symbol *, const QSet<QString> *>> History::ownerResolutionRequests() const {
  return _ownerResolutionRequests.events();
}

rpl::producer<const QString *> History::newTokenWalletRequests() const {
  return _newTokenWalletRequests.events();
}

rpl::producer<const QString *> History::collectTokenRequests() const {
  return _collectTokenRequests.events();
}

rpl::producer<const QString *> History::executeSwapBackRequests() const {
  return _executeSwapBackRequests.events();
}

rpl::lifetime &History::lifetime() {
  return _widget.lifetime();
}

void History::setupContent(rpl::producer<HistoryState> &&state,
                           rpl::producer<std::pair<Ton::Symbol, Ton::LoadedSlice>> &&loaded,
                           rpl::producer<std::optional<SelectedAsset>> &&selectedAsset) {
  std::move(state) | rpl::start_with_next([=](HistoryState &&state) { mergeState(std::move(state)); }, lifetime());

  std::move(loaded)  //
      | rpl::start_with_next(
            [=](const std::pair<Ton::Symbol, Ton::LoadedSlice> &slice) {
              auto it = _transactions.find(slice.first);
              if (it == end(_transactions)) {
                it = _transactions
                         .emplace(std::piecewise_construct, std::forward_as_tuple(slice.first),
                                  std::forward_as_tuple(TransactionsState{}))
                         .first;
              }
              auto &transactions = it->second;

              const auto loadedLast = (transactions.previousId.lt != 0) && (slice.second.data.previousId.lt == 0);
              transactions.previousId = slice.second.data.previousId;
              transactions.list.insert(end(transactions.list), slice.second.data.list.begin(),
                                       slice.second.data.list.end());
              if (loadedLast) {
                computeInitTransactionId();
              }
              refreshRows();
            },
            lifetime());

  _widget.paintRequest()  //
      | rpl::start_with_next(
            [=](QRect clip) {
              auto p = Painter(&_widget);
              paint(p, clip);
            },
            lifetime());

  _widget.setAttribute(Qt::WA_MouseTracking);
  _widget.events()  //
      | rpl::start_with_next(
            [=](not_null<QEvent *> e) {
              switch (e->type()) {
                case QEvent::Leave:
                  selectRow(-1, nullptr);
                  return;
                case QEvent::Enter:
                case QEvent::MouseMove:
                  selectRowByMouse();
                  return;
                case QEvent::MouseButtonPress:
                  pressRow();
                  return;
                case QEvent::MouseButtonRelease:
                  releaseRow();
                  return;
                default:
                  return;
              }
            },
            lifetime());

  rpl::combine(std::move(selectedAsset))  //
      | rpl::start_with_next(
            [=](const std::optional<SelectedAsset> &asset) {
              _selectedAsset = asset.value_or(SelectedToken::defaultToken());
              refreshShowDates();
            },
            _widget.lifetime());
}

void History::selectRow(int selected, const ClickHandlerPtr &handler) {
  Expects(selected >= 0 || !handler);

  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = rowsIt->second;

  if (_selected != selected) {
    const auto was = (_selected >= 0 && _selected < int(rows.regular.size())) ? rows.regular[_selected].get() : nullptr;
    if (was) {
      repaintRow(was);
    }
    _selected = selected;
    _widget.setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
  }
  if (ClickHandler::getActive() != handler) {
    const auto now = (_selected >= 0 && _selected < int(rows.regular.size())) ? rows.regular[_selected].get() : nullptr;
    if (now) {
      repaintRow(now);
    }
    ClickHandler::setActive(handler);
  }
}

void History::selectRowByMouse() {
  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = rowsIt->second;

  const auto point = _widget.mapFromGlobal(QCursor::pos());
  const auto from = ranges::upper_bound(rows.regular, point.y(), ranges::less(), &HistoryRow::bottom);
  const auto till = ranges::lower_bound(rows.regular, point.y(), ranges::less(), &HistoryRow::top);

  if (from != rows.regular.end() && from != till && (*from)->isUnderCursor(point)) {
    selectRow(from - begin(rows.regular), (*from)->handlerUnderCursor(point));
  } else {
    selectRow(-1, nullptr);
  }
}

void History::pressRow() {
  _pressed = _selected;
  ClickHandler::pressed();
}

void History::releaseRow() {
  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto transactionsIt = _transactions.find(symbol);
  const auto rowsIt = _rows.find(symbol);
  if (transactionsIt == end(_transactions) || rowsIt == end(_rows)) {
    return;
  }
  const auto &transactions = transactionsIt->second;
  const auto &rows = rowsIt->second;

  Expects(_selected < static_cast<int>(rows.regular.size()));

  const auto handler = ClickHandler::unpressed();
  if (std::exchange(_pressed, -1) != _selected || _selected < 0) {
    if (handler)
      handler->onClick(ClickContext());
    return;
  }
  if (handler) {
    handler->onClick(ClickContext());
  } else {
    const auto i = ranges::find(transactions.list, rows.regular[_selected]->id(), &Ton::Transaction::id);
    Assert(i != end(transactions.list));
    _viewRequests.fire_copy(*i);
  }
}

void History::decryptById(const Ton::TransactionId &id) {
  const auto transactionsIt = _transactions.find(Ton::Symbol::ton());
  if (transactionsIt == _transactions.end()) {
    return;
  }
  const auto &transactions = transactionsIt->second;

  const auto i = ranges::find(transactions.list, id, &Ton::Transaction::id);
  Assert(i != end(transactions.list));
  _decryptRequests.fire_copy(*i);
}

void History::paint(Painter &p, QRect clip) {
  const auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  auto rowsIt = _rows.find(symbol);
  if (rowsIt == _rows.end()) {
    return;
  }
  const auto &rows = rowsIt->second;

  if (rows.pendingRows.empty() && rows.regular.empty()) {
    return;
  }

  const auto paintRows = [&](const std::vector<std::unique_ptr<HistoryRow>> &rows) {
    const auto from = ranges::upper_bound(rows, clip.top(), ranges::less(), &HistoryRow::bottom);
    const auto till = ranges::lower_bound(rows, clip.top() + clip.height(), ranges::less(), &HistoryRow::top);
    if (from == till || from == rows.end()) {
      return;
    }
    for (const auto &row : ranges::make_subrange(from, till)) {
      row->paint(p, 0, row->top());
    }
    auto lastDateTop = rows.back()->bottom();
    const auto dates = ranges::make_subrange(begin(rows), till);
    for (const auto &row : dates | ranges::views::reverse) {
      if (!row->showDate()) {
        continue;
      }
      const auto top = std::max(std::min(_visibleTop, lastDateTop - st::walletRowDateHeight), row->top());
      row->paintDate(p, 0, top);
      if (row->top() <= _visibleTop) {
        break;
      }
      lastDateTop = top;
    }
  };
  paintRows(rows.pendingRows);
  paintRows(rows.regular);
}

void History::mergeState(HistoryState &&state) {
  mergePending(std::move(state.pendingTransactions));
  refreshPending();
  if (mergeListChanged(std::move(state.lastTransactions))) {
    refreshRows();
  }
}

void History::mergePending(std::vector<Ton::PendingTransaction> &&list) {
  _pendingDataChanged = _pendingData != list;
  if (_pendingDataChanged) {
    _pendingData = std::move(list);
  }
}

bool History::mergeListChanged(std::map<Ton::Symbol, Ton::TransactionsSlice> &&data) {
  auto changed = false;
  for (auto &&[symbol, newTransactions] : data) {
    auto transactionsIt = _transactions.find(symbol);
    if (transactionsIt == end(_transactions)) {
      transactionsIt = _transactions
                           .emplace(std::piecewise_construct, std::forward_as_tuple(symbol),
                                    std::forward_as_tuple(TransactionsState{}))
                           .first;
    }
    auto &transactions = transactionsIt->second;

    const auto i = transactions.list.empty()  //
                       ? newTransactions.list.cend()
                       : ranges::find(std::as_const(newTransactions.list), transactions.list.front());
    if (i == newTransactions.list.cend()) {
      transactions.list = newTransactions.list | ranges::to_vector;
      transactions.previousId = std::move(newTransactions.previousId);
      if (!transactions.previousId.lt) {
        computeInitTransactionId();
      }
      changed = true;
    } else if (i != newTransactions.list.cbegin()) {
      transactions.list.insert(begin(transactions.list), newTransactions.list.cbegin(), i);
      changed = true;
    }
  }
  return changed;
}

void History::setRowShowDate(const std::unique_ptr<HistoryRow> &row, bool show) {
  const auto raw = row.get();
  row->setShowDate(show, [=] { repaintShadow(raw); });
}

bool History::takeDecrypted(int index, const std::vector<Ton::Transaction> &decrypted) {
  const auto symbol = Ton::Symbol::ton();
  auto rowsIt = _rows.find(symbol);
  auto transactionsIt = _transactions.find(symbol);
  Expects(rowsIt != _rows.end() && transactionsIt != _transactions.end());
  auto &rows = rowsIt->second;
  auto &transactions = transactionsIt->second;

  Expects(index >= 0 && index < transactions.list.size());
  Expects(index >= 0 && index < rows.regular.size());
  Expects(rows.regular[index]->id() == transactions.list[index].id);

  const auto i = ranges::find(decrypted, transactions.list[index].id, &Ton::Transaction::id);
  if (i == end(decrypted)) {
    return false;
  }
  if (IsEncryptedMessage(*i)) {
    rows.regular[index]->setDecryptionFailed();
  } else {
    transactions.list[index] = *i;
    rows.regular[index] = makeRow(*i, i->id == transactions.initTransactionId);
  }
  return true;
}

std::unique_ptr<HistoryRow> History::makeRow(const Ton::Transaction &data, bool isInit) {
  const auto id = data.id;
  if (id.lt == 0) {
    // pending
    return std::make_unique<HistoryRow>(data);
  }

  return std::make_unique<HistoryRow>(
      data, [=] { decryptById(id); }, isInit);
}

void History::computeInitTransactionId() {
  for (auto &[symbol, transactions] : _transactions) {
    const auto was = transactions.initTransactionId;
    auto found = static_cast<Ton::Transaction *>(nullptr);
    for (auto &row : ranges::views::reverse(transactions.list)) {
      if (IsServiceTransaction(row)) {
        found = &row;
        break;
      } else if (row.incoming.source.isEmpty()) {
        break;
      }
    }
    const auto now = found ? found->id : Ton::TransactionId();
    if (was == now) {
      return;
    }

    transactions.initTransactionId = now;

    auto it = _rows.find(symbol);
    const auto wasItem = ranges::find(transactions.list, was, &Ton::Transaction::id);
    if (wasItem != end(transactions.list)) {
      wasItem->initializing = false;

      if (it != end(_rows)) {
        const auto wasRow = ranges::find(it->second.regular, was, &HistoryRow::id);
        if (wasRow != end(it->second.regular)) {
          *wasRow = makeRow(*wasItem, false);
        }
      }
    }
    if (found) {
      found->initializing = true;
      if (it != end(_rows)) {
        const auto nowRow = ranges::find(it->second.regular, now, &HistoryRow::id);
        if (nowRow != end(it->second.regular)) {
          *nowRow = makeRow(*found, true);
        }
      }
    }
  }
}

void History::refreshShowDates() {
  const auto selectedAsset = _selectedAsset.current();

  const auto [symbol, targetAddress] = v::match(
      selectedAsset, [](const SelectedToken &selectedToken) { return std::make_pair(selectedToken.symbol, QString{}); },
      [](const SelectedDePool &selectedDePool) {
        return std::make_pair(Ton::Symbol::ton(), Ton::Wallet::ConvertIntoRaw(selectedDePool.address));
      });

  const auto rowsIt = _rows.find(symbol);
  const auto transactionsIt = _transactions.find(symbol);
  if (rowsIt == _rows.end() || transactionsIt == _transactions.end()) {
    return;
  }
  auto &rows = rowsIt->second;
  auto &transactions = transactionsIt->second;

  QSet<QString> unknownOwners;

  std::map<QString, Ton::EthEventStatus> latestEthStatuses;
  std::map<QString, Ton::TonEventStatus> latestTonStatuses;

  auto filterTransaction = [&, targetAddress = targetAddress](const SelectedAsset &selectedAsset,
                                                              decltype(rows.regular.front()) &row) {
    const auto &transaction = row->transaction();

    const auto isUnprocessed = transaction.id.lt < transactions.leastScannedTransactionLt ||
                               transaction.id.lt > transactions.latestScannedTransactionLt;

    v::match(
        selectedAsset,
        [&](const SelectedToken &selectedToken) {
          row->setVisible(true);
          row->clearAdditionalInfo();

          if (selectedToken.symbol.isTon()) {
            if (auto notification = Ton::Wallet::ParseNotification(transaction.incoming.message);
                notification.has_value()) {
              v::match(
                  *notification,
                  [&](const Ton::TokenWalletDeployed &deployed) {
                    _newTokenWalletRequests.fire(&deployed.rootTokenContract);
                  },
                  [&](const Ton::EthEventStatusChanged &event) {
                    const auto it = latestEthStatuses.find(transaction.incoming.source);
                    if (it != latestEthStatuses.end()) {
                      return;
                    }
                    latestEthStatuses.insert(std::make_pair(transaction.incoming.source, event.status));
                    if (event.status == Ton::EthEventStatus::Confirmed) {
                      row->setAdditionalInfo(&_widget, EventType::EthEvent, [=, address = transaction.incoming.source] {
                        _collectTokenRequests.fire(&address);
                      });
                    }
                  },
                  [&](const Ton::TonEventStatusChanged &event) {
                    const auto it = latestTonStatuses.find(transaction.incoming.source);
                    if (it != latestTonStatuses.end()) {
                      return;
                    }
                    latestTonStatuses.insert(std::make_pair(transaction.incoming.source, event.status));
                    if (event.status == Ton::TonEventStatus::Confirmed) {
                      row->setAdditionalInfo(&_widget, EventType::TonEvent, [=, address = transaction.incoming.source] {
                        _executeSwapBackRequests.fire(&address);
                      });
                    }
                  });
            }
            return;
          }

          if (transaction.aborted || !transaction.incoming.bounce) {
            return row->setVisible(false);
          }

          auto tokenTransaction = Ton::Wallet::ParseTokenTransaction(transaction.incoming.message);
          if (!tokenTransaction.has_value()) {
            return row->setVisible(false);
          }

          v::match(
              *tokenTransaction,
              [&](Ton::TokenTransfer &tokenTransfer) {
                if (!tokenTransfer.direct) {
                  return;
                }
                const auto it = _tokenOwners.find(tokenTransfer.address);
                if (it != _tokenOwners.end()) {
                  tokenTransfer.address = it->second;
                  tokenTransfer.direct = false;
                } else if (isUnprocessed) {
                  unknownOwners.insert(tokenTransfer.address);
                }
              },
              [](auto &&) {});

          row->setAdditionalInfo(selectedToken.symbol, std::move(*tokenTransaction));
        },
        [&](const SelectedDePool &selectedDePool) {
          const auto incoming = !transaction.incoming.source.isEmpty();

          if (incoming && Ton::Wallet::ConvertIntoRaw(transaction.incoming.source) == targetAddress) {
            auto parsedTransaction = Ton::Wallet::ParseDePoolTransaction(transaction.incoming.message, incoming);
            if (parsedTransaction.has_value()) {
              row->setAdditionalInfo(std::move(*parsedTransaction));
              row->setVisible(true);
              return;
            }
          } else if (!incoming) {
            if (transaction.aborted) {
              return row->setVisible(false);
            }

            for (const auto &out : transaction.outgoing) {
              if (Ton::Wallet::ConvertIntoRaw(out.destination) != targetAddress) {
                continue;
              }

              auto stakeTransaction = Ton::Wallet::ParseDePoolTransaction(out.message, incoming);
              if (!stakeTransaction.has_value()) {
                break;
              }

              row->setAdditionalInfo(std::move(*stakeTransaction));
              row->setVisible(true);
              return;
            }
          }

          row->setVisible(false);
        });
  };

  //  for (auto &row : rows.pendingRows) {
  //    filterTransaction(SelectedToken{.symbol = Ton::Symbol::ton()}, row);
  //  }

  auto previous = QDate();
  for (auto &row : rows.regular) {
    filterTransaction(selectedAsset, row);

    const auto current = row->date().date();
    setRowShowDate(row, row->isVisible() && current != previous);
    if (row->isVisible()) {
      previous = current;
    }
  }

  if (!rows.regular.empty()) {
    transactions.latestScannedTransactionLt = rows.regular.front()->transaction().id.lt;
    transactions.leastScannedTransactionLt = rows.regular.back()->transaction().id.lt;
  }

  resizeToWidth(_widget.width());

  if (!unknownOwners.empty()) {
    _ownerResolutionRequests.fire(std::make_pair(&symbol, &unknownOwners));
  }

  _widget.update(0, _visibleTop, _widget.width(), _visibleBottom - _visibleTop);
}

void History::refreshPending() {
  const auto symbol = v::match(
      _selectedAsset.current(), [&](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [&](const SelectedDePool &) { return Ton::Symbol::ton(); });
  const auto it = _rows.find(symbol);
  if (it == end(_rows)) {
    return;
  }
  auto &pendingRows = it->second.pendingRows;

  if (symbol.isTon()) {
    if (_pendingDataChanged) {
      pendingRows =
          ranges::views::all(_pendingData)                                                                            //
          | ranges::views::transform([&](const Ton::PendingTransaction &data) { return makeRow(data.fake, false); })  //
          | ranges::to_vector;
    }
  }

  if (!pendingRows.empty()) {
    const auto &pendingRow = pendingRows.front();
    if (pendingRow->isVisible()) {
      setRowShowDate(pendingRow);
    }
  }
  resizeToWidth(_widget.width());
}

void History::refreshRows() {
  for (const auto &item : _transactions) {
    const auto &symbol = item.first;
    const auto &transactions = item.second;

    auto rowsIt = _rows.find(symbol);
    if (rowsIt == end(_rows)) {
      rowsIt = _rows
                   .emplace(std::piecewise_construct, std::forward_as_tuple(symbol),
                            std::forward_as_tuple(RowsState{.regular = std::vector<std::unique_ptr<HistoryRow>>{}}))
                   .first;
    }
    auto &rows = rowsIt->second;

    auto addedFront = std::vector<std::unique_ptr<HistoryRow>>();
    auto addedBack = std::vector<std::unique_ptr<HistoryRow>>();
    for (const auto &element : transactions.list) {
      if (!rows.regular.empty() && element.id == rows.regular.front()->id()) {
        break;
      }
      addedFront.push_back(makeRow(element, element.id == transactions.initTransactionId));
    }
    if (!rows.regular.empty()) {
      const auto from = ranges::find(transactions.list, rows.regular.back()->id(), &Ton::Transaction::id);
      if (from != end(transactions.list)) {
        addedBack = ranges::make_subrange(from + 1, end(transactions.list))  //
                    | ranges::views::transform([&](const Ton::Transaction &data) {
                        return makeRow(data, data.id == transactions.initTransactionId);
                      })  //
                    | ranges::to_vector;
      }
    }
    if (addedFront.empty() && addedBack.empty()) {
      continue;
    } else if (!addedFront.empty()) {
      if (addedFront.size() < transactions.list.size()) {
        addedFront.insert(end(addedFront), std::make_move_iterator(begin(rows.regular)),
                          std::make_move_iterator(end(rows.regular)));
      }
      rows.regular = std::move(addedFront);
    }
    rows.regular.insert(end(rows.regular), std::make_move_iterator(begin(addedBack)),
                        std::make_move_iterator(end(addedBack)));
  }

  refreshShowDates();
}

void History::repaintRow(not_null<HistoryRow *> row) {
  _widget.update(0, row->top(), _widget.width(), row->height());
}

void History::repaintShadow(not_null<HistoryRow *> row) {
  const auto min = std::min(row->top(), _visibleTop);
  const auto delta = std::max(row->top(), _visibleTop) - min;
  _widget.update(0, min, _widget.width(), delta + st::walletRowDateHeight);
}

void History::checkPreload() const {
  const auto visibleHeight = (_visibleBottom - _visibleTop);
  const auto preloadHeight = kPreloadScreens * visibleHeight;

  const auto symbol = v::match(
      _selectedAsset.current(), [&](const SelectedToken &selectedToken) { return selectedToken.symbol; },
      [](auto &&) { return Ton::Symbol::ton(); });

  const auto it = _transactions.find(symbol);
  if (it != _transactions.end() && _visibleBottom + preloadHeight >= _widget.height() &&
      it->second.previousId.lt != 0) {
    _preloadRequests.fire_copy(std::make_pair(symbol, it->second.previousId));
  }
}

rpl::producer<HistoryState> MakeHistoryState(rpl::producer<Ton::WalletViewerState> state) {
  return rpl::combine(std::move(state)) | rpl::map([](Ton::WalletViewerState &&state) {
           std::map<Ton::Symbol, Ton::TransactionsSlice> lastTransactions{
               {Ton::Symbol::ton(), state.wallet.lastTransactions}};
           for (auto &&[symbol, token] : state.wallet.tokenStates) {
             lastTransactions.emplace(symbol, token.lastTransactions);
           }
           return HistoryState{.lastTransactions = std::move(lastTransactions),
                               .pendingTransactions = std::move(state.wallet.pendingTransactions)};
         });
}

}  // namespace Wallet
