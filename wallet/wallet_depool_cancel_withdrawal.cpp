#include "wallet_depool_cancel_withdrawal.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

void DePoolCancelWithdrawalBox(not_null<Ui::GenericBox *> box, const CancelWithdrawalInvoice &invoice,
                               const Fn<void(CancelWithdrawalInvoice)> &done) {
  box->setTitle(ph::lng_wallet_cancel_withdrawal_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_cancel_withdrawal_description(), st::walletSendAbout),
              st::walletCancelWithdrawalDescriptionPadding);

  box->addButton(
         ph::lng_wallet_cancel_withdrawal_button(), [=] { done(invoice); }, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
