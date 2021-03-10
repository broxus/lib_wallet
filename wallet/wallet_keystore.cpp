#include "wallet_keystore.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/input_fields.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/lottie_widget.h"
#include "ui/ton_word_input.h"
#include "styles/style_layers.h"
#include "styles/style_wallet.h"
#include "wallet/wallet_common.h"
#include "wallet/wallet_phrases.h"
#include "wallet/create/wallet_create_view.h"
#include "base/platform/base_platform_layout_switch.h"
#include "ton/ton_wallet.h"

namespace Wallet {

namespace {

using TonWordInput = Ui::TonWordInput;

const base::flat_set<QString> &ValidWords() {
  static auto words = Ton::Wallet::GetValidWords();
  return words;
}

style::TextStyle ComputePubKeyStyle(const style::TextStyle &parent) {
  auto result = parent;
  result.font = result.font->monospace();
  result.linkFont = result.linkFont->monospace();
  result.linkFontOver = result.linkFontOver->monospace();
  return result;
}

QString PublicIntoRaw(const QByteArray &publicKey) {
  auto decoded = QByteArray::fromBase64(publicKey, QByteArray::Base64Option::Base64UrlEncoding);
  return decoded.mid(2, 32).toHex();
}

not_null<Ui::RpWidget *> CreatePubKeyLabel(not_null<Ui::RpWidget *> parent, rpl::producer<QString> &&text,
                                           const style::FlatLabel &st, const Fn<void()> &onClick) {
  const auto mono = parent->lifetime().make_state<style::FlatLabel>(st);
  mono->style = ComputePubKeyStyle(mono->style);
  mono->minWidth = 50;

  const auto result = CreateChild<Ui::RpWidget>(parent.get());
  const auto label = CreateChild<Ui::FlatLabel>(result, rpl::duplicate(text), *mono);
  label->setBreakEverywhere(true);

  label->setAttribute(Qt::WA_TransparentForMouseEvents);
  result->setCursor(style::cur_pointer);
  result->events()  //
      | rpl::start_with_next(
            [=](not_null<QEvent *> event) {
              if (event->type() == QEvent::MouseButtonRelease) {
                onClick();
              }
            },
            result->lifetime());

  std::forward<std::decay_t<decltype(text)>>(text)  //
      | rpl::start_with_next(
            [mono, label, result](QString &&text) {
              const auto half = text.size() / 2;
              const auto first = text.mid(0, half);
              const auto second = text.mid(half);
              const auto width = std::max(mono->style.font->width(first), mono->style.font->width(second)) +
                                 mono->style.font->spacew / 2;
              label->resizeToWidth(width);
              result->resize(label->size());
            },
            parent->lifetime());

  result->widthValue()  //
      | rpl::start_with_next(
            [=](int width) {
              if (st.align & Qt::AlignHCenter) {
                label->moveToLeft((width - label->widthNoMargins()) / 2, label->getMargins().top(), width);
              } else {
                label->moveToLeft(0, label->getMargins().top(), width);
              }
            },
            result->lifetime());

  return result;
}

std::vector<QString> WordsByPrefix(const QString &word) {
  const auto &validWords = ValidWords();

  const auto adjusted = word.trimmed().toLower();
  if (adjusted.isEmpty()) {
    return {};
  } else if (validWords.empty()) {
    return {word};
  }
  auto prefix = QString();
  auto count = 0;
  auto maxCount = 0;
  for (const auto &validWord : validWords) {
    if (validWord.midRef(0, 3) != prefix) {
      prefix = validWord.mid(0, 3);
      count = 1;
    } else {
      ++count;
    }
    if (maxCount < count) {
      maxCount = count;
    }
  }
  auto result = std::vector<QString>();
  const auto from = ranges::lower_bound(validWords, adjusted);
  const auto end = validWords.end();
  for (auto i = from; i != end && i->startsWith(adjusted); ++i) {
    result.push_back(*i);
  }
  return result;
}

}  // namespace

class KeystoreItem {
 public:
  KeystoreItem(not_null<Ui::VerticalLayout *> widget, Ton::KeyType keyType, QByteArray publicKey, QString name,
               const Fn<void(QString)> &share, const OnKeystoreAction &handler)
      : _widget(widget)
      , _keyType(keyType)
      , _publicKey(std::move(publicKey))
      , _name(std::move(name))
      , _share(share)
      , _handler(handler) {
    setupContent();
  }

