#include "wallet_deploy_token_wallet.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

void DeployTokenWalletBox(not_null<Ui::GenericBox *> box, const DeployTokenWalletInvoice &invoice,
                          const Fn<void(DeployTokenWalletInvoice)> &done) {
  box->setTitle(ph::lng_wallet_deploy_token_wallet_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  box->addRow(                    //
      object_ptr<Ui::FlatLabel>(  //
          box,
          invoice.owned  //
              ? ph::lng_wallet_deploy_token_wallet_owned_description()
              : ph::lng_wallet_deploy_token_wallet_target_description(),
          st::walletSendAbout),
      st::walletDeployTokenWalletDescriptionPadding);

  box->addButton(
         ph::lng_wallet_deploy_token_wallet_button(), [=] { done(invoice); }, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
