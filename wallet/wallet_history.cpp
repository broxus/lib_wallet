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
#include "ui/address_label.h"
#include "ui/inline_token_icon.h"
#include "ui/painter.h"
#include "ui/text/text.h"
#include "ui/text/text_utilities.h"
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
  if (data.fee) {
    const auto fee = FormatAmount(data.fee, Ton::Symbol::ton()).full;
    result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));
  }
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
        return std::make_tuple(Ton::Wallet::ConvertIntoRaw(transfer.address), transfer.value, transfer.incoming,
                               Flag(0));
      },

      [](const Ton::TokenSwapBack &tokenSwapBack) {
        return std::make_tuple(tokenSwapBack.address, tokenSwapBack.value, false, Flag::SwapBack);
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
  HistoryRow(Ton::Transaction transaction, const Fn<void()> &decrypt = nullptr, bool isInitTransaction = false);
  HistoryRow(Ton::Transaction transaction, Ton::DePoolTransaction dePoolTransaction);
  HistoryRow(Ton::Symbol symbol, Ton::Transaction transaction, Ton::TokenTransaction tokenTransaction);

  HistoryRow(const HistoryRow &) = delete;
  HistoryRow &operator=(const HistoryRow &) = delete;

  [[nodiscard]] Ton::TransactionId id() const;
  [[nodiscard]] const Ton::Transaction &transaction() const;

  void refreshDate();
  [[nodiscard]] QDateTime date() const;
  void setShowDate(bool show, const Fn<void()> &repaintDate);
  void setDecryptionFailed();
  bool showDate() const;

  [[nodiscard]] int top() const;
  void setTop(int top);

  void resizeToWidth(int width);
  [[nodiscard]] int height() const;
  [[nodiscard]] int bottom() const;

  void setVisible(bool visible);
  [[nodiscard]] bool isVisible() const;

  void clearAdditionalInfo();
  void setAdditionalInfo(const Ton::Symbol &symbol, Ton::TokenTransaction &&tokenTransaction);
  void setAdditionalInfo(Ton::DePoolTransaction &&dePoolTransaction);

  void paint(Painter &p, int x, int y);
  void paintDate(Painter &p, int x, int y);
  [[nodiscard]] bool isUnderCursor(QPoint point) const;
  [[nodiscard]] ClickHandlerPtr handlerUnderCursor(QPoint point) const;

 private:
  [[nodiscard]] QRect computeInnerRect() const;

  Ton::Symbol symbol_;
  TransactionLayout layout_;

  Ton::Transaction transaction_;
  std::optional<AdditionalTransactionInfo> additionalInfo_{};

  Fn<void()> decrypt_ = [] {};

  bool isInitTransaction_ = false;
  int top_ = 0;
  int width_ = 0;
  int height_ = 0;
  int commentHeight_ = 0;

  Ui::Animations::Simple _dateShadowShown;
  Fn<void()> _repaintDate;
  bool _dateHasShadow = false;
  bool _decryptionFailed = false;
};

HistoryRow::HistoryRow(Ton::Transaction transaction, const Fn<void()> &decrypt, bool isInitTransaction)
    : symbol_(Ton::Symbol::ton())
    , layout_(prepareLayout(transaction, decrypt_, isInitTransaction))
    , transaction_(std::move(transaction))
    , decrypt_(decrypt)
    , isInitTransaction_(isInitTransaction) {
}

HistoryRow::HistoryRow(Ton::Transaction transaction, Ton::DePoolTransaction dePoolTransaction)
    : symbol_(Ton::Symbol::ton())
    , layout_(prepareLayout(transaction, dePoolTransaction))
    , transaction_(std::move(transaction))
    , additionalInfo_(std::move(dePoolTransaction)) {
}

HistoryRow::HistoryRow(Ton::Symbol symbol, Ton::Transaction transaction, Ton::TokenTransaction tokenTransaction)
    : symbol_(std::move(symbol))
    , layout_(prepareLayout(symbol, transaction, tokenTransaction))
    , transaction_(std::move(transaction))
    , additionalInfo_(std::move(tokenTransaction)) {
}

Ton::TransactionId HistoryRow::id() const {
  return transaction_.id;
}

const Ton::Transaction &HistoryRow::transaction() const {
  return transaction_;
}

