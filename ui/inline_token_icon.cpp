#include "inline_token_icon.h"

#include "ui/rp_widget.h"
#include "qr/qr_generate.h"
#include "styles/style_wallet.h"

#include <QtGui/QPainter>

namespace Ui {
namespace {

constexpr auto kShareQrSize = 768;
constexpr auto kShareQrPadding = 16;

const std::vector<std::pair<int, QString>> &TonVariants() {
  static const auto iconTon = std::vector<std::pair<int, QString>>{
      {22, "gem.png"}, {44, "gem@2x.png"}, {88, "gem@4x.png"}, {192, "gem@large.png"}};
  return iconTon;
}

const std::vector<std::pair<int, QString>> &UnknownTokenVariants() {
  static const auto unknownToken = std::vector<std::pair<int, QString>>{
      {22, "unknown.png"}, {44, "unknown@2x.png"}, {88, "unknown@4x.png"}, {192, "unknown@large.png"}};
  return unknownToken;
}

const std::vector<std::pair<int, QString>> &TokenVariants(const QString &name) {
  static const std::map<QString, std::vector<std::pair<int, QString>>> tokenIcons =  //
      {{"usdt",
        {
            {24, "usdt.png"},
            {44, "usdt@2x.png"},
            {88, "usdt@4x.png"},
            {192, "usdt@large.png"},
        }},
       {"usdc",
        {
            {24, "usdc.png"},
            {44, "usdc@2x.png"},
            {88, "usdc@4x.png"},
            {192, "usdc@large.png"},
        }},
       {"dai",
        {
            {24, "dai.png"},
            {44, "dai@2x.png"},
            {88, "dai@4x.png"},
            {192, "dai@large.png"},
        }},
       {"wbtc",
        {
            {24, "wbtc.png"},
            {44, "wbtc@2x.png"},
            {88, "wbtc@4x.png"},
            {192, "wbtc@large.png"},
        }},
       {"weth",
        {
            {24, "weth.png"},
            {44, "weth@2x.png"},
            {88, "weth@4x.png"},
            {192, "weth@large.png"},
        }}};

  const auto it = tokenIcons.find(name.trimmed().toLower());
  if (it != tokenIcons.end()) {
    return it->second;
  } else {
    return UnknownTokenVariants();
  }
}

QString ChooseVariant(const std::vector<std::pair<int, QString>> &variants, int desiredSize) {
  for (const auto &[size, name] : variants) {
    if (size == desiredSize || size >= desiredSize * 2) {
      return name;
    }
  }
  return variants.back().second;
}

QString ChooseVariant(int desiredSize) {
  return ChooseVariant(TonVariants(), desiredSize);
}

QString ChooseVariant(const QString &name, int desiredSize) {
  return ChooseVariant(TokenVariants(name), desiredSize);
}

QImage CreateImage(const QString &variant, int size) {
  Expects(size > 0);
  auto result = QImage(":/gui/art/" + variant).scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  result.setDevicePixelRatio(1.);

  Ensures(!result.isNull());
  return result;
}

QImage TonImage(int size) {
  return CreateImage(ChooseVariant(size), size);
}

QImage TokenImage(const QString &name, int size) {
  return CreateImage(ChooseVariant(name, size), size);
}

QImage UnknownImage(int size) {
  return CreateImage(ChooseVariant(UnknownTokenVariants(), size), size);
}

const QImage &Image(const Ton::Symbol &symbol) {
  static const auto iconTon = TonImage(st::walletTokenIconSize * style::DevicePixelRatio());
  static const auto iconUnknown = UnknownImage(st::walletTokenIconSize * style::DevicePixelRatio());

  static const std::map<QString, QImage> tokenIcons = {
      {"usdt", TokenImage("USDT", st::walletTokenIconSize * style::DevicePixelRatio())},
      {"usdc", TokenImage("USDC", st::walletTokenIconSize * style::DevicePixelRatio())},
      {"dai", TokenImage("DAI", st::walletTokenIconSize * style::DevicePixelRatio())},
      {"wbtc", TokenImage("WBTC", st::walletTokenIconSize * style::DevicePixelRatio())},
      {"weth", TokenImage("WETH", st::walletTokenIconSize * style::DevicePixelRatio())},
  };

  if (symbol.isTon()) {
    return iconTon;
  }

  const auto escaped = symbol.name().trimmed().toLower();
  const auto it = tokenIcons.find(escaped);
  if (it != tokenIcons.end()) {
    return it->second;
  } else {
    return iconUnknown;
  }
}

void Paint(const Ton::Symbol &kind, QPainter &p, int x, int y) {
  p.drawImage(QRect(x, y, st::walletTokenIconSize, st::walletTokenIconSize), Image(kind));
}

}  // namespace

void PaintInlineTokenIcon(const Ton::Symbol &symbol, QPainter &p, int x, int y, const style::font &font) {
  Paint(symbol, p, x, y + font->ascent - st::walletTokenIconAscent);
}

QImage InlineTokenIcon(const Ton::Symbol &symbol, int size) {
  if (symbol.isTon()) {
    return TonImage(size);
  } else {
    return TokenImage(symbol.name(), size);
  }
}

not_null<RpWidget *> CreateInlineTokenIcon(const Ton::Symbol &symbol, not_null<QWidget *> parent, int x, int y,
                                           const style::font &font) {
  auto result = Ui::CreateChild<RpWidget>(parent.get());

  result->setGeometry(x, y + font->ascent - st::walletTokenIconAscent, st::walletDiamondSize, st::walletDiamondSize);

  rpl::combine(result->paintRequest())  //
      | rpl::start_with_next(
            [=, symbol = symbol](const QRect &) {
              auto p = QPainter(result);
              Paint(symbol, p, 0, 0);
            },
            result->lifetime());

  return result;
}

QImage TokenQrExact(const Ton::Symbol &symbol, const Qr::Data &data, int pixel) {
  return Qr::ReplaceCenter(Qr::Generate(data, pixel), Ui::InlineTokenIcon(symbol, Qr::ReplaceSize(data, pixel)));
}

QImage TokenQr(const Ton::Symbol &symbol, const Qr::Data &data, int pixel, int max = 0) {
  Expects(data.size > 0);

  if (max > 0 && data.size * pixel > max) {
    pixel = std::max(max / data.size, 1);
  }
  return TokenQrExact(symbol, data, pixel * style::DevicePixelRatio());
}

QImage TokenQr(const Ton::Symbol &symbol, const QString &text, int pixel, int max) {
  QImage img;
  return TokenQr(symbol, Qr::Encode(text), pixel, max);
}

QImage TokenQrForShare(const Ton::Symbol &symbol, const QString &text) {
  const auto data = Qr::Encode(text);
  const auto size = (kShareQrSize - 2 * kShareQrPadding);
  const auto image = TokenQrExact(symbol, data, size / data.size);
  auto result = QImage(kShareQrPadding * 2 + image.width(), kShareQrPadding * 2 + image.height(),
                       QImage::Format_ARGB32_Premultiplied);
  result.fill(Qt::white);
  {
    auto p = QPainter(&result);
    p.drawImage(kShareQrPadding, kShareQrPadding, image);
  }
  return result;
}

}  // namespace Ui
