package com.dz.camerafast.perf

import android.app.UiAutomation
import android.content.Intent
import android.os.ParcelFileDescriptor
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Test
import org.junit.runner.RunWith

// Drives CameraActivity and captures N back-to-back Perfetto traces into
// additionalTestOutputDir for FTL --directories-to-pull to export. The
// resulting *.pftrace files are aggregated by scripts/aggregate-traces.py.
//
// Runner arguments (-e on the command line, or --environment-variables on FTL):
//   dz.iterations           Number of capture iterations (default 5).
//   dz.duration.ms          Capture window per iteration in ms (default 10000).
//   additionalTestOutputDir Where to write the .pftrace files. Must be a path
//                           the shell user can write and that FTL pulls via
//                           --directories-to-pull. Locally AGP injects its own
//                           value; on FTL we pass it via --environment-variables.
//
// Note: perfetto's short-form CLI (-t, -a, positional categories) requires
// Android 12+. .github/workflows/benchmark.yml pins both FTL devices to API 31+
// for this reason.
@RunWith(AndroidJUnit4::class)
class FrameLatencyCapture {

    @Test
    fun captureFrameLatencyTraces() {
        val args = InstrumentationRegistry.getArguments()
        val iterations = args.getString("dz.iterations", "5").toInt()
        val durationS = args.getString("dz.duration.ms", "10000").toLong() / 1000L
        val outputDir = args.getString("additionalTestOutputDir")
            ?: error("Missing instrumentation arg 'additionalTestOutputDir'")

        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val targetContext = instrumentation.targetContext
        val ui = instrumentation.uiAutomation

        ui.shell("mkdir -p $outputDir")
        ui.shell("pm grant $TARGET_PKG android.permission.CAMERA")

        // The instrumentation runs in the target app's own process (default
        // when androidTest lives in :app), so `am force-stop com.dz.camerafast`
        // would SIGKILL the test. Launch CameraActivity once and capture N
        // adjacent steady-state windows — dz.frame_* slices are emitted
        // continuously by the preview pipeline.
        targetContext.startActivity(
            Intent().setClassName(TARGET_PKG, "$TARGET_PKG.CameraActivity")
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        )
        Thread.sleep(2_000L)  // camera + GPU contexts spin up

        repeat(iterations) { i ->
            val deviceTrace = "/data/misc/perfetto-traces/dz-frame-latency-$i.pftrace"
            val outputTrace = "$outputDir/dz-frame-latency-$i.pftrace"

            // -a <pkg> is mandatory: without it, app-tag atrace sections (where
            // dz.frame_* lands) are filtered out. perfetto blocks for -t seconds.
            ui.shell(
                "perfetto -o $deviceTrace -t ${durationS}s -b 32mb " +
                    "-a $TARGET_PKG gfx view app sched"
            )

            // /data/misc/perfetto-traces is shell:shell — copy out into the
            // FTL-collected dir (shell can write /sdcard/Android/media/<pkg>).
            ui.shell("cp $deviceTrace $outputTrace")
            ui.shell("rm $deviceTrace")

            // UiAutomation.executeShellCommand returns the moment the command
            // exits but doesn't expose its exit code; if perfetto rejects the
            // command line (e.g. short-form not available on this Android
            // version) the trace file is missing — fail fast with context.
            val ls = ui.shell("ls -l $outputTrace")
            check(ls.isNotBlank()) {
                "perfetto did not produce $outputTrace on iteration $i. " +
                    "Output dir contents: ${ui.shell("ls -la $outputDir")}"
            }
        }
    }

    private fun UiAutomation.shell(cmd: String): String {
        val pfd: ParcelFileDescriptor = executeShellCommand(cmd)
        ParcelFileDescriptor.AutoCloseInputStream(pfd).use { stream ->
            return stream.readBytes().decodeToString()
        }
    }

    private companion object {
        const val TARGET_PKG = "com.dz.camerafast"
    }
}