void HistoryRow::refreshDate() {
  refreshTimeTexts(layout_);
}

QDateTime HistoryRow::date() const {
  return layout_.dateTime;
}

void HistoryRow::setShowDate(bool show, const Fn<void()> &repaintDate) {
  width_ = 0;
  if (!show) {
    layout_.date.clear();
  } else {
    _repaintDate = std::move(repaintDate);
    refreshTimeTexts(layout_, true);
  }
}

void HistoryRow::setDecryptionFailed() {
  width_ = 0;
  _decryptionFailed = true;
  layout_.comment.setText(st::defaultTextStyle, ph::lng_wallet_decrypt_failed(ph::now), _textPlainOptions);
}

bool HistoryRow::showDate() const {
  return !layout_.date.isEmpty();
}

int HistoryRow::top() const {
  return top_;
}

void HistoryRow::setTop(int top) {
  top_ = top;
}

void HistoryRow::resizeToWidth(int width) {
  if (width_ == width) {
    return;
  }
  width_ = width;
  if (!isVisible()) {
    return;
  }

  const auto padding = st::walletRowPadding;
  const auto use = std::min(width_, st::walletRowWidthMax);
  const auto avail = use - padding.left() - padding.right();
  height_ = 0;
  if (!layout_.date.isEmpty()) {
    height_ += st::walletRowDateSkip;
  }
  height_ += padding.top() + layout_.amountGrams.minHeight();
  if (!layout_.address.isEmpty()) {
    height_ += st::walletRowAddressTop + layout_.addressHeight;
  }
  if (!layout_.comment.isEmpty()) {
    commentHeight_ = std::min(layout_.comment.countHeight(avail), st::defaultTextStyle.font->height * kCommentLinesMax);
    height_ += st::walletRowCommentTop + commentHeight_;
  }
  if (!layout_.fees.isEmpty()) {
    height_ += st::walletRowFeesTop + layout_.fees.minHeight();
  }
  height_ += padding.bottom();
}

int HistoryRow::height() const {
  return height_;
}

int HistoryRow::bottom() const {
  return top_ + height_;
}

void HistoryRow::setVisible(bool visible) {
  if (visible) {
    height_ = 1;
    resizeToWidth(width_);
  } else {
    height_ = 0;
  }
}

bool HistoryRow::isVisible() const {
  return height_ > 0;
}

void HistoryRow::clearAdditionalInfo() {
  additionalInfo_.reset();
  symbol_ = Ton::Symbol::ton();
  layout_ = prepareLayout(transaction_, decrypt_, isInitTransaction_);
}

void HistoryRow::setAdditionalInfo(const Ton::Symbol &symbol, Ton::TokenTransaction &&tokenTransaction) {
  layout_ = prepareLayout(symbol, transaction_, tokenTransaction);
  additionalInfo_ = std::forward<Ton::TokenTransaction>(tokenTransaction);
  symbol_ = symbol;
}

void HistoryRow::setAdditionalInfo(Ton::DePoolTransaction &&dePoolTransaction) {
  layout_ = prepareLayout(transaction_, dePoolTransaction);
  additionalInfo_ = std::forward<Ton::DePoolTransaction>(dePoolTransaction);
  symbol_ = Ton::Symbol::ton();
}

