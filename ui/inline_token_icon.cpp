#include "inline_token_icon.h"

#include "ui/rp_widget.h"
#include "qr/qr_generate.h"
#include "styles/style_wallet.h"

#include <QtGui/QPainter>

namespace Ui
{
namespace
{

constexpr auto kShareQrSize = 768;
constexpr auto kShareQrPadding = 16;

const std::vector<std::pair<int, QString>> &Variants(Ton::TokenKind kind) {
	static const auto iconTon = std::vector<std::pair<int, QString>>{
		{22,  "gem.png"},
		{44,  "gem@2x.png"},
		{88,  "gem@4x.png"},
		{192, "gem@large.png"},
	};

	static const auto iconUsdt = std::vector<std::pair<int, QString>>{
		{24,  "usdt.png"},
		{44,  "usdt@2x.png"},
		{88,  "usdt@4x.png"},
		{192, "usdt@large.png"},
	};

	static const auto iconUsdc = std::vector<std::pair<int, QString>>{
		{24,  "usdc.png"},
		{44,  "usdc@2x.png"},
		{88,  "usdc@4x.png"},
		{192, "usdc@large.png"},
	};

	static const auto iconDai = std::vector<std::pair<int, QString>>{
		{24,  "dai.png"},
		{44,  "dai@2x.png"},
		{88,  "dai@4x.png"},
		{192, "dai@large.png"},
	};

	static const auto iconWbtc = std::vector<std::pair<int, QString>>{
		{24,  "wbtc.png"},
		{44,  "wbtc@2x.png"},
		{88,  "wbtc@4x.png"},
		{192, "wbtc@large.png"},
	};

	static const auto iconWeth = std::vector<std::pair<int, QString>>{
		{24,  "weth.png"},
		{44,  "weth@2x.png"},
		{88,  "weth@4x.png"},
		{192, "weth@large.png"},
	};

	switch (kind) {
		case Ton::TokenKind::DefaultToken:
			return iconTon;
		case Ton::TokenKind::USDT:
			return iconUsdt;
		case Ton::TokenKind::USDC:
			return iconUsdc;
		case Ton::TokenKind::DAI:
			return iconDai;
		case Ton::TokenKind::WBTC:
			return iconWbtc;
		case Ton::TokenKind::WETH:
			return iconWeth;
		default:
			return iconTon;
	}
}

QString ChooseVariant(Ton::TokenKind kind, int desiredSize) {
	const auto &variants = Variants(kind);
	for (const auto &[size, name] : variants) {
		if (size == desiredSize || size >= desiredSize * 2) {
			return name;
		}
	}
	return variants.back().second;
}

QImage CreateImage(Ton::TokenKind kind, int size) {
	Expects(size > 0);

	const auto variant = ChooseVariant(kind, size);
	auto result = QImage(":/gui/art/" + variant).scaled(
		size,
		size,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result.setDevicePixelRatio(1.);

	Ensures(!result.isNull());
	return result;
}

QImage Image(Ton::TokenKind kind) {
	static const QImage images[] = {
		CreateImage(Ton::TokenKind::DefaultToken, st::walletTokenIconSize * style::DevicePixelRatio()),
		CreateImage(Ton::TokenKind::USDT, st::walletTokenIconSize * style::DevicePixelRatio()),
	};
	return images[static_cast<int>(kind)];
}

void Paint(Ton::TokenKind kind, QPainter &p, int x, int y) {
	p.drawImage(
		QRect(x, y, st::walletTokenIconSize, st::walletTokenIconSize),
		Image(kind));
}

} // namespace

void PaintInlineTokenIcon(Ton::TokenKind kind, QPainter &p, int x, int y, const style::font &font) {
	Paint(kind, p, x, y + font->ascent - st::walletTokenIconAscent);
}

QImage InlineTokenIcon(Ton::TokenKind kind, int size) {
	return CreateImage(kind, size);
}

not_null<RpWidget *> CreateInlineTokenIcon(
	rpl::producer<Ton::TokenKind> kind,
	not_null<QWidget *> parent,
	int x,
	int y,
	const style::font &font) {
	auto result = Ui::CreateChild<RpWidget>(parent.get());

	result->setGeometry(
		x,
		y + font->ascent - st::walletTokenIconAscent,
		st::walletDiamondSize,
		st::walletDiamondSize);

	rpl::combine(
		result->paintRequest(),
		std::move(kind)
	) | rpl::start_with_next([=](const QRect&, Ton::TokenKind kind) {
		auto p = QPainter(result);
		Paint(kind, p, 0, 0);
	}, result->lifetime());

	return result;
}


QImage TokenQrExact(Ton::TokenKind kind, const Qr::Data &data, int pixel) {
    return Qr::ReplaceCenter(
        Qr::Generate(data, pixel),
        Ui::InlineTokenIcon(kind, Qr::ReplaceSize(data, pixel)));
}

QImage TokenQr(Ton::TokenKind kind, const Qr::Data &data, int pixel, int max = 0) {
    Expects(data.size > 0);

    if (max > 0 && data.size * pixel > max) {
        pixel = std::max(max / data.size, 1);
    }
    return TokenQrExact(kind, data, pixel * style::DevicePixelRatio());
}

QImage TokenQr(Ton::TokenKind kind,
               const QString &text,
               int pixel,
               int max) {
    QImage img;
    return TokenQr(kind, Qr::Encode(text), pixel, max);
}

QImage TokenQrForShare(Ton::TokenKind kind, const QString &text) {
    const auto data = Qr::Encode(text);
    const auto size = (kShareQrSize - 2 * kShareQrPadding);
    const auto image = TokenQrExact(kind, data, size / data.size);
    auto result = QImage(
        kShareQrPadding * 2 + image.width(),
        kShareQrPadding * 2 + image.height(),
        QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::white);
    {
        auto p = QPainter(&result);
        p.drawImage(kShareQrPadding, kShareQrPadding, image);
    }
    return result;
}

} // namespace Ui
