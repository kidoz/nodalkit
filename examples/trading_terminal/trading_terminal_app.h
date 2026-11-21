#pragma once

#include <memory>

namespace nk {
class Widget;
}

namespace trading {
std::shared_ptr<nk::Widget> build_workspace(std::shared_ptr<nk::Widget> watchlist,
                                            std::shared_ptr<nk::Widget> chart,
                                            std::shared_ptr<nk::Widget> ticket,
                                            std::shared_ptr<nk::Widget> orders,
                                            std::shared_ptr<nk::Widget> positions);
}

int run_trading_terminal(int argc, char** argv);
