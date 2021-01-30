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
  const auto [address, value, flags] = v::match(
      tokenTransaction,
      [](const Ton::TokenTransfer &transfer) { return std::make_tuple(transfer.dest, transfer.value, Flag(0)); },

      [](const Ton::TokenSwapBack &tokenSwapBack) {
        return std::make_tuple(tokenSwapBack.dest, tokenSwapBack.value, Flag::SwapBack);
      });

  const auto amount = FormatAmount(-value, token, FormatFlag::Signed | FormatFlag::Rounded);
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

  const auto fee = FormatAmount(-CalculateValue(transaction), Ton::Symbol::ton()).full;
  result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));

  result.flags = flags;

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
  void setAdditionalInfo(const AdditionalTransactionInfo &info);

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
  layout_ = prepareLayout(transaction_, decrypt_, isInitTransaction_);
}

void HistoryRow::setAdditionalInfo(const AdditionalTransactionInfo &info) {
  v::match(
      info,
      [&](const Ton::TokenTransaction &tokenTransaction) {
        additionalInfo_ = tokenTransaction;
        layout_ = prepareLayout(symbol_, transaction_, tokenTransaction);
      },
      [&](const Ton::DePoolTransaction &dePoolTransaction) {
        additionalInfo_ = dePoolTransaction;
        layout_ = prepareLayout(transaction_, dePoolTransaction);
      });
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
                 rpl::producer<Ton::LoadedSlice> loaded,
                 rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
                 rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted,
                 rpl::producer<std::optional<SelectedAsset>> selectedAsset)
    : _widget(parent), _selectedAsset(SelectedToken{.token = Ton::Symbol::ton()}) {
  setupContent(std::move(state), std::move(loaded), std::move(selectedAsset));

  base::unixtime::updates()  //
      | rpl::start_with_next(
            [=] {
              for (const auto &row : ranges::views::concat(_pendingRows, _rows)) {
                row->refreshDate();
              }
              refreshShowDates();
              _widget.update();
            },
            _widget.lifetime());

  std::move(collectEncrypted)  //
      | rpl::start_with_next(
            [=](not_null<std::vector<Ton::Transaction> *> list) {
              auto &&encrypted = ranges::views::all(_listData) | ranges::views::filter(IsEncryptedMessage);
              list->insert(list->end(), encrypted.begin(), encrypted.end());
            },
            _widget.lifetime());

  std::move(updateDecrypted)  //
      | rpl::start_with_next(
            [=](not_null<const std::vector<Ton::Transaction> *> list) {
              auto changed = false;
              for (auto i = 0, count = int(_listData.size()); i != count; ++i) {
                if (IsEncryptedMessage(_listData[i])) {
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
  if (!width) {
    return;
  }

  auto top = (_pendingRows.empty() && _rows.empty()) ? 0 : st::walletRowsSkip;
  int height = 0;
  for (const auto &row : ranges::views::concat(_pendingRows, _rows)) {
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
  _visibleTop = top - _widget.y();
  _visibleBottom = bottom - _widget.y();
  if (_visibleBottom <= _visibleTop || !_previousId.lt || _rows.empty()) {
    return;
  }
  checkPreload();
}

rpl::producer<Ton::TransactionId> History::preloadRequests() const {
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

void History::setupContent(rpl::producer<HistoryState> &&state, rpl::producer<Ton::LoadedSlice> &&loaded,
                           rpl::producer<std::optional<SelectedAsset>> &&selectedAsset) {
  std::move(state) | rpl::start_with_next([=](HistoryState &&state) { mergeState(std::move(state)); }, lifetime());

  std::move(loaded)                                                                               //
      | rpl::filter([=](const Ton::LoadedSlice &slice) { return (slice.after == _previousId); })  //
      | rpl::start_with_next(
            [=](Ton::LoadedSlice &&slice) {
              const auto loadedLast = (_previousId.lt != 0) && (slice.data.previousId.lt == 0);
              _previousId = slice.data.previousId;
              _listData.insert(end(_listData), slice.data.list.begin(), slice.data.list.end());
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
              }
            },
            lifetime());

  rpl::combine(std::move(selectedAsset))  //
      | rpl::start_with_next(
            [=](const std::optional<SelectedAsset> &asset) {
              auto newAsset = asset.value_or(SelectedToken::defaultToken());
              _selectedAsset = std::move(newAsset);
              refreshShowDates();
              _widget.update();
              _widget.repaint();
            },
            _widget.lifetime());
}

void History::selectRow(int selected, const ClickHandlerPtr &handler) {
  Expects(selected >= 0 || !handler);

  if (_selected != selected) {
    const auto was = (_selected >= 0 && _selected < int(_rows.size())) ? _rows[_selected].get() : nullptr;
    if (was)
      repaintRow(was);
    _selected = selected;
    _widget.setCursor((_selected >= 0) ? style::cur_pointer : style::cur_default);
  }
  if (ClickHandler::getActive() != handler) {
    const auto now = (_selected >= 0 && _selected < int(_rows.size())) ? _rows[_selected].get() : nullptr;
    if (now)
      repaintRow(now);
    ClickHandler::setActive(handler);
  }
}

void History::selectRowByMouse() {
  const auto point = _widget.mapFromGlobal(QCursor::pos());
  const auto from = ranges::upper_bound(_rows, point.y(), ranges::less(), &HistoryRow::bottom);
  const auto till = ranges::lower_bound(_rows, point.y(), ranges::less(), &HistoryRow::top);

  if (from != _rows.end() && from != till && (*from)->isUnderCursor(point)) {
    selectRow(from - begin(_rows), (*from)->handlerUnderCursor(point));
  } else {
    selectRow(-1, nullptr);
  }
}

void History::pressRow() {
  _pressed = _selected;
  ClickHandler::pressed();
}

void History::releaseRow() {
  Expects(_selected < int(_rows.size()));

  const auto handler = ClickHandler::unpressed();
  if (std::exchange(_pressed, -1) != _selected || _selected < 0) {
    if (handler)
      handler->onClick(ClickContext());
    return;
  }
  if (handler) {
    handler->onClick(ClickContext());
  } else {
    const auto i = ranges::find(_listData, _rows[_selected]->id(), &Ton::Transaction::id);
    Assert(i != end(_listData));
    _viewRequests.fire_copy(*i);
  }
}

void History::decryptById(const Ton::TransactionId &id) {
  const auto i = ranges::find(_listData, id, &Ton::Transaction::id);
  Assert(i != end(_listData));
  _decryptRequests.fire_copy(*i);
}

void History::paint(Painter &p, QRect clip) {
  if (_pendingRows.empty() && _rows.empty()) {
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
  paintRows(_pendingRows);
  paintRows(_rows);
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

bool History::mergeListChanged(Ton::TransactionsSlice &&data) {
  const auto i = _listData.empty() ? data.list.cend() : ranges::find(std::as_const(data.list), _listData.front());
  if (i == data.list.cend()) {
    _listData = data.list | ranges::to_vector;
    _previousId = std::move(data.previousId);
    if (!_previousId.lt) {
      computeInitTransactionId();
    }
    return true;
  } else if (i != data.list.cbegin()) {
    _listData.insert(begin(_listData), data.list.cbegin(), i);
    return true;
  }
  return false;
}

void History::setRowShowDate(const std::unique_ptr<HistoryRow> &row, bool show) {
  const auto raw = row.get();
  row->setShowDate(show, [=] { repaintShadow(raw); });
}

bool History::takeDecrypted(int index, const std::vector<Ton::Transaction> &decrypted) {
  Expects(index >= 0 && index < _listData.size());
  Expects(index >= 0 && index < _rows.size());
  Expects(_rows[index]->id() == _listData[index].id);

  const auto i = ranges::find(decrypted, _listData[index].id, &Ton::Transaction::id);
  if (i == end(decrypted)) {
    return false;
  }
  if (IsEncryptedMessage(*i)) {
    _rows[index]->setDecryptionFailed();
  } else {
    _listData[index] = *i;
    _rows[index] = makeRow(*i);
  }
  return true;
}

std::unique_ptr<HistoryRow> History::makeRow(const Ton::Transaction &data) {
  const auto id = data.id;
  if (id.lt == 0) {
    // pending
    return std::make_unique<HistoryRow>(data);
  }

  const auto isInitTransaction = (_initTransactionId == id);
  return std::make_unique<HistoryRow>(
      data, [=] { decryptById(id); }, isInitTransaction);
}

void History::computeInitTransactionId() {
  const auto was = _initTransactionId;
  auto found = static_cast<Ton::Transaction *>(nullptr);
  for (auto &row : ranges::views::reverse(_listData)) {
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

  _initTransactionId = now;
  const auto wasItem = ranges::find(_listData, was, &Ton::Transaction::id);
  if (wasItem != end(_listData)) {
    wasItem->initializing = false;
    const auto wasRow = ranges::find(_rows, was, &HistoryRow::id);
    if (wasRow != end(_rows)) {
      *wasRow = makeRow(*wasItem);
    }
  }
  if (found) {
    found->initializing = true;
    const auto nowRow = ranges::find(_rows, now, &HistoryRow::id);
    if (nowRow != end(_rows)) {
      *nowRow = makeRow(*found);
    }
  }
}

void History::refreshShowDates() {
  const auto selectedAsset = _selectedAsset.current();

  const auto targetAddress = v::match(
      selectedAsset,
      [this](const SelectedToken &) {
        return _tokenContractAddress.current().isEmpty() ? QString{}
                                                         : Ton::Wallet::ConvertIntoRaw(_tokenContractAddress.current());
      },
      [](const SelectedDePool &selectedDePool) { return Ton::Wallet::ConvertIntoRaw(selectedDePool.address); });

  auto filterTransaction = [selectedAsset, targetAddress](decltype(_rows.front()) &row) {
    const auto &transaction = row->transaction();

    v::match(
        selectedAsset,
        [&](const SelectedToken &selectedToken) {
          if (selectedToken.token.isTon()) {
            row->setVisible(true);
            row->clearAdditionalInfo();
            return;
          }

          //  for (const auto &out : transaction.outgoing) {
          //    if (Ton::Wallet::ConvertIntoRaw(out.destination) != targetAddress) {
          //      continue;
          //    }
          //
          //    auto tokenTransaction = Ton::Wallet::ParseTokenTransaction(out.message);
          //    if (tokenTransaction.has_value() && !Ton::CheckTokenTransaction(selectedToken.token, *tokenTransaction)) {
          //      break;
          //    }
          //
          //    row->setVisible(true);
          //    row->attachTokenTransaction(std::move(tokenTransaction));
          //    return;
          //  }

          row->setVisible(false);
        },
        [&](const SelectedDePool &selectedDePool) {
          const auto incoming = !transaction.incoming.source.isEmpty();

          if (incoming && Ton::Wallet::ConvertIntoRaw(transaction.incoming.source) == targetAddress) {
            auto parsedTransaction = Ton::Wallet::ParseDePoolTransaction(transaction.incoming.message, incoming);
            if (parsedTransaction.has_value()) {
              row->setAdditionalInfo(*parsedTransaction);
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

              row->setAdditionalInfo(*stakeTransaction);
              row->setVisible(true);
              return;
            }
          }

          row->setVisible(false);
        });
  };

  auto previous = QDate();
  for (auto &row : _rows) {
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
  _pendingRows = ranges::views::all(_pendingData)                                                                     //
                 | ranges::views::transform([&](const Ton::PendingTransaction &data) { return makeRow(data.fake); })  //
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
  auto addedFront = std::vector<std::unique_ptr<HistoryRow>>();
  auto addedBack = std::vector<std::unique_ptr<HistoryRow>>();
  for (const auto &element : _listData) {
    if (!_rows.empty() && element.id == _rows.front()->id()) {
      break;
    }
    addedFront.push_back(makeRow(element));
  }
  if (!_rows.empty()) {
    const auto from = ranges::find(_listData, _rows.back()->id(), &Ton::Transaction::id);
    if (from != end(_listData)) {
      addedBack = ranges::make_subrange(from + 1, end(_listData)) |
                  ranges::views::transform([=](const Ton::Transaction &data) { return makeRow(data); }) |
                  ranges::to_vector;
    }
  }
  if (addedFront.empty() && addedBack.empty()) {
    return;
  } else if (!addedFront.empty()) {
    if (addedFront.size() < _listData.size()) {
      addedFront.insert(end(addedFront), std::make_move_iterator(begin(_rows)), std::make_move_iterator(end(_rows)));
    }
    _rows = std::move(addedFront);
  }
  _rows.insert(end(_rows), std::make_move_iterator(begin(addedBack)), std::make_move_iterator(end(addedBack)));

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
  if (_visibleBottom + preloadHeight >= _widget.height() && _previousId.lt != 0) {
    _preloadRequests.fire_copy(_previousId);
  }
}

rpl::producer<HistoryState> MakeHistoryState(rpl::producer<Ton::WalletViewerState> state) {
  return rpl::combine(std::move(state)) | rpl::map([](Ton::WalletViewerState &&state) {
           return HistoryState{.lastTransactions = std::move(state.wallet.lastTransactions),
                               .pendingTransactions = std::move(state.wallet.pendingTransactions)};
         });
}

}  // namespace Wallet
