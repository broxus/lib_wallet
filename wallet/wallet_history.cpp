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
constexpr auto kExecuteVisibleTimeout = 86400;

static const HistoryPageKey kMainPageKey = std::make_pair(Ton::Symbol::ton(), QString{});

enum class Flag : uchar {
  Incoming = 0x01,
  Pending = 0x02,
  Encrypted = 0x04,
  Service = 0x08,
  Initialization = 0x10,
};
inline constexpr bool is_flag_type(Flag) {
  return true;
};
using Flags = base::flags<Flag>;

enum class TransactionType {
  Transfer,
  ExplicitTokenTransfer,
  Change,
  TokenWalletDeployed,
  EthEventStatusChanged,
  TonEventStatusChanged,
  SwapBack,
  Mint,
  DePoolReward,
  DePoolRewardNotification,
  DePoolStake,
  MultisigSubmit,
  MultisigConfirm,
};

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
  QString additionalInfo;
  int addressWidth = 0;
  int addressHeight = 0;
  int lineCount = 2;
  Flags flags = Flags();
  TransactionType type;
};

enum class EventType {
  EthEvent,
  TonEvent,
};

struct RegularTransactionParams {
  bool brief{};
  bool asReturnedChange{};
};

[[nodiscard]] HistoryPageKey accountPageKey(const QString &address) {
  return std::make_pair(Ton::Symbol::ton(), address);
}

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

[[nodiscard]] TransactionLayout prepareRegularLayout(const Ton::Transaction &data, const Fn<void()> &decrypt,
                                                     const RegularTransactionParams &params) {
  const auto service = IsServiceTransaction(data);
  const auto encrypted = IsEncryptedMessage(data) && decrypt;
  const auto amount = FormatAmount(service ? (-data.fee) : CalculateValue(data), Ton::Symbol::ton(),
                                   FormatFlag::Signed | FormatFlag::Rounded);
  const auto incoming = !data.incoming.source.isEmpty();
  const auto pending = (data.id.lt == 0);

  const auto extractedAddress = ExtractAddress(data);
  const auto address = extractedAddress.isEmpty() ? QString{} : Ton::Wallet::ConvertIntoRaw(extractedAddress);
  const auto addressPartWidth = [&](int from, int length = -1) {
    return addressStyle().font->width(address.mid(from, length));
  };

  auto result = TransactionLayout();
  result.serverTime = data.time;

  if (!params.brief) {
    result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
    result.amountNano.setText(st::walletRowNanoStyle, amount.separator + amount.nanoString);
  }

  result.address =
      Ui::Text::String(addressStyle(), service ? QString() : address, _defaultOptions, st::walletAddressWidthMin);
  result.addressWidth = (addressStyle().font->spacew / 2) +
                        std::max(addressPartWidth(0, address.size() / 2), addressPartWidth(address.size() / 2));
  result.addressHeight = addressStyle().font->height * 2;
  result.comment = Ui::Text::String(st::walletAddressWidthMin);
  result.comment.setText(st::defaultTextStyle, (encrypted ? QString() : ExtractMessage(data)), _textPlainOptions);

  const auto fee = FormatAmount(data.fee, Ton::Symbol::ton()).full;
  result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));

  result.flags = Flag(0)                                    //
                 | (service ? Flag::Service : Flag(0))      //
                 | (encrypted ? Flag::Encrypted : Flag(0))  //
                 | (incoming ? Flag::Incoming : Flag(0))    //
                 | (pending ? Flag::Pending : Flag(0));
  result.type = v::match(
      data.additional,
      [&](const Ton::EthEventStatusChanged &event) {
        result.additionalInfo = ph::lng_wallet_eth_event_status(event.status)(ph::now);
        return TransactionType::EthEventStatusChanged;
      },
      [&](const Ton::TonEventStatusChanged &event) {
        result.additionalInfo = ph::lng_wallet_ton_event_status(event.status)(ph::now);
        return TransactionType::TonEventStatusChanged;
      },
      [](const Ton::TokenWalletDeployed &deployed) { return TransactionType::TokenWalletDeployed; },
      [](const Ton::TokenTransfer &transfer) { return TransactionType::ExplicitTokenTransfer; },
      [](const Ton::TokenSwapBack &tokenSwapBack) { return TransactionType::SwapBack; },
      [](const Ton::DePoolOnRoundCompleteTransaction &) { return TransactionType::DePoolRewardNotification; },
      [](const Ton::DePoolOrdinaryStakeTransaction &) { return TransactionType::DePoolStake; },
      [&](auto &&) {
        if (params.asReturnedChange) {
          return TransactionType::Change;
        } else {
          return TransactionType::Transfer;
        }
      });

  refreshTimeTexts(result);
  return result;
}

