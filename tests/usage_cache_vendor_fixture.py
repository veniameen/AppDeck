#!/usr/bin/python3
"""Offline Claude stand-in: emits only synthetic account data and counts quota requests."""
import json
import os
import sys

with open(os.environ["APPDECK_USAGE_FIXTURE_LOG"], "a", encoding="utf-8") as log:
    if sys.argv[1:] == ["auth", "status"]:
        log.write("auth\n")
        account = {"loggedIn": os.environ.get("APPDECK_USAGE_FIXTURE_SIGNED_OUT") != "1",
                   "authMethod": os.environ.get("APPDECK_USAGE_FIXTURE_METHOD", "claude.ai"),
                   "email": os.environ.get("APPDECK_USAGE_FIXTURE_EMAIL", "first@example.invalid"),
                   "subscriptionType": os.environ.get("APPDECK_USAGE_FIXTURE_PLAN", "max")}
        if "APPDECK_USAGE_FIXTURE_ORG" in os.environ:
            account["orgId"] = os.environ["APPDECK_USAGE_FIXTURE_ORG"]
        print(json.dumps(account), flush=True)
    else:
        request = json.loads(sys.stdin.readline())
        assert request["request"]["subtype"] == "get_usage"
        assert "--safe-mode" in sys.argv and "--no-session-persistence" in sys.argv
        log.write("usage\n")
        print(json.dumps({"type": "control_response", "response": {"subtype": "success",
            "request_id": request["request_id"], "response": {
                "subscription_type": "max", "rate_limits_available": True,
                "rate_limits": {"limits": [{"kind": "weekly_all", "percent": 42,
                                            "resets_at": "2027-01-01T00:00:00Z"}]}}}}), flush=True)
