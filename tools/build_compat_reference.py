"""Compare exact default audio/MIDI with a pinned pre-0.4 integration source."""

import argparse
import filecmp
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile


BASELINE_COMMIT = "102dc33f6bc3202a114a28bb0482e24138b18f8d"
ROOT = Path(__file__).resolve().parents[1]


def run(arguments, **kwargs):
    return subprocess.run(arguments, check=True, **kwargs)


def write_changed(path, content):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != content:
        path.write_bytes(content)


def declaration(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError(f"Unbalanced fixture declaration: {marker}")
    return source[start:end] + (";" if marker.startswith("struct ") else "")


def canonical_renderer():
    source = (ROOT / "tests/harness/PluginHarness.cpp").read_text(encoding="utf-8")
    rig = source[source.index("struct Rig {"):source.index("    // Runs numBlocks")]
    declarations = [
        declaration(source, "struct TestPlayHead"), rig + "};",
        declaration(source, "float pluck("), declaration(source, "struct Fnv"),
        declaration(source, "std::uint64_t renderBaselineHash("),
    ]
    return '\n'.join([
        '#include "plugin/PluginProcessor.h"', '#include <functional>', '#include <cmath>',
        '#include <cstdio>', '#include <cstdint>', '#include <array>',
        'constexpr double kSampleRate = 48000.0;', 'constexpr int kBlock = 512;',
        *declarations,
        '''int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc != 2) return 2;
    juce::FileOutputStream capture(juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]));
    if (!capture.openedOk() || !capture.setPosition(0) || capture.truncate().failed()) return 3;
    const auto hash = renderBaselineHash(&capture);
    capture.flush();
    std::printf("historical baseline %016llx\\n", static_cast<unsigned long long>(hash));
    return capture.getStatus().wasOk() ? 0 : 3;
}''',
    ])


def cmake_quote(path):
    value = Path(path).as_posix()
    if "]==]" in value:
        raise ValueError("Unsupported CMake path")
    return "[==[" + value + "]==]"


def cache_values(path):
    result = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.match(r"([^/#][^:]*):[^=]+=(.*)", line)
        if match:
            result[match[1]] = match[2]
    return result


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="Current configured and built JUCE tree")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--config", default="Release", choices=("Release", "Debug", "RelWithDebInfo", "MinSizeRel"))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("jobs must be positive")
    build = args.build.resolve()
    work = build / "compat-reference"
    historical, legacy_build = work / "source", work / "build"
    work.mkdir(parents=True, exist_ok=True)
    present = subprocess.run(
        ["git", "cat-file", "-e", BASELINE_COMMIT + "^{commit}"], cwd=ROOT, capture_output=True,
    )
    if present.returncode:
        run(["git", "fetch", "--no-tags", "--depth=1", "origin", BASELINE_COMMIT], cwd=ROOT)
    archive = run(
        ["git", "archive", BASELINE_COMMIT, "CMakeLists.txt", "src", "tests"],
        cwd=ROOT, capture_output=True,
    ).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as contents:
        for entry in contents:
            path = Path(entry.name)
            if path.is_absolute() or ".." in path.parts or not (entry.isdir() or entry.isfile()):
                raise ValueError("Unsafe historical archive member")
            if entry.isfile():
                write_changed(historical / path, contents.extractfile(entry).read())
    renderer = work / "CanonicalBaseline.cpp"
    write_changed(renderer, canonical_renderer().encode())
    pins = lambda text: re.findall(r"GIT_REPOSITORY\s+(\S+)\s+GIT_TAG\s+(\S+)", text)
    if pins((ROOT / "CMakeLists.txt").read_text(encoding="utf-8")) != pins((historical / "CMakeLists.txt").read_text(encoding="utf-8")):
        raise ValueError("Framework pins differ; historical comparison requires a separate SDK qualification")
    hook = work / "Reference.cmake"
    write_changed(hook, f'''
function(_rd_add_compat_reference)
  if(MSVC)
    target_compile_options(robodrummer_core INTERFACE /fp:precise)
  else()
    target_compile_options(robodrummer_core INTERFACE -ffp-contract=off)
  endif()
  juce_add_console_app(RoboDrummerBaselineReference PRODUCT_NAME "RoboDrummerBaselineReference")
  juce_generate_juce_header(RoboDrummerBaselineReference)
  target_sources(RoboDrummerBaselineReference PRIVATE {cmake_quote(renderer)})
  target_include_directories(RoboDrummerBaselineReference PRIVATE {cmake_quote(historical / 'src')})
  target_compile_definitions(RoboDrummerBaselineReference PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(RoboDrummerBaselineReference PRIVATE robodrummer_core RoboDrummer juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endfunction()
cmake_language(DEFER CALL _rd_add_compat_reference)
'''.encode())
    cache = cache_values(build / "CMakeCache.txt")
    command = [
        "cmake", "-S", str(historical), "-B", str(legacy_build), "-G", cache["CMAKE_GENERATOR"],
        "-DCMAKE_BUILD_TYPE=" + args.config, "-DROBODRUMMER_BUILD_JUCE=ON", "-DROBODRUMMER_BUILD_CLAP=ON",
        "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=" + str(hook),
    ]
    for name in ("CMAKE_GENERATOR_PLATFORM", "CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_C_FLAGS", "CMAKE_CXX_FLAGS", "CMAKE_C_FLAGS_RELEASE", "CMAKE_CXX_FLAGS_RELEASE", "CMAKE_OSX_ARCHITECTURES", "CMAKE_OSX_DEPLOYMENT_TARGET", "CMAKE_TOOLCHAIN_FILE", "CMAKE_MSVC_RUNTIME_LIBRARY"):
        if cache.get(name):
            command.append("-D" + name + "=" + cache[name])
    # Reuse exact pinned dependency sources, never current application objects.
    for name, content in (("juce", "JUCE"), ("clap_juce_extensions", "CLAP_JUCE_EXTENSIONS")):
        source = cache.get(name + "_SOURCE_DIR") or cache.get(name.upper() + "_SOURCE_DIR")
        if source:
            command.append("-DFETCHCONTENT_SOURCE_DIR_" + content + "=" + source)
    run(command)
    run(["cmake", "--build", str(legacy_build), "--config", args.config, "--target", "RoboDrummerBaselineReference", "--parallel", str(args.jobs)])
    suffix = ".exe" if __import__("os").name == "nt" else ""
    reference = legacy_build / ("RoboDrummerBaselineReference_artefacts/" + args.config + "/RoboDrummerBaselineReference" + suffix)
    current = build / ("RoboDrummerHarness_artefacts/" + args.config + "/RoboDrummerHarness" + suffix)
    old_runs = [work / "historical-1.bin", work / "historical-2.bin"]
    new_runs = [work / "current-1.bin", work / "current-2.bin"]
    for output in old_runs:
        run([str(reference), str(output)])
    for output in new_runs:
        run([str(current), "--export-baseline", str(output)])
    comparisons = {
        "historical_deterministic": filecmp.cmp(*old_runs, shallow=False),
        "current_deterministic": filecmp.cmp(*new_runs, shallow=False),
        "exact_default_audio_midi_match": filecmp.cmp(old_runs[0], new_runs[0], shallow=False),
    }
    report = {
        "historical_commit": BASELINE_COMMIT, "canonical_renderer_sha256": digest(renderer),
        "captures": {path.name: {"sha256": digest(path), "bytes": path.stat().st_size} for path in old_runs + new_runs},
        **comparisons,
    }
    (build / "qa").mkdir(exist_ok=True)
    (build / "qa/compatibility.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if not all(comparisons.values()) or old_runs[0].stat().st_size == 0:
        raise RuntimeError("Historical/current default output comparison failed; see qa/compatibility.json")
    shutil.copyfile(old_runs[0], work / "reference.bin")
    print("Verified exact same-toolchain default audio/MIDI compatibility", flush=True)


if __name__ == "__main__":
    main()
