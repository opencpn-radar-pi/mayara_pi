#!/usr/bin/env bash
set -euo pipefail

CMAKE_FILE="CMakeLists.txt"

if ! command -v gh &>/dev/null; then
    echo "Error: gh CLI not found. Install from https://cli.github.com/"
    exit 1
fi
if ! gh auth status &>/dev/null; then
    echo "Error: gh is not authenticated. Run 'gh auth login' first."
    exit 1
fi

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Portable in-place sed (macOS vs GNU)
sedi() {
    if sed --version &>/dev/null; then
        sed -i "$@"    # GNU
    else
        sed -i '' "$@" # BSD
    fi
}

get_version() {
    local major minor patch
    major=$(grep '^set(VERSION_MAJOR ' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')
    minor=$(grep '^set(VERSION_MINOR ' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')
    patch=$(grep '^set(VERSION_PATCH ' "$CMAKE_FILE" | sed 's/.*"\(.*\)".*/\1/')
    echo "${major}.${minor}.${patch}"
}

set_version() {
    local new="$1"
    local major minor patch
    major=$(parse_major "$new")
    minor=$(parse_minor "$new")
    patch=$(parse_patch "$new")
    sedi "s/^set(VERSION_MAJOR \".*\")/set(VERSION_MAJOR \"${major}\")/" "$CMAKE_FILE"
    sedi "s/^set(VERSION_MINOR \".*\")/set(VERSION_MINOR \"${minor}\")/" "$CMAKE_FILE"
    sedi "s/^set(VERSION_PATCH \".*\")/set(VERSION_PATCH \"${patch}\")/" "$CMAKE_FILE"
    echo "Version set to $new"
}

parse_major() { echo "${1%%.*}"; }
parse_minor() { local tmp="${1#*.}"; echo "${tmp%%.*}"; }
parse_patch() { echo "${1##*.}"; }

confirm() {
    local prompt="$1"
    read -r -p "$prompt [type 'yes' to continue]: " answer
    if [ "$answer" != "yes" ]; then
        echo "Aborted."
        exit 1
    fi
}

ensure_clean() {
    if [ -n "$(git status --porcelain)" ]; then
        echo "Error: working tree is not clean. Commit or stash changes first."
        exit 1
    fi
}

ensure_on_main() {
    local branch
    branch=$(git branch --show-current)
    if [ "$branch" != "main" ]; then
        echo "Error: must be on 'main' branch (currently on '$branch')."
        exit 1
    fi
}

check_changelog() {
    local version="$1"
    if [ -f CHANGELOG.md ] && grep -q "## \[Unreleased\]" CHANGELOG.md; then
        echo "Note: CHANGELOG.md [Unreleased] section exists."
        echo "      CI (git-cliff) will generate the [${version}] entry on tag push."
    fi
}

# Tag main's current tip directly -- no branch, no PR, nothing to merge.
# There used to be a release commit here (a version-bump PR, then a wait for
# its squash-merge SHA to tag), but major/minor/patch are already correct by
# release time (the *previous* release's "begin next dev cycle" bump set
# them), so it never had a real diff to commit beyond a VERSION_DATE stamp
# nobody read. That indirection was also the actual bug behind v0.1.2/0.1.3/
# 0.1.4: whatever it tagged was not reliably the commit that landed on main.
tag_release() {
    local version="$1"
    local tag="v${version}"

    git tag "$tag" HEAD
    git push origin "$tag"
    echo "Tagged and pushed ${tag} at $(git rev-parse --short HEAD)"
}

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

do_release() {
    ensure_clean
    ensure_on_main
    git pull --ff-only origin main

    local version
    version=$(get_version)

    echo "Current version: $version"
    confirm "Tag and release $version?"

    check_changelog "$version"
    tag_release "$version"

    echo ""
    echo "Bumping to next patch version for continued development..."
    local major minor patch
    major=$(parse_major "$version")
    minor=$(parse_minor "$version")
    patch=$(parse_patch "$version")
    local next="${major}.${minor}.$((patch + 1))"

    set_version "$next"

    local branch="post-release-${next}"
    git checkout -b "$branch"
    git add "$CMAKE_FILE"
    git commit -m "chore(release): begin ${next}"
    git push -u origin "$branch"

    gh pr create \
        --base main \
        --head "$branch" \
        --title "chore(release): begin ${next}" \
        --body "Bump version to ${next} for next development cycle."

    git checkout main
    echo ""
    echo "Development version PR created for $next."
    echo "Merge it to continue development."
}

do_beta() {
    ensure_clean
    ensure_on_main
    git pull --ff-only origin main
    git fetch --tags --quiet

    local version
    version=$(get_version)

    # Find next beta number from existing tags for this version.
    local last_beta
    last_beta=$(git tag -l "v${version}-beta.*" \
        | sed "s/^v${version}-beta\.//" \
        | awk '/^[0-9]+$/' \
        | sort -n | tail -1)
    local next_beta
    if [ -z "$last_beta" ]; then
        next_beta=1
    else
        next_beta=$((last_beta + 1))
    fi
    local beta_tag="v${version}-beta.${next_beta}"

    echo "Current version: $version"
    echo "Beta tag:        $beta_tag"
    confirm "Tag and publish beta ${beta_tag}?"

    # Unlike --release, this does not touch CMakeLists.txt or open a PR:
    # CMake's numeric-only VERSION_MAJOR/MINOR/PATCH/TWEAK can't carry a
    # "-beta.N" suffix, so the plugin's own version stays ${version}
    # (unreleased) straight through to the real --release. The beta tag
    # alone is what routes this build to the beta Cloudsmith channel (see
    # cmake/in-files/cloudsmith-upload.sh.in and build.yml) and skips the
    # changelog boundary (see cliff.toml's skip_tags / release.yml).
    git tag "$beta_tag" HEAD
    git push origin "$beta_tag"
    echo "Tagged and pushed ${beta_tag} at $(git rev-parse --short HEAD)"
}

do_bump() {
    local part="$1"

    ensure_clean
    ensure_on_main

    local current
    current=$(get_version)
    local major minor patch
    major=$(parse_major "$current")
    minor=$(parse_minor "$current")
    patch=$(parse_patch "$current")

    case "$part" in
        major)
            major=$((major + 1))
            minor=0
            patch=0
            ;;
        minor)
            minor=$((minor + 1))
            patch=0
            ;;
        patch)
            patch=$((patch + 1))
            ;;
    esac

    local new_version="${major}.${minor}.${patch}"
    echo "Current version: $current"
    echo "New version:     $new_version"
    confirm "Bump to $new_version?"

    set_version "$new_version"

    local branch="bump-${new_version}"
    git checkout -b "$branch"
    git add "$CMAKE_FILE"
    git commit -m "chore(release): bump to ${new_version}"
    git push -u origin "$branch"

    gh pr create \
        --base main \
        --head "$branch" \
        --title "chore(release): bump to ${new_version}" \
        --body "Bump version to ${new_version}."

    git checkout main
    echo ""
    echo "PR created for version bump to $new_version. Merge it to apply."
}

