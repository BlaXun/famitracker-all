#!/bin/bash
# Removes what a feature or fix branch (feat-*, fix-*) leaves behind once it is merged into
# the branch this run built: its rolling pre-release and tag (build-feat-*,
# build-fix-*)
# and the branch itself. A feature counts as merged when the branch just built
# contains its commit. Called by the "publish" job in build.yml on next.
#
#   GITHUB_REPOSITORY, GITHUB_SHA  set by the workflow
#   GH_TOKEN                       the workflow's token (contents: write)
set -u
repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
head="${GITHUB_SHA:?GITHUB_SHA is required}"

# Whether commit $1 is in the history of the commit just built
merged() {
	local status
	status=$(gh api "repos/$repo/compare/$1...$head" --jq '.status' 2>/dev/null) || return 1
	[ "$status" = "ahead" ] || [ "$status" = "identical" ]
}

gh api --paginate "repos/$repo/releases?per_page=100" --jq '.[] | select(.tag_name | startswith("build-feat-") or startswith("build-fix-")) | .tag_name' |
while read -r tag; do
	branch="${tag#build-}"
	sha=$(gh api "repos/$repo/commits/$tag" --jq '.sha' 2>/dev/null) || continue
	if merged "$sha"; then
		echo "$branch is merged: removing its pre-release $tag"
		gh release delete "$tag" --repo "$repo" --cleanup-tag --yes || true
	fi
done

gh api --paginate "repos/$repo/branches?per_page=100" --jq '.[] | select(.name | startswith("feat-") or startswith("fix-")) | .name + " " + .commit.sha' |
while read -r branch sha; do
	if merged "$sha"; then
		echo "$branch is merged: deleting the branch"
		gh api -X DELETE "repos/$repo/git/refs/heads/$branch" || true
	fi
done
exit 0
