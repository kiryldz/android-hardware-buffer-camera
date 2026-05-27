# CI setup

## Prerequisites

This project uses two GitHub Actions workflows:

| Workflow | File | Trigger | Required |
|---|---|---|---|
| **Build** | `.github/workflows/build.yml` | push to `main`, all PRs | always |
| **Benchmark** | `.github/workflows/benchmark.yml` | PRs only | always (required check) |

The benchmark workflow runs on **Firebase Test Lab Spark** (free tier):
- 5 physical device-runs per day — enough for 2 full PR validations (2 devices each)
  before the daily cap resets. Force-pushes and re-runs consume the same quota.
- If the cap becomes tight, apply for the
  [BrowserStack Open Source Program](https://www.browserstack.com/open-source) —
  see the swap instructions at the end of this doc.

---

## One-time GCP / FTL setup (~15 min)

### 1. Create a GCP project on the Spark (free) plan

1. Go to [console.cloud.google.com](https://console.cloud.google.com) and create a new project.
   **Do not** add billing — the Spark plan is no-cost.
2. Enable these APIs (Console → APIs & Services → Library):
   - **Firebase Test Lab API** (`testing.googleapis.com`)
   - **Cloud Storage API** (`storage.googleapis.com`)
   - **Cloud Tool Results API** (`toolresults.googleapis.com`)

### 2. Link a Firebase project

1. Go to [console.firebase.google.com](https://console.firebase.google.com).
2. Click **Add project** → select **"Use existing Google Cloud project"** → pick the project you created above.
3. Accept the Spark plan.

### 3. Create a service account

In the GCP Console → IAM & Admin → Service Accounts:

1. Create a service account (e.g. `github-ftl-runner`).
2. Grant these roles:
   - `roles/firebase.testLab.admin`
   - `roles/storage.admin`
   - `roles/cloudtoolresults.viewer`
3. Create a JSON key and download it.

### 4. Create a GCS results bucket

In the GCP Console → Cloud Storage → Create bucket.
Pick any name (`camerafast-ftl-results` works) in a single region nearest to you.
Leave all other settings as default.

Grant the service account `roles/storage.objectAdmin` on the bucket specifically
(or the `roles/storage.admin` you granted in step 3 already covers it project-wide).

### 5. Add GitHub Secrets

Repo → Settings → Secrets and variables → Actions → New repository secret:

| Secret name | Value |
|---|---|
| `GCP_SA_KEY` | Full contents of the JSON key file downloaded in step 3 |
| `GCP_RESULTS_BUCKET` | Bucket name from step 4 (no `gs://` prefix) |

---

## Seeding the per-GPU baselines (first run)

The checked-in `benchmark/baselines/baseline-{adreno,mali}.json` are placeholders.
On the first PR run CI will fail with exit-2 ("improvement") because the placeholders
have no real values to compare against.

Steps to seed them:

1. Open the failing PR's GitHub Actions run.
2. Open the `benchmark-adreno` (or `benchmark-mali`) job.
3. In the **"Compare against baseline"** step summary, copy the JSON block under
   **"Proposed updated baseline"**.
4. Paste it into `benchmark/baselines/baseline-adreno.json` (or `-mali.json`).
5. Commit and push — the benchmark jobs will now compare against the real FTL values.

After seeding, the baselines reflect FTL device performance. The existing
`.cache/frame-latency/baseline.json` (from the local SM-F936B) is a separate
reference and will diverge — that's expected.

---

## Regenerating a baseline after a real improvement

When CI exits 2 (improvement beyond tolerance):

1. The step summary shows a **"Proposed updated baseline"** JSON block.
2. Copy-paste it into the relevant `benchmark/baselines/baseline-<gpu>.json`.
3. Commit the file and push — the check will go green.

You can alternatively re-run the capture locally with a tethered device.
The same instrumented test that CI runs on FTL also runs via Gradle:

```bash
./gradlew :app:installRelease :app:connectedReleaseAndroidTest \
  -Pandroid.injected.build.abi=$(adb shell getprop ro.product.cpu.abi | tr -d '\r') \
  -Pandroid.testInstrumentationRunnerArguments.additionalTestOutputDir=/sdcard/Android/media/com.dz.camerafast/additional_test_output \
  -Pandroid.testInstrumentationRunnerArguments.dz.iterations=5 \
  -Pandroid.testInstrumentationRunnerArguments.dz.duration.ms=10000

# AGP's UTP auto-pulls traces from the device into:
python3 scripts/aggregate-traces.py \
  "app/build/outputs/connected_android_test_additional_output/releaseAndroidTest/connected/<device>" \
  benchmark/baselines/baseline-<gpu>.json \
  --device-model "My Device" --gpu "Adreno 620" --ftl-model-id "redfin" --android-sdk 30
```

For ad-hoc local measurement without going through Gradle, the Bash
equivalents `scripts/measure-frame-latency.sh` and
`scripts/baseline-frame-latency.sh` capture the same `dz.frame_*` slices.

Note: locally-captured values differ from FTL — if CI already seeded the
baseline from FTL, prefer the FTL numbers (copy from step summary).

---

## Branch protection (manual, one-time)

Repo → Settings → Branches → Add rule for `main`:

- [x] Require a pull request before merging
- [x] Require status checks to pass:
  - `build`
  - `benchmark-adreno`
  - `benchmark-mali`
- [x] Require branches to be up to date before merging
- [ ] Allow force pushes (leave unchecked)

---

## FTL device catalogue

The workflow pins specific `model,version` pairs so a Spark catalogue change
surfaces as a CI break rather than silent baseline drift.

| Job | Model | Device | GPU | API |
|---|---|---|---|---|
| `benchmark-adreno` | `redfin` | Pixel 5 | Adreno 620 (Snapdragon 765G) | 30 |
| `benchmark-mali` | `oriole` | Pixel 6 | Mali-G78 (Google Tensor) | 32 |

To check current Spark availability:
```bash
gcloud firebase test android models list --filter=manufacturer=google --format=table
```

If a model is no longer in the Spark catalogue, update `benchmark.yml` and
regenerate both baselines.

---

## Swap path: BrowserStack Open Source Program

BrowserStack's OSS Program offers unlimited real-device automation for public
open-source repos — apply at [browserstack.com/open-source](https://www.browserstack.com/open-source).
Requirements: public repo, OSS licence, BrowserStack logo in README.

If accepted:

1. Add secrets: `BROWSERSTACK_USERNAME`, `BROWSERSTACK_ACCESS_KEY`.
2. Replace the FTL steps in each benchmark job with BrowserStack App Automate
   (`curl -u "$BS_USER:$BS_KEY" -X POST ... `) targeting equivalent Adreno and
   Mali devices.
3. The `aggregate-traces.py` / `compare-baseline.py` / baseline files are
   provider-agnostic — no changes needed there.

This lifts the 5-runs/day cap entirely.

---

## Cost estimate

| Scenario | Cost |
|---|---|
| FTL Spark, ≤5 physical runs/day | **$0/month** |
| FTL Spark cap exceeded (runs over 5/day) | Requires upgrading to Blaze; ~$1/device-min (~$10 for a 10-min run) |
| BrowserStack OSS Program (if approved) | **$0/month** |
| BrowserStack paid | ~$249/mo base |
| AWS Device Farm after free trial | ~$0.17/device-min (~$1.02 for a 6-min run, per device) |
