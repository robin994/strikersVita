#!/bin/sh
# Check an executable depends only on what is present on another machine:
# tools/check-runtime-deps.sh <executable> [package dir]

set -e

BIN="${1:?usage: check-runtime-deps.sh <executable> [package dir]}"
PKGDIR="${2:-$(dirname "$BIN")}"

if [ ! -f "$BIN" ]; then
    echo "check-runtime-deps: no such file: $BIN" >&2
    exit 1
fi

echo "==> runtime dependencies of $BIN"
BAD=0

# Which branch to take is a question about the *file*, not about this machine.
KIND=$(uname -s)
if [ "$(head -c 2 "$BIN" 2>/dev/null)" = "MZ" ]; then
    KIND=Windows
fi

case "$KIND" in
Darwin)
    # Every load command of every Mach-O in the closure, resolved as dyld resolves it; a dylib
    # inside the package is queued for its own commands.
    EXE_DIR=$(cd "$(dirname "$BIN")" && pwd -P)
    PKG_REAL=$(cd "$PKGDIR" && pwd -P)

    # Load commands only: `otool -L` lists a dylib's own install name first, which is no dependency.
    # The file itself through every symlink: a dylib that is a symlink out of the archive depends on it.
    realfile() {
        if readlink -f / >/dev/null 2>&1; then
            readlink -f "$1"
        else
            python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$1"
        fi
    }

    deps_of() {
        otool -l "$1" | awk '
            /^ *cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LOAD_UPWARD)_DYLIB/ { f = 1; next }
            f && /^ *name / { sub(/^ *name /, ""); sub(/ \(offset [0-9]+\)$/, ""); print; f = 0 }'
    }
    rpaths_of() {
        otool -l "$1" | sed -n 's/^ *path \(.*\) (offset [0-9]*)$/\1/p'
    }
    # Where dyld would find "$2" when loading it for the image "$1"; nothing if nowhere.
    resolve() {
        image_dir=$(cd "$(dirname "$1")" && pwd -P)
        case "$2" in
        @executable_path/*) printf '%s\n' "$EXE_DIR/${2#@executable_path/}" ;;
        @loader_path/*)     printf '%s\n' "$image_dir/${2#@loader_path/}" ;;
        @rpath/*)
            rest=${2#@rpath/}
            rpaths_of "$1" | while IFS= read -r rp; do
                case "$rp" in
                @executable_path/*) rp="$EXE_DIR/${rp#@executable_path/}" ;;
                @loader_path/*)     rp="$image_dir/${rp#@loader_path/}" ;;
                esac
                if [ -f "$rp/$rest" ]; then
                    printf '%s\n' "$rp/$rest"
                    break
                fi
            done ;;
        esac
    }

    QUEUE=$(mktemp)
    SEEN=$(mktemp)
    DEPS=$(mktemp)
    printf '%s\n' "$BIN" > "$QUEUE"
    while [ -s "$QUEUE" ]; do
        image=$(head -n 1 "$QUEUE")
        sed -i '' '1d' "$QUEUE"
        echo "  load commands of $(basename "$image")"
        deps_of "$image" > "$DEPS"
        while IFS= read -r dep; do
            [ -n "$dep" ] || continue
            case "$dep" in
            /usr/lib/*|/System/Library/*)
                echo "    ok    $dep" ;;
            @*)
                target=$(resolve "$image" "$dep")
                if [ -z "$target" ] || [ ! -f "$target" ]; then
                    echo "    BAD   $dep: resolves to nothing the loader can open${target:+ ($target)}"
                    BAD=1
                    continue
                fi
                real=$(realfile "$target")
                case "$real" in
                "$PKG_REAL"/*)
                    echo "    ok    $dep (shipped: ${real#"$PKG_REAL"/})"
                    if ! grep -qxF "$real" "$SEEN"; then
                        echo "$real" >> "$SEEN"
                        echo "$real" >> "$QUEUE"
                    fi ;;
                *)
                    echo "    BAD   $dep: resolves outside the archive, to $real"
                    BAD=1 ;;
                esac ;;
            *)
                echo "    BAD   $dep"
                BAD=1 ;;
            esac
        done < "$DEPS"
    done
    rm -f "$QUEUE" "$SEEN" "$DEPS"
    ;;

Linux)
    NEEDED=$(mktemp)
    if command -v readelf >/dev/null 2>&1; then
        readelf -d "$BIN" 2>/dev/null |
            sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' > "$NEEDED"
    elif command -v objdump >/dev/null 2>&1; then
        objdump -p "$BIN" 2>/dev/null | awk '$1 == "NEEDED" { print $2 }' > "$NEEDED"
    else
        # Fail closed, for the reason the Windows branch fails closed: a check that reports "clean"
        # because its tool was missing is worse than no check at all.
        echo "==> FAILED: neither readelf nor objdump is on PATH, so the direct" >&2
        echo "    dependencies of $BIN could not be read" >&2
        rm -f "$NEEDED"
        exit 1
    fi

    DEPS=$(mktemp)
    if ! ldd "$BIN" > "$DEPS" 2>&1; then
        echo "    BAD   ldd could not inspect $BIN" >&2
        sed 's/^/          /' "$DEPS" >&2
        rm -f "$DEPS"
        exit 1
    fi
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        case "$line" in
        *"not found"*)
            echo "    BAD   $line"; BAD=1 ;;
        *"${HOME:-/nonexistent}"*|*/usr/local/*|*/opt/*|*linuxbrew*|*build-aur*)
            echo "    BAD   $line"; BAD=1 ;;
        *libzstd*|*libpng*|*libfreetype*|*libsqlite3*|*libavcodec*|*libavutil*)
            # Rejected wherever they live when the executable links them itself; libz is a system
            # copy by design.
            if grep -Fxq "$(printf '%s' "$line" | awk '{print $1}')" "$NEEDED"; then
                echo "    BAD   $line: should have been built in, not linked"
                BAD=1
            else
                echo "    ok   $line (indirect, not linked by the executable)"
            fi ;;
        *)
            echo "    ok   $line" ;;
        esac
    done < "$DEPS"
    rm -f "$DEPS"

    if ! command -v readelf >/dev/null 2>&1; then
        echo "==> FAILED: readelf is not on PATH, so the ELF checks could not run" >&2
        rm -f "$NEEDED"
        exit 1
    fi

    ALLOWED='libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libz.so.1
             ld-linux-x86-64.so.2 ld-linux-aarch64.so.1'
    case "$(basename "$BIN")" in
    strikers-settings) ALLOWED="$ALLOWED libQt6*.so.6 libstdc++.so.6 libgcc_s.so.1" ;;
    esac
    ALLOWED="$ALLOWED ${STRIKERS_EXTRA_NEEDED:-}"
    SHARED_CXX=0
    if grep -qx 'libstdc++.so.6' "$NEEDED"; then SHARED_CXX=1; fi
    while IFS= read -r lib; do
        [ -n "$lib" ] || continue
        allowed=0
        for pattern in $ALLOWED; do
            # shellcheck disable=SC2254
            case "$lib" in $pattern) allowed=1; break ;; esac
        done
        if [ "$allowed" = 1 ]; then
            echo "    ok    NEEDED $lib"
        else
            echo "    BAD   NEEDED $lib: not part of the base system; link it in, or load it with dlopen"
            BAD=1
        fi
    done < "$NEEDED"
    rm -f "$NEEDED"

    VERSIONS=$(readelf -V -W "$BIN" 2>/dev/null |
        grep -oE 'Name: (GLIBC|GLIBCXX|CXXABI)_[0-9][0-9.]*' | sed 's/^Name: //' | sort -u)
    for family in GLIBC GLIBCXX CXXABI; do
        newest=$(printf '%s\n' "$VERSIONS" | sed -n "s/^${family}_//p" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)
        [ -n "$newest" ] || continue
        case "$family" in
        GLIBC)   limit="${STRIKERS_MAX_GLIBC:-}" ;;
        GLIBCXX) limit="${STRIKERS_MAX_GLIBCXX:-}" ;;
        *)       limit="" ;;
        esac
        if [ -n "$limit" ] &&
           [ "$(printf '%s\n%s\n' "$newest" "$limit" | sort -t. -k1,1n -k2,2n -k3,3n | tail -n 1)" != "$limit" ]; then
            echo "    BAD   needs ${family}_$newest, newer than the ${family}_$limit this build promises"
            BAD=1
        else
            echo "    ok    needs ${family}_$newest${limit:+ (limit $limit)}"
        fi
    done

    STACK=$(readelf -lW "$BIN" 2>/dev/null | grep 'GNU_STACK' || true)
    case "$STACK" in
    "")    echo "    BAD   no GNU_STACK header, which the loader reads as an executable stack"; BAD=1 ;;
    *RWE*) echo "    BAD   executable stack (GNU_STACK RWE); link with -Wl,-z,noexecstack"; BAD=1 ;;
    *)     echo "    ok    stack not executable" ;;
    esac

    if readelf -d "$BIN" 2>/dev/null | grep -q 'TEXTREL'; then
        echo "    BAD   text relocations, which SELinux refuses"
        BAD=1
    fi

    SEARCH=$(readelf -d "$BIN" 2>/dev/null |
        sed -n 's/.*(R\(UN\)\{0,1\}PATH).*\[\(.*\)\]/\2/p' | tr ':' '\n')
    while IFS= read -r entry; do
        [ -n "$entry" ] || continue
        case "$entry" in
        '$ORIGIN'|'$ORIGIN/'*) echo "    ok    search path $entry" ;;
        *) echo "    BAD   search path $entry is not relative to \$ORIGIN"; BAD=1 ;;
        esac
    done <<EOF
$SEARCH
EOF

    # An exported static library interposes on a driver's own copy; _Z is expected with a shared libstdc++.
    PREFIXES='SDL_|png_|FT_|sqlite3_|av_|avcodec_|ff_|inflate|deflate|ZSTD_'
    if [ "$SHARED_CXX" = 0 ]; then PREFIXES="$PREFIXES|_Z"; fi
    EXPORTS=$(readelf --dyn-syms -W "$BIN" 2>/dev/null |
        awk '$7 != "UND" && ($5 == "GLOBAL" || $5 == "WEAK") { print $8 }' |
        grep -E "^($PREFIXES)" || true)
    if [ -n "$EXPORTS" ]; then
        echo "    BAD   exports symbols of a library linked into it:"
        printf '%s\n' "$EXPORTS" | head -n 10 | sed 's/^/            /'
        BAD=1
    fi

    if readelf -h "$BIN" 2>/dev/null | grep -q 'AArch64'; then
        SMALL=$(readelf -lW "$BIN" 2>/dev/null |
            awk '$1 == "LOAD" { a = $NF; sub(/^0x/, "", a); if (length(a) < 5) print $NF }')
        if [ -n "$SMALL" ]; then
            echo "    BAD   LOAD segments aligned below 64K ($(printf '%s' "$SMALL" | tr '\n' ' '))"
            BAD=1
        fi
    fi
    ;;

Windows|MINGW*|MSYS*|CYGWIN*)
    # A PE image, under Git bash on Windows, or cross-built elsewhere.
    if ! command -v llvm-readobj >/dev/null 2>&1; then
        echo "==> FAILED: llvm-readobj not on PATH, so nothing was checked" >&2
        exit 1
    fi

    # msvcr[0-9], because msvcrt.dll is Windows' own and must not ship; D3Dcompiler_47.dll links it.
    APP_DEPLOYED='vcruntime|msvcp|msvcr[0-9]|concrt|mfc|vcomp'

    # The "is it in System32" rule needs a System32 to look in, and a cross-built archive is audited
    # on a host that has none.
    SYS32=/c/Windows/System32
    UNVERIFIED=0
    if [ ! -d "$SYS32" ]; then
        echo "    (no $SYS32 on this host: system DLLs are listed, not verified)"
    fi

    QUEUE=$(mktemp)
    printf '%s\n' "$BIN" > "$QUEUE"
    for d in "$PKGDIR"/*.[Dd][Ll][Ll]; do
        if [ -f "$d" ]; then printf '%s\n' "$d"; fi
    done >> "$QUEUE"

    IMP=$(mktemp)
    RAW=$(mktemp)
    while IFS= read -r image; do
        echo "  imports of $(basename "$image")"
        if ! llvm-readobj --coff-imports "$image" > "$RAW" 2>&1; then
            echo "    BAD   llvm-readobj could not inspect $image" >&2
            sed 's/^/          /' "$RAW" >&2
            rm -f "$RAW" "$IMP" "$QUEUE"
            exit 1
        fi
        sed -n 's/^ *Name: *//p' "$RAW" | sort -u > "$IMP"
        while IFS= read -r dll; do
            if [ -z "$dll" ]; then continue; fi
            lower=$(printf '%s' "$dll" | tr 'A-Z' 'a-z')
            # A shipped CRT DLL is a legitimate answer and so is /MT leaving no import; System32's
            # copy is not.
            if [ -f "$PKGDIR/$dll" ]; then
                echo "    ok    $dll (shipped)"
            elif printf '%s' "$lower" | grep -qE '^(api|ext)-ms-win-'; then
                # API-set names are loader contracts with no same-named file to find.
                echo "    ok    $dll (Windows API set)"
            elif printf '%s' "$lower" | grep -qE "^($APP_DEPLOYED)"; then
                echo "    BAD   $dll: app-deployed CRT, and not in the archive."
                echo "          Ship it beside the exe, or build with the static CRT."
                BAD=1
            elif [ -f "$SYS32/$dll" ]; then
                echo "    ok    $dll (system)"
            elif [ ! -d "$SYS32" ]; then
                echo "    ?     $dll (system, unverified)"
                UNVERIFIED=$((UNVERIFIED + 1))
            else
                echo "    BAD   $dll: neither shipped nor in System32"
                BAD=1
            fi
        done < "$IMP"
    done < "$QUEUE"
    rm -f "$RAW" "$IMP" "$QUEUE"
    if [ "$UNVERIFIED" -ne 0 ]; then
        echo "==> $UNVERIFIED import(s) taken on trust: no Windows install to check"
        echo "    them against. This audit is partial; the windows-2022 job's is not."
    fi
    ;;
esac

if [ "$BAD" -ne 0 ]; then
    echo "==> FAILED: the archive depends on something it does not ship" >&2
    exit 1
fi
echo "==> ok: nothing outside the archive and the base system"
