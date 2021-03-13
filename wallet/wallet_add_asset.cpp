#include "wallet_add_asset.h"

#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/text/text_utilities.h"
#include "ui/address_label.h"

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

FixedAmount FixCountInput(const QString &text, int position) {
  auto result = FixedAmount{text, position};
  while (!result.text.isEmpty() && result.text.startsWith('0')) {
    result.text.remove(0, 1);
    if (result.position > 0) {
      --result.position;
    }
  }
  if (result.text.isEmpty()) {
    return result;
  }
  for (auto i = 0; i != result.text.size();) {
    const auto ch = result.text[i];
    if (ch >= '0' && ch <= '9') {
      ++i;
      continue;
    }
    result.text.remove(i, 1);
    if (result.position > i) {
      --result.position;
    }
  }
  return result;
}

not_null<Ui::InputField *> CreateCountInput(not_null<QWidget *> parent, int64 amount,
                                            rpl::producer<QString> placeholder) {
  const auto result =
      Ui::CreateChild<Ui::InputField>(parent.get(), st::walletInput, Ui::InputField::Mode::SingleLine, placeholder);

  result->setText(amount > 0 ? QString::number(amount) : QString());

  Ui::Connect(result, &Ui::InputField::changed, [=] {
    Ui::PostponeCall(result, [=] {
      const auto position = result->textCursor().position();
      const auto now = result->getLastText();
      const auto fixed = FixCountInput(now, position);
      if (fixed.text == now) {
        return;
      }
      result->setText(fixed.text);
      result->setFocusFast();
      result->setCursorPosition(fixed.position);
    });
  });
  return result;
}

QString DefaultCustodian(const Ton::MultisigInitialInfo &info) {
  return Ton::Wallet::UnpackPublicKey(info.publicKey).toHex();
}

not_null<Ui::InputField *> CreateCustodiansInput(not_null<QWidget *> parent, const QString &value) {
  const auto result =
      Ui::CreateChild<Ui::InputField>(parent.get(), st::walletCustodianListInput, Ui::InputField::Mode::MultiLine,
                                      ph::lng_wallet_add_multisig_enter_custodians_list(), value);
  result->setSubmitSettings(Ui::InputSubmitSettings::None);
  result->setMaxLength(kMaxCustodiansLength);
  return result;
}

const QRegExp &CustodiansListRegex() {
  static const QRegExp regex{R"((\ |\,|\;|\.|\n|\t))"};
  return regex;
}

