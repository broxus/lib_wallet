#pragma once

#include "ui/style/style_core.h"
#include "ton/ton_state.h"

class QPainter;

namespace Ui {

class RpWidget;

void PaintInlineTokenIcon(Ton::TokenKind kind, QPainter &p, int x, int y, const style::font &font);

[[nodiscard]] QImage InlineTokenIcon(Ton::TokenKind kind, int size);

not_null<RpWidget*> CreateInlineTokenIcon(
	rpl::producer<std::optional<Ton::TokenKind>> kind,
	not_null<QWidget*> parent,
	int x,
	int y,
	const style::font & font);

[[nodiscard]] QImage TokenQr(Ton::TokenKind token, const QString &text, int pixel, int max = 0);
[[nodiscard]] QImage TokenQrForShare(Ton::TokenKind token, const QString &text);

} // namespace Ui
