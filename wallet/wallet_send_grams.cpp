// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_send_grams.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/input_fields.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/inline_token_icon.h"
#include "base/algorithm.h"
#include "base/qt_signal_producer.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <iostream>

namespace Wallet {
namespace {

struct FixedAddress {
  QString address{};
  int position = 0;
};

[[nodiscard]] FixedAddress FixAddressInput(const QString &text, int position, bool acceptEthereumAddress) {
  auto [address, isEth] = v::match(
      ParseAddress(text), [](const ParsedAddressTon &ton) { return std::make_pair(ton.address, false); },
      [](const ParsedAddressEth &eth) { return std::make_pair(eth.address, true); });
  if (!acceptEthereumAddress && isEth) {
    address = address.mid(0, address.indexOf('x'));
  }

  auto result = FixedAddress{address, position};

  if (result.address != text) {
    const auto removed = std::max(int(text.size()) - int(result.address.size()), 0);
    result.position = std::max(position - removed, 0);
  }
  return result;
}

}  // namespace

template <typename T>
void SendGramsBox(not_null<Ui::GenericBox *> box, const T &invoice, rpl::producer<Ton::WalletState> state,
                  const Fn<void(const T &, Fn<void(InvoiceField)> error)> &done) {
  constexpr auto isTonTransfer = std::is_same_v<T, TonTransferInvoice>;
  constexpr auto isTokenTransfer = std::is_same_v<T, TokenTransferInvoice>;
  static_assert(isTonTransfer || isTokenTransfer);

  auto symbol = Ton::Symbol::ton();
  if constexpr (isTokenTransfer) {
    symbol = invoice.token;
  }
  const auto tokenDecimals = symbol.decimals();

  const auto prepared = box->lifetime().make_state<T>(invoice);

  auto unlockedBalance =     //
      rpl::duplicate(state)  //
      | rpl::map([=](const Ton::WalletState &state) {
          if constexpr (isTonTransfer) {
            return state.account.fullBalance - state.account.lockedBalance;
          } else if constexpr (isTokenTransfer) {
            const auto it = state.tokenStates.find(symbol);
            if (it != state.tokenStates.end()) {
              return it->second.balance;
            } else {
              return int128{};
            }
          } else {
            return int64{};
          }
        });

  using BalanceType = decltype(invoice.amount);

  const auto funds = std::make_shared<BalanceType>();

  const auto replaceTickerTag = [symbol = symbol] {
    return rpl::map([=](QString &&text) { return text.replace("{ticker}", symbol.name()); });
  };

  const auto replaceAmountTag = [] {
    return rpl::map([=](QString &&text, const QString &amount) { return text.replace("{amount}", amount); });
  };

  const auto titleText = rpl::duplicate(ph::lng_wallet_send_title()) | replaceTickerTag();

  box->setTitle(titleText);
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  AddBoxSubtitle(box, ph::lng_wallet_send_recipient());
  const auto address = box->addRow(object_ptr<Ui::InputField>(
      box, st::walletSendInput, Ui::InputField::Mode::NoNewlines, ph::lng_wallet_send_address(), prepared->address));
  address->rawTextEdit()->setWordWrapMode(QTextOption::WrapAnywhere);

  const auto subtitle = AddBoxSubtitle(box, ph::lng_wallet_send_amount());

  const auto amount = box->addRow(  //
      object_ptr<Ui::InputField>::fromRaw(
          CreateAmountInput(box, rpl::single("0" + AmountSeparator() + "0"), prepared->amount, symbol)),
      st::walletSendAmountPadding);

  auto balanceText =
      rpl::combine(ph::lng_wallet_send_balance(), rpl::duplicate(unlockedBalance)) |
      rpl::map([=, symbol = symbol](QString &&phrase, const BalanceType &value) {
        return phrase.replace("{amount}", FormatAmount(value > 0 ? value : 0, symbol, FormatFlag::Rounded).full);
      });

  const auto diamondLabel =
      Ui::CreateInlineTokenIcon(symbol, subtitle->parentWidget(), 0, 0, st::walletSendBalanceLabel.style.font);
  const auto balanceLabel =
      Ui::CreateChild<Ui::FlatLabel>(subtitle->parentWidget(), std::move(balanceText), st::walletSendBalanceLabel);
  rpl::combine(subtitle->geometryValue(), balanceLabel->widthValue()) |
      rpl::start_with_next(
          [=](QRect rect, int innerWidth) {
            const auto diamondTop = rect.top() + st::walletSubsectionTitle.style.font->ascent - st::walletDiamondAscent;
            const auto diamondRight = st::boxRowPadding.right();
            diamondLabel->moveToRight(diamondRight, diamondTop);
            const auto labelTop = rect.top() + st::walletSubsectionTitle.style.font->ascent -
                                  st::walletSendBalanceLabel.style.font->ascent;
            const auto labelRight =
                diamondRight + st::walletDiamondSize + st::walletSendBalanceLabel.style.font->spacew;
            balanceLabel->moveToRight(labelRight, labelTop);
          },
          balanceLabel->lifetime());

  Ui::InputField *callbackAddress = nullptr;
  Ui::VerticalLayout *callbackAddressWrapper = nullptr;
  rpl::variable<Ton::TokenTransferType> *transferType = nullptr;
  if constexpr (isTokenTransfer) {
    transferType = box->lifetime().make_state<rpl::variable<Ton::TokenTransferType>>(prepared->transferType);
    callbackAddressWrapper = box->addRow(object_ptr<Ui::VerticalLayout>(box), QMargins{});

    AddBoxSubtitle(callbackAddressWrapper, ph::lng_wallet_send_token_proxy_address());
    callbackAddress = callbackAddressWrapper->add(
        object_ptr<Ui::InputField>(box, st::walletSendInput, Ui::InputField::Mode::NoNewlines,
                                   ph::lng_wallet_send_token_proxy_address_placeholder(),
                                   prepared->callbackAddress.isEmpty()
                                       ? prepared->callbackAddress
                                       : Ton::Wallet::ConvertIntoRaw(prepared->callbackAddress)),
        st::boxRowPadding);
    callbackAddress->setSizePolicy(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);
  }

  Ui::InputField *comment = nullptr;
  if constexpr (isTonTransfer) {
    comment = box->addRow(
        object_ptr<Ui::InputField>::fromRaw(CreateCommentInput(box, ph::lng_wallet_send_comment(), prepared->comment)),
        st::walletSendCommentPadding);
  }

  auto isEthereumAddress = box->lifetime().make_state<rpl::variable<bool>>(false);

  if (transferType != nullptr) {
    isEthereumAddress->value()  //
        | rpl::start_with_next(
              [=](bool isSwapBack) {
                *transferType = isSwapBack ? Ton::TokenTransferType::SwapBack : Ton::TokenTransferType::ToOwner;
              },
              box->lifetime());

    transferType->value()  //
        | rpl::start_with_next(
              [=](const Ton::TokenTransferType &type) {
                const auto isSwapBack = type == Ton::TokenTransferType::SwapBack;
                callbackAddress->setEnabled(isSwapBack);
                callbackAddressWrapper->setMaximumHeight(isSwapBack ? QWIDGETSIZE_MAX : 0);
                callbackAddressWrapper->adjustSize();
              },
              box->lifetime());

    address->setPlaceholder(  //
        transferType->value() | rpl::map([](Ton::TokenTransferType type) {
          switch (type) {
            case Ton::TokenTransferType::Direct:
              return ph::lng_wallet_send_token_direct_address();
            case Ton::TokenTransferType::ToOwner:
              return ph::lng_wallet_send_token_owner_address();
            case Ton::TokenTransferType::SwapBack:
              return ph::lng_wallet_send_token_ethereum_address();
            default:
              return ph::lng_wallet_send_address();  // unreachable
          }
        }) |
        rpl::flatten_latest());
  }

  auto text =                                                                  //
      rpl::single(rpl::empty_value())                                          //
      | rpl::then(base::qt_signal_producer(amount, &Ui::InputField::changed))  //
      | rpl::map([=, symbol = symbol]() -> rpl::producer<QString> {
          const auto text = amount->getLastText();
          const auto value = ParseAmountString(text, tokenDecimals).value_or(0);
          if (value > 0) {
            return rpl::combine(                   //
                       isEthereumAddress->value()  //
                           | rpl::map([](bool isEthereumAddress) {
                               if (isEthereumAddress) {
                                 return ph::lng_wallet_send_button_swap_back_amount();
                               } else {
                                 return ph::lng_wallet_send_button_amount();
                               }
                             })  //
                           | rpl::flatten_latest(),
                       ph::lng_wallet_grams_count(FormatAmount(value, symbol).full, symbol)())  //
                   | replaceAmountTag();
          } else {
            return rpl::combine(  //
                       isEthereumAddress->value(),
                       [&]() -> rpl::producer<bool> {
                         if (transferType == nullptr) {
                           return rpl::single(false);
                         } else {
                           return transferType->value() | rpl::map([](Ton::TokenTransferType type) {
                                    return type == Ton::TokenTransferType::SwapBack;
                                  });
                         }
                       }())  //
                   | rpl::map([](bool isEthereumAddress, bool isSwapBack) {
                       if (isEthereumAddress || isSwapBack) {
                         return ph::lng_wallet_send_button_swap_back();
                       } else {
                         return ph::lng_wallet_send_button();
                       }
                     })                     //
                   | rpl::flatten_latest()  //
                   | replaceTickerTag();
          }
        })  //
      | rpl::flatten_latest();

  const auto showError = crl::guard(box, [=](InvoiceField field) {
    switch (field) {
      case InvoiceField::Address:
        return address->showError();
      case InvoiceField::Amount:
        return amount->showError();
      case InvoiceField::Comment: {
        if (comment != nullptr) {
          comment->showError();
        }
        return;
      }
      case InvoiceField::CallbackAddress: {
        if (callbackAddress != nullptr) {
          callbackAddress->showError();
        }
        return;
      }
    }
    Unexpected("Field value in SendGramsBox error callback.");
  });

  const auto submit = [=] {
    auto collected = T{};
    const auto parsed = ParseAmountString(amount->getLastText(), tokenDecimals);
    if (!parsed) {
      amount->showError();
      return;
    }
    collected.address = address->getLastText();
    if constexpr (isTonTransfer) {
      collected.amount = static_cast<int64>(*parsed);
      collected.comment = comment->getLastText();
    } else if constexpr (isTokenTransfer) {
      collected.ownerAddress = collected.address;
      collected.amount = *parsed;
      collected.token = symbol;
      collected.callbackAddress = callbackAddress->getLastText();
      collected.transferType = transferType->current();
    }
    done(collected, showError);
  };

  box->addButton(std::move(text), submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);

  const auto checkFunds = [=, symbol = symbol](const QString &amount) {
    if (const auto value = ParseAmountString(amount, symbol.decimals())) {
      const auto insufficient = (*value > 0 && *value > *funds);
      balanceLabel->setTextColorOverride(insufficient ? std::make_optional(st::boxTextFgError->c) : std::nullopt);
    }
  };

  std::move(unlockedBalance)  //
      | rpl::start_with_next(
            [=](const BalanceType &value) {
              *funds = value;
              checkFunds(amount->getLastText());
            },
            amount->lifetime());

  Ui::Connect(amount, &Ui::InputField::changed,
              [=] { Ui::PostponeCall(amount, [=] { checkFunds(amount->getLastText()); }); });

  Ui::Connect(address, &Ui::InputField::changed, [=] {
    Ui::PostponeCall(address, [=] {
      const auto position = address->textCursor().position();
      const auto now = address->getLastText();
      const auto fixed = FixAddressInput(now, position, isTokenTransfer);
      if (fixed.address != now) {
        address->setText(fixed.address);
        address->setFocusFast();
        address->setCursorPosition(fixed.position);
      }

      const auto colonPosition = fixed.address.indexOf(':');
      const auto isRaw = colonPosition > 0;
      const auto hexPrefixPosition = fixed.address.indexOf("0x");
      *isEthereumAddress = hexPrefixPosition == 0;

      bool shouldShiftFocus = false;
      if (isRaw && ((fixed.address.size() - colonPosition - 1) == kRawAddressLength)) {
        shouldShiftFocus = true;
      } else if (isEthereumAddress->current() && (fixed.address.size() - 2) == kEtheriumAddressLength) {
        shouldShiftFocus = true;
      } else if (!(isRaw || isEthereumAddress->current()) && fixed.address.size() == kEncodedAddressLength) {
        shouldShiftFocus = true;
      }

      if (shouldShiftFocus && address->hasFocus()) {
        if (amount->getLastText().isEmpty()) {
          return amount->setFocus();
        }

        if constexpr (isTonTransfer) {
          return comment->setFocus();
        }

        if (callbackAddress != nullptr && transferType != nullptr &&
            transferType->current() == Ton::TokenTransferType::SwapBack) {
          callbackAddress->setFocus();
        }
      }
    });
  });

  if (callbackAddress != nullptr) {
    Ui::Connect(callbackAddress, &Ui::InputField::changed, [=] {
      Ui::PostponeCall(callbackAddress, [=] {
        const auto position = callbackAddress->textCursor().position();
        const auto now = callbackAddress->getLastText();
        const auto fixed = FixAddressInput(now, position, false);
        if (fixed.address != now) {
          callbackAddress->setText(fixed.address);
          callbackAddress->setFocusFast();
          callbackAddress->setCursorPosition(fixed.position);
        }
      });
    });
  }

  box->setFocusCallback([=] {
    if (prepared->address.isEmpty() || address->getLastText() != prepared->address) {
      address->setFocusFast();
    } else {
      amount->setFocusFast();
    }
  });

  Ui::Connect(address, &Ui::InputField::submitted, [=] {
    const auto text = address->getLastText();
    const auto colonPosition = text.indexOf(':');
    const auto isRaw = colonPosition > 0;
    const auto hexPrefixPosition = text.indexOf("0x");
    const auto isEtheriumAddress = hexPrefixPosition == 0;

    bool showAddressError = false;
    if (isRaw && ((text.size() - colonPosition - 1) != kRawAddressLength)) {
      showAddressError = true;
    } else if (isEtheriumAddress && (text.size() - 2) != kEtheriumAddressLength) {
      showAddressError = true;
    } else if (!(isRaw || isEtheriumAddress) && (text.size() != kEncodedAddressLength)) {
      showAddressError = true;
    }

    if (showAddressError) {
      address->showError();
    } else {
      amount->setFocus();
    }
  });

  Ui::Connect(amount, &Ui::InputField::submitted, [=] {
    if (ParseAmountString(amount->getLastText(), tokenDecimals).value_or(0) <= 0) {
      return amount->showError();
    }

    if constexpr (isTokenTransfer) {
      comment->setFocus();
    }
  });

  if (comment != nullptr) {
    Ui::Connect(comment, &Ui::InputField::submitted, submit);
  }

  if (callbackAddressWrapper != nullptr) {
    callbackAddressWrapper->setMaximumHeight(0);
  }
}

template void SendGramsBox<TonTransferInvoice>(
    not_null<Ui::GenericBox *> box, const TonTransferInvoice &invoice, rpl::producer<Ton::WalletState> state,
    const Fn<void(const TonTransferInvoice &, Fn<void(InvoiceField)> error)> &done);

template void SendGramsBox<TokenTransferInvoice>(
    not_null<Ui::GenericBox *> box, const TokenTransferInvoice &invoice, rpl::producer<Ton::WalletState> state,
    const Fn<void(const TokenTransferInvoice &, Fn<void(InvoiceField)> error)> &done);

}  // namespace Wallet
