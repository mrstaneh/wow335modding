#!/usr/bin/env bash
# Interface mods, kept as a diff against the original client files so no Blizzard files land in git.
# Run from the repo root in Git Bash:
#   Mods/mods.sh extract 'Interface\GlueXML\GlueStrings.lua'   copy an original into build/mods/work to edit
#   Mods/mods.sh save                                          record your edits in build/mods/work as Mods/interface.patch
#   Mods/mods.sh build                                         originals + Mods/interface.patch + generated maps -> Data/<locale>/patch-<locale>-4.MPQ
#   Mods/mods.sh server                                        install the generated maps on the AzerothCore server (after build)
# Env: DATA (default bin/Win/Data), LOCALE (default enUS), AC (AzerothCore bin folder).
set -euo pipefail

DATA=${DATA:-bin/Win/Data}
LOCALE=${LOCALE:-enUS}
AC=${AC:-../azerothcore-wotlk/build/bin/RelWithDebInfo}
MAP_IDS="800" # keep in sync with Tools/MpqTool/mapgen.cpp
TOOL=build/MpqTool/Release/MpqTool.exe
MAPGEN=build/MpqTool/Release/MapGen.exe
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
    case "$rel" in
    *.lua | *.xml | *.toc | *.txt) sed -b -i 's/\r$//' "$WORK/orig/$rel" ;; # LF, so the patch is plain text in git
    esac
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
    # Generated files go into a copy, so work only ever holds hand edits
    extract 'DBFilesClient\Map.dbc'
    rm -rf "$WORK/pack"
    cp -r "$WORK/work" "$WORK/pack"
    "$MAPGEN" "$WORK/pack" "$WORK/orig/DBFilesClient/Map.dbc"
    rm -f "$WORK/patch.MPQ"
    "$TOOL" pack "$WORK/patch.MPQ" "$WORK/pack"
    cp "$WORK/patch.MPQ" "$DATA/$LOCALE/patch-$LOCALE-4.MPQ"
    echo "installed $DATA/$LOCALE/patch-$LOCALE-4.MPQ"
    ;;
server)
    [ -f "$WORK/pack/DBFilesClient/Map.dbc" ] || { echo "run '$0 build' first"; exit 1; }
    [ -e "$AC/data/dbc/Map.dbc.orig" ] || cp "$AC/data/dbc/Map.dbc" "$AC/data/dbc/Map.dbc.orig"
    cp "$WORK/pack/DBFilesClient/Map.dbc" "$AC/data/dbc/Map.dbc"
    # Terrain (maps) and collision of buildings and doodads (vmaps), only for our maps (-m is our extractor patch)
    for id in $MAP_IDS; do
        rm -rf "$WORK/extract"
        mkdir -p "$WORK/extract/vmaps"
        "$AC/map_extractor.exe" -i "$(dirname "$DATA")" -o "$WORK/extract" -e 1 -m "$id" > "$WORK/extract.log"
        cp "$WORK/extract/maps/$id"*.map "$AC/data/maps/"
        # the vmap tools write into the current directory, so they get absolute paths
        ac=$(realpath "$AC") data=$(realpath "$DATA")
        (cd "$WORK/extract" && "$ac/vmap4_extractor.exe" -d "$data" -m "$id" > vmap.log && "$ac/vmap4_assembler.exe" Buildings vmaps > assemble.log)
        cp "$WORK/extract/vmaps/"* "$AC/data/vmaps/"
    done
    for sql in Mods/sql/*.sql; do
        docker exec -i -e MYSQL_PWD=acore ac-mysql mysql -uacore acore_world < "$sql"
    done
    echo "installed maps $MAP_IDS and Mods/sql on the server; restart worldserver to load them"
    ;;
*)
    sed -n '2,9p' "$0"
    exit 1
    ;;
esac