  [[nodiscard]] Ui::VerticalLayout *widget() const {
    return _widget.get();
  }

  [[nodiscard]] int desiredHeight() const {
    return _desiredHeight;
  }

 private:
  void setupContent() {
    _desiredHeight = 0;

    const auto rawPublicKey = PublicIntoRaw(_publicKey);

    const auto menu = Ui::CreateChild<Ui::IconButton>(widget(), st::walletTopMenuButton);
    menu->setClickedCallback([=]() mutable { showMenu(menu); });

    const auto title = AddBoxSubtitle(widget(), rpl::single(_name));
    title->setSelectable(true);

    const auto &titlePadding = st::walletSubsectionTitlePadding;
    _desiredHeight += titlePadding.top() + title->height() + titlePadding.bottom();

    const auto &labelPadding = st::boxRowPadding;
    const auto label = _widget->add(  //
        object_ptr<Ui::RpWidget>::fromRaw(CreatePubKeyLabel(
            _widget, rpl::single(rawPublicKey), st::walletTransactionAddress, [=] { _share(rawPublicKey); })),
        labelPadding);
    _desiredHeight += labelPadding.top() + label->height() + labelPadding.bottom();

    _widget->widthValue() |
        rpl::start_with_next([=](int width) { menu->moveToRight(0, (_widget->height() - menu->height()) / 2, width); },
                             _widget->lifetime());
  }

  void showMenu(not_null<Ui::IconButton *> toggle) {
    if (_menu) {
      return;
    }
    _menu.emplace(_widget);

    const auto menu = _menu.get();
    toggle->installEventFilter(menu);

    menu->addAction(ph::lng_wallet_keystore_export(ph::now),
                    [=] { _handler(_keyType, _publicKey, KeystoreAction::Export); });

    menu->addAction(ph::lng_wallet_keystore_change_password(ph::now),
                    [=] { _handler(_keyType, _publicKey, KeystoreAction::ChangePassword); });

    if (_keyType != Ton::KeyType::Original) {
      menu->addAction(ph::lng_wallet_keystore_delete(ph::now),
                      [=] { _handler(_keyType, _publicKey, KeystoreAction::Delete); });
    }

    const QPoint pos{_widget->width() - st::walletKeystoreMenuPosition.x() - menu->getMargins().right() - menu->width(),
                     st::walletKeystoreMenuPosition.y() - menu->getMargins().top() + toggle->geometry().bottom()};

    menu->popup(_widget->mapToGlobal(pos));
  }

  not_null<Ui::VerticalLayout *> _widget;
  Ton::KeyType _keyType;
  QByteArray _publicKey;
  QString _name;

  Fn<void(QString)> _share;
  OnKeystoreAction _handler;

  int _desiredHeight{};

