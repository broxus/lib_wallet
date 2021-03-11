#include "wallet_add_asset.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"

#include "styles/style_wallet.h"
#include "styles/style_layers.h"
#include "styles/palette.h"

#include "wallet/wallet_phrases.h"
#include "wallet/wallet_common.h"

#include "ton/ton_wallet.h"

namespace Wallet {

namespace {

enum class NewAssetType {
  Token,
  DePool,
  ExistingMultisig,
  NewMultisig,
};

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

void AddAssetBox(not_null<Ui::GenericBox *> box, const Fn<void(NewAsset)> &done) {
  box->setTitle(ph::lng_wallet_add_asset_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto assetType = box->lifetime().make_state<rpl::variable<NewAssetType>>(NewAssetType::Token);
  const auto assetTypeSelector = std::make_shared<Ui::RadiobuttonGroup>(static_cast<int>(NewAssetType::Token));
  const auto radioButtonMargin = QMargins(st::walletSendAmountPadding.left(), 0, 0, 0);
  const auto radioButtonHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  auto addSelectorItem = [&](NewAssetType type, const ph::phrase &text) {
    const auto item = box->addRow(  //
        object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
    Ui::CreateChild<Ui::Radiobutton>(item, assetTypeSelector, static_cast<int>(type), text(ph::now));
  };

  addSelectorItem(NewAssetType::Token, ph::lng_wallet_add_asset_token);
  addSelectorItem(NewAssetType::DePool, ph::lng_wallet_add_asset_depool);
  addSelectorItem(NewAssetType::ExistingMultisig, ph::lng_wallet_add_asset_existing_multisig);
  addSelectorItem(NewAssetType::NewMultisig, ph::lng_wallet_add_asset_new_multisig);

  const auto addressWrapper = box->addRow(object_ptr<Ui::VerticalLayout>(box), QMargins{});

  AddBoxSubtitle(addressWrapper, ph::lng_wallet_add_asset_address());
  const auto address = addressWrapper->add(  //
      object_ptr<Ui::InputField>(box, st::walletSendInput, Ui::InputField::Mode::NoNewlines,
                                 ph::lng_wallet_add_asset_token_address()),
      st::boxRowPadding);
  address->rawTextEdit()->setWordWrapMode(QTextOption::WrapAnywhere);

  assetTypeSelector->setChangedCallback([=](int value) {
    const auto type = static_cast<NewAssetType>(value);

    const auto withAddress = type != NewAssetType::NewMultisig;
    address->setEnabled(withAddress);
    addressWrapper->setMaximumHeight(withAddress ? QWIDGETSIZE_MAX : 0);
    addressWrapper->adjustSize();

    address->setPlaceholder(  //
        [&]() {
          switch (type) {
            case NewAssetType::Token:
              return ph::lng_wallet_add_asset_token_address();
            case NewAssetType::DePool:
              return ph::lng_wallet_add_asset_depool_address();
            case NewAssetType::ExistingMultisig:
              return ph::lng_wallet_add_asset_multisig_address();
            case NewAssetType::NewMultisig:
              return ph::lng_wallet_add_asset_existing_multisig();
            default:
              Unexpected("Unknown custom asset type");
          }
        }());
    *assetType = type;
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

  const auto submit = [=] {
    NewAsset asset{};

    const auto currentAssetType = assetType->current();
    switch (currentAssetType) {
      case NewAssetType::Token: {
        asset.type = CustomAssetType::Token;
        asset.address = address->getLastText();
        break;
      }
      case NewAssetType::DePool: {
        asset.type = CustomAssetType::DePool;
        asset.address = address->getLastText();
        break;
      }
      case NewAssetType::ExistingMultisig: {
        asset.type = CustomAssetType::Multisig;
        asset.address = address->getLastText();
        break;
      }
      case NewAssetType::NewMultisig: {
        asset.type = CustomAssetType::Multisig;
        break;
      }
    }

    if (currentAssetType != NewAssetType::NewMultisig && !Ton::Wallet::CheckAddress(asset.address)) {
      return showError(AddAssetField::Address);
    }

    return done(asset);
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

  box->addButton(assetType->value()  //
                     | rpl::map([](NewAssetType type) {
                         switch (type) {
                           case NewAssetType::Token:
                           case NewAssetType::DePool:
                             return ph::lng_wallet_add_asset_confirm();
                           case NewAssetType::ExistingMultisig:
                           case NewAssetType::NewMultisig:
                             return ph::lng_wallet_next();
                           default:
                             Unexpected("Asset type");
                         }
                       })  //
                     | rpl::flatten_latest(),
                 submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void SelectMultisigKeyBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInfo &info,
                          const std::vector<Ton::AvailableKey> &availableKeys, int defaultIndex,
                          const Fn<void()> &addNewKey, const Fn<void(QByteArray)> &done) {
  Assert(!availableKeys.empty());

  box->setTitle(ph::lng_wallet_add_multisig_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto selectedPublicKey = box->lifetime().make_state<rpl::variable<int>>(defaultIndex);
  const auto indexSelector = std::make_shared<Ui::RadiobuttonGroup>(0);
  const auto radioButtonMargin = QMargins(st::walletSendAmountPadding.left(), 0, 0, 0);
  const auto radioButtonHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  AddBoxSubtitle(box, ph::lng_wallet_add_multisig_select_key());

  for (int i = 0; i < availableKeys.size(); ++i) {
    const auto item = box->addRow(  //
        object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
    Ui::CreateChild<Ui::Radiobutton>(item, indexSelector, i, availableKeys[i].name);
  }

  if (availableKeys.size() < info.custodians.size()) {
    const auto newItem = box->addRow(  //
        object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
    Ui::CreateChild<Ui::Radiobutton>(newItem, indexSelector, -1, ph::lng_wallet_add_multisig_add_new_key(ph::now));
  }

  indexSelector->setChangedCallback([=](int index) { *selectedPublicKey = index; });

  const auto submit = [=] {
    const auto index = selectedPublicKey->current();
    Assert(index < 0 || index < availableKeys.size());

    if (index < 0) {
      addNewKey();
    } else {
      const auto &selectedKey = availableKeys[index];
      done(selectedKey.publicKey);
    }
  };

  box->addButton(                    //
         selectedPublicKey->value()  //
             | rpl::map([](int index) {
                 if (index < 0) {
                   return ph::lng_wallet_next();
                 } else {
                   return ph::lng_wallet_add_multisig_confirm();
                 }
               })  //
             | rpl::flatten_latest(),
         submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
