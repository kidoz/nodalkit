#include "../tools/trace_capture_baseline.h"

#include <catch2/catch_test_macros.hpp>

namespace {

struct Capture {
    nk::FrameDiagnosticsArtifact artifact;
    nk::TraceCapture trace;
};

Capture reference_capture() {
    Capture result;
    nk::FrameDiagnostics frame;
    frame.frame_id = 1;
    frame.layout_ms = 1;
    frame.snapshot_ms = 1;
    frame.render_ms = 1;
    frame.total_ms = 3;
    frame.render_node_count = 10;
    frame.widget_hotspot_totals.measure_count = 10;
    frame.widget_hotspot_totals.allocate_count = 5;
    frame.widget_hotspot_totals.text_measure_count = 12;
    frame.render_hotspot_counters.text_shape_count = 5;
    frame.render_hotspot_counters.text_node_count = 5;
    frame.layout_request_count = 1;
    frame.redraw_request_count = 1;
    result.artifact.frames = {frame, frame};
    nk::TraceEvent event;
    event.name = "frame";
    event.category = "window";
    event.duration_ms = 3;
    result.trace.events = {event, event};
    return result;
}

bool compare(const Capture& before,
             const Capture& after,
             nk::tools::BaselinePolicy policy,
             nk::RendererBackend backend = nk::RendererBackend::Software) {
    return nk::tools::validate_capture_baseline(
        "text-heavy", backend, before.artifact, before.trace, after.artifact, after.trace, policy);
}

} // namespace

TEST_CASE("Metal counter baselines gate GPU uploads drawing and cache reuse", "[tools][baseline]") {
    auto reference = reference_capture();
    for (auto& frame : reference.artifact.frames) {
        auto& c = frame.render_hotspot_counters;
        c.gpu_draw_call_count = 10;
        c.gpu_present_path = nk::GpuPresentPath::FullRedrawCopyBack;
        c.text_texture_upload_count = 5;
        c.image_texture_upload_count = 2;
        c.text_shape_cache_hit_count = 3;
    }
    auto candidate = reference;
    bool expected = false;
    SECTION("matching GPU work") {
        expected = true;
    }
    SECTION("direct GPU presentation") {
        expected = true;
        candidate.artifact.frames[0].render_hotspot_counters.gpu_present_path =
            nk::GpuPresentPath::FullRedrawDirect;
    }
    SECTION("partial GPU presentation") {
        expected = true;
        candidate.artifact.frames[0].render_hotspot_counters.gpu_present_path =
            nk::GpuPresentPath::PartialRedrawCopy;
    }
    SECTION("work at documented tolerance") {
        expected = true;
        auto& counters = candidate.artifact.frames[0].render_hotspot_counters;
        ++counters.text_texture_upload_count;
        ++counters.image_texture_upload_count;
        ++counters.gpu_draw_call_count;
        --counters.text_shape_cache_hit_count;
    }
    SECTION("extra text uploads") {
        candidate.artifact.frames[0].render_hotspot_counters.text_texture_upload_count += 2;
    }
    SECTION("extra image uploads across frames") {
        reference.artifact.frames[1].render_hotspot_counters.image_texture_upload_count = 0;
    }
    SECTION("extra draw calls") {
        candidate.artifact.frames[0].render_hotspot_counters.gpu_draw_call_count += 2;
    }
    SECTION("lost shape cache reuse") {
        candidate.artifact.frames[0].render_hotspot_counters.text_shape_cache_hit_count -= 2;
    }
    SECTION("software fallback") {
        candidate.artifact.frames[0].render_hotspot_counters.gpu_present_path =
            nk::GpuPresentPath::SoftwareUpload;
    }
    SECTION("missing GPU work") {
        candidate.artifact.frames[0].render_hotspot_counters.gpu_draw_call_count = 0;
    }
    CHECK(compare(reference,
                  candidate,
                  nk::tools::BaselinePolicy::Counters,
                  nk::RendererBackend::Metal) == expected);
}

TEST_CASE("Capture baselines accept matching work in both policies", "[tools][baseline]") {
    const auto reference = reference_capture();
    CHECK(compare(reference, reference, nk::tools::BaselinePolicy::Timed));
    CHECK(compare(reference, reference, nk::tools::BaselinePolicy::Counters));
    auto improved = reference;
    improved.artifact.frames[0].widget_hotspot_totals.measure_count = 0;
    CHECK(compare(reference, improved, nk::tools::BaselinePolicy::Counters));
}

