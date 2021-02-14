#include "wallet_collect_tokens.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

namespace Wallet {

void CollectTokensBox(not_null<Ui::GenericBox *> box, const CollectTokensInvoice &invoice,
                      const Fn<void(CollectTokensInvoice)> &done) {
  box->setTitle(ph::lng_wallet_collect_tokens_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  box->addRow(                    //
      object_ptr<Ui::FlatLabel>(  //
          box, ph::lng_wallet_collect_tokens_description(), st::walletSendAbout),
      st::walletCollectTokensDescriptionPadding);

  box->addButton(
         ph::lng_wallet_collect_tokens_button(), [=] { done(invoice); }, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