void HistoryRow::paint(Painter &p, int x, int y) {
  if (!isVisible()) {
    return;
  }

  const auto padding = st::walletRowPadding;
  const auto use = std::min(width_, st::walletRowWidthMax);
  const auto avail = use - padding.left() - padding.right();
  x += (width_ - use) / 2 + padding.left();

  if (!layout_.date.isEmpty()) {
    y += st::walletRowDateSkip;
  } else {
    const auto shadowLeft = (use < width_) ? (x - st::walletRowShadowAdd) : x;
    const auto shadowWidth =
        (use < width_) ? (avail + 2 * st::walletRowShadowAdd) : width_ - padding.left() - padding.right();
    p.fillRect(shadowLeft, y, shadowWidth, st::lineWidth, st::shadowFg);
  }
  y += padding.top();

  if (layout_.flags & Flag::Service) {
    const auto labelLeft = x;
    const auto labelTop = y + st::walletRowGramsStyle.font->ascent - st::normalFont->ascent;
    p.setPen(st::windowFg);
    p.setFont(st::normalFont);
    p.drawText(labelLeft, labelTop + st::normalFont->ascent,
               ((layout_.flags & Flag::Initialization) ? ph::lng_wallet_row_init(ph::now)
                                                       : ph::lng_wallet_row_service(ph::now)));
  } else {
    const auto incoming = (layout_.flags & Flag::Incoming);
    const auto swapBack = (layout_.flags & Flag::SwapBack);

    const auto reward = (layout_.flags & Flag::DePoolReward);
    const auto stake = (layout_.flags & Flag::DePoolStake);

    p.setPen(incoming ? st::boxTextFgGood : st::boxTextFgError);
    layout_.amountGrams.draw(p, x, y, avail);

    const auto nanoTop = y + st::walletRowGramsStyle.font->ascent - st::walletRowNanoStyle.font->ascent;
    const auto nanoLeft = x + layout_.amountGrams.maxWidth();
    layout_.amountNano.draw(p, nanoLeft, nanoTop, avail);

    const auto diamondTop = y + st::walletRowGramsStyle.font->ascent - st::normalFont->ascent;
    const auto diamondLeft = nanoLeft + layout_.amountNano.maxWidth() + st::normalFont->spacew;
    Ui::PaintInlineTokenIcon(symbol_, p, diamondLeft, diamondTop, st::normalFont);

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
    const auto timeLeft = x + avail - layout_.time.maxWidth();
    p.setPen(st::windowSubTextFg);
    layout_.time.draw(p, timeLeft, timeTop, avail);
    if (layout_.flags & Flag::Encrypted) {
      const auto iconLeft = x + avail - st::walletCommentIconLeft - st::walletCommentIcon.width();
      const auto iconTop = labelTop + st::walletCommentIconTop;
      st::walletCommentIcon.paint(p, iconLeft, iconTop, avail);
    }
    if (layout_.flags & Flag::Pending) {
      st::walletRowPending.paint(p, (timeLeft - st::walletRowPendingPosition.x() - st::walletRowPending.width()),
                                 timeTop + st::walletRowPendingPosition.y(), avail);
    }
  }
  y += layout_.amountGrams.minHeight();

  if (!layout_.address.isEmpty()) {
    p.setPen(st::windowFg);
    y += st::walletRowAddressTop;
    layout_.address.drawElided(p, x, y, layout_.addressWidth, 2, style::al_topleft, 0, -1, 0, true);
    y += layout_.addressHeight;
  }
  if (!layout_.comment.isEmpty()) {
    y += st::walletRowCommentTop;
    if (_decryptionFailed) {
      p.setPen(st::boxTextFgError);
    }
    layout_.comment.drawElided(p, x, y, avail, kCommentLinesMax);
    y += commentHeight_;
  }
  if (!layout_.fees.isEmpty()) {
    p.setPen(st::windowSubTextFg);
    y += st::walletRowFeesTop;
    layout_.fees.draw(p, x, y, avail);
  }
}

void HistoryRow::paintDate(Painter &p, int x, int y) {
  if (!isVisible()) {
    return;
  }

  Expects(!layout_.date.isEmpty());
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
    p.fillRect(x, y + noShadowHeight, width_, line, st::shadowFg);
  }

  const auto padding = st::walletRowPadding;
  const auto use = std::min(width_, st::walletRowWidthMax);
  x += (width_ - use) / 2;

  p.setOpacity(0.9);
  p.fillRect(x, y, use, noShadowHeight, st::windowBg);

  const auto avail = use - padding.left() - padding.right();
  x += padding.left();
  p.setOpacity(1.);
  p.setPen(st::windowFg);
  layout_.date.draw(p, x, y + st::walletRowDateTop, avail);
}

QRect HistoryRow::computeInnerRect() const {
  const auto padding = st::walletRowPadding;
  const auto use = std::min(width_, st::walletRowWidthMax);
  const auto avail = use - padding.left() - padding.right();
  const auto left = (use < width_) ? ((width_ - use) / 2 + padding.left() - st::walletRowShadowAdd) : 0;
  const auto width = (use < width_) ? (avail + 2 * st::walletRowShadowAdd) : width_;
  auto y = top();
  if (!layout_.date.isEmpty()) {
    y += st::walletRowDateSkip;
  }
  return QRect(left, y, width, bottom() - y);
}

