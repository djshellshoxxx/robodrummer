#!/usr/bin/env bash
# Resolve release inputs (workflow_dispatch inputs or a pushed v* tag) into job outputs.
# Env: EVENT_NAME REF_NAME IN_VERSION IN_PRERELEASE IN_PLATFORMS IN_QA GITHUB_OUTPUT
set -euo pipefail

if [ "${EVENT_NAME:-}" = "push" ]; then
  version="${REF_NAME#v}"
  platforms="all"
  qa="false"
  case "$version" in *-*) prerelease="true" ;; *) prerelease="false" ;; esac
else
  version="${IN_VERSION:-}"
  platforms="${IN_PLATFORMS:-all}"
  prerelease="${IN_PRERELEASE:-true}"
  qa="${IN_QA:-false}"
fi

if ! printf '%s' "$version" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$'; then
  echo "::error::Invalid version '$version' (expected e.g. 0.4.0 or 0.4.0-beta.1)"
  exit 1
fi

platforms="$(printf '%s' "$platforms" | tr '[:upper:]' '[:lower:]' | tr -d ' ')"
if [ -z "$platforms" ]; then platforms="all"; fi

# Reject unknown tokens so typos do not silently build nothing.
if [ "$platforms" != "all" ]; then
  IFS=',' read -ra toks <<< "$platforms"
  for t in "${toks[@]}"; do
    case "$t" in
      windows|mac|mac-arm64|mac-x64|linux) ;;
      *) echo "::error::Unknown platform '$t' (use all or a list of: windows,mac-arm64,mac-x64,linux)"; exit 1 ;;
    esac
  done
fi

want() { # want <token>: prints true/false
  if [ "$platforms" = "all" ]; then echo true; return; fi
  case ",$platforms," in *",$1,"*) echo true; return ;; esac
  case "$1" in
    mac-arm64|mac-x64) case ",$platforms," in *",mac,"*) echo true; return ;; esac ;;
  esac
  echo false
}

windows="$(want windows)"
mac_arm64="$(want mac-arm64)"
mac_x64="$(want mac-x64)"
linux="$(want linux)"

if [ "$windows$mac_arm64$mac_x64$linux" = "falsefalsefalsefalse" ]; then
  echo "::error::No platforms selected"
  exit 1
fi

base="${version%%-*}"
pretag=""
case "$version" in *-*) pretag="${version#*-}" ;; esac
expected=""
if [ "$windows" = true ]; then expected="$expected windows-x64"; fi
if [ "$mac_arm64" = true ]; then expected="$expected mac-arm64"; fi
if [ "$mac_x64" = true ]; then expected="$expected mac-x64"; fi
if [ "$linux" = true ]; then expected="$expected linux-x64"; fi
expected="${expected# }"

{
  echo "version=$version"
  echo "base_version=$base"
  echo "pretag=$pretag"
  echo "tag=v$version"
  echo "prerelease=$prerelease"
  echo "run_qa=$qa"
  echo "windows=$windows"
  echo "mac_arm64=$mac_arm64"
  echo "mac_x64=$mac_x64"
  echo "linux=$linux"
  echo "expected=$expected"
} >> "${GITHUB_OUTPUT:-/dev/stdout}"
echo "Plan: v$version prerelease=$prerelease platforms=[$expected] qa=$qa"
