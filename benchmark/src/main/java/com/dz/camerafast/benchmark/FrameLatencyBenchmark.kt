package com.dz.camerafast.benchmark

import android.content.Intent
import androidx.benchmark.macro.CompilationMode
import androidx.benchmark.macro.StartupMode
import androidx.benchmark.macro.TraceSectionMetric
import androidx.benchmark.macro.TraceSectionMetric.Mode
import androidx.benchmark.macro.junit4.MacrobenchmarkRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Macrobenchmark harness for frame-latency SLA gate.
 *
 * Runs N cold-start iterations (default 5) each lasting D ms (default 10000).
 * Emits one perfetto trace per iteration into connected_android_test_additional_output/
 * so scripts/aggregate-traces.py can post-process them for p90/p99.
 *
 * Run locally:
 *   ./gradlew :benchmark:connectedReleaseAndroidTest \
 *     -Pandroid.testInstrumentationRunnerArguments.dz.iterations=5 \
 *     -Pandroid.testInstrumentationRunnerArguments.dz.duration.ms=10000
 */
@RunWith(AndroidJUnit4::class)
class FrameLatencyBenchmark {

    @get:Rule
    val benchmarkRule = MacrobenchmarkRule()

    @Test
    fun frameLatency() {
        val args = InstrumentationRegistry.getArguments()
        val iterations = args.getString("dz.iterations", "5").toInt()
        val durationMs = args.getString("dz.duration.ms", "10000").toLong()

        benchmarkRule.measureRepeated(
            packageName = TARGET_PACKAGE,
            metrics = METRICS,
            iterations = iterations,
            startupMode = StartupMode.COLD,
            compilationMode = CompilationMode.None(),
            setupBlock = {
                device.executeShellCommand(
                    "pm grant $TARGET_PACKAGE android.permission.CAMERA"
                )
            }
        ) {
            startActivityAndWait(
                Intent().setClassName(TARGET_PACKAGE, "$TARGET_PACKAGE.CameraActivity")
            )
            Thread.sleep(durationMs)
        }
    }

    private companion object {
        const val TARGET_PACKAGE = "com.dz.camerafast"

        // TraceSectionMetric covers avg/min/max per section name across all slices
        // within a single iteration. scripts/aggregate-traces.py adds p50/p90/p99
        // by querying the raw perfetto traces directly.
        val METRICS = listOf(
            TraceSectionMetric("dz.frame_e2e.gl",         mode = Mode.Average),
            TraceSectionMetric("dz.frame_e2e.vk",         mode = Mode.Average),
            TraceSectionMetric("dz.frame_to_screen.gl",   mode = Mode.Average),
            TraceSectionMetric("dz.frame_to_screen.vk",   mode = Mode.Average),
            TraceSectionMetric("dz.frame_render.gl",      mode = Mode.Average),
            TraceSectionMetric("dz.frame_render.vk",      mode = Mode.Average),
            TraceSectionMetric("dz.frame_native_proc.gl", mode = Mode.Average),
            TraceSectionMetric("dz.frame_native_proc.vk", mode = Mode.Average),
            TraceSectionMetric("dz.frame_to_native.gl",   mode = Mode.Average),
            TraceSectionMetric("dz.frame_to_native.vk",   mode = Mode.Average),
        )
    }
}
