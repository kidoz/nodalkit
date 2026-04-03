frame_diag_diff = executable('nk_frame_diag_diff',
    'frame_diag_diff.cpp',
    dependencies : nk_dep,
)

debug_bundle_view = executable('nk_debug_bundle_view',
    'debug_bundle_view.cpp',
    dependencies : nk_dep,
)

frame_diag_view = executable('nk_frame_diag_view',
    'frame_diag_view.cpp',
    dependencies : nk_dep,
)

render_snapshot_view = executable('nk_render_snapshot_view',
    'render_snapshot_view.cpp',
    dependencies : nk_dep,
)

atspi_snapshot_view = executable('nk_atspi_snapshot_view',
    'atspi_snapshot_view.cpp',
    dependencies : nk_dep,
)

widget_debug_view = executable('nk_widget_debug_view',
    'widget_debug_view.cpp',
    dependencies : nk_dep,
)

trace_diff = executable('nk_trace_diff',
    'trace_diff.cpp',
    dependencies : nk_dep,
)

trace_view = executable('nk_trace_view',
    'trace_view.cpp',
    dependencies : nk_dep,
)

trace_capture_harness = executable('nk_trace_capture_harness',
    'trace_capture_harness.cpp',
    dependencies : nk_dep,
)

overlay_screenshot_harness = executable('nk_overlay_screenshot_harness',
    'overlay_screenshot_harness.cpp',
    dependencies : nk_dep,
)

frame_diag_baseline = meson.project_source_root() / 'tests' / 'fixtures' / 'frame_diag_baseline.json'
frame_diag_candidate_ok = meson.project_source_root() / 'tests' / 'fixtures' / 'frame_diag_candidate_ok.json'
frame_diag_candidate_bad = meson.project_source_root() / 'tests' / 'fixtures' / 'frame_diag_candidate_bad.json'
trace_baseline = meson.project_source_root() / 'tests' / 'fixtures' / 'trace_baseline.json'
trace_candidate_ok = meson.project_source_root() / 'tests' / 'fixtures' / 'trace_candidate_ok.json'
trace_candidate_bad = meson.project_source_root() / 'tests' / 'fixtures' / 'trace_candidate_bad.json'
stable_mixed_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_mixed_frame_diagnostics.json'
stable_mixed_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_mixed_trace.json'
stable_text_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_text_heavy_frame_diagnostics.json'
stable_text_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_text_heavy_trace.json'
stable_list_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_list_heavy_frame_diagnostics.json'
stable_list_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_list_heavy_trace.json'
stable_animation_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_animation_heavy_frame_diagnostics.json'
stable_animation_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_animation_heavy_trace.json'
stable_localized_redraw_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_localized_redraw_frame_diagnostics.json'
stable_localized_redraw_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_localized_redraw_trace.json'
stable_metal_localized_redraw_artifact = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_metal_localized_redraw_frame_diagnostics.json'
stable_metal_localized_redraw_trace = meson.project_source_root() / 'tests' / 'fixtures' / 'stable_metal_localized_redraw_trace.json'
trace_capture_harness_mixed_artifact = meson.project_build_root() / 'trace_capture_harness_mixed_artifact.json'
trace_capture_harness_mixed_trace = meson.project_build_root() / 'trace_capture_harness_mixed_trace.json'
trace_capture_harness_text_artifact = meson.project_build_root() / 'trace_capture_harness_text_artifact.json'
trace_capture_harness_text_trace = meson.project_build_root() / 'trace_capture_harness_text_trace.json'
trace_capture_harness_list_artifact = meson.project_build_root() / 'trace_capture_harness_list_artifact.json'
trace_capture_harness_list_trace = meson.project_build_root() / 'trace_capture_harness_list_trace.json'
trace_capture_harness_animation_artifact = meson.project_build_root() / 'trace_capture_harness_animation_artifact.json'
trace_capture_harness_animation_trace = meson.project_build_root() / 'trace_capture_harness_animation_trace.json'
trace_capture_harness_localized_redraw_artifact = meson.project_build_root() / 'trace_capture_harness_localized_redraw_artifact.json'
trace_capture_harness_localized_redraw_trace = meson.project_build_root() / 'trace_capture_harness_localized_redraw_trace.json'
trace_capture_harness_metal_localized_redraw_artifact = meson.project_build_root() / 'trace_capture_harness_metal_localized_redraw_artifact.json'
trace_capture_harness_metal_localized_redraw_trace = meson.project_build_root() / 'trace_capture_harness_metal_localized_redraw_trace.json'
overlay_screenshot_layout = meson.project_build_root() / 'overlay_layout_bounds.ppm'
overlay_screenshot_dirty = meson.project_build_root() / 'overlay_dirty_widgets.ppm'
overlay_screenshot_inspector = meson.project_build_root() / 'overlay_inspector.ppm'
overlay_screenshot_docked = meson.project_build_root() / 'overlay_inspector_docked.ppm'
overlay_screenshot_filter = meson.project_build_root() / 'overlay_widget_filter.ppm'
overlay_screenshot_render_focus = meson.project_build_root() / 'overlay_render_focus.ppm'
sanitizers_enabled = get_option('b_sanitize') != 'none'