do_help() {
    local current
    current=$(get_version)
    cat <<EOF
release.sh — version management for mayara_pi (PR-based flow)

Current version: $current

Commands:
  --release     Release the current version:
                  • tags main's current tip directly (no PR -- major/minor/
                    patch are already correct from the last version-bump PR,
                    so there is nothing to commit at release time)
                  • the tag push triggers release.yml (GitHub Release +
                    changelog) and build.yml's Cloudsmith PROD upload
                  • opens a follow-up PR to bump to the next patch version

  --beta        Cut a beta pre-release of the current version:
                  • tags main's current tip directly as
                    vMAJOR.MINOR.PATCH-beta.N (N auto-incremented)
                  • no CMakeLists.txt change, no PR: CMake's numeric-only
                    version fields can't carry a "-beta.N" suffix, so the
                    plugin's own version stays what it is until --release
                  • the tag push triggers release.yml (GitHub Release,
                    marked prerelease, no CHANGELOG.md change) and
                    build.yml's Cloudsmith BETA upload

  --major       Bump major version (N+1.0.0) via PR
  --minor       Bump minor version (x.N+1.0) via PR
  --patch       Bump patch version (x.y.N+1) via PR

Workflow:
  1. ./release.sh --minor       # PR: 0.1.0 → 0.2.0
  2. (development happens)
  3. ./release.sh --beta        # tag v0.2.0-beta.1 (repeatable)
  4. ./release.sh --release     # tag v0.2.0, PR: 0.2.1

Cloudsmith alpha/beta/prod routing (see cmake/in-files/cloudsmith-upload.sh.in
and build.yml) is driven by the tag string, not by branch: no tag is alpha,
a tag containing "-beta." is beta, any other tag is prod. release.sh always
tags main's tip directly for both --release and --beta, so every tag sits
"on main" and branch/ancestry can no longer tell them apart.

Requires: gh CLI (authenticated)
EOF
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "${1:-}" in
    --release)  do_release ;;
    --beta)     do_beta ;;
    --major)    do_bump major ;;
    --minor)    do_bump minor ;;
    --patch)    do_bump patch ;;
    *)          do_help ;;
esac
