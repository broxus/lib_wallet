// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_confirm_transaction.h"

#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "wallet/wallet_send_grams.h"
#include "ui/address_label.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/text/text_utilities.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

#include <QtGui/QtEvents>

namespace Wallet {
namespace {

constexpr auto kWarningPreviewLength = 30;

template <typename T>
[[nodiscard]] rpl::producer<TextWithEntities> PrepareEncryptionWarning(const T &invoice) {
  static_assert(is_any_of<T, TonTransferInvoice, TokenTransferInvoice, StakeInvoice, WithdrawalInvoice,
                          WithdrawalInvoice, DeployTokenWalletInvoice>);

  QString text{};
  if constexpr (std::is_same_v<T, TonTransferInvoice>) {
    text = (invoice.comment.size() > kWarningPreviewLength)
               ? (invoice.comment.mid(0, kWarningPreviewLength - 3) + "...")
               : invoice.comment;
  }

  return ph::lng_wallet_confirm_warning(Ui::Text::RichLangValue) | rpl::map([=](TextWithEntities value) {
           const auto was = QString("{comment}");
           const auto wasLength = was.size();
           const auto nowLength = text.size();
           const auto position = value.text.indexOf(was);
           if (position >= 0) {
             value.text = value.text.mid(0, position) + text + value.text.mid(position + wasLength);
             auto entities = EntitiesInText();
             for (auto &entity : value.entities) {
               const auto from = entity.offset();
               const auto till = from + entity.length();
               if (till < position + wasLength) {
                 if (from < position) {
                   entity.shrinkFromRight(std::max(till - position, 0));
                   entities.push_back(std::move(entity));
                 }
               } else if (from > position) {
                 if (till > position + wasLength) {
                   entity.extendToLeft(std::min(from - (position + wasLength), 0));
                   entity.shiftRight(nowLength - wasLength);
                   entities.push_back(std::move(entity));
                 }
               } else {
                 entity.shrinkFromRight(wasLength - nowLength);
                 entities.push_back(std::move(entity));
               }
             }
             value.entities = std::move(entities);
           }
           return value;
         });
}

}  // namespace

template <typename T>
void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const T &invoice, int64 fee, const Fn<void()> &confirmed) {
  constexpr auto isTonTransfer = std::is_same_v<T, TonTransferInvoice>;
  constexpr auto isTokenTransfer = std::is_same_v<T, TokenTransferInvoice>;
  constexpr auto isStakeTransfer = std::is_same_v<T, StakeInvoice>;
  constexpr auto isWithdrawal = std::is_same_v<T, WithdrawalInvoice>;
  constexpr auto isCancelWithdrawal = std::is_same_v<T, CancelWithdrawalInvoice>;
  constexpr auto isDeployTokenWallet = std::is_same_v<T, DeployTokenWalletInvoice>;
  constexpr auto isCollectTokens = std::is_same_v<T, CollectTokensInvoice>;
  static_assert(isTonTransfer || isTokenTransfer || isStakeTransfer || isWithdrawal || isCancelWithdrawal ||
                isDeployTokenWallet || isCollectTokens);

  auto token = Ton::Symbol::ton();
  if constexpr (isTokenTransfer) {
    token = invoice.token;
  }

  QString address{};
  if constexpr (isTonTransfer || isTokenTransfer) {
    address = invoice.address;
  } else if constexpr (isStakeTransfer || isWithdrawal || isCancelWithdrawal) {
    address = invoice.dePool;
  } else if constexpr (isDeployTokenWallet) {
    address = invoice.rootContractAddress;
  } else if constexpr (isCollectTokens) {
    address = invoice.eventContractAddress;
  }

  bool showAsRaw = true;
  if constexpr (isTokenTransfer) {
    showAsRaw = Ton::Wallet::CheckAddress(address);
  }
  if (showAsRaw) {
    address = Ton::Wallet::ConvertIntoRaw(address);
  }

  box->setTitle(ph::lng_wallet_confirm_title());

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });
  box->setCloseByOutsideClick(false);

  const auto amount = [=]() constexpr {
    if constexpr (isTonTransfer || isTokenTransfer) {
      return FormatAmount(invoice.amount, token).full;
    } else if constexpr (isStakeTransfer) {
      return FormatAmount(invoice.stake, token).full;
    } else if constexpr (isWithdrawal) {
      return FormatAmount(invoice.amount, token).full;
    } else if constexpr (isCancelWithdrawal || isDeployTokenWallet || isCollectTokens) {
      return FormatAmount(0, token).full;
    }
  }
  ();

  auto confirmationText = [=]() constexpr {
    if constexpr (isWithdrawal) {
      return ph::lng_wallet_confirm_withdrawal_text();
    } else if constexpr (isCancelWithdrawal) {
      return ph::lng_wallet_confirm_cancel_withdrawal_text();
    } else if constexpr (isDeployTokenWallet) {
      return ph::lng_wallet_confirm_deploy_token_wallet_text();
    } else if constexpr (isCollectTokens) {
      return ph::lng_wallet_confirm_collect_tokens_text();
    } else {
      return ph::lng_wallet_confirm_text();
    }
  }
  ();

  auto text = rpl::combine(std::move(confirmationText), ph::lng_wallet_grams_count(amount, token)())  //
              | rpl::map([=](QString &&text, const QString &grams) {
                  return Ui::Text::RichLangValue(text.replace("{grams}", grams));
                });

  box->addRow(object_ptr<Ui::FlatLabel>(box, std::move(text), st::walletLabel), st::walletConfirmationLabelPadding);

  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(
                  box, rpl::single(address), st::walletConfirmationAddressLabel, nullptr, st::windowBgOver->c)),
              st::walletConfirmationAddressPadding);

  const auto feeParsed = FormatAmount(fee, Ton::Symbol::ton()).full;
  auto feeText =
      rpl::combine(ph::lng_wallet_confirm_fee(), ph::lng_wallet_grams_count(feeParsed, Ton::Symbol::ton())()) |
      rpl::map([=](QString &&text, const QString &grams) { return text.replace("{grams}", grams); });
  const auto feeWrap = box->addRow(object_ptr<Ui::FixedHeightWidget>(
      box, (st::walletConfirmationFee.style.font->height + st::walletConfirmationSkip)));
  const auto feeLabel = Ui::CreateChild<Ui::FlatLabel>(feeWrap, std::move(feeText), st::walletConfirmationFee);
  rpl::combine(feeLabel->widthValue(), feeWrap->widthValue())  //
      | rpl::start_with_next(
            [=](int innerWidth, int outerWidth) { feeLabel->moveToLeft((outerWidth - innerWidth) / 2, 0, outerWidth); },
            feeLabel->lifetime());

  if constexpr (isTonTransfer) {
    if (invoice.sendUnencryptedText && !invoice.comment.isEmpty()) {
      box->addRow(object_ptr<Ui::FlatLabel>(box, PrepareEncryptionWarning(invoice), st::walletLabel));
    }
  }

  box->events()  //
      | rpl::start_with_next(
            [=](not_null<QEvent *> e) {
              if (e->type() == QEvent::KeyPress) {
                const auto key = dynamic_cast<QKeyEvent *>(e.get())->key();
                if (key == Qt::Key_Enter || key == Qt::Key_Return) {
                  confirmed();
                }
              }
            },
            box->lifetime());

  const auto replaceTickerTag = [](const Ton::Symbol &selectedToken) {
    return rpl::map([selectedToken](QString &&text) { return text.replace("{ticker}", selectedToken.name()); });
  };

  auto buttonText = [=]() constexpr {
    if constexpr (isWithdrawal) {
      return ph::lng_wallet_confirm_withdrawal();
    } else if constexpr (isCancelWithdrawal) {
      return ph::lng_wallet_confirm_cancel_withdrawal();
    } else if constexpr (isDeployTokenWallet) {
      return ph::lng_wallet_confirm_deploy_token_wallet();
    } else if constexpr (isCollectTokens) {
      return ph::lng_wallet_confirm_execute();
    } else {
      return ph::lng_wallet_confirm_send();
    }
  }
  ();

  box->addButton(std::move(buttonText) | replaceTickerTag(token), confirmed);
  box->addButton(ph::lng_wallet_cancel(), [=] { box->closeBox(); });
}

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const TonTransferInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const TokenTransferInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const StakeInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const WithdrawalInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const CancelWithdrawalInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const DeployTokenWalletInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

template void ConfirmTransactionBox(not_null<Ui::GenericBox *> box, const CollectTokensInvoice &invoice, int64 fee,
                                    const Fn<void()> &confirmed);

}  // namespace Wallet