frame_diag_diff_args = [
    '--max-total-ms-regression=0.5',
    '--max-frame-total-ms-regression=0.5',
    '--max-frame-layout-ms-regression=0.3',
    '--max-frame-snapshot-ms-regression=0.3',
    '--max-frame-render-ms-regression=0.5',
    '--max-frame-present-ms-regression=0.3',
    '--max-render-node-regression=2',
    '--max-text-shape-regression=1',
    '--max-image-upload-regression=1',
    '--max-model-materialize-regression=1',
    '--max-frame-redraw-request-regression=1',
    '--max-frame-layout-request-regression=1',
]

trace_diff_args = [
    '--max-frame-ms-regression=1.0',
    '--max-over-budget-frame-regression=0',
    '--max-slow-frame-regression=0',
    '--max-very-slow-frame-regression=0',
    '--max-posted-task-regression=1',
    '--max-timeout-regression=0',
    '--max-interval-regression=0',
    '--max-idle-regression=0',
    '--max-unlabeled-runtime-regression=0',
]

test('frame_diag_diff_ok',
    frame_diag_diff,
    args : [frame_diag_baseline, frame_diag_candidate_ok] + frame_diag_diff_args,
)

test('frame_diag_diff_regression',
    frame_diag_diff,
    args : [frame_diag_baseline, frame_diag_candidate_bad] + frame_diag_diff_args,
    should_fail : true,
)

test('debug_bundle_view',
    debug_bundle_view,
    args : [meson.project_source_root() / 'tests' / 'fixtures' / 'debug_bundle_fixture', '--summary-only'],
)

test('frame_diag_view',
    frame_diag_view,
    args : [frame_diag_baseline, '--summary-only'],
)

test('frame_diag_view_filter',
    frame_diag_view,
    args : [frame_diag_baseline, '--reason=widget-redraw'],
)

test('render_snapshot_view',
    render_snapshot_view,
    args : [meson.project_source_root() / 'tests' / 'fixtures' / 'render_snapshot_fixture.json'],
)

test('render_snapshot_view_filter',
    render_snapshot_view,
    args : [
        meson.project_source_root() / 'tests' / 'fixtures' / 'render_snapshot_fixture.json',
        '--kind=text',
        '--source-widget=fixture-label',
    ],
)

test('atspi_snapshot_view',
    atspi_snapshot_view,
    args : [meson.project_source_root() / 'tests' / 'fixtures' / 'atspi_widget_debug_fixture.json', '--summary-only'],
)

test('atspi_snapshot_view_filter',
    atspi_snapshot_view,
    args : [
        meson.project_source_root() / 'tests' / 'fixtures' / 'atspi_widget_debug_fixture.json',
        '--role=push button',
        '--action=activate',
        '--find=search',
    ],
)

