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
    sedi "s|^set(VERSION_DATE \".*\")|set(VERSION_DATE \"$(date +%d/%m/%Y)\")|" "$CMAKE_FILE"
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

# Create a PR branch, commit, push, open PR, wait for merge, then
# tag the merge commit on main.
create_release_pr() {
    local version="$1"
    local tag="v${version}"
    local branch="release-${version}"

    git checkout -b "$branch"
    git add "$CMAKE_FILE"
    git commit -m "chore(release): ${version}"
    git push -u origin "$branch"

    echo ""
    echo "Creating PR for ${version}..."
    local pr_url
    pr_url=$(gh pr create \
        --base main \
        --head "$branch" \
        --title "chore(release): ${version}" \
        --body "Release ${version}. Merge this PR, then the tag will be created automatically.")

    echo "PR created: $pr_url"

    # Enable auto-merge so the PR merges as soon as checks pass
    gh pr merge "$branch" --auto --squash || true

    echo ""
    echo "Waiting for PR to be merged..."

    # Poll until merged (check every 10s, timeout after 30min)
    local elapsed=0
    while [ $elapsed -lt 1800 ]; do
        local state
        state=$(gh pr view "$branch" --json state -q '.state' 2>/dev/null || echo "UNKNOWN")
        if [ "$state" = "MERGED" ]; then
            echo "PR merged!"
            break
        elif [ "$state" = "CLOSED" ]; then
            echo "Error: PR was closed without merging. Aborting."
            git checkout main
            git branch -D "$branch" 2>/dev/null || true
            exit 1
        fi
        sleep 10
        elapsed=$((elapsed + 10))
    done

    if [ $elapsed -ge 1800 ]; then
        echo "Timeout waiting for PR merge. Tag manually with:"
        echo "  merge_sha=\$(gh pr view \"$branch\" --json mergeCommit -q '.mergeCommit.oid')"
        echo "  git tag ${tag} \"\$merge_sha\" && git push origin ${tag}"
        git checkout main
        git branch -D "$branch" 2>/dev/null || true
        exit 1
    fi

    # Tag the exact merge commit, not whatever HEAD happens to be
    local merge_sha
    merge_sha=$(gh pr view "$branch" --json mergeCommit -q '.mergeCommit.oid')
    git checkout main
    git pull origin main
    git tag "$tag" "$merge_sha"
    git push origin "$tag"
    git branch -D "$branch" 2>/dev/null || true
    echo "Tagged and pushed ${tag}"
}

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

do_release() {
    ensure_clean
    ensure_on_main

    local version
    version=$(get_version)

    echo "Current version: $version"
    confirm "Tag and release $version?"

    check_changelog "$version"
    # The release PR's commit needs an actual diff to commit -- major/minor/
    # patch are already at $version (no suffix to strip, unlike Cargo's
    # -dev/-beta.N), so without this, create_release_pr's `git commit` fails
    # with "nothing to commit". Restamping VERSION_DATE to today gives it
    # one, and correctly marks the release date on days it actually differs
    # from whenever the version number was bumped.
    set_version "$version"
    create_release_pr "$version"

    echo ""
    echo "Bumping to next patch version for continued development..."
    local major minor patch
    major=$(parse_major "$version")
    minor=$(parse_minor "$version")
    patch=$(parse_patch "$version")
    local next="${major}.${minor}.$((patch + 1))"

    git checkout main
    git pull origin main
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
                  • creates a PR, waits for merge, tags the result
                  • the tag push triggers release.yml (GitHub Release +
                    changelog) and build.yml's Cloudsmith PROD upload
                  • opens a follow-up PR to bump to the next patch version

  --major       Bump major version (N+1.0.0) via PR
  --minor       Bump minor version (x.N+1.0) via PR
  --patch       Bump patch version (x.y.N+1) via PR

Workflow:
  1. ./release.sh --minor       # PR: 0.1.0 → 0.2.0
  2. (development happens)
  3. ./release.sh --release     # PR, tag v0.2.0, PR: 0.2.1

Unlike mayara-server, there's no --beta command: this plugin's Cloudsmith
alpha/beta/prod routing is driven by branch + tag state (see
cmake/in-files/cloudsmith-upload.sh.in), not by a version-string suffix, so
a beta release is just: tag from a non-main branch instead of main.

Requires: gh CLI (authenticated)
EOF
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "${1:-}" in
    --release)  do_release ;;
    --major)    do_bump major ;;
    --minor)    do_bump minor ;;
    --patch)    do_bump patch ;;
    *)          do_help ;;
esac
