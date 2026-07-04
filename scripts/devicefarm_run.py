#!/usr/bin/env python3
"""Upload the openblocks APK to AWS Device Farm and run a Fuzz test on a real
device, reporting pass/fail. Used by the devicefarm CI workflow.

openblocks is a native raylib game with no tappable UI widgets, so a scripted
Appium/Espresso test would give little; the built-in Fuzz test (random taps)
validates that the app launches and runs without crashing on a real device.

Config via env (no ARNs are hard-coded, so this file is safe to commit):
  DEVICEFARM_PROJECT_ARN   required  the Device Farm project ARN
  APK_PATH                 required  path to the .apk to test
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
APK_PATH = os.environ["APK_PATH"]
UPLOAD_ONLY = os.environ.get("DEVICEFARM_UPLOAD_ONLY") == "1"
MAX_DEVICES = int(os.environ.get("DEVICEFARM_MAX_DEVICES", "1"))

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


def main():
    # 1) Register an upload slot and PUT the APK to the presigned URL.
    up = df.create_upload(
        projectArn=PROJECT, name="openblocks.apk", type="ANDROID_APP"
    )["upload"]
    with open(APK_PATH, "rb") as f:
        data = f.read()
    req = urllib.request.Request(
        up["url"], data=data, method="PUT",
        headers={"Content-Type": "application/octet-stream"},
    )
    urllib.request.urlopen(req).read()
    print(f"uploaded {len(data)} bytes ({APK_PATH})", flush=True)

    # 2) Wait for Device Farm to validate the APK.
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

    # 3) Schedule a Fuzz run on real Android device(s).
    run = df.schedule_run(
        projectArn=PROJECT,
        appArn=up["arn"],
        name="openblocks CI fuzz",
        test={"type": "BUILTIN_FUZZ"},
        deviceSelectionConfiguration={
            "filters": [
                {"attribute": "PLATFORM", "operator": "EQUALS", "values": ["ANDROID"]},
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
    if run["result"] != "PASSED":
        sys.exit(f"Device Farm run did not pass: {run['result']}")


if __name__ == "__main__":
    main()
