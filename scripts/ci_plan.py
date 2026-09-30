"""Choose CI platforms and real JUCE deliverable targets."""

import json
import os
from pathlib import Path


def build_matrix(event_name, ref, platform="windows", package="vst3"):
    if event_name != "workflow_dispatch":
        release = ref.startswith("refs/tags/v") or ref in (
            "refs/heads/main", "refs/heads/master"
        )
        platform, package = ("both", "full") if release else ("windows", "vst3")

    if platform not in ("windows", "macos", "both"):
        raise ValueError(f"Unsupported platform: {platform}")
    if package not in ("vst3", "full"):
        raise ValueError(f"Unsupported package: {package}")

    jobs = []
    for system, runner, label in (
        ("windows", "windows-2022", "Windows-x64"),
        ("macos", "macos-14", "macOS-Universal"),
    ):
        if platform not in (system, "both"):
            continue
        targets = ["ozoPRISM_VST3"]
        if package == "full":
            targets += ["ozoPRISM_Standalone", "ozoPRISMTests", "ozoPRISMVST3Host"]
            if system == "macos":
                targets.append("ozoPRISM_AU")
        jobs.append({
            "platform": system,
            "os": runner,
            "package": package,
            "targets": " ".join(targets),
            "artifact": f"ozoPRISM-{label}-{package}",
            "tests": "ON" if package == "full" else "OFF",
        })
    return {"include": jobs}


def main():
    matrix = build_matrix(
        os.environ["GITHUB_EVENT_NAME"],
        os.environ["GITHUB_REF"],
        os.environ.get("INPUT_PLATFORM") or "windows",
        os.environ.get("INPUT_PACKAGE") or "vst3",
    )
    value = json.dumps(matrix, separators=(",", ":"))
    print(json.dumps(matrix, indent=2))
    if output := os.environ.get("GITHUB_OUTPUT"):
        with Path(output).open("a", encoding="utf-8") as stream:
            stream.write(f"matrix={value}\n")
    if summary := os.environ.get("GITHUB_STEP_SUMMARY"):
        with Path(summary).open("a", encoding="utf-8") as stream:
            stream.write("## Build selection\n\n")
            for job in matrix["include"]:
                stream.write(f"- **{job['artifact']}**: `{job['targets']}`\n")


if __name__ == "__main__":
    main()
