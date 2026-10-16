#!/usr/bin/env bash
# Interface mods, kept as a diff against the original client files so no Blizzard files land in git.
# Run from the repo root in Git Bash:
#   Mods/mods.sh extract 'Interface\GlueXML\GlueStrings.lua'   copy an original into build/mods/work to edit
#   Mods/mods.sh save                                          record your edits in build/mods/work as Mods/interface.patch
#   Mods/mods.sh build                                         originals + Mods/interface.patch -> Data/<locale>/patch-<locale>-4.MPQ
# Env: DATA (default bin/Win/Data), LOCALE (default enUS).
set -euo pipefail

DATA=${DATA:-bin/Win/Data}
LOCALE=${LOCALE:-enUS}
TOOL=build/MpqTool/Release/MpqTool.exe
PATCH=Mods/interface.patch
WORK=build/mods
# ponytail: only the locale MPQ chain, which holds GlueXML/FrameXML; add the base chain to mod models, DBCs, etc.
CHAIN=("$DATA/$LOCALE/locale-$LOCALE.MPQ" "$DATA/$LOCALE/patch-$LOCALE.MPQ" "$DATA/$LOCALE/patch-$LOCALE-2.MPQ" "$DATA/$LOCALE/patch-$LOCALE-3.MPQ")

[ -x "$TOOL" ] || { echo "build MpqTool first, see Tools/MpqTool/CMakeLists.txt"; exit 1; }

# $1 = path inside the MPQ, e.g. Interface\GlueXML\GlueStrings.lua
extract() {
    local rel=${1//\\//}
    "$TOOL" extract "$WORK/orig" "$1" "${CHAIN[@]}" > /dev/null
    [ -f "$WORK/orig/$rel" ] || { echo "$1 is not in the $LOCALE MPQs"; exit 1; }
    sed -b -i 's/\r$//' "$WORK/orig/$rel" # LF, so the patch is plain text in git
    mkdir -p "$(dirname "$WORK/work/$rel")"
    cp "$WORK/orig/$rel" "$WORK/work/$rel"
}

# The edits in work as a unified diff, without timestamps so the patch only changes when the edits do.
changes() {
    (cd "$WORK" && diff -ruN orig work || [ $? -eq 1 ]) | sed -E 's/^((---|\+\+\+) [^\t]*)\t.*/\1/'
}

case "${1:-}" in
extract)
    [ $# -eq 2 ] || { echo "usage: $0 extract 'Interface\\...\\File.lua'"; exit 1; }
    [ ! -e "$WORK/work/${2//\\//}" ] || { echo "already in $WORK/work"; exit 1; }
    extract "$2"
    echo "edit $WORK/work/${2//\\//}, then run: $0 save"
    ;;
save)
    changes > "$PATCH"
    echo "saved $PATCH ($(grep -c '^+++ ' "$PATCH") files changed)"
    ;;
build)
    if [ -d "$WORK/work" ] && ! cmp -s <(changes) "$PATCH"; then
        echo "$WORK/work has edits that are not in $PATCH; run '$0 save' first"
        exit 1
    fi
    rm -rf "$WORK/orig" "$WORK/work"
    for rel in $(sed -n 's|^+++ work/||p' "$PATCH"); do
        extract "${rel//\//\\}"
    done
    patch -s -p1 -d "$WORK/work" < "$PATCH"
    rm -f "$WORK/patch.MPQ"
    "$TOOL" pack "$WORK/patch.MPQ" "$WORK/work"
    cp "$WORK/patch.MPQ" "$DATA/$LOCALE/patch-$LOCALE-4.MPQ"
    echo "installed $DATA/$LOCALE/patch-$LOCALE-4.MPQ"
    ;;
*)
    sed -n '2,8p' "$0"
    exit 1
    ;;
esac
