#!/usr/bin/python3
"""Offline protocol fixture for project deletion; never opens Codex or user data."""
import json
import os
import sys

removed = []
for line in sys.stdin:
    request = json.loads(line)
    if "id" not in request:
        continue
    method, params = request["method"], request.get("params", {})
    result = {}
    if method == "initialize":
        assert params["capabilities"]["experimentalApi"] is True
    elif method == "project/list":
        result = {"data": [{"id": "native-project", "name": "Fixture", "roots": []}], "nextCursor": None}
    elif method == "thread/list":
        assert params["projectId"] == "native-project"
        assert params["useStateDbOnly"] is True
        assert len(params["sourceKinds"]) == 10
        assert not removed, "all membership pages must be collected before mutation"
        page = "archived" if params["archived"] else (params.get("cursor") or "active")
        project = "unrelated-project" if os.environ.get("APPDECK_PROJECT_FIXTURE_UNRELATED") else "native-project"
        result = {"data": [{"id": page + "-thread", "projectId": project}], "nextCursor": "page-two" if page == "active" else None}
    elif method == "thread/metadata/update":
        assert params["projectId"] == "", "empty string clears membership; null leaves it unchanged"
        removed.append(params["threadId"])
    elif method == "project/delete":
        assert params["projectId"] == "native-project"
        assert removed == ["active-thread", "page-two-thread", "archived-thread"]
    else:
        raise AssertionError("Unexpected method: " + method)
    print(json.dumps({"id": request["id"], "result": result}), flush=True)