TEST_CASE("Counter baselines accept work at the documented tolerance", "[tools][baseline]") {
    const auto reference = reference_capture();
    auto candidate = reference;
    auto& frame = candidate.artifact.frames[0];
    frame.widget_hotspot_totals.measure_count += 1;
    frame.widget_hotspot_totals.allocate_count += 1;
    frame.widget_hotspot_totals.text_measure_count += 2;
    frame.render_hotspot_counters.text_shape_count += 2;
    frame.render_hotspot_counters.text_node_count += 2;
    frame.layout_request_count += 1;
    frame.redraw_request_count += 1;
    CHECK(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
}

TEST_CASE("Counter baselines report slow timing without failing", "[tools][baseline]") {
    const auto reference = reference_capture();
    auto slow = reference;
    for (auto& frame : slow.artifact.frames) {
        frame.render_ms = 10000;
        frame.total_ms = 10002;
    }
    for (auto& event : slow.trace.events) {
        event.duration_ms = 10002;
    }
    CHECK_FALSE(compare(reference, slow, nk::tools::BaselinePolicy::Timed));
    CHECK(compare(reference, slow, nk::tools::BaselinePolicy::Counters));
    slow.artifact.frames[0].render_hotspot_counters.text_shape_count += 3;
    CHECK_FALSE(compare(reference, slow, nk::tools::BaselinePolicy::Counters));
}

TEST_CASE("Capture baselines reject structural and runtime regressions", "[tools][baseline]") {
    const auto reference = reference_capture();
    for (const auto policy :
         {nk::tools::BaselinePolicy::Timed, nk::tools::BaselinePolicy::Counters}) {
        DYNAMIC_SECTION("policy=" << static_cast<int>(policy)) {
            SECTION("missing artifact frame") {
                auto candidate = reference;
                candidate.artifact.frames.pop_back();
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("missing trace frame") {
                auto candidate = reference;
                candidate.trace.events.pop_back();
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("render nodes") {
                auto candidate = reference;
                candidate.artifact.frames[0].render_node_count += 3;
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("image uploads") {
                auto candidate = reference;
                candidate.artifact.frames[0].render_hotspot_counters.image_texture_upload_count = 2;
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("model materialization") {
                auto candidate = reference;
                candidate.artifact.frames[0].widget_hotspot_totals.model_row_materialize_count = 2;
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("unexpected timeout") {
                auto candidate = reference;
                nk::TraceEvent event;
                event.name = "timeout";
                event.category = "event-loop-timer";
                event.source_label = "test.extra-timeout";
                candidate.trace.events.push_back(event);
                CHECK_FALSE(compare(reference, candidate, policy));
            }
            SECTION("unlabeled runtime work") {
                auto candidate = reference;
                nk::TraceEvent event;
                event.name = "posted-task";
                event.category = "event-loop-task";
                candidate.trace.events.push_back(event);
                CHECK_FALSE(compare(reference, candidate, policy));
            }
        }
    }
}

TEST_CASE("Counter baselines detect repeated work below per-frame maxima", "[tools][baseline]") {
    const auto reference = reference_capture();
    SECTION("extra measurement pass") {
        auto candidate = reference;
        candidate.artifact.frames[1].widget_hotspot_totals.measure_count += 2;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
    SECTION("repeated text shaping") {
        auto baseline = reference;
        baseline.artifact.frames[1].render_hotspot_counters.text_shape_count = 0;
        CHECK_FALSE(compare(baseline, reference, nk::tools::BaselinePolicy::Counters));
        CHECK(compare(baseline, reference, nk::tools::BaselinePolicy::Timed));
    }
    SECTION("extra allocation pass") {
        auto candidate = reference;
        candidate.artifact.frames[1].widget_hotspot_totals.allocate_count += 2;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
    SECTION("extra text measurements") {
        auto candidate = reference;
        candidate.artifact.frames[1].widget_hotspot_totals.text_measure_count += 3;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
    SECTION("extra text nodes") {
        auto candidate = reference;
        candidate.artifact.frames[1].render_hotspot_counters.text_node_count += 3;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
    SECTION("extra layout requests") {
        auto candidate = reference;
        candidate.artifact.frames[1].layout_request_count += 2;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
    SECTION("extra redraw requests") {
        auto candidate = reference;
        candidate.artifact.frames[1].redraw_request_count += 2;
        CHECK_FALSE(compare(reference, candidate, nk::tools::BaselinePolicy::Counters));
    }
}
