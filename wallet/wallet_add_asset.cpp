#include "wallet_add_asset.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"

#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"

namespace Wallet {

namespace {

struct FixedAddress {
  QString address{};
  int position = 0;
};

[[nodiscard]] FixedAddress FixAddressInputExtended(const QString &text, int position) {
  const auto address = v::match(
      ParseInvoice(text), [](const TonTransferInvoice &tonTransferInvoice) { return tonTransferInvoice.address; },
      [](auto &&) { return QString{}; });

  auto result = FixedAddress{address, position};

  if (result.address != text) {
    const auto removed = std::max(int(text.size()) - int(result.address.size()), 0);
    result.position = std::max(position - removed, 0);
  }
  return result;
}

}  // namespace

void AddAssetBox(not_null<Ui::GenericBox *> box, const Fn<void(CustomAsset, Fn<void(AddAssetField)>)> &done) {
  box->setTitle(ph::lng_wallet_add_asset_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto assetType = box->lifetime().make_state<rpl::variable<CustomAssetType>>(CustomAssetType::Token);
  const auto assetTypeSelector = std::make_shared<Ui::RadiobuttonGroup>(static_cast<int>(CustomAssetType::Token));
  const auto radioButtonMargin = QMargins(st::walletSendAmountPadding.left(), 0, 0, 0);
  const auto radioButtonHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  const auto assetTypeToken = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
  Ui::CreateChild<Ui::Radiobutton>(assetTypeToken, assetTypeSelector, CustomAssetType::Token,
                                   ph::lng_wallet_add_asset_token(ph::now));

  const auto assetTypeDePool = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
  Ui::CreateChild<Ui::Radiobutton>(assetTypeDePool, assetTypeSelector, CustomAssetType::DePool,
                                   ph::lng_wallet_add_asset_depool(ph::now));

  AddBoxSubtitle(box, ph::lng_wallet_add_asset_address());
  const auto address = box->addRow(  //
      object_ptr<Ui::InputField>(box, st::walletSendInput, Ui::InputField::Mode::NoNewlines,
                                 ph::lng_wallet_add_asset_token_address()));
  address->rawTextEdit()->setWordWrapMode(QTextOption::WrapAnywhere);

  assetTypeSelector->setChangedCallback([=](int value) {
    address->setPlaceholder(          //
        value == CustomAssetType::Token  //
            ? ph::lng_wallet_add_asset_token_address()
            : ph::lng_wallet_add_asset_depool_address());
    *assetType = static_cast<CustomAssetType>(value);
  });

  Ui::Connect(address, &Ui::InputField::changed, [=] {
    Ui::PostponeCall(address, [=] {
      const auto position = address->textCursor().position();
      const auto now = address->getLastText();
      const auto fixed = FixAddressInputExtended(now, position);
      if (fixed.address != now) {
        address->setText(fixed.address);
        address->setFocusFast();
        address->setCursorPosition(fixed.position);
      }
    });
  });

  box->setFocusCallback([=] { address->setFocusFast(); });

  const auto showError = crl::guard(box, [=](AddAssetField field) {
    switch (field) {
      case AddAssetField::Address:
        return address->showError();
      default:
        Unexpected("Field value in AddAssetBox error callback");
    }
  });

  const auto submit = [=, done = done] {
    return done(CustomAsset{.type = assetType->current(), .address = address->getLastText()}, showError);
  };

  Ui::Connect(address, &Ui::InputField::submitted, [=] {
    const auto text = address->getLastText();
    const auto colonPosition = text.indexOf(':');
    const auto isRaw = colonPosition > 0;

    bool showAddressError = false;
    if (isRaw && ((text.size() - colonPosition - 1) != kRawAddressLength)) {
      showAddressError = true;
    } else if (!isRaw && (text.size() != kEncodedAddressLength)) {
      showAddressError = true;
    }

    if (showAddressError) {
      address->showError();
    } else {
      submit();
    }
  });

  box->addButton(ph::lng_wallet_add_asset_confirm(), submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
