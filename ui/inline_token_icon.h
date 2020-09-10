#pragma once

#include "ui/style/style_core.h"

class QPainter;

namespace Ui {

class RpWidget;

enum class TokenIconKind {
	Ton,
	Pepe
};

void PaintInlineTokenIcon(TokenIconKind kind, QPainter &p, int x, int y, const style::font &font);

[[nodiscard]] QImage InlineTokenIcon(TokenIconKind kind, int size);

not_null<RpWidget*> CreateInlineTokenIcon(
	TokenIconKind kind,
	not_null<QWidget*> parent,
	int x,
	int y,
	const style::font & font);

} // namespace Ui
