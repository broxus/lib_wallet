#include "wallet_collect_tokens.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/address_label.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"
#include "ton/ton_wallet.h"

namespace Wallet {

void CollectTokensBox(not_null<Ui::GenericBox *> box, const CollectTokensInvoice &invoice,
                      rpl::producer<Ton::Result<Ton::EthEventDetails>> loadedEventDetails,
                      rpl::producer<Ton::Symbol> loadedSymbol, const Fn<void(QImage, QString)> &share,
                      const Fn<void(CollectTokensInvoice)> &done) {
  box->setTitle(ph::lng_wallet_collect_tokens_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  AddBoxSubtitle(box, ph::lng_wallet_collect_tokens_details());
  box->addRow(  //
      object_ptr<Ui::RpWidget>::fromRaw(
          Ui::CreateAddressLabel(box, rpl::single(Ton::Wallet::ConvertIntoRaw(invoice.eventContractAddress)),
                                 st::walletTransactionAddress, [=] { share(QImage(), invoice.eventContractAddress); })),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  auto replaceUnknown = [](const ph::phrase &phrase) { return phrase(ph::now).replace("{value}", "unknown"); };
  auto replaceRatio = [](const ph::phrase &phrase, int current, int required) {
    return phrase(ph::now).replace("{value}",
                                   QString{"%1 / %2"}.arg(QString::number(current), QString::number(required)));
  };

  auto status =
      box->lifetime().make_state<rpl::variable<QString>>(replaceUnknown(ph::lng_wallet_collect_tokens_status));
  auto confirmations =
      box->lifetime().make_state<rpl::variable<QString>>(replaceUnknown(ph::lng_wallet_collect_tokens_confirmations));
  auto rejections =
      box->lifetime().make_state<rpl::variable<QString>>(replaceUnknown(ph::lng_wallet_collect_tokens_rejections));

  std::move(loadedEventDetails) |
      rpl::start_with_next(
          [=](Ton::Result<Ton::EthEventDetails> details) {
            if (details.has_value()) {
              *status =
                  ph::lng_wallet_collect_tokens_status(ph::now).replace("{value}", [](Ton::EthEventStatus status) {
                    switch (status) {
                      case Ton::EthEventStatus::InProcess:
                        return ph::lng_wallet_collect_tokens_status_in_process;
                      case Ton::EthEventStatus::Confirmed:
                        return ph::lng_wallet_collect_tokens_status_confirmed;
                      case Ton::EthEventStatus::Executed:
                        return ph::lng_wallet_collect_tokens_status_executed;
                      case Ton::EthEventStatus::Rejected:
                        return ph::lng_wallet_collect_tokens_status_rejected;
                      default:
                        Unexpected("eth event status");
                    }
                  }(details->status)(ph::now));
              *confirmations = replaceRatio(ph::lng_wallet_collect_tokens_confirmations, details->confirmationCount,
                                            details->requiredConfirmationCount);
              *rejections = replaceRatio(ph::lng_wallet_collect_tokens_rejections, details->rejectionCount,
                                         details->requiredRejectionCount);
            }
          },
          box->lifetime());

  box->addRow(  //
      object_ptr<Ui::FlatLabel>(box, status->value(), st::walletCollectTokensEventDetails),
      st::walletCollectTokensDescriptionPadding);

  box->addRow(  //
      object_ptr<Ui::FlatLabel>(box, confirmations->value(), st::walletCollectTokensEventDetails),
      st::walletCollectTokensDescriptionPadding);

  box->addRow(  //
      object_ptr<Ui::FlatLabel>(box, rejections->value(), st::walletCollectTokensEventDetails),
      st::walletCollectTokensDescriptionPadding);

  box->addButton(
         ph::lng_wallet_collect_tokens_button(), [=] { done(invoice); }, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