[[nodiscard]] TransactionLayout prepareMultisigLayout(const Ton::Transaction &data) {
  const auto amount = FormatAmount(CalculateValue(data), Ton::Symbol::ton(), FormatFlag::Signed | FormatFlag::Rounded);
  const auto incoming = !data.incoming.source.isEmpty();
  const auto pending = (data.id.lt == 0);

  const auto extractedAddress = ExtractAddress(data);
  const auto address = extractedAddress.isEmpty() ? QString{} : Ton::Wallet::ConvertIntoRaw(extractedAddress);
  const auto partWidth = [](const QString &address, int from, int length = -1) {
    return addressStyle().font->width(address.mid(from, length));
  };

  auto result = TransactionLayout();
  result.serverTime = data.time;

  const auto setAddress = [&] {
    result.address = Ui::Text::String(addressStyle(), address, _defaultOptions, st::walletAddressWidthMin);
    result.addressWidth = (addressStyle().font->spacew / 2) +
                          std::max(partWidth(address, 0, address.size() / 2), partWidth(address, address.size() / 2));
    result.addressHeight = addressStyle().font->height * 2;
  };

  auto showAmount = false;
  QString comment;
  v::match(
      data.additional,
      [&](const Ton::MultisigSubmitTransaction &submitTransaction) {
        showAmount = submitTransaction.executed;
        comment = submitTransaction.comment;
        result.additionalInfo = FormatTransactionId(submitTransaction.transactionId);
        if (submitTransaction.executed) {
          result.type = TransactionType::Transfer;
          setAddress();
        } else {
          result.type = TransactionType::MultisigSubmit;

          const auto dest = Ton::Wallet::ConvertIntoRaw(submitTransaction.dest);
          const auto requestedAmount = FormatAmount(submitTransaction.amount, Ton::Symbol::ton());

          const auto text = QString{"Amount: %1 TON\n\nTransactionId:\n%2\n\nDestination:\n%3\n%4"}
                                .arg(requestedAmount.full)
                                .arg(FormatTransactionId(submitTransaction.transactionId))
                                .arg(dest.mid(0, dest.size() / 2))
                                .arg(dest.mid(dest.size() / 2, -1));
          result.lineCount = 9;
          result.address = Ui::Text::String(addressStyle(), text, _defaultOptions, st::walletAddressWidthMin);
          result.addressWidth = (addressStyle().font->spacew / 2) +
                                std::max(partWidth(dest, 0, dest.size() / 2), partWidth(dest, dest.size() / 2));
          result.addressHeight = addressStyle().font->height * result.lineCount;
        }
      },
      [&](const Ton::MultisigConfirmTransaction &confirmTransaction) {
        showAmount = confirmTransaction.executed;
        result.additionalInfo = FormatTransactionId(confirmTransaction.transactionId);
        result.type = TransactionType::MultisigConfirm;
        setAddress();
      },
      [&](auto &&) {
        showAmount = true;
        comment = ExtractMessage(data);
        result.type = TransactionType::Transfer;
        setAddress();
      });

  if (showAmount) {
    result.amountGrams.setText(st::walletRowGramsStyle, amount.gramsString);
    result.amountNano.setText(st::walletRowNanoStyle, amount.separator + amount.nanoString);
  }

  result.comment = Ui::Text::String(st::walletAddressWidthMin);
  result.comment.setText(st::defaultTextStyle, comment, _textPlainOptions);

  const auto fee = FormatAmount(data.fee, Ton::Symbol::ton()).full;
  result.fees.setText(st::defaultTextStyle, ph::lng_wallet_row_fees(ph::now).replace("{amount}", fee));

  result.flags = Flag(0)                                  //
                 | (incoming ? Flag::Incoming : Flag(0))  //
                 | (pending ? Flag::Pending : Flag(0));

  refreshTimeTexts(result);
  return result;
}

[[nodiscard]] std::optional<TransactionLayout> prepareDePoolLayout(const Ton::Transaction &data) {
  using Properties = std::optional<std::tuple<int64, int64, TransactionType>>;
  auto properties = v::match(
      data.additional,
      [&](const Ton::DePoolOrdinaryStakeTransaction &dePoolOrdinaryStakeTransaction) -> Properties {
        return std::make_tuple(-dePoolOrdinaryStakeTransaction.stake,
                               -CalculateValue(data) - dePoolOrdinaryStakeTransaction.stake + data.otherFee,
                               TransactionType::DePoolStake);
      },
      [&](const Ton::DePoolOnRoundCompleteTransaction &dePoolOnRoundCompleteTransaction) -> Properties {
        return std::make_tuple(dePoolOnRoundCompleteTransaction.reward, data.otherFee, TransactionType::DePoolReward);
      },
      [](auto &&) -> Properties { return std::nullopt; });
  if (!properties.has_value()) {
    return std::nullopt;
  }
  const auto [value, fee, type] = std::move(*properties);

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
                 | (pending ? Flag::Pending : Flag(0));
  result.type = type;

  refreshTimeTexts(result);
  return result;
}

[[nodiscard]] std::optional<TransactionLayout> prepareTokenLayout(const Ton::Symbol &token,
                                                                  const Ton::Transaction &transaction) {
  using Properties = std::optional<std::tuple<QString, int128, bool, TransactionType>>;
  auto properties = v::match(
      transaction.additional,
      [](const Ton::TokenWalletDeployed &deployed) -> Properties {
        return std::make_tuple(QString{}, 0, /*incoming*/ true, TransactionType::TokenWalletDeployed);
      },
      [&](const Ton::EthEventStatusChanged &ethEventStatusChanged) -> Properties {
        return std::make_tuple(Ton::Wallet::ConvertIntoRaw(transaction.incoming.source), 0, /*incoming*/ true,
                               TransactionType::EthEventStatusChanged);
      },
      [&](const Ton::TonEventStatusChanged &tonEventStatusChanged) -> Properties {
        return std::make_tuple(Ton::Wallet::ConvertIntoRaw(transaction.incoming.source), 0, /*incoming*/ true,
                               TransactionType::TonEventStatusChanged);
      },
      [](const Ton::TokenTransfer &transfer) -> Properties {
        return std::make_tuple(transfer.direct ? Ton::kZeroAddress : Ton::Wallet::ConvertIntoRaw(transfer.address),
                               transfer.value, transfer.incoming, TransactionType::Transfer);
      },
      [](const Ton::TokenMint &tokenMint) -> Properties {
        return std::make_tuple(QString{}, tokenMint.value, /*incoming*/ true, TransactionType::Mint);
      },
      [](const Ton::TokenSwapBack &tokenSwapBack) -> Properties {
        return std::make_tuple(tokenSwapBack.address, tokenSwapBack.value, /*incoming*/ false,
                               TransactionType::SwapBack);
      },
      [](const Ton::TokensBounced &tokensBounced) -> Properties {
        return std::make_tuple(QString{}, tokensBounced.amount, /*incoming*/ false, TransactionType::Transfer);
      },
      [](auto &&) -> Properties { return std::nullopt; });
  if (!properties.has_value()) {
    return std::nullopt;
  }
  const auto [address, value, incoming, type] = std::move(*properties);

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

  result.flags = incoming ? Flag::Incoming : Flag(0);
  result.type = type;

  refreshTimeTexts(result);
  return result;
}

}  // namespace

