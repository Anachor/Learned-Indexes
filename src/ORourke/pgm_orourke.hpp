#pragma once

#include <optional>

#include "../../third_party/PGM-index/include/pgm/piecewise_linear_model.hpp"
#include "orourke.hpp"

// O'Rourke using PGM-index's OptimalPiecewiseLinearModel.
template <typename T = int64_t>
class PgmORourke : public ORourke<T> {
    using Model = pgm::internal::OptimalPiecewiseLinearModel<T, T>;
    mutable std::optional<Model> model_;  // mutable: PGM's get_segment() isn't const

public:
    using ORourke<T>::ORourke;

protected:
    void start(T delta) override { model_.emplace(delta); }

    bool try_add(T x, T y) override { return model_->add_point(x, y); }

    // Not get_floating_point_segment: that rounds the intercept, so it can be
    // off by up to delta + 0.5.
    Line line() const override {
        auto seg = model_->get_segment();
        auto [x0, y0] = seg.get_intersection();
        auto [lo, hi] = seg.get_slope_range();
        return {x0, y0, (lo + hi) / 2};
    }
};
