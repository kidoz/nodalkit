#pragma once

#include <any>
#include <functional>
#include <nk/model/abstract_list_model.h>
#include <utility>

namespace trading {

class ViewListModel final : public nk::AbstractListModel {
public:
    using RowCount = std::function<std::size_t()>;
    using RowText = std::function<std::string(std::size_t)>;

    ViewListModel(RowCount row_count, RowText row_text)
        : row_count_(std::move(row_count)), row_text_(std::move(row_text)) {}

    [[nodiscard]] std::size_t row_count() const override { return row_count_(); }

    [[nodiscard]] std::any data(std::size_t row) const override { return row; }

    [[nodiscard]] std::string display_text(std::size_t row) const override {
        return row_text_(row);
    }

    void rows_changed() { notify_model_reset(); }

    void data_changed() {
        if (row_count() > 0) {
            notify_data_changed(0, row_count() - 1);
        }
    }

private:
    RowCount row_count_;
    RowText row_text_;
};

} // namespace trading
