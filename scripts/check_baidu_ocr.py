#!/usr/bin/env python3
"""Smoke-test a Baidu OCR app without putting keys in source.

Usage:
  BAIDU_OCR_KEY=... BAIDU_OCR_SECRET=... python3 scripts/check_baidu_ocr.py [image]
  BAIDU_OCR_ENDPOINT=general_basic ...   # standard edition instead of general

The default endpoint is "general" (通用文字识别（含位置信息版）). Keys are read
from the environment only and are never printed. This makes one OCR request and
prints the line count plus the first few line boxes.
"""
import base64
import json
import os
import sys
import urllib.parse
import urllib.request

TOKEN_URL = "https://aip.baidubce.com/oauth/2.0/token"
OCR_BASE = "https://aip.baidubce.com/rest/2.0/ocr/v1/"


def post(url, data):
    body = urllib.parse.urlencode(data).encode()
    request = urllib.request.Request(url, data=body, method="POST")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode())


def main():
    key = os.environ.get("BAIDU_OCR_KEY", "").strip()
    secret = os.environ.get("BAIDU_OCR_SECRET", "").strip()
    endpoint = os.environ.get("BAIDU_OCR_ENDPOINT", "general").strip() or "general"
    image_path = sys.argv[1] if len(sys.argv) > 1 else "dist/baidu-test.png"
    if not key or not secret:
        print("Set BAIDU_OCR_KEY and BAIDU_OCR_SECRET in the environment.", file=sys.stderr)
        return 2
    try:
        with open(image_path, "rb") as image_file:
            image = base64.b64encode(image_file.read()).decode()
    except OSError as error:
        print(f"Cannot read image: {error}", file=sys.stderr)
        return 2

    token = post(TOKEN_URL, {
        "grant_type": "client_credentials",
        "client_id": key,
        "client_secret": secret,
    })
    if "access_token" not in token:
        print("Token request failed:", {k: token.get(k) for k in ("error", "error_description")})
        return 1

    result = post(OCR_BASE + endpoint + "?access_token=" + urllib.parse.quote(token["access_token"]), {
        "image": image,
        "language_type": "ENG",
        "recognize_granularity": "big",
        "detect_direction": "true",
    })
    if result.get("error_code"):
        print("OCR failed:", {"error_code": result.get("error_code"), "error_msg": result.get("error_msg")})
        print("Common causes: 6 = service not enabled/permission, 17 = daily free limit, 110/111 = token.")
        return 1

    lines = result.get("words_result", [])
    print(f"endpoint={endpoint} lines={len(lines)}")
    for line in lines[:8]:
        print(json.dumps({"words": line.get("words"), "location": line.get("location")}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
