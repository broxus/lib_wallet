// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "wallet/wallet_sending_transaction.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ton/ton_state.h"
#include "ui/widgets/buttons.h"
#include "ui/lottie_widget.h"
#include "base/timer_rpl.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"

namespace Wallet {
namespace {

constexpr auto kShowCloseDelay = 10 * crl::time(1000);

[[nodiscard]] int AskPasswordBoxHeight() {
  return st::boxTitleHeight - st::boxTopMargin + st::walletPasscodeHeight;
}

}  // namespace

void SendingTransactionBox(not_null<Ui::GenericBox *> box, const Ton::Symbol &symbol, rpl::producer<> confirmed) {
  const auto inner = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, AskPasswordBoxHeight()));

  const auto lottie = inner->lifetime().make_state<Ui::LottieAnimation>(inner, Ui::LottieFromResource("money"));
  lottie->start();

  box->setCloseByEscape(false);
  box->setCloseByOutsideClick(false);

  rpl::merge(std::move(confirmed), base::timer_once(kShowCloseDelay))  //
      | rpl::take(1)                                                   //
      | rpl::start_with_next([=] { box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); }); }, box->lifetime());

  const auto title = Ui::CreateChild<Ui::FlatLabel>(
      inner, ph::lng_wallet_sending_title() | rpl::map([symbol = symbol](QString &&title) {
               return title.replace("{ticker}", symbol.name());
             }),
      st::walletSendingTitle);
  const auto text = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_sending_text(), st::walletSendingText);

  inner->widthValue() |
      rpl::start_with_next(
          [=](int width) {
            lottie->setGeometry({(width - st::walletSendingLottieSize) / 2, st::walletSendingLottieTop,
                                 st::walletSendingLottieSize, st::walletSendingLottieSize});
            title->moveToLeft((width - title->width()) / 2, st::walletSendingTitleTop, width);
            text->moveToLeft((width - text->width()) / 2, st::walletSendingTextTop, width);
          },
          inner->lifetime());
}

template <typename T>
void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result, const T &invoice,
                    const Fn<void()> &onClose) {
  constexpr auto isTonTransfer = std::is_same_v<T, TonTransferInvoice>;
  constexpr auto isTokenTransfer = std::is_same_v<T, TokenTransferInvoice>;
  constexpr auto isStakeTransfer = std::is_same_v<T, StakeInvoice>;
  constexpr auto isWithdrawal = std::is_same_v<T, WithdrawalInvoice>;
  constexpr auto isCancelWithdrawal = std::is_same_v<T, CancelWithdrawalInvoice>;
  constexpr auto isDeployTokenWallet = std::is_same_v<T, DeployTokenWalletInvoice>;
  static_assert(isTonTransfer || isTokenTransfer || isStakeTransfer || isWithdrawal || isCancelWithdrawal ||
                isDeployTokenWallet);

  const auto defaultToken = Ton::Symbol::ton();

  const auto inner = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, AskPasswordBoxHeight()));

  const auto lottie = inner->lifetime().make_state<Ui::LottieAnimation>(inner, Ui::LottieFromResource("done"));
  lottie->start();
  lottie->stopOnLoop(1);

  const auto title = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_sent_title(), st::walletSendingTitle);

  Ui::FlatLabel *amountLabel = nullptr;
  if constexpr (isTokenTransfer) {
    const auto amount = FormatAmount(invoice.amount, invoice.token).full;
    amountLabel = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_grams_count_sent(amount, invoice.token)(),
                                                 st::walletSendingText);
  } else if constexpr (isWithdrawal) {
    const auto amount = FormatAmount(invoice.amount, defaultToken).full;
    amountLabel = Ui::CreateChild<Ui::FlatLabel>(
        inner, invoice.all ? ph::lng_wallet_sending_all_stake() : ph::lng_wallet_grams_count_withdrawn(amount)(),
        st::walletSendingText);
  } else if constexpr (isCancelWithdrawal) {
    amountLabel = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_sent_cancel_withdrawal(), st::walletSendingText);
  } else if constexpr (isDeployTokenWallet) {
    amountLabel =
        Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_sent_deploy_token_wallet(), st::walletSendingText);
  }

  const auto realAmount = FormatAmount(-CalculateValue(result), defaultToken).full;
  Ui::FlatLabel *text = nullptr;
  if constexpr (isTonTransfer || isStakeTransfer || isWithdrawal || isCancelWithdrawal || isDeployTokenWallet) {
    text = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_grams_count_sent(realAmount, defaultToken)(),
                                          st::walletSendingText);
  } else if constexpr (isTokenTransfer) {
    text = Ui::CreateChild<Ui::FlatLabel>(inner, ph::lng_wallet_row_fees() | rpl::map([realAmount](QString &&text) {
                                                   return text.replace("{amount}", realAmount);
                                                 }),
                                          st::walletSendingText);
  }

  inner->widthValue()  //
      | rpl::start_with_next(
            [=](int width) {
              const auto left = +st::walletSentLottieLeft;
              lottie->setGeometry({(width - st::walletSentLottieSize) / 2 + left, st::walletSentLottieTop,
                                   st::walletSentLottieSize, st::walletSentLottieSize});
              title->moveToLeft((width - title->width()) / 2, st::walletSendingTitleTop, width);

              if (amountLabel != nullptr) {
                amountLabel->moveToLeft((width - amountLabel->width()) / 2, st::walletSendingTextTop, width);
              }

              if (text != nullptr) {
                text->moveToLeft((width - text->width()) / 2,
                                 st::walletSendingTextTop + ((amountLabel == nullptr) ? 0 : text->height()), width);
              }
            },
            inner->lifetime());

  auto isSwapBack = false;
  if constexpr (isTokenTransfer) {
    isSwapBack = invoice.swapBack;
  }

  box->addButton(isSwapBack ? ph::lng_wallet_sent_close_view() : ph::lng_wallet_sent_close(), [=] {
    box->closeBox();
    onClose();
  });
}

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const TonTransferInvoice &invoice, const Fn<void()> &onClose);

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const TokenTransferInvoice &invoice, const Fn<void()> &onClose);

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const StakeInvoice &invoice, const Fn<void()> &onClose);

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const WithdrawalInvoice &invoice, const Fn<void()> &onClose);

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const CancelWithdrawalInvoice &invoice, const Fn<void()> &onClose);

template void SendingDoneBox(not_null<Ui::GenericBox *> box, const Ton::Transaction &result,
                             const DeployTokenWalletInvoice &invoice, const Fn<void()> &onClose);

}  // namespace Wallet
