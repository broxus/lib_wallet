#pragma once

#include "ui/rp_widget.h"
#include "ton/ton_state.h"

namespace Wallet {

struct DePoolInfoState {
	QString address;
	Ton::DePoolParticipantState participantState;
};

class StakeRow;

class DePoolInfo final {
public:
	DePoolInfo(
		not_null<Ui::RpWidget*> parent,
		rpl::producer<DePoolInfoState> state);
	~DePoolInfo();

	void setGeometry(QRect geometry);
	auto geometry() const -> const QRect&;
	[[nodiscard]] auto heightValue() -> rpl::producer<int>;

	void setVisible(bool visible);

	[[nodiscard]] rpl::lifetime &lifetime();

private:
	void setupControls(rpl::producer<DePoolInfoState> &&state);

	Ui::RpWidget _widget;
	rpl::variable<int> _height;
	std::vector<std::unique_ptr<StakeRow>> _stakeRows;
};

[[nodiscard]] rpl::producer<DePoolInfoState> MakeDePoolInfoState(
	rpl::producer<Ton::WalletViewerState> state,
	rpl::producer<QString> selectedDePool);

} // namespace Wallet
