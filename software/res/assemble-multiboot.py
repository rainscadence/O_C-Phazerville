"""PlatformIO post-build adapter; freshness is enforced by build-t41-suite.py."""
Import("env")

import importlib.util
from pathlib import Path
import subprocess


def get_git_rev():
    revision = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode().strip()
    dirty = subprocess.check_output(["git", "status", "-s", "--untracked-files=no"]).decode()
    return revision + ("dirty" if dirty else "")


env.Append(BUILD_FLAGS=[f'-DOC_BUILD_TAG=\\"{get_git_rev()}\\"'])


def after_build(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    spec = importlib.util.spec_from_file_location("suite_image", project / "res/suite_image.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    build_root = Path(env.subst("$BUILD_DIR")).parent
    inputs = {profile: build_root / profile / "firmware.hex" for profile in module.SLOTS}
    tool = Path(env.PioPlatform().get_package_dir("tool-sreccat")) / "srec_cat"
    output = build_root / "T41-suite.hex"
    details = module.assemble(tool, inputs, output)
    print(f"Suite byte/slot validation PASS: {output} SHA256 {details['image']['sha256']}")
    print("Structural validation only; use the fresh-build receipt before a hardware test.")


env.AddPostAction("buildprog", after_build)