bool HistoryRow::isUnderCursor(QPoint point) const {
  return isVisible() && computeInnerRect().contains(point);
}

ClickHandlerPtr HistoryRow::handlerUnderCursor(QPoint point) const {
  return nullptr;
}

History::History(not_null<Ui::RpWidget *> parent, rpl::producer<HistoryState> state,
                 rpl::producer<std::pair<Ton::Symbol, Ton::LoadedSlice>> loaded,
                 rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
                 rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted,
                 rpl::producer<std::optional<SelectedAsset>> selectedAsset)
    : _widget(parent), _selectedAsset(SelectedToken{.token = Ton::Symbol::ton()}) {
  setupContent(std::move(state), std::move(loaded), std::move(selectedAsset));

  base::unixtime::updates()  //
      | rpl::start_with_next(
            [=] {
              for (const auto &row : _pendingRows) {
                row->refreshDate();
              }
              for (auto &items : _rows) {
                for (auto &row : items.second) {
                  row->refreshDate();
                }
              }
              refreshShowDates();
              _widget.update();
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
                _widget.update();
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
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  if (!width) {
    return;
  }

  auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  auto &rows = rowsIt->second;

  auto top = ((symbol.isTon() && _pendingRows.empty() || symbol.isToken()) && rows.empty()) ? 0 : st::walletRowsSkip;
  int height = 0;
  if (symbol.isTon()) {
    for (const auto &row : _pendingRows) {
      row->setTop(top + height);
      row->resizeToWidth(width);
      height += row->height();
    }
  }
  for (const auto &row : rows) {
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
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  _visibleTop = top - _widget.y();
  _visibleBottom = bottom - _widget.y();

  auto transactionsIt = _transactions.find(symbol);
  auto rowsIt = _rows.find(symbol);
  if (_visibleBottom <= _visibleTop || transactionsIt != end(_transactions) && !transactionsIt->second.previousId.lt ||
      rowsIt != end(_rows) && rowsIt->second.empty()) {
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
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = rowsIt->second;

  if (_selected != selected) {
    const auto was = (_selected >= 0 && _selected < int(rows.size())) ? rows[_selected].get() : nullptr;
    if (was) {
      repaintRow(was);
    }
    _selected = selected;
    _widget.setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
  }
  if (ClickHandler::getActive() != handler) {
    const auto now = (_selected >= 0 && _selected < int(rows.size())) ? rows[_selected].get() : nullptr;
    if (now) {
      repaintRow(now);
    }
    ClickHandler::setActive(handler);
  }
}

void History::selectRowByMouse() {
  auto symbol = v::match(
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto rowsIt = _rows.find(symbol);
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = rowsIt->second;

  const auto point = _widget.mapFromGlobal(QCursor::pos());
  const auto from = ranges::upper_bound(rows, point.y(), ranges::less(), &HistoryRow::bottom);
  const auto till = ranges::lower_bound(rows, point.y(), ranges::less(), &HistoryRow::top);

  if (from != rows.end() && from != till && (*from)->isUnderCursor(point)) {
    selectRow(from - begin(rows), (*from)->handlerUnderCursor(point));
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
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });
  const auto transactionsIt = _transactions.find(symbol);
  const auto rowsIt = _rows.find(symbol);
  if (transactionsIt == end(_transactions) || rowsIt == end(_rows)) {
    return;
  }
  const auto &transactions = transactionsIt->second;
  const auto &rows = rowsIt->second;

  Expects(_selected < static_cast<int>(rows.size()));

  const auto handler = ClickHandler::unpressed();
  if (std::exchange(_pressed, -1) != _selected || _selected < 0) {
    if (handler)
      handler->onClick(ClickContext());
    return;
  }
  if (handler) {
    handler->onClick(ClickContext());
  } else {
    const auto i = ranges::find(transactions.list, rows[_selected]->id(), &Ton::Transaction::id);
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
      _selectedAsset.current(), [](const SelectedToken &selectedToken) { return selectedToken.token; },
      [](const SelectedDePool &selectedDePool) { return Ton::Symbol::ton(); });

  auto rowsIt = _rows.find(symbol);
  if (rowsIt == _rows.end()) {
    return;
  }
  const auto &rows = rowsIt->second;

  if ((symbol.isTon() && _pendingRows.empty() || symbol.isToken()) && rows.empty()) {
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
  if (symbol.isTon()) {
    paintRows(_pendingRows);
  }
  paintRows(rows);
}

void History::mergeState(HistoryState &&state) {
  if (mergePendingChanged(std::move(state.pendingTransactions))) {
    refreshPending();
  }
  if (mergeListChanged(std::move(state.lastTransactions))) {
    refreshRows();
  }
}

bool History::mergePendingChanged(std::vector<Ton::PendingTransaction> &&list) {
  if (_pendingData == list) {
    return false;
  }
  _pendingData = std::move(list);
  return true;
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
  Expects(index >= 0 && index < rows.size());
  Expects(rows[index]->id() == transactions.list[index].id);

  const auto i = ranges::find(decrypted, transactions.list[index].id, &Ton::Transaction::id);
  if (i == end(decrypted)) {
    return false;
  }
  if (IsEncryptedMessage(*i)) {
    rows[index]->setDecryptionFailed();
  } else {
    transactions.list[index] = *i;
    rows[index] = makeRow(*i, i->id == transactions.initTransactionId);
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
        const auto wasRow = ranges::find(it->second, was, &HistoryRow::id);
        if (wasRow != end(it->second)) {
          *wasRow = makeRow(*wasItem, false);
        }
      }
    }
    if (found) {
      found->initializing = true;
      if (it != end(_rows)) {
        const auto nowRow = ranges::find(it->second, now, &HistoryRow::id);
        if (nowRow != end(it->second)) {
          *nowRow = makeRow(*found, true);
        }
      }
    }
  }
}

void History::refreshShowDates() {
  const auto selectedAsset = _selectedAsset.current();

  const auto [symbol, targetAddress] = v::match(
      selectedAsset, [](const SelectedToken &selectedToken) { return std::make_pair(selectedToken.token, QString{}); },
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

  auto filterTransaction = [selectedAsset, targetAddress = targetAddress](decltype(rows.front()) &row) {
    const auto &transaction = row->transaction();

    v::match(
        selectedAsset,
        [&](const SelectedToken &selectedToken) {
          row->setVisible(true);
          if (selectedToken.token.isTon()) {
            row->clearAdditionalInfo();
            return;
          }

          auto tokenTransaction = Ton::Wallet::ParseTokenTransaction(transaction.incoming.message);
          if (!tokenTransaction.has_value()) {
            return row->setVisible(false);
          }
          row->setAdditionalInfo(selectedToken.token, std::move(*tokenTransaction));
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

  auto previous = QDate();
  for (auto &row : rows) {
    filterTransaction(row);

    const auto current = row->date().date();
    setRowShowDate(row, row->isVisible() && current != previous);
    if (row->isVisible()) {
      previous = current;
    }
  }
  resizeToWidth(_widget.width());
}

void History::refreshPending() {
  _pendingRows =
      ranges::views::all(_pendingData)                                                                            //
      | ranges::views::transform([&](const Ton::PendingTransaction &data) { return makeRow(data.fake, false); })  //
      | ranges::to_vector;

  if (!_pendingRows.empty()) {
    const auto &pendingRow = _pendingRows.front();
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
                            std::forward_as_tuple(std::vector<std::unique_ptr<HistoryRow>>{}))
                   .first;
    }
    auto &rows = rowsIt->second;

    auto addedFront = std::vector<std::unique_ptr<HistoryRow>>();
    auto addedBack = std::vector<std::unique_ptr<HistoryRow>>();
    for (const auto &element : transactions.list) {
      if (!rows.empty() && element.id == rows.front()->id()) {
        break;
      }
      addedFront.push_back(makeRow(element, element.id == transactions.initTransactionId));
    }
    if (!rows.empty()) {
      const auto from = ranges::find(transactions.list, rows.back()->id(), &Ton::Transaction::id);
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
        addedFront.insert(end(addedFront), std::make_move_iterator(begin(rows)), std::make_move_iterator(end(rows)));
      }
      rows = std::move(addedFront);
    }
    rows.insert(end(rows), std::make_move_iterator(begin(addedBack)), std::make_move_iterator(end(addedBack)));
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
      _selectedAsset.current(), [&](const SelectedToken &selectedToken) { return selectedToken.token; },
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
