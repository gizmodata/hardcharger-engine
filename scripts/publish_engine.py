"""Publish the six archives from one successful engine CI run. Safe to retry."""

import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


SDK_PLATFORMS = (
    "linux-amd64", "linux-amd64-baseline", "linux-arm64", "macos-arm64",
    "ios-iphoneos", "ios-iphonesimulator", "android-arm64-v8a", "android-x86_64",
    "windows-amd64", "windows-arm64", "linux-arm64-ubuntu", "linux-arm64-tsan",
)


def release_files(directory):
    archives = sorted(
        directory / f"hardcharger-engine-{kind}-{platform}.tar.gz"
        for kind in ("cli", "shared-libs")
        for platform in ("linux-amd64", "linux-arm64", "macos-arm64")
    )
    archives.extend(directory / f"hardcharger-engine-sdk-{platform}.tar.gz" for platform in SDK_PLATFORMS)
    checksums = directory / "SHA256SUMS"
    signatures = {}
    lines = []
    for archive in archives:
        if not archive.is_file() or not archive.stat().st_size:
            raise ValueError(f"Missing archive {archive.name}")
        digest = sha256(archive)
        signatures[archive.name] = (archive.stat().st_size, "sha256:" + digest)
        lines.append(f"{digest}  ./{archive.name}\n")
    checksums.write_text("".join(lines), encoding="ascii")
    signatures[checksums.name] = (checksums.stat().st_size, "sha256:" + sha256(checksums))
    return signatures


def tag_commit(repo, tag):
    ref = f"refs/tags/{tag}"
    output = subprocess.check_output(
        ["git", "ls-remote", f"https://github.com/{repo}.git", ref, ref + "^{}"], text=True
    )
    refs = {name: sha for sha, name in (line.split() for line in output.splitlines())}
    return refs.get(ref + "^{}", refs.get(ref))


def release_state(repo, tag):
    result = subprocess.run(
        ["gh", "release", "view", tag, "--repo", repo, "--json", "isDraft,assets"],
        capture_output=True, text=True,
    )
    if result.returncode and result.stderr.strip() == "release not found":
        return None
    if result.returncode:
        raise RuntimeError(result.stderr.strip())
    return json.loads(result.stdout)


def pending_files(release, expected):
    assets = {asset["name"]: asset for asset in release["assets"]}
    unexpected = assets.keys() - expected.keys()
    if unexpected:
        raise ValueError(f"Unexpected release assets: {', '.join(sorted(unexpected))}")
    return [
        name for name, signature in expected.items()
        if name not in assets or assets[name]["state"] != "uploaded"
        or (assets[name]["size"], assets[name]["digest"]) != signature
    ]


def gh(*args):
    subprocess.run(["gh", *args], check=True)


def publish(repo, tag, sha, directory):
    if not re.fullmatch(r"v[0-9]+\.[0-9]+\.[0-9]+", tag):
        raise ValueError(f"Invalid release tag {tag}")
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise ValueError(f"Invalid commit {sha}")
    expected = release_files(directory)
    current = tag_commit(repo, tag)
    if current is None:
        raise ValueError(f"Tag {tag} must already exist")
    if current != sha:
        raise ValueError(f"{tag} points to {current}, expected tested commit {sha}")
    release = release_state(repo, tag)
    if release is None:
        notes = (
            "CLI and C SDK builds for Linux and Apple Silicon, plus Hardcharger integration SDKs for Linux, macOS, iOS, Android and Windows. "
            f"See the [engine differences](https://github.com/{repo}/blob/{tag}/docs/optimizations.md) "
            "for the upstream baseline and measured results."
        )
        gh("release", "create", tag, "--repo", repo, "--verify-tag", "--draft",
           "--target", sha, "--title", f"Hardcharger Engine {tag}", "--notes", notes)
        release = release_state(repo, tag)
    pending = pending_files(release, expected)
    if not release["isDraft"]:
        if pending:
            raise ValueError(f"Published release {tag} differs from tested artifacts: {', '.join(pending)}")
        return
    if pending:
        gh("release", "upload", tag, "--repo", repo, "--clobber", *(str(directory / name) for name in pending))
    release = release_state(repo, tag)
    if pending_files(release, expected) or tag_commit(repo, tag) != sha:
        raise ValueError(f"{tag} failed final asset or tag verification")
    if release["isDraft"]:
        gh("release", "edit", tag, "--repo", repo, "--draft=false")


if __name__ == "__main__":
    publish(os.environ["GH_REPO"], os.environ["ENGINE_TAG"], os.environ["GITHUB_SHA"], Path(sys.argv[1]))
