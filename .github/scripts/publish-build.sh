#!/usr/bin/env bash
# Packages this run's Windows build and replaces the branch's rolling GitHub
# release (tag build-<branch>). Called by the "publish" job in build.yml.
#
# Environment:
#   BRANCH, GITHUB_SHA, GITHUB_REPOSITORY  set by the workflow
#   GH_TOKEN                               token with contents: write
#   DIST     where the artifacts were downloaded (default: dist)
#   DRY_RUN  1 = print the gh commands instead of running them
set -euo pipefail

dist="${DIST:-dist}"
branch="${BRANCH:?BRANCH is required}"
sha="${GITHUB_SHA:?GITHUB_SHA is required}"
repo="${GITHUB_REPOSITORY:?GITHUB_REPOSITORY is required}"
tag="build-${branch}"
short="$(git rev-parse --short "$sha")"
date="$(git show -s --format=%cs "$sha")"

exe="$(find "$dist" -name FamiTracker.exe -print -quit)"
pdb="$(find "$dist" -name FamiTracker.pdb -print -quit)"
wav2ftm="$(find "$dist" -name wav2ftm.exe -print -quit)"
bridge="$(find "$dist" -name famitracker-mcp.exe -print -quit)"
bridge_linux="$(find "$dist" -type f -name famitracker-mcp-linux -print -quit)"
nsf2ftm="$(find "$dist" -name nsf2ftm.exe -print -quit)"
if [ -z "$exe" ]; then
	echo "FamiTracker.exe not found under $dist" >&2
	exit 1
fi

work="$(mktemp -d)"
folder="FamiTracker-$branch"
mkdir -p "$work/$folder"
cp "$exe" "$work/$folder/"
contents="FamiTracker.exe"
if [ -n "$wav2ftm" ]; then
	cp "$wav2ftm" "$work/$folder/"
	contents="FamiTracker.exe and wav2ftm.exe"
fi
if [ -n "$bridge" ]; then
	cp "$bridge" "$work/$folder/"
	contents="${contents/ and /, } and famitracker-mcp.exe"
fi
if [ -n "$nsf2ftm" ]; then
	cp "$nsf2ftm" "$work/$folder/"
	contents="${contents/ and /, } and nsf2ftm.exe"
fi
platforms="for 32-bit Windows"
if [ -n "$bridge_linux" ]; then
	cp "$bridge_linux" "$work/$folder/"
	chmod +x "$work/$folder/famitracker-mcp-linux"
	platforms="for 32-bit Windows, and famitracker-mcp-linux for Linux (x86-64)"
fi