class HistoryRow final {
 public:
  explicit HistoryRow(Ton::Transaction transaction, const Fn<void()> &decrypt = nullptr)
      : _symbol(Ton::Symbol::ton())
      , _layout(prepareRegularLayout(transaction, _decrypt, RegularTransactionParams{}))
      , _transaction(std::move(transaction))
      , _decrypt(decrypt) {
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
  [[nodiscard]] Ton::Transaction &transaction() {
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
    _height += padding.top() + std::max(_layout.amountGrams.minHeight(), st::normalFont->height);
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

  void setRegularLayout(const RegularTransactionParams &params) {
    resetButton();
    _symbol = Ton::Symbol::ton();
    _layout = prepareRegularLayout(_transaction, _decrypt, params);
    setVisible(true);
  }
  void setTokenTransactionLayout(const Ton::Symbol &symbol) {
    resetButton();
    auto layout = prepareTokenLayout(symbol, _transaction);
    if (layout.has_value()) {
      _layout = std::move(*layout);
      _symbol = symbol;
      setVisible(!_transaction.aborted || _transaction.incoming.bounce);
    } else {
      setVisible(false);
    }
  }
  void setDePoolTransactionLayout() {
    resetButton();
    auto layout = prepareDePoolLayout(_transaction);
    if (layout.has_value()) {
      _layout = std::move(*layout);
      _symbol = Ton::Symbol::ton();
      setVisible(true);
    } else {
      setVisible(false);
    }
  }
  void setNotificationLayout(not_null<Ui::RpWidget *> parent, EventType eventType,
                             const RegularTransactionParams &params, const Fn<void()> &openRequest) {
    setRegularLayout(params);
    if (openRequest) {
      auto button = object_ptr<Ui::RoundButton>(  //
          parent,
          eventType == EventType::EthEvent  //
              ? ph::lng_wallet_history_receive_tokens()
              : ph::lng_wallet_history_execute_callback(),
          st::walletRowButton);
      button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
      button->setVisible(false);
      button->setClickedCallback(openRequest);
      _button = std::move(button);
    }
  }
  void setMultisigLayout() {
    resetButton();
    _symbol = Ton::Symbol::ton();
    _layout = prepareMultisigLayout(_transaction);
    setVisible(true);
  }
  void setMultisigSubmitTransactionLayout(not_null<Ui::RpWidget *> parent, const Fn<void()> &openRequest) {
    setMultisigLayout();
    if (openRequest) {
      auto button = object_ptr<Ui::RoundButton>(parent, ph::lng_wallet_history_confirm(), st::walletRowButton);
      button->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
      button->setVisible(false);
      button->setClickedCallback(openRequest);
      _button = std::move(button);
    }
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

      p.setPen(incoming ? st::boxTextFgGood : st::boxTextFgError);

      auto drawIcon = false;
      if (!_layout.amountGrams.isEmpty()) {
        _layout.amountGrams.draw(p, x, y, avail);
        drawIcon = true;
      }

      const auto nanoTop = y + st::walletRowGramsStyle.font->ascent - st::walletRowNanoStyle.font->ascent;
      const auto nanoLeft = x + _layout.amountGrams.maxWidth();
      if (!_layout.amountNano.isEmpty()) {
        _layout.amountNano.draw(p, nanoLeft, nanoTop, avail);
        drawIcon = true;
      }

      const auto diamondTop = y + st::walletRowGramsStyle.font->ascent - st::normalFont->ascent;
      const auto diamondLeft = nanoLeft + _layout.amountNano.maxWidth() + st::normalFont->spacew;
      if (drawIcon) {
        Ui::PaintInlineTokenIcon(_symbol, p, diamondLeft, diamondTop, st::normalFont);
      }

      auto labelTop = drawIcon ? diamondTop : y;
      const auto labelLeft = drawIcon ? (diamondLeft + st::walletDiamondSize + st::normalFont->spacew) : x;
      p.setPen(st::windowFg);
      p.setFont(st::normalFont);
      p.drawText(labelLeft, labelTop + st::normalFont->ascent, [&] {
        switch (_layout.type) {
          case TransactionType::ExplicitTokenTransfer:
            return ph::lng_wallet_row_token_transfer(ph::now);
          case TransactionType::TokenWalletDeployed:
            return ph::lng_wallet_row_token_wallet_deployed(ph::now);
          case TransactionType::EthEventStatusChanged:
            return ph::lng_wallet_row_eth_event_notification(ph::now).replace("{value}", _layout.additionalInfo);
          case TransactionType::TonEventStatusChanged:
            return ph::lng_wallet_row_ton_event_notification(ph::now).replace("{value}", _layout.additionalInfo);
          case TransactionType::SwapBack:
            return ph::lng_wallet_row_swap_back_to(ph::now);
          case TransactionType::Mint:
            return ph::lng_wallet_row_minted(ph::now);
          case TransactionType::Change:
            return ph::lng_wallet_row_change(ph::now);
          case TransactionType::DePoolReward:
            return ph::lng_wallet_row_reward_from(ph::now);
          case TransactionType::DePoolRewardNotification:
            return ph::lng_wallet_row_reward_notification_from(ph::now);
          case TransactionType::DePoolStake:
            return ph::lng_wallet_row_ordinary_stake_to(ph::now);
          case TransactionType::MultisigSubmit:
            return ph::lng_wallet_row_requested_to(ph::now);
          case TransactionType::MultisigConfirm:
            return ph::lng_wallet_row_confirmed(ph::now).replace("{value}", _layout.additionalInfo);
          default:
            if (incoming) {
              return ph::lng_wallet_row_from(ph::now);
            } else {
              return ph::lng_wallet_row_to(ph::now);
            }
        };
      }());

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
    y += std::max(_layout.amountGrams.minHeight(), st::normalFont->height);

    if (_button.has_value()) {
      auto &button = *_button;
      const auto buttonWidth = button->width();
      button->setGeometry(x + avail - buttonWidth, y + st::walletRowAddressTop, buttonWidth,
                          addressStyle().font->height * 2);
      button->setVisible(true);
    }

    if (!_layout.address.isEmpty()) {
      p.setPen(st::windowFg);
      y += st::walletRowAddressTop;
      _layout.address.drawElided(p, x, y, _layout.addressWidth, _layout.lineCount, style::al_topleft, 0, -1, 0, true);
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

  Fn<void()> _decrypt = [] {};

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
                 rpl::producer<std::pair<HistoryPageKey, Ton::LoadedSlice>> loaded,
                 rpl::producer<not_null<std::vector<Ton::Transaction> *>> collectEncrypted,
                 rpl::producer<not_null<const std::vector<Ton::Transaction> *>> updateDecrypted,
                 rpl::producer<not_null<std::map<QString, QString> *>> updateWalletOwners,
                 rpl::producer<NotificationsHistoryUpdate> updateNotifications,
                 rpl::producer<std::optional<SelectedAsset>> selectedAsset)
    : _widget(parent), _selectedAsset(SelectedToken{.symbol = Ton::Symbol::ton()}) {
  setupContent(std::move(state), std::move(loaded), std::move(selectedAsset));

  base::unixtime::updates()  //
      | rpl::start_with_next(
            [=] {
              for (auto &items : _rows) {
                for (auto &row : items.second.pending) {
                  row->refreshDate();
                }
                for (auto &row : items.second.regular) {
                  row->refreshDate();
                }
              }
              refreshShowDates(_selectedAsset.current());
            },
            _widget.lifetime());

  std::move(collectEncrypted)  //
      | rpl::start_with_next(
            [=](not_null<std::vector<Ton::Transaction> *> list) {
              auto it = _transactions.find(kMainPageKey);
              if (it != end(_transactions)) {
                auto &&encrypted = ranges::views::all(it->second.list) | ranges::views::filter(IsEncryptedMessage);
                list->insert(list->end(), encrypted.begin(), encrypted.end());
              }
            },
            _widget.lifetime());

  std::move(updateDecrypted)  //
      | rpl::start_with_next(
            [=](not_null<const std::vector<Ton::Transaction> *> list) {
              auto it = _transactions.find(kMainPageKey);
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
                refreshShowDates(_selectedAsset.current());
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
                refreshShowDates(selectedAsset);
              }
            },
            _widget.lifetime());

  std::move(updateNotifications)  //
      | rpl::start_with_next(
            [=](NotificationsHistoryUpdate &&update) {
              mergeNotifications(std::forward<std::decay_t<decltype(update)>>(update));
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

  auto rowsIt = _rows.find(currentPage());
  if (rowsIt == end(_rows)) {
    return;
  }
  auto &rows = rowsIt->second;

  auto top = (rows.pending.empty() && rows.regular.empty()) ? 0 : st::walletRowsSkip;
  int height = 0;

  auto maxLt = std::numeric_limits<int64>::max();
  for (auto i = 0, j = 0; i < rows.pending.size() || j < rows.regular.size();) {
    auto &pending = rows.pending;
    auto &regular = rows.regular;

    HistoryRow *row = nullptr;

    const auto pendingLt = i < pending.size() ? pending[i]->transaction().id.lt : 0;
    const auto regularLt = j < regular.size() ? regular[j]->transaction().id.lt : 0;

    if (pendingLt < maxLt && pendingLt > std::max(regularLt, int64{0})) {
      row = pending[i++].get();
    } else if (regularLt < maxLt && regularLt > std::max(pendingLt, int64{0})) {
      row = regular[j++].get();
    }

    if (row != nullptr) {
      maxLt = row->transaction().id.lt;
      row->setTop(top + height);
      row->resizeToWidth(width);
      height += row->height();
    }
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
  auto page = currentPage();

  _visibleTop = top - _widget.y();
  _visibleBottom = bottom - _widget.y();

  auto transactionsIt = _transactions.find(page);
  auto rowsIt = _rows.find(page);
  if (_visibleBottom <= _visibleTop ||
      (transactionsIt != end(_transactions) && !transactionsIt->second.previousId.lt) ||
      (rowsIt != end(_rows) && rowsIt->second.regular.empty())) {
    return;
  }
  checkPreload();
}

rpl::producer<std::pair<HistoryPageKey, Ton::TransactionId>> History::preloadRequests() const {
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

rpl::producer<not_null<const QString *>> History::dePoolDetailsRequests() const {
  return _dePoolDetailsRequests.events();
}

rpl::producer<not_null<const Ton::Transaction *>> History::tokenDetailsRequests() const {
  return _tokenDetailsRequests.events();
}

rpl::producer<not_null<const QString *>> History::collectTokenRequests() const {
  return _collectTokenRequests.events();
}

rpl::producer<not_null<const QString *>> History::executeSwapBackRequests() const {
  return _executeSwapBackRequests.events();
}

rpl::producer<std::pair<QString, int64>> History::multisigConfirmRequests() const {
  return _multisigConfirmRequests.events();
}

rpl::lifetime &History::lifetime() {
  return _widget.lifetime();
}

void History::setupContent(rpl::producer<HistoryState> &&state,
                           rpl::producer<std::pair<HistoryPageKey, Ton::LoadedSlice>> &&loaded,
                           rpl::producer<std::optional<SelectedAsset>> &&selectedAsset) {
  std::forward<std::decay_t<decltype(state)>>(state)  //
      | rpl::start_with_next([=](HistoryState &&state) { mergeState(std::move(state)); }, lifetime());

  std::forward<std::decay_t<decltype(loaded)>>(loaded)  //
      | rpl::start_with_next(
            [=](const std::pair<HistoryPageKey, Ton::LoadedSlice> &slice) {
              auto it = _transactions.find(slice.first);
              if (it == end(_transactions)) {
                it = _transactions
                         .emplace(std::piecewise_construct, std::forward_as_tuple(slice.first),
                                  std::forward_as_tuple(TransactionsState{}))
                         .first;
              }
              auto &transactions = it->second;

              transactions.previousId = slice.second.data.previousId;
              transactions.list.insert(end(transactions.list), slice.second.data.list.begin(),
                                       slice.second.data.list.end());
              refreshRows(_selectedAsset.current());
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
                  selectRow(std::make_pair(false, -1), nullptr);
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

  rpl::combine(std::forward<std::decay_t<decltype(selectedAsset)>>(selectedAsset))  //
      | rpl::start_with_next(
            [=](const std::optional<SelectedAsset> &asset) {
              v::match(
                  _selectedAsset.current(),
                  [&](const SelectedToken &selectedToken) {
                    const auto it = _rows.find(std::make_pair(selectedToken.symbol, QString{}));
                    if (it == _rows.end()) {
                      return;
                    }
                    for (auto &row : it->second.pending) {
                      row->setVisible(false);
                    }
                  },
                  [&](const SelectedMultisig &selectedMultisig) {
                    const auto it = _rows.find(accountPageKey(selectedMultisig.address));
                    if (it == _rows.end()) {
                      return;
                    }
                    for (auto &row : it->second.regular) {
                      row->setVisible(false);
                    }
                  },
                  [&](auto &&) {});

              _selectedAsset = asset.value_or(SelectedToken::defaultToken());
              refreshShowDates(_selectedAsset.current());
            },
            _widget.lifetime());
}

void History::selectRow(const std::pair<bool, int> &selected, const ClickHandlerPtr &handler) {
  Expects(selected.second >= 0 || !handler);

  const auto rowsIt = _rows.find(currentPage());
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = selected.first ? rowsIt->second.pending : rowsIt->second.regular;

  if (_selected != selected) {
    const auto was = (_selected.second >= 0 && _selected.second < int(rows.size()))  //
                         ? rows[_selected.second].get()
                         : nullptr;
    if (was) {
      repaintRow(was);
    }
    _selected = selected;
    _widget.setCursor((_selected.second >= 0) ? style::cur_pointer : style::cur_default);
  }

  if (ClickHandler::getActive() != handler) {
    const auto now = (_selected.second >= 0 && _selected.second < int(rows.size()))  //
                         ? rows[_selected.second].get()
                         : nullptr;
    if (now) {
      repaintRow(now);
    }
    ClickHandler::setActive(handler);
  }
}

void History::selectRowByMouse() {
  const auto rowsIt = _rows.find(currentPage());
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = rowsIt->second;

  const auto point = _widget.mapFromGlobal(QCursor::pos());

  const auto searchRow = [&](const std::decay_t<decltype(rows.regular)> &rows, bool pending) -> bool {
    const auto from = ranges::upper_bound(rows, point.y(), ranges::less(), &HistoryRow::bottom);
    const auto till = ranges::lower_bound(rows, point.y(), ranges::less(), &HistoryRow::top);

    if (from != rows.end() && from != till && (*from)->isUnderCursor(point)) {
      selectRow(std::make_pair(pending, from - begin(rows)), (*from)->handlerUnderCursor(point));
      return true;
    }
    return false;
  };

  if (!searchRow(rows.regular, false) && !searchRow(rows.pending, true)) {
    selectRow(std::make_pair(false, -1), nullptr);
  }
}

void History::pressRow() {
  _pressed = _selected;
  ClickHandler::pressed();
}

void History::releaseRow() {
  auto page = currentPage();
  const auto [pending, selected] = _selected;

  const auto rowsIt = _rows.find(page);
  if (rowsIt == end(_rows)) {
    return;
  }
  const auto &rows = pending ? rowsIt->second.pending : rowsIt->second.regular;

  Expects(selected < static_cast<int>(rows.size()));

  const auto handler = ClickHandler::unpressed();
  if (std::exchange(_pressed, std::make_pair(false, -1)) != _selected || selected < 0) {
    if (handler) {
      handler->onClick(ClickContext());
    }
    return;
  }

  if (handler) {
    handler->onClick(ClickContext());
  } else {
    const auto it = _transactions.find(pending ? kMainPageKey : page);
    if (it != _transactions.end()) {
      const auto i = ranges::find(it->second.list, rows[selected]->id(), &Ton::Transaction::id);
      Assert(i != end(it->second.list));
      _viewRequests.fire_copy(*i);
    }
  }
}

void History::decryptById(const Ton::TransactionId &id) {
  const auto transactionsIt = _transactions.find(kMainPageKey);
  if (transactionsIt == _transactions.end()) {
    return;
  }
  const auto &transactions = transactionsIt->second;

  const auto i = ranges::find(transactions.list, id, &Ton::Transaction::id);
  Assert(i != end(transactions.list));
  _decryptRequests.fire_copy(*i);
}

void History::paint(Painter &p, QRect clip) {
  auto rowsIt = _rows.find(currentPage());
  if (rowsIt == _rows.end()) {
    return;
  }
  const auto &rows = rowsIt->second;

  if (rows.pending.empty() && rows.regular.empty()) {
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
  paintRows(rows.pending);
  paintRows(rows.regular);
}

void History::mergeState(HistoryState &&state) {
  _knownContracts = std::move(state.knownContracts);
  mergePending(std::move(state.pendingTransactions));
  //refreshPending();
  if (mergeListChanged(std::move(state.lastTransactions))) {
    refreshRows(_selectedAsset.current());
  }
}

void History::mergePending(std::vector<Ton::PendingTransaction> &&list) {
  _pendingDataChanged = _pendingData != list;
  if (_pendingDataChanged) {
    _pendingData = std::move(list);
  }
}

void History::mergeNotifications(NotificationsHistoryUpdate &&update) {
  v::match(
      update,
      [&](AddNotification &notification) {
        const auto page = std::make_pair(notification.symbol, QString{});

        auto it = _rows.find(page);
        auto newSymbol = it == _rows.end();
        if (newSymbol) {
          it = _rows.emplace(std::piecewise_construct, std::forward_as_tuple(page), std::forward_as_tuple(RowsState{}))
                   .first;
        }
        auto &rows = it->second.pending;

        auto latestIt = rows.begin();
        while (latestIt != rows.end() && notification.transaction.id.lt < (*latestIt)->transaction().id.lt) {
          ++latestIt;
        }
        it->second.pending.insert(latestIt, makeRow(notification.transaction));

        const auto asset = SelectedToken{.symbol = notification.symbol};
        if (newSymbol) {
          refreshRows(asset);
        } else {
          refreshShowDates(asset);
        }
      },
      [&](RemoveNotification &notification) {
        const auto page = std::make_pair(notification.symbol, QString{});

        const auto it = _rows.find(page);
        if (it == _rows.end()) {
          return;
        }
        auto &rows = it->second.pending;
        using Item = std::decay_t<decltype(rows.front())>;
        rows.erase(  //
            ranges::remove_if(rows,
                              [&](const Item &item) { return item->transaction().id == notification.transactionId; }),
            end(rows));
      },
      [&](RefreshNotifications &) { refreshShowDates(_selectedAsset.current()); });
}

bool History::mergeListChanged(std::map<HistoryPageKey, Ton::TransactionsSlice> &&data) {
  auto changed = false;
  for (auto &[page, newTransactions] : data) {
    auto transactionsIt = _transactions.find(page);
    if (transactionsIt == end(_transactions)) {
      transactionsIt = _transactions
                           .emplace(std::piecewise_construct, std::forward_as_tuple(std::move(page)),
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
      changed = true;
    } else if (i != newTransactions.list.cbegin()) {
      transactions.list.insert(begin(transactions.list), newTransactions.list.cbegin(), i);
      changed = true;
    }
  }
  return changed;
}

void History::setRowShowDate(not_null<HistoryRow *> row, bool show) {
  row->setShowDate(show, [=] { repaintShadow(row); });
}

bool History::takeDecrypted(int index, const std::vector<Ton::Transaction> &decrypted) {
  auto rowsIt = _rows.find(kMainPageKey);
  auto transactionsIt = _transactions.find(kMainPageKey);
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
    rows.regular[index] = makeRow(*i);
  }
  return true;
}

std::unique_ptr<HistoryRow> History::makeRow(const Ton::Transaction &data) {
  const auto id = data.id;
  if (id.lt == 0) {
    // pending
    return std::make_unique<HistoryRow>(data);
  }

  return std::make_unique<HistoryRow>(data, [=] { decryptById(id); });
}

void History::refreshShowDates(const SelectedAsset &selectedAsset) {
  const auto [page, targetAddress] = v::match(
      selectedAsset,
      [](const SelectedToken &token) { return std::make_pair(std::make_pair(token.symbol, QString{}), QString{}); },
      [](const SelectedDePool &depool) { return std::make_pair(kMainPageKey, depool.address); },
      [](const SelectedMultisig &multisig) { return std::make_pair(accountPageKey(multisig.address), QString{}); });

  const auto rowsIt = _rows.find(page);
  if (rowsIt == _rows.end()) {
    return;
  }
  auto &rows = rowsIt->second;

  const auto transactionsIt = _transactions.find(page);
  auto *transactions = transactionsIt != end(_transactions) ? &transactionsIt->second : nullptr;

  QSet<QString> unknownOwners;

  std::map<QString, Ton::EthEventStatus> latestEthStatuses;
  std::map<QString, Ton::TonEventStatus> latestTonStatuses;

  auto filterTransaction = [&, targetAddress = targetAddress, pageAddress = page.second](
                               const SelectedAsset &selectedAsset, bool briefNotifications,
                               not_null<HistoryRow *> row) {
    auto &transaction = row->transaction();

    const auto isUnprocessed = transactions == nullptr ||  //
                               transaction.id.lt < transactions->leastScannedTransactionLt ||
                               transaction.id.lt > transactions->latestScannedTransactionLt;

    v::match(
        selectedAsset,
        [&](const SelectedToken &selectedToken) {
          row->setVisible(true);
          if (selectedToken.symbol.isTon()) {
            return v::match(
                transaction.additional,
                [&](const Ton::EthEventStatusChanged &event) {
                  auto showButton = event.status == Ton::EthEventStatus::Confirmed;
                  const auto it = latestEthStatuses.find(transaction.incoming.source);
                  if (it != latestEthStatuses.end()) {
                    showButton = false;
                  } else {
                    latestEthStatuses.insert(std::make_pair(transaction.incoming.source, event.status));
                  }

                  const auto &address = transaction.incoming.source;
                  row->setNotificationLayout(
                      &_widget, EventType::EthEvent, RegularTransactionParams{.brief = briefNotifications},
                      showButton ? [=] { _collectTokenRequests.fire(&address); } : Fn<void()>{nullptr});
                },
                [&](const Ton::TonEventStatusChanged &event) {
                  auto showButton = event.status == Ton::TonEventStatus::Confirmed;
                  const auto it = latestTonStatuses.find(transaction.incoming.source);
                  if (it != latestTonStatuses.end()) {
                    showButton = false;
                  } else {
                    latestTonStatuses.insert(std::make_pair(transaction.incoming.source, event.status));
                  }

                  if (showButton &&
                      (base::unixtime::now() - static_cast<TimeId>(transaction.time) > kExecuteVisibleTimeout)) {
                    showButton = false;
                  }

                  const auto &address = transaction.incoming.source;
                  row->setNotificationLayout(
                      &_widget, EventType::TonEvent, RegularTransactionParams{.brief = briefNotifications},
                      showButton ? [=] { _executeSwapBackRequests.fire(&address); } : Fn<void()>{nullptr});
                },
                [&](auto &&) {
                  const auto asReturnedChange = !transaction.incoming.source.isEmpty() &&
                                                v::is<Ton::RegularTransaction>(transaction.additional) &&
                                                (_knownContracts.contains(transaction.incoming.source) ||
                                                 _tokenOwners.find(transaction.incoming.source) != _tokenOwners.end());
                  row->setRegularLayout(RegularTransactionParams{.asReturnedChange = asReturnedChange});
                });
          } else {
            v::match(
                transaction.additional,
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
                [&](auto &&) {});
            row->setTokenTransactionLayout(selectedToken.symbol);
          }
        },
        [&](const SelectedDePool &selectedDePool) {
          auto maybeDePool = false;

          const auto incoming = !transaction.incoming.source.isEmpty();
          if (incoming && transaction.incoming.source == targetAddress) {
            maybeDePool = true;
          } else if (!incoming && !transaction.aborted) {
            for (const auto &out : transaction.outgoing) {
              if (out.destination != targetAddress) {
                continue;
              }
              maybeDePool = true;
              break;
            }
          }

          if (maybeDePool) {
            row->setDePoolTransactionLayout();
          } else {
            row->setVisible(false);
          }
        },
        [&](const SelectedMultisig & /*selectedMultisig*/) {
          v::match(
              transaction.additional,
              [&](const Ton::MultisigSubmitTransaction &submitTransaction) {
                row->setMultisigSubmitTransactionLayout(&_widget, [=] {
                  if (submitTransaction.transactionId) {
                    _multisigConfirmRequests.fire(std::make_pair(pageAddress, submitTransaction.transactionId));
                  }
                });
              },
              [&](auto &&) { row->setMultisigLayout(); });
        });
  };

  auto previous = QDate();

  auto maxLt = std::numeric_limits<int64>::max();
  for (auto i = 0, j = 0; i < rows.pending.size() || j < rows.regular.size();) {
    auto &pending = rows.pending;
    auto &regular = rows.regular;

    HistoryRow *row = nullptr;
    const auto pendingLt = i < pending.size() ? pending[i]->transaction().id.lt : 0;
    const auto regularLt = j < regular.size() ? regular[j]->transaction().id.lt : 0;

    if (pendingLt < maxLt && pendingLt > std::max(regularLt, int64{0})) {
      row = pending[i++].get();
      filterTransaction(SelectedToken{.symbol = Ton::Symbol::ton()}, true, row);
    } else if (regularLt < maxLt && regularLt > std::max(pendingLt, int64{0})) {
      row = regular[j++].get();
      filterTransaction(selectedAsset, false, row);
    }

    if (row != nullptr) {
      maxLt = row->transaction().id.lt;
      const auto current = row->date().date();
      setRowShowDate(row, row->isVisible() && current != previous);
      if (row->isVisible()) {
        previous = current;
      }
    }
  }

  if (!rows.regular.empty() && transactions != nullptr) {
    transactions->latestScannedTransactionLt = rows.regular.front()->transaction().id.lt;
    transactions->leastScannedTransactionLt = rows.regular.back()->transaction().id.lt;
  }

  resizeToWidth(_widget.width());

  if (!unknownOwners.empty()) {
    _ownerResolutionRequests.fire(std::make_pair(&page.first, &unknownOwners));
  }

  _widget.update(0, _visibleTop, _widget.width(), _visibleBottom - _visibleTop);
}

void History::refreshPending() {
  const auto page = currentPage();
  if (page != kMainPageKey) {
    return;
  }

  const auto it = _rows.find(page);
  if (it == end(_rows)) {
    return;
  }
  auto &pendingRows = it->second.pending;

  if (_pendingDataChanged) {
    pendingRows =                                                                                            //
        ranges::views::all(_pendingData)                                                                     //
        | ranges::views::transform([&](const Ton::PendingTransaction &data) { return makeRow(data.fake); })  //
        | ranges::to_vector;
  }

  if (!pendingRows.empty()) {
    auto pendingRow = pendingRows.front().get();
    if (pendingRow->isVisible()) {
      setRowShowDate(pendingRow);
    }
  }
  resizeToWidth(_widget.width());
}

void History::refreshRows(const SelectedAsset &selectedAsset) {
  using RowItem = std::decay_t<decltype(_rows.begin()->second.regular.front())>;

  auto mergeTransactions = [&](std::vector<std::unique_ptr<HistoryRow>> &rows,
                               const std::vector<Ton::Transaction> &transactions,
                               const Fn<RowItem(const Ton::Transaction &)> &makeRow) {
    auto addedFront = std::vector<std::unique_ptr<HistoryRow>>();
    auto addedBack = std::vector<std::unique_ptr<HistoryRow>>();
    for (const auto &element : transactions) {
      if (!rows.empty() && element.id == rows.front()->id()) {
        break;
      }
      addedFront.push_back(makeRow(element));
    }
    if (!rows.empty()) {
      const auto from = ranges::find(transactions, rows.back()->id(), &Ton::Transaction::id);
      if (from != end(transactions)) {
        addedBack = ranges::make_subrange(from + 1, end(transactions))                                       //
                    | ranges::views::transform([&](const Ton::Transaction &data) { return makeRow(data); })  //
                    | ranges::to_vector;
      }
    }
    if (addedFront.empty() && addedBack.empty()) {
      return;
    } else if (!addedFront.empty()) {
      if (addedFront.size() < transactions.size()) {
        addedFront.insert(end(addedFront), std::make_move_iterator(begin(rows)), std::make_move_iterator(end(rows)));
      }
      rows = std::move(addedFront);
    }
    rows.insert(end(rows), std::make_move_iterator(begin(addedBack)), std::make_move_iterator(end(addedBack)));
  };

  auto addDePool = [&](const QString &address) {
    if (_knownDePools.insert(address).second) {
      _dePoolDetailsRequests.fire(&address);
    }
  };

  auto requestDetails = [this](const Ton::Transaction &transaction) {
    if (!transaction.incoming.source.isEmpty()) {
      _tokenDetailsRequests.fire(&transaction);
    }
  };

  for (const auto &[page, transactions] : _transactions) {
    auto rowsIt = _rows.find(page);
    if (rowsIt == end(_rows)) {
      rowsIt = _rows
                   .emplace(std::piecewise_construct, std::forward_as_tuple(page),
                            std::forward_as_tuple(RowsState{.regular = std::vector<std::unique_ptr<HistoryRow>>{}}))
                   .first;
    }
    if (page == kMainPageKey) {
      mergeTransactions(rowsIt->second.regular, transactions.list, [&](const Ton::Transaction &transaction) {
        v::match(
            transaction.additional,  //
            [&](const Ton::TokenWalletDeployed &event) {
              if (_knownRootTokenContracts.insert(event.rootTokenContract).second) {
                _tokenDetailsRequests.fire(&transaction);
              }
            },
            [&](const Ton::EthEventStatusChanged &) { requestDetails(transaction); },
            [&](const Ton::TonEventStatusChanged &) { requestDetails(transaction); },
            [&](const Ton::DePoolOrdinaryStakeTransaction &) {
              for (const auto &out : transaction.outgoing) {
                addDePool(out.destination);
                break;
              }
            },
            [&](const Ton::DePoolOnRoundCompleteTransaction &) {
              if (!transaction.incoming.source.isEmpty()) {
                addDePool(transaction.incoming.source);
              }
            },
            [](auto &&) {});
        return makeRow(transaction);
      });
    } else {
      mergeTransactions(rowsIt->second.regular, transactions.list,
                        [&](const Ton::Transaction &transaction) { return makeRow(transaction); });
    }
  }

  refreshShowDates(selectedAsset);
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

  const auto page = currentPage();

  const auto it = _transactions.find(page);
  if (it != _transactions.end() && _visibleBottom + preloadHeight >= _widget.height() &&
      it->second.previousId.lt != 0) {
    _preloadRequests.fire_copy(std::make_pair(page, it->second.previousId));
  }
}

HistoryPageKey History::currentPage() const {
  return v::match(
      _selectedAsset.current(),                                                             //
      [&](const SelectedToken &token) { return std::make_pair(token.symbol, QString{}); },  //
      [&](const SelectedDePool &depool) { return kMainPageKey; },                           //
      [&](const SelectedMultisig &multisig) { return accountPageKey(multisig.address); });
}

rpl::producer<HistoryState> MakeHistoryState(rpl::producer<Ton::WalletViewerState> state) {
  return std::move(state)  //
         | rpl::map([](Ton::WalletViewerState &&state) {
             QSet<QString> knownContracts;
             for (const auto &item : state.wallet.dePoolParticipantStates) {
               knownContracts.insert(item.first);
             }

             std::map<HistoryPageKey, Ton::TransactionsSlice> lastTransactions{
                 {kMainPageKey, state.wallet.lastTransactions}};

             for (auto &&[address, multisig] : state.wallet.multisigStates) {
               lastTransactions.emplace(std::piecewise_construct, std::forward_as_tuple(accountPageKey(address)),
                                        std::forward_as_tuple(std::move(multisig.lastTransactions)));
             }

             for (auto &&[symbol, token] : state.wallet.tokenStates) {
               lastTransactions.emplace(std::make_pair(symbol, QString{}), token.lastTransactions);
               knownContracts.insert(token.walletContractAddress);
               knownContracts.insert(token.rootOwnerAddress);
               knownContracts.insert(symbol.rootContractAddress());
             }

             return HistoryState{
                 .lastTransactions = std::move(lastTransactions),
                 .pendingTransactions = std::move(state.wallet.pendingTransactions),
                 .knownContracts = std::move(knownContracts),
             };
           });
}

}  // namespace Wallet
