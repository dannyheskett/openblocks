#!/usr/bin/env python3
"""Upload the openblocks app (.apk or .ipa) to AWS Device Farm and run a Fuzz
test on a real device, reporting pass/fail. Used by the devicefarm CI workflow.

openblocks is a native game with no tappable UI widgets, so a scripted
Appium/Espresso/XCUITest test would give little; the built-in Fuzz test (random
taps) validates that the app launches and runs without crashing on a real
device. The platform (Android vs iOS) is inferred from the app file extension;
the unsigned iOS .ipa works because Device Farm re-signs apps on upload.

Config via env (no ARNs are hard-coded, so this file is safe to commit):
  DEVICEFARM_PROJECT_ARN   required  the Device Farm project ARN
  APP_PATH                 required  path to the .apk or .ipa to test
  DEVICEFARM_UPLOAD_ONLY   optional  "1" to stop after upload (free; no devices)
  DEVICEFARM_MAX_DEVICES   optional  device count for the run (default 1)
  AWS_REGION               optional  defaults to us-west-2
"""
import os
import sys
import time
import urllib.request

import boto3

REGION = os.environ.get("AWS_REGION", "us-west-2")
PROJECT = os.environ["DEVICEFARM_PROJECT_ARN"]
APP_PATH = os.environ["APP_PATH"]
UPLOAD_ONLY = os.environ.get("DEVICEFARM_UPLOAD_ONLY") == "1"
MAX_DEVICES = int(os.environ.get("DEVICEFARM_MAX_DEVICES", "1"))

if APP_PATH.endswith(".apk"):
    PLATFORM, UPLOAD_TYPE = "ANDROID", "ANDROID_APP"
elif APP_PATH.endswith(".ipa"):
    PLATFORM, UPLOAD_TYPE = "IOS", "IOS_APP"
else:
    sys.exit(f"APP_PATH must be a .apk or .ipa, got: {APP_PATH}")

df = boto3.client("devicefarm", region_name=REGION)


def poll(fn, done, desc, timeout=1800, interval=10):
    start = time.time()
    while True:
        obj = fn()
        status = obj["status"]
        if done(obj):
            return obj
        if time.time() - start > timeout:
            sys.exit(f"timed out waiting for {desc} (last status {status})")
        print(f"  {desc}: {status} ...", flush=True)
        time.sleep(interval)


def download_media_artifacts(run_arn, out_dir):
    """Download the run's screenshots and video (skipping logs) so CI can upload
    them as a workflow artifact for visual inspection."""
    os.makedirs(out_dir, exist_ok=True)
    n = 0
    for job in df.list_jobs(arn=run_arn)["jobs"]:
        for suite in df.list_suites(arn=job["arn"])["suites"]:
            sname = suite["name"].replace(" ", "_")
            for test in df.list_tests(arn=suite["arn"])["tests"]:
                for atype in ("SCREENSHOT", "FILE"):
                    for a in df.list_artifacts(arn=test["arn"], type=atype)["artifacts"]:
                        ext = (a.get("extension") or "").lower()
                        if ext not in ("mp4", "png", "jpg", "jpeg"):
                            continue
                        name = a["name"].replace(" ", "_")
                        fn = os.path.join(out_dir, f"{sname}-{name}.{ext}")
                        urllib.request.urlretrieve(a["url"], fn)
                        n += 1
    print(f"downloaded {n} media artifact(s) to {out_dir}", flush=True)


def main():
    # 1) Register an upload slot and PUT the app to the presigned URL.
    up = df.create_upload(
        projectArn=PROJECT, name=os.path.basename(APP_PATH), type=UPLOAD_TYPE
    )["upload"]
    with open(APP_PATH, "rb") as f:
        data = f.read()
    req = urllib.request.Request(
        up["url"], data=data, method="PUT",
        headers={"Content-Type": "application/octet-stream"},
    )
    urllib.request.urlopen(req).read()
    print(f"uploaded {len(data)} bytes ({APP_PATH})", flush=True)

    # 2) Wait for Device Farm to validate the app.
    up = poll(
        lambda: df.get_upload(arn=up["arn"])["upload"],
        lambda o: o["status"] in ("SUCCEEDED", "FAILED"),
        "upload",
        timeout=300,
    )
    if up["status"] != "SUCCEEDED":
        sys.exit(f"upload failed: {up.get('metadata')}")
    print("upload validated: SUCCEEDED", flush=True)

    if UPLOAD_ONLY:
        print("DEVICEFARM_UPLOAD_ONLY set — stopping before scheduling a run.")
        return

    # 3) Schedule a Fuzz run on real device(s) of the matching platform.
    run = df.schedule_run(
        projectArn=PROJECT,
        appArn=up["arn"],
        name=f"openblocks CI fuzz ({PLATFORM})",
        test={"type": "BUILTIN_FUZZ"},
        deviceSelectionConfiguration={
            "filters": [
                {"attribute": "PLATFORM", "operator": "EQUALS", "values": [PLATFORM]},
                {"attribute": "AVAILABILITY", "operator": "EQUALS", "values": ["HIGHLY_AVAILABLE"]},
            ],
            "maxDevices": MAX_DEVICES,
        },
    )["run"]
    print(f"scheduled run: {run['arn']}", flush=True)

    # 4) Wait for completion and report.
    run = poll(
        lambda: df.get_run(arn=run["arn"])["run"],
        lambda o: o["status"] == "COMPLETED",
        "run",
    )
    c = run.get("counters", {})
    print(f"run result: {run['result']}  counters={c}", flush=True)

    art_dir = os.environ.get("DEVICEFARM_ARTIFACT_DIR")
    if art_dir:
        download_media_artifacts(run["arn"], art_dir)

    if run["result"] != "PASSED":
        sys.exit(f"Device Farm run did not pass: {run['result']}")


if __name__ == "__main__":
    main()