{
	echo "FamiTracker, $branch branch, build $short ($date)"
	echo
	echo "FamiTracker.exe   The tracker. 32-bit Windows, no installation needed."
	echo "                  New here? Help > Tutorials shows you around, and View >"
	echo "                  Profile makes FamiTracker simpler for writing game music."
	echo "                  File > Import NSF opens game music (NSF files) as modules."
	if [ -n "$wav2ftm" ]; then
		echo "                  The microphone button on the toolbar records a whistle,"
		echo "                  hum, song or finger taps straight into the song, Edit >"
		echo "                  Add chords and bass line fits a backing to a melody, and"
		echo "                  the clips panel beside the pattern editor keeps takes and"
		echo "                  selections as clips to drag into the song again, as"
		echo "                  copies or linked. The piano roll button shows the song's"
		echo "                  notes under the pattern editor, where notes are drawn,"
		echo "                  painted and moved with the mouse. Instrument > Instrument"
		echo "                  library has ready-made sounds to try, Edit > Drum grid"
		echo "                  puts drums in a bar at a time, and Edit > Sound effect"
		echo "                  from your voice turns a \"pew\" or a \"boing\" into a"
		echo "                  sound effect for a game."
	fi
	if [ -n "$wav2ftm" ]; then
		echo "wav2ftm.exe       Turns a WAV recording of a whistle, hum or finger taps"
		echo "                  into a FamiTracker text file. Run it from a command prompt"
		echo "                  (wav2ftm --help), then open the result in FamiTracker"
		echo "                  with File > Import Text."
	fi
	if [ -n "$bridge" ]; then
		echo "famitracker-mcp.exe"
		echo "                  Connects Claude Code running in WSL to FamiTracker. Keep"
		echo "                  it next to FamiTracker.exe; Claude Code starts it by itself."
	fi
	if [ -n "$nsf2ftm" ]; then
		echo "nsf2ftm.exe       Turns the tracks of an NSF file (game music) into a"
		echo "                  FamiTracker text file, or says how they're made (-study)."
		echo "                  FamiTracker's File > Import NSF does the same in a few"
		echo "                  clicks. Run nsf2ftm from a command prompt for its options."
	fi
	if [ -n "$bridge_linux" ]; then
		echo "famitracker-mcp-linux"
		echo "                  For Linux (x86-64): lets an MCP client on another computer"
		echo "                  of your home network that starts its servers over stdio"
		echo "                  use FamiTracker's tools: famitracker-mcp-linux --host"
		echo "                  <this PC's address> --token <access code>. Both show in"
		echo "                  File > Connect Claude Code once other computers are allowed."
	fi
	if [ -n "$bridge" ]; then
		echo
		echo "File > Connect Claude Code lets Claude Code on this computer, in Windows or"
		echo "in WSL, work on the open song: switch it on there and run the command it"
		echo "shows once. See MCP.md in the source for what Claude can do and how to ask."
		echo "Other computers on your home network (an AI assistant on a Linux PC, say)"
		echo "can connect too once you allow it there; a small set of the tools at"
		echo "/mcp/core suits local models."
		echo "Claude can study the tracks of NSF files you have (study_nsf) and keep what"
		echo "it learns in a reference library, Documents\\FamiTracker references, to lean"
		echo "on when it writes music for you; it also knows a guide to the classic NES"
		echo "styles."
		echo "File > Export for games saves tracks as OGG files that loop without a seam"
		echo "(jingles and sound effects play once), with the GameMaker code that plays"
		echo "them."
	fi
	echo
	echo "These programs are not code-signed. If Windows SmartScreen warns on first"
	echo "launch, choose \"More info\", then \"Run anyway\"."
	echo
	echo "Source: https://github.com/$repo/tree/$sha"
} | sed 's/$/\r/' > "$work/$folder/README.txt"

zip_main="$work/FamiTracker-$branch-win32.zip"
zip_symbols="$work/FamiTracker-$branch-win32-symbols.zip"
(cd "$work" && zip -q -9 -r "$zip_main" "$folder")
assets=("$zip_main")
if [ -n "$pdb" ]; then
	(cd "$(dirname "$pdb")" && zip -q -9 "$zip_symbols" "$(basename "$pdb")")
	assets+=("$zip_symbols")
fi

notes="$work/notes.md"
{
	echo "Automatic build of the \`$branch\` branch at $short ($date), replaced on every push to the branch."
	echo
	echo "- **$(basename "$zip_main")**: $contents $platforms. Unzip and run."
	if [ -n "$pdb" ]; then
		echo "- **$(basename "$zip_symbols")**: debug symbols, only needed to analyse crashes."
	fi
	echo
	echo "The programs are not code-signed, so Windows SmartScreen may warn on first launch: choose \"More info\", then \"Run anyway\"."
	echo
	echo "Recent changes:"
	git log -10 --format='- %s (%h)' "$sha"
} > "$notes"

flags=(--target "$sha" --title "Latest build: $branch" --notes-file "$notes")
if [ "$branch" = "beginner-friendly" ]; then
	flags+=(--latest)
else
	flags+=(--prerelease)
fi

run() {
	if [ "${DRY_RUN:-}" = 1 ]; then echo "+ $*"; else "$@"; fi
}
run gh release delete "$tag" --repo "$repo" --cleanup-tag --yes || true
run gh release create "$tag" --repo "$repo" "${flags[@]}" "${assets[@]}"
echo "Published $tag at $short: ${assets[*]##*/}"
