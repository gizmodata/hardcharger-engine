import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import publish_engine


class TestPublishEngine(unittest.TestCase):
    repo = "example/engine"
    tag = "v2.0.1"
    sha = "a" * 40

    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        for kind in ("cli", "shared-libs"):
            for platform in ("linux-amd64", "linux-arm64", "macos-arm64"):
                name = f"hardcharger-engine-{kind}-{platform}.tar.gz"
                (self.directory / name).write_text(name)
        for platform in publish_engine.SDK_PLATFORMS:
            name = f"hardcharger-engine-sdk-{platform}.tar.gz"
            (self.directory / name).write_text(name)
        self.commit = None
        self.release = None
        self.commands = []
        self.fail_upload_after = None
        for name, fn in (
            ("tag_commit", lambda *args: self.commit),
            ("release_state", lambda *args: self.release),
            ("gh", self.command),
        ):
            p = patch.object(publish_engine, name, side_effect=fn)
            p.start()
            self.addCleanup(p.stop)

    def command(self, *args):
        self.commands.append(args)
        if args[0] == "api":
            self.assertIn(f"ref=refs/tags/{self.tag}", args)
            self.assertIn(f"sha={self.sha}", args)
            self.commit = self.sha
        elif args[1] == "create":
            self.assertIn("--draft", args)
            self.assertIn("--verify-tag", args)
            self.release = {"isDraft": True, "assets": []}
        elif args[1] == "upload":
            self.assertTrue(self.release["isDraft"])
            existing = {a["name"]: a for a in self.release["assets"]}
            for i, arg in enumerate(args[args.index("--clobber") + 1:]):
                if self.fail_upload_after == i:
                    raise RuntimeError("interrupted")
                path = Path(arg)
                existing[path.name] = {
                    "name": path.name, "state": "uploaded",
                    "size": path.stat().st_size, "digest": "sha256:" + publish_engine.sha256(path),
                }
                self.release["assets"] = list(existing.values())
        else:
            self.assertEqual(args, ("release", "edit", self.tag, "--repo", self.repo, "--draft=false"))
            self.release["isDraft"] = False

    def publish(self):
        publish_engine.publish(self.repo, self.tag, self.sha, self.directory)

    def test_resume_after_interrupted_upload_and_completed_publication(self):
        self.fail_upload_after = 3
        with self.assertRaisesRegex(RuntimeError, "interrupted"):
            self.publish()
        self.assertEqual(3, len(self.release["assets"]))
        self.fail_upload_after = None
        self.commands.clear()
        self.publish()
        self.assertEqual(["upload", "edit"], [args[1] for args in self.commands])
        uploads = self.commands[0]
        self.assertEqual(4 + len(publish_engine.SDK_PLATFORMS), len(uploads[uploads.index("--clobber") + 1:]))
        self.assertEqual(7 + len(publish_engine.SDK_PLATFORMS), len(self.release["assets"]))
        self.commands.clear()
        self.publish()
        self.assertEqual([], self.commands)

    def test_existing_tag_with_no_release(self):
        self.commit = self.sha
        self.publish()
        self.assertEqual(["create", "upload", "edit"], [args[1] for args in self.commands])

    def test_refuse_tag_pointing_to_another_commit(self):
        self.commit = "b" * 40
        with self.assertRaisesRegex(ValueError, "points to"):
            self.publish()
        self.assertEqual([], self.commands)

    def test_repair_only_draft_assets_with_different_digest(self):
        self.publish()
        self.release["assets"][0]["digest"] = "sha256:" + "0" * 64
        self.commands.clear()
        with self.assertRaisesRegex(ValueError, "Published release"):
            self.publish()
        self.assertEqual([], self.commands)
        self.release["isDraft"] = True
        self.publish()
        upload = self.commands[0]
        self.assertEqual(1, len(upload[upload.index("--clobber") + 1:]))
        self.assertFalse(self.release["isDraft"])

    def test_all_archives_required_before_creating_tag(self):
        next(self.directory.iterdir()).unlink()
        with self.assertRaisesRegex(ValueError, "Missing archive"):
            self.publish()
        self.assertEqual([], self.commands)


if __name__ == "__main__":
    unittest.main()