std::optional<base::flat_set<QByteArray>> ParseCustodiansList(const QString &value) {
  constexpr auto pubkeyLength = 64;
  base::flat_set<QByteArray> result;
  const auto list = value.split(CustodiansListRegex(), QString::SplitBehavior::SkipEmptyParts);
  if (list.empty()) {
    return std::nullopt;
  }
  for (const auto &item : list) {
    if (item.size() != pubkeyLength) {
      return std::nullopt;
    }
    auto decoded = QByteArray::fromHex(item.toUtf8());
    if (decoded.size() != pubkeyLength / 2) {
      return std::nullopt;
    }
    result.emplace(std::move(decoded));
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

  addressWrapper->setMaximumHeight(assetType->current() != NewAssetType::NewMultisig ? QWIDGETSIZE_MAX : 0);

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

void SelectMultisigKeyBox(not_null<Ui::GenericBox *> box, const std::vector<QByteArray> &custodians,
                          const std::vector<Ton::AvailableKey> &availableKeys, int defaultIndex, bool allowNewKeys,
                          const Fn<void()> &addNewKey, const Fn<void(QByteArray)> &done) {
  Assert(!availableKeys.empty());

  box->setTitle(ph::lng_wallet_add_multisig_title_select_key());
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

  if (availableKeys.size() < custodians.size() || allowNewKeys) {
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

void SelectMultisigVersionBox(not_null<Ui::GenericBox *> box, const Fn<void(Ton::MultisigVersion)> &done) {
  box->setTitle(ph::lng_wallet_add_multisig_title_deploy());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto selectedVersion =
      box->lifetime().make_state<rpl::variable<Ton::MultisigVersion>>(Ton::MultisigVersion::SafeMultisig);
  const auto versionSelector = std::make_shared<Ui::RadiobuttonGroup>(0);
  const auto radioButtonMargin = QMargins(st::walletSendAmountPadding.left(), 0, 0, 0);
  const auto radioButtonHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  AddBoxSubtitle(box, ph::lng_wallet_add_multisig_select_version());

  auto addSelectorItem = [&](Ton::MultisigVersion type) {
    const auto item = box->addRow(  //
        object_ptr<Ui::FixedHeightWidget>(box, radioButtonHeight), radioButtonMargin);
    Ui::CreateChild<Ui::Radiobutton>(item, versionSelector, static_cast<int>(type),
                                     ph::lng_wallet_multisig_version(type)(ph::now));
  };

  addSelectorItem(Ton::MultisigVersion::SafeMultisig);
  addSelectorItem(Ton::MultisigVersion::SafeMultisig24h);
  addSelectorItem(Ton::MultisigVersion::SetcodeMultisig);
  addSelectorItem(Ton::MultisigVersion::Surf);

  versionSelector->setChangedCallback(
      [=](int version) { *selectedVersion = static_cast<Ton::MultisigVersion>(version); });

  const auto submit = [=] { done(selectedVersion->current()); };

  box->addButton(ph::lng_wallet_next(), submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void PredeployMultisigBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInitialInfo &info,
                          const Fn<void(QImage, QString)> &share, const Fn<void()> &done) {
  box->setTitle(ph::lng_wallet_predeploy_multisig_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  AddBoxSubtitle(box, ph::lng_wallet_predeploy_multisig_address());

  const auto address = Ton::Wallet::ConvertIntoRaw(info.address);
  box->addRow(  //
      object_ptr<Ui::RpWidget>::fromRaw(Ui::CreateAddressLabel(
          box, rpl::single(address), st::walletConfirmationAddressLabel, [=] { share(QImage(), address); },
          st::windowBgOver->c)),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  box->addRow(                   //
      object_ptr<Ui::FlatLabel>  //
      (box,
       ph::lng_wallet_predeploy_multisig_description()  //
           | rpl::map([](QString &&description) {
               return Ui::Text::RichLangValue(description.replace(
                   "{value}", FormatAmount(Ton::kMinimalDeploymentBalance, Ton::Symbol::ton()).full));
             }),
       st::walletSendAbout),
      st::walletPredeployMultisigDescriptionPadding);

  box->addButton(ph::lng_wallet_next(), done, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void DeployMultisigBox(not_null<Ui::GenericBox *> box, const Ton::MultisigInitialInfo &info,
                       const Fn<void(MultisigDeployInvoice)> &done) {
  box->setWidth(st::boxWideWidth);
  box->setTitle(ph::lng_wallet_add_multisig_title_deploy());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  const auto defaultCustodian = DefaultCustodian(info);

  auto custodianCount = box->lifetime().make_state<rpl::variable<std::optional<int>>>(1);
  auto custodians = box->lifetime().make_state<base::flat_set<QByteArray>>(
      base::flat_set<QByteArray>{QByteArray::fromHex(defaultCustodian.toUtf8())});

  ////

  const auto countSubtitle = AddBoxSubtitle(box, ph::lng_wallet_add_multisig_required_confirmations());
  const auto count = box->addRow(  //
      object_ptr<Ui::InputField>::fromRaw(CreateCountInput(box, 1, ph::lng_wallet_add_multisig_confirmation_count())),
      {
          st::boxRowPadding.left(),
          st::boxRowPadding.top(),
          st::boxRowPadding.right(),
          st::walletTransactionDateTop,
      });

  auto maxRequiredCountText =                                                                 //
      rpl::combine(ph::lng_wallet_add_multisig_max_confirmations(), custodianCount->value())  //
      | rpl::map([=](QString &&phrase, std::optional<int> value) {
          return phrase.replace("{value}", value.has_value() ? QString::number(*value) : "?");
        });
  const auto maxRequiredCountLabel = Ui::CreateChild<Ui::FlatLabel>(
      countSubtitle->parentWidget(), std::move(maxRequiredCountText), st::walletSendBalanceLabel);
  rpl::combine(countSubtitle->geometryValue(), maxRequiredCountLabel->widthValue()) |
      rpl::start_with_next(
          [=](QRect rect, int innerWidth) {
            const auto labelTop = rect.top() + st::walletSubsectionTitle.style.font->ascent -
                                  st::walletSendBalanceLabel.style.font->ascent;
            maxRequiredCountLabel->moveToRight(st::boxRowPadding.right(), labelTop);
          },
          maxRequiredCountLabel->lifetime());

  ////

  const auto custodiansSubtitle = AddBoxSubtitle(box, ph::lng_wallet_add_multisig_custodians());
  const auto custodiansList = box->addRow(
      object_ptr<Ui::InputField>::fromRaw(CreateCustodiansInput(box, defaultCustodian)), st::walletSendCommentPadding);

  auto maxCustodianCountText =  //
      ph::lng_wallet_add_multisig_max_custodians() | rpl::map([=](QString &&phrase) {
        return phrase.replace("{value}", QString::number(Ton::kMaxMultisigCustodianCount));
      });
  const auto maxCustodianCountLabel = Ui::CreateChild<Ui::FlatLabel>(
      custodiansSubtitle->parentWidget(), std::move(maxCustodianCountText), st::walletSendBalanceLabel);
  rpl::combine(custodiansSubtitle->geometryValue(), maxCustodianCountLabel->widthValue()) |
      rpl::start_with_next(
          [=](QRect rect, int innerWidth) {
            const auto labelTop = rect.top() + st::walletSubsectionTitle.style.font->ascent -
                                  st::walletSendBalanceLabel.style.font->ascent;
            maxCustodianCountLabel->moveToRight(st::boxRowPadding.right(), labelTop);
          },
          maxCustodianCountLabel->lifetime());

  box->addRow(object_ptr<Ui::FlatLabel>(box, ph::lng_wallet_add_multisig_custodians_list_tip(), st::walletSendAbout));

  ////

  auto checkCount = [=](const std::optional<int> &max, const QString &value) -> std::optional<int> {
    if (!max.has_value() || value.isEmpty()) {
      count->showErrorNoFocus();
      return std::nullopt;
    }
    auto ok = true;
    int numericValue = value.toInt(&ok);
    if (!numericValue || value < 1) {
      count->showError();
      return std::nullopt;
    }
    auto tooMuch = numericValue > *max;
    maxRequiredCountLabel->setTextColorOverride(tooMuch ? std::make_optional(st::boxTextFgError->c) : std::nullopt);
    count->setErrorShown(tooMuch);
    return tooMuch ? std::nullopt : std::make_optional(numericValue);
  };

  custodianCount->value() |
      rpl::start_with_next([=](std::optional<int> custodianCount) { checkCount(custodianCount, count->getLastText()); },
                           count->lifetime());

  Ui::Connect(count, &Ui::InputField::changed,
              [=] { Ui::PostponeCall(count, [=] { checkCount(custodianCount->current(), count->getLastText()); }); });

  auto resetCustodians = [=] {
    custodiansList->showError();
    custodians->clear();
    *custodianCount = std::nullopt;
  };

  auto checkList = [=]() -> std::optional<base::flat_set<QByteArray> *> {
    auto list = ParseCustodiansList(custodiansList->getLastText());
    if (list.has_value()) {
      const auto count = list->size();
      const auto tooMuch = count > Ton::kMaxMultisigCustodianCount;
      maxCustodianCountLabel->setTextColorOverride(tooMuch ? std::make_optional(st::boxTextFgError->c) : std::nullopt);
      if (!tooMuch) {
        *custodians = std::move(*list);
        *custodianCount = count;
        return custodians;
      }
    }
    resetCustodians();
    return std::nullopt;
  };

  Ui::Connect(custodiansList, &Ui::InputField::changed,
              [=] { Ui::PostponeCall(custodiansList, [=] { checkList(); }); });

  const auto submit = [=] {
    auto collected = MultisigDeployInvoice{
        .initialInfo = info,
    };
    const auto requiredConfirmations = checkCount(custodianCount->current(), count->getLastText());
    if (!requiredConfirmations.has_value()) {
      return;
    }
    collected.requiredConfirmations = static_cast<uint8>(*requiredConfirmations);

    const auto owners = checkList();
    if (!owners.has_value()) {
      return;
    }
    const auto ownerCount = (*owners)->size();
    if (collected.requiredConfirmations > ownerCount) {
      return;
    }

    collected.owners.reserve(ownerCount);
    for (const auto &owner : **owners) {
      collected.owners.emplace_back(owner);
    }

    done(std::move(collected));
  };

  box->addButton(ph::lng_wallet_deploy(), submit, st::walletWideBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

}  // namespace Wallet
