#!/usr/bin/env python3
"""Publish an AAB to a Google Play testing track.

Flow: create edit -> upload bundle -> assign versionCode to the track -> commit.
Uses the Play Developer API directly (stdlib + `cryptography` for the JWT), so
no google-api-python-client dependency.

Credential resolution (first that is set wins):
  1. $PLAY_SERVICE_ACCOUNT_JSON  -- the service-account JSON *content* (CI).
  2. $PLAY_PUBLISHER_KEY          -- path to the service-account JSON file.
  3. ~/.config/openblocks/play-publisher.json  -- local default.

Usage:
  play_upload.py <path-to.aab> [track] [release-notes]
    track         defaults to "internal"
    release-notes defaults to $PLAY_RELEASE_NOTES, then a generic string.
"""
import base64, json, os, sys, time, urllib.request, urllib.parse, urllib.error
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding

PACKAGE = "com.danheskett.openblocks"
SCOPE = "https://www.googleapis.com/auth/androidpublisher"

AAB = sys.argv[1]
TRACK = sys.argv[2] if len(sys.argv) > 2 else "internal"
NOTES = (sys.argv[3] if len(sys.argv) > 3
         else os.environ.get("PLAY_RELEASE_NOTES", "Automated release build."))


def load_credential():
    raw = os.environ.get("PLAY_SERVICE_ACCOUNT_JSON")
    if raw:
        return json.loads(raw)
    path = os.environ.get("PLAY_PUBLISHER_KEY") or os.path.expanduser(
        "~/.config/openblocks/play-publisher.json")
    with open(path) as f:
        return json.load(f)


sa = load_credential()
b64url = lambda b: base64.urlsafe_b64encode(b).rstrip(b"=")
now = int(time.time())
si = b64url(json.dumps({"alg": "RS256", "typ": "JWT"}).encode()) + b"." + b64url(json.dumps(
    {"iss": sa["client_email"], "scope": SCOPE, "aud": sa["token_uri"],
     "iat": now, "exp": now + 3600}).encode())
key = serialization.load_pem_private_key(sa["private_key"].encode(), password=None)
assertion = (si + b"." + b64url(key.sign(si, padding.PKCS1v15(), hashes.SHA256()))).decode()
tok = json.load(urllib.request.urlopen(urllib.request.Request(sa["token_uri"], data=urllib.parse.urlencode(
    {"grant_type": "urn:ietf:params:oauth:grant-type:jwt-bearer", "assertion": assertion}).encode())))
auth = {"Authorization": "Bearer " + tok["access_token"]}


def call(url, data=None, method=None, headers=None, raw=False):
    h = dict(auth)
    h.update(headers or {})
    req = urllib.request.Request(url, data=data, headers=h,
                                 method=method or ("POST" if data is not None else "GET"))
    try:
        resp = urllib.request.urlopen(req)
        body = resp.read()
        return json.loads(body) if body and not raw else body
    except urllib.error.HTTPError as e:
        print(f"HTTP {e.code} on {method or 'POST'} {url}\n{e.read().decode()[:1500]}")
        sys.exit(1)


base = f"https://androidpublisher.googleapis.com/androidpublisher/v3/applications/{PACKAGE}"
edit = call(base + "/edits", data=b"")
eid = edit["id"]
print("edit:", eid)

aab = open(AAB, "rb").read()
print(f"uploading {AAB} ({len(aab)//1024} KB)...")
up = call(f"https://androidpublisher.googleapis.com/upload/androidpublisher/v3/applications/{PACKAGE}/edits/{eid}/bundles",
          data=aab, headers={"Content-Type": "application/octet-stream"})
vc = up["versionCode"]
print("bundle uploaded, versionCode:", vc, "sha256:", up.get("sha256", "")[:16])

track_body = json.dumps({"track": TRACK, "releases": [{
    "versionCodes": [str(vc)], "status": "completed",
    "releaseNotes": [{"language": "en-US", "text": NOTES}],
}]}).encode()
call(f"{base}/edits/{eid}/tracks/{TRACK}", data=track_body, method="PUT",
     headers={"Content-Type": "application/json"})
print(f"assigned to '{TRACK}' track")

committed = call(f"{base}/edits/{eid}:commit", data=b"")
print("edit committed:", committed.get("id"), "- release is live on the", TRACK, "track")