test('widget_debug_view',
    widget_debug_view,
    args : [meson.project_source_root() / 'tests' / 'fixtures' / 'widget_debug_fixture.json', '--summary-only'],
)

test('widget_debug_view_filter',
    widget_debug_view,
    args : [
        meson.project_source_root() / 'tests' / 'fixtures' / 'widget_debug_fixture.json',
        '--find=accent',
    ],
)

test('widget_debug_view_state_filter',
    widget_debug_view,
    args : [
        meson.project_source_root() / 'tests' / 'fixtures' / 'widget_debug_fixture.json',
        '--state=focused',
    ],
)

test('trace_diff_ok',
    trace_diff,
    args : [trace_baseline, trace_candidate_ok] + trace_diff_args,
)

test('trace_diff_regression',
    trace_diff,
    args : [trace_baseline, trace_candidate_bad] + trace_diff_args,
    should_fail : true,
)

test('trace_view',
    trace_view,
    args : [trace_baseline, '--summary-only'],
)

test('trace_view_filter',
    trace_view,
    args : [trace_baseline, '--reason=idle', '--label=baseline.idle'],
)

test('trace_capture_harness',
    trace_capture_harness,
    args : [
        'mixed',
        trace_capture_harness_mixed_artifact,
        trace_capture_harness_mixed_trace,
    ] + (sanitizers_enabled ? [] : [
        '--baseline-artifact=' + stable_mixed_artifact,
        '--baseline-trace=' + stable_mixed_trace,
    ]),
)

test('trace_capture_harness_text_heavy',
    trace_capture_harness,
    args : [
        'text-heavy',
        trace_capture_harness_text_artifact,
        trace_capture_harness_text_trace,
    ] + (sanitizers_enabled ? [] : [
        '--baseline-artifact=' + stable_text_artifact,
        '--baseline-trace=' + stable_text_trace,
    ]),
)

test('trace_capture_harness_list_heavy',
    trace_capture_harness,
    args : [
        'list-heavy',
        trace_capture_harness_list_artifact,
        trace_capture_harness_list_trace,
    ] + (sanitizers_enabled ? [] : [
        '--baseline-artifact=' + stable_list_artifact,
        '--baseline-trace=' + stable_list_trace,
    ]),
)

test('trace_capture_harness_animation_heavy',
    trace_capture_harness,
    args : [
        'animation-heavy',
        trace_capture_harness_animation_artifact,
        trace_capture_harness_animation_trace,
    ] + (sanitizers_enabled ? [] : [
        '--baseline-artifact=' + stable_animation_artifact,
        '--baseline-trace=' + stable_animation_trace,
    ]),
)

test('trace_capture_harness_localized_redraw',
    trace_capture_harness,
    args : [
        'localized-redraw',
        trace_capture_harness_localized_redraw_artifact,
        trace_capture_harness_localized_redraw_trace,
    ] + (sanitizers_enabled ? [] : [
        '--baseline-artifact=' + stable_localized_redraw_artifact,
        '--baseline-trace=' + stable_localized_redraw_trace,
    ]),
)

if host_machine.system() == 'darwin'
    test('trace_capture_harness_metal_localized_redraw',
        trace_capture_harness,
        args : [
            'localized-redraw',
            trace_capture_harness_metal_localized_redraw_artifact,
            trace_capture_harness_metal_localized_redraw_trace,
            '--backend=metal',
        ] + (sanitizers_enabled ? [] : [
            '--baseline-artifact=' + stable_metal_localized_redraw_artifact,
            '--baseline-trace=' + stable_metal_localized_redraw_trace,
        ]),
    )
endif

test('overlay_screenshot_harness',
    overlay_screenshot_harness,
    args : [
        overlay_screenshot_layout,
        overlay_screenshot_dirty,
        overlay_screenshot_inspector,
        overlay_screenshot_docked,
        overlay_screenshot_filter,
        overlay_screenshot_render_focus,
    ],
)