  base::unique_qptr<Ui::PopupMenu> _menu;
};

void KeystoreBox(not_null<Ui::GenericBox *> box, const QByteArray &mainPublicKey,
                 const std::vector<Ton::FtabiKey> &ftabiKeys, const Fn<void(QString)> &share,
                 const OnKeystoreAction &onAction, const Fn<void()> &createFtabiKey) {
  box->setWidth(st::boxWideWidth);
  box->setStyle(st::walletBox);
  box->setNoContentMargin(true);
  box->setTitle(ph::lng_wallet_keystore_title());

  box->addTopButton(st::boxTitleClose, [=] { box->closeBox(); });

  auto widget = box->lifetime().make_state<Ui::RpWidget>();
  auto scroll = Ui::CreateChild<Ui::ScrollArea>(widget, st::walletScrollArea);
  auto inner = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll)).data();

  int desiredHeight = 0;

  std::vector<Ui::RpWidget *> dividers;
  const auto addDivider = [&] {
    const auto &margin = st::walletSettingsDividerMargin;
    auto *divider = inner->add(object_ptr<Ui::BoxContentDivider>(widget), margin);
    desiredHeight += margin.top() + divider->height() + margin.bottom();
    dividers.emplace_back(divider);
  };

  std::vector<Ui::RpWidget *> items;
  auto addItem = [&](Ton::KeyType keyType, const QByteArray &pubkey, const QString &name) {
    auto item = inner->add(object_ptr<Ui::VerticalLayout>(box), QMargins{});
    items.emplace_back(item);

    auto *content = box->lifetime().make_state<KeystoreItem>(item, keyType, pubkey, name, share, onAction);
    desiredHeight += content->desiredHeight();
  };

  addDivider();
  addItem(Ton::KeyType::Original, mainPublicKey, ph::lng_wallet_keystore_main_wallet_key(ph::now));

  for (const auto &key : ftabiKeys) {
    addDivider();
    addItem(Ton::KeyType::Ftabi, key.publicKey, key.name);
  }
  addDivider();

  widget->sizeValue()  //
      | rpl::start_with_next(
            [=](QSize size) {
              for (auto divider : dividers) {
                divider->setFixedWidth(size.width());
              }
              for (auto item : items) {
                item->setFixedWidth(size.width());
              }
              scroll->setGeometry({QPoint{}, size});
              inner->setGeometry(0, 0, size.width(), std::max(desiredHeight, size.height()));
            },
            box->lifetime());

  widget->resize(st::boxWideWidth, desiredHeight);

  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(widget), QMargins());

  box->addButton(ph::lng_wallet_keystore_create(), createFtabiKey, st::walletWideBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void NewFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(NewFtabiKey)> &done) {
  box->setTitle(ph::lng_wallet_new_ftabi_key_title());
  box->setStyle(st::walletBox);

  box->addTopButton(st::boxTitleClose, [=] {
    box->closeBox();
    cancel();
  });

  AddBoxSubtitle(box, ph::lng_wallet_new_ftabi_key_name());
  const auto name = box->addRow(object_ptr<Ui::InputField>(box, st::walletSendInput, Ui::InputField::Mode::NoNewlines,
                                                           ph::lng_wallet_new_ftabi_key_enter_key_name()));
  name->setMaxLength(32);

  const auto generate = box->lifetime().make_state<rpl::variable<bool>>(false);
  const auto creationMethodSelector = std::make_shared<Ui::RadiobuttonGroup>(generate->current());
  const auto radioButtonItemHeight =
      st::defaultCheckbox.margin.top() + st::defaultRadio.diameter + st::defaultCheckbox.margin.bottom();

  const auto checkboxGenerate = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonItemHeight),
      QMargins(st::walletSendAmountPadding.left(), st::walletSendAmountPadding.bottom(), 0, 0));
  Ui::CreateChild<Ui::Radiobutton>(checkboxGenerate, creationMethodSelector, /*generate*/ true,
                                   ph::lng_wallet_new_ftabi_key_generate_new(ph::now));

  const auto checkboxImport = box->addRow(  //
      object_ptr<Ui::FixedHeightWidget>(box, radioButtonItemHeight),
      QMargins(st::walletSendAmountPadding.left(), 0, 0, 0));
  Ui::CreateChild<Ui::Radiobutton>(checkboxImport, creationMethodSelector, /*generate*/ false,
                                   ph::lng_wallet_new_ftabi_key_import_existing(ph::now));

  creationMethodSelector->setChangedCallback([=](bool value) { *generate = value; });

  const auto submit = [=] {
    const auto nameValue = name->getLastText();
    if (nameValue.isEmpty()) {
      return name->showError();
    }

    done(NewFtabiKey{.name = nameValue, .generate = generate->current()});
  };

  auto buttonText =      //
      generate->value()  //
      | rpl::map([=](bool generate) {
          if (generate) {
            return ph::lng_wallet_new_ftabi_key_generate();
          } else {
            return ph::lng_wallet_new_ftabi_key_import();
          }
        })  //
      | rpl::flatten_latest();
  box->addButton(buttonText, submit, st::walletBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void ImportFtabiKeyBox(not_null<Ui::GenericBox *> box, const Fn<void()> &cancel, const Fn<void(WordsList)> &done) {
  box->setTitle(ph::lng_wallet_import_ftabi_key_title());
  box->setStyle(st::walletNoButtonsBox);

  box->addTopButton(st::boxTitleClose, [=] {
    box->closeBox();
    cancel();
  });

  auto widget = box->lifetime().make_state<Ui::RpWidget>();
  auto scroll = Ui::CreateChild<Ui::ScrollArea>(widget, st::walletScrollArea);
  auto inner = scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(scroll)).data();

  constexpr auto rows = 6;
  constexpr auto count = rows * 2;

  auto inputs = std::make_shared<std::vector<std::unique_ptr<TonWordInput>>>();
  const auto rowsTop = st::walletWordHeight;
  const auto rowsBottom = rowsTop + rows * st::walletWordHeight;

  const auto currentWords = [=] {
    return (*inputs)                                                                                     //
           | ranges::views::transform([](const std::unique_ptr<TonWordInput> &p) { return p->word(); })  //
           | ranges::to_vector;
  };

  const auto isValid = [=](int index) {
    Expects(index < count);

    const auto word = (*inputs)[index]->word();
    const auto words = WordsByPrefix(word);
    return !words.empty() && (words.front() == word);
  };

  const auto showError = [=](int index) {
    Expects(index < count);

    if (isValid(index)) {
      return false;
    }
    (*inputs)[index]->showError();
    return true;
  };

  const auto checkAll = [=] {
    auto result = true;
    for (auto i = count; i != 0;) {
      result = !showError(--i) && result;
    }
    return result;
  };

  const auto init = [&](const TonWordInput &word, int index) {
    const auto next = [=] { return (index + 1 < count) ? (*inputs)[index + 1].get() : nullptr; };
    const auto previous = [=] { return (index > 0) ? (*inputs)[index - 1].get() : nullptr; };

    word.pasted()  //
        | rpl::start_with_next(
              [=](QString text) {
                text = text.simplified();
                int cnt = 0;
                for (const auto &w : text.split(' ')) {
                  if (index + cnt < count) {
                    (*inputs)[index + cnt]->setText(w);
                    (*inputs)[index + cnt]->setFocus();
                    cnt++;
                  } else {
                    break;
                  }
                }
              },
              box->lifetime());

    word.blurred()                                                                                       //
        | rpl::filter([=] { return !(*inputs)[index]->word().trimmed().isEmpty() && !isValid(index); })  //
        | rpl::start_with_next([=] { (*inputs)[index]->showErrorNoFocus(); }, box->lifetime());

    word.tabbed()  //
        | rpl::start_with_next(
              [=](TonWordInput::TabDirection direction) {
                if (direction == TonWordInput::TabDirection::Forward) {
                  if (const auto word = next()) {
                    word->setFocus();
                  }
                } else {
                  if (const auto word = previous()) {
                    word->setFocus();
                  }
                }
              },
              box->lifetime());

    word.submitted()  //
        | rpl::start_with_next(
              [=] {
                if (const auto word = next()) {
                  word->setFocus();
                } else if (checkAll()) {
                  done(currentWords());
                }
              },
              box->lifetime());
  };

  for (auto i = 0; i != count; ++i) {
    inputs->push_back(std::make_unique<TonWordInput>(widget, st::walletImportInputField, i, WordsByPrefix));
    init(*inputs->back(), i);
  }

  inputs->front()->setFocus();

  widget->sizeValue()  //
      | rpl::start_with_next(
            [=](QSize size) {
              const auto half = size.width() / 2;
              const auto left = half - st::walletImportSkipLeft;
              const auto right = half + st::walletImportSkipRight;
              auto x = left;
              auto y = rowsTop;
              auto index = 0;
              for (const auto &input : *inputs) {
                input->move(x, y);
                y += st::walletWordHeight;
                if (++index == rows) {
                  x = right;
                  y = rowsTop;
                }
              }
            },
            box->lifetime());

  widget->resize(st::boxWideWidth, rowsBottom);

  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(widget), QMargins());
}

void GeneratedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words, const Fn<void()> &done) {
  box->setWidth(st::boxWideWidth);
  box->setStyle(st::walletBox);
  box->setNoContentMargin(true);

  const auto view = box->lifetime().make_state<Create::View>(words, Create::View::Layout::Export);
  view->widget()->resize(st::boxWideWidth, view->desiredHeight());
  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(view->widget()), QMargins());
  view->showFast();

  box->addButton(ph::lng_wallet_next(), done, st::walletWideBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void ExportedFtabiKeyBox(not_null<Ui::GenericBox *> box, const WordsList &words) {
  box->setWidth(st::boxWideWidth);
  box->setStyle(st::walletBox);
  box->setNoContentMargin(true);

  const auto view = box->lifetime().make_state<Create::View>(words, Create::View::Layout::Export);
  view->widget()->resize(st::boxWideWidth, view->desiredHeight());
  box->addRow(object_ptr<Ui::RpWidget>::fromRaw(view->widget()), QMargins());
  view->showFast();

  box->addButton(
         ph::lng_wallet_done(), [=] { box->closeBox(); }, st::walletWideBottomButton)
      ->setTextTransform(Ui::RoundButton::TextTransform::NoTransform);
}

void NewFtabiKeyPasswordBox(not_null<Ui::GenericBox *> box,
                            const Fn<void(const QByteArray &, const Fn<void(QString)> &)> &done) {
  box->setTitle(ph::lng_wallet_set_passcode_title());

  const auto inner = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::walletFtabiKeyPasscodeHeight));

  const auto lottie = inner->lifetime().make_state<Ui::LottieAnimation>(inner, Ui::LottieFromResource("lock"));
  lottie->start();
  lottie->stopOnLoop(1);

  const auto error = Ui::CreateChild<Ui::FadeWrap<Ui::FlatLabel>>(
      inner, object_ptr<Ui::FlatLabel>(inner, QString(), st::walletPasscodeError));

  const auto enter =
      Ui::CreateChild<Ui::PasswordInput>(inner, st::walletPasscodeInput, ph::lng_wallet_set_passcode_enter());
  const auto repeat =
      Ui::CreateChild<Ui::PasswordInput>(inner, st::walletPasscodeInput, ph::lng_wallet_set_passcode_repeat());

  inner->widthValue() |
      rpl::start_with_next(
          [=](int width) {
            lottie->setGeometry(QRect((width - st::walletPasscodeLottieSize) / 2, st::walletPasscodeLottieTop,
                                      st::walletPasscodeLottieSize, st::walletPasscodeLottieSize));

            error->resizeToWidth(width);
            error->moveToLeft(0, st::walletFtabiKeyPasscodeErrorTop, width);

            enter->move((width - enter->width()) / 2, st::walletFtabiKeyPasscodeNowTop);
            repeat->move((width - repeat->width()) / 2, st::walletFtabiKeyPasscodeRepeatTop);
          },
          inner->lifetime());

  error->hide(anim::type::instant);

  const auto save = [=] {
    auto password = enter->getLastText().toUtf8();
    if (password.isEmpty()) {
      enter->showError();
      return;
    } else if (repeat->getLastText().toUtf8() != password) {
      repeat->showError();
      return;
    }

    done(password, crl::guard(box, [=](const QString &text) {
           error->entity()->setText(text);
           error->show(anim::type::normal);
         }));
  };

  Ui::Connect(enter, &Ui::PasswordInput::submitted, [=] {
    if (enter->getLastText().isEmpty()) {
      enter->showError();
    } else {
      repeat->setFocus();
    }
  });
  Ui::Connect(repeat, &Ui::PasswordInput::submitted, save);

  box->setFocusCallback([=] {
    base::Platform::SwitchKeyboardLayoutToEnglish();
    enter->setFocusFast();
  });

  box->addButton(ph::lng_wallet_save(), save);
  box->addButton(ph::lng_wallet_cancel(), [=] { box->closeBox(); });
}

}  // namespace Wallet
