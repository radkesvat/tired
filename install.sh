#!/bin/sh
# Installation only. Generated services and administrative state are never touched.
set -eu
umask 022

fail() { printf '%s\n' "tired installer: $*" >&2; exit 1; }
version=0.1.0
prefix=/usr/local
base=https://github.com/radkesvat/tired/releases/download
uninstall=false
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version|--prefix|--base-url)
            [ "$#" -ge 2 ] || fail "Missing value for $1"
            case "$1" in
                --version) version=${2#v} ;;
                --prefix) prefix=$2 ;;
                --base-url) base=$2 ;;
            esac
            shift 2 ;;
        --uninstall) uninstall=true; shift ;;
        --help)
            printf '%s\n' 'Usage: sh install.sh [--version VERSION] [--prefix /usr/local] [--uninstall]' \
                'An explicit HTTPS --base-url can select a release mirror.' \
                'Only package payload is installed/removed. Existing services and state are preserved.'
            exit 0 ;;
        *) fail "Unknown option: $1" ;;
    esac
done
case "$version" in ''|*[!0-9A-Za-z.+-]*) fail 'Invalid release version' ;; esac
case "$prefix" in
    /*) ;;
    *) fail 'Install prefix must be absolute' ;;
esac
case "$prefix" in
    /|/usr|/usr/bin|/bin|*/../*|*/..|*/./*|*/.|*//*|*'
'*|*' '*|*\\*|*'|'*) fail 'Choose a normalized prefix without whitespace or metacharacters' ;;
esac
case "$base" in https://*) ;; *) fail 'Release downloads require HTTPS' ;; esac
prefix=${prefix%/}
for utility in uname getconf mktemp curl tar sha256sum stat install find id grep rm mv mkdir od tr; do
    command -v "$utility" >/dev/null 2>&1 || fail "Required utility is missing: $utility"
done
[ "$(uname -s)" = Linux ] || fail 'Only Linux is supported'
case "$(uname -m)" in x86_64) arch=amd64 ;; aarch64|arm64) arch=arm64 ;; *) fail 'Only x86-64 and ARM64 are supported' ;; esac
libc=$(getconf GNU_LIBC_VERSION 2>/dev/null) || fail 'A glibc system is required'
libc=${libc#glibc }
major=${libc%%.*}; minor=${libc#*.}; minor=${minor%%.*}
case "$major:$minor" in *[!0-9:]*|:*) fail 'Cannot determine the glibc version' ;; esac
[ "$major" -gt 2 ] || { [ "$major" -eq 2 ] && [ "$minor" -ge 35 ]; } || fail 'Direct releases require glibc 2.35 or newer'
authority_parent=$prefix
while [ ! -e "$authority_parent" ]; do
    authority_parent=${authority_parent%/*}; [ -n "$authority_parent" ] || authority_parent=/
done
if [ "$(id -u)" -ne 0 ] && [ ! -w "$authority_parent" ]; then
    [ -x /usr/bin/sudo ] || fail 'Destination requires administrator access; sudo is unavailable'
    set -- --version "$version" --prefix "$prefix" --base-url "$base"
    if "$uninstall"; then set -- "$@" --uninstall; fi
    exec /usr/bin/sudo -- /bin/sh "$0" "$@"
fi

# Check the actual ancestry before an installation can acquire root authority.
ancestor=$prefix
while [ "$ancestor" != / ]; do
    [ ! -L "$ancestor" ] || fail "Symlink in install prefix: $ancestor"
    if [ -e "$ancestor" ]; then
        [ -d "$ancestor" ] || fail "Prefix component is not a directory: $ancestor"
        mode=$(stat -c %a "$ancestor")
        [ "$((0$mode & 022))" -eq 0 ] || fail "Writable install ancestry: $ancestor"
        if [ "$(id -u)" -eq 0 ]; then
            [ "$(stat -c %u "$ancestor")" -eq 0 ] || fail "Nonroot-owned install ancestry: $ancestor"
        fi
    fi
    ancestor=${ancestor%/*}; [ -n "$ancestor" ] || ancestor=/
done
manifest=$prefix/share/tired/install-manifest.sha256
owned_path() {
    [ -f "$manifest" ] || return 1
    while IFS= read -r row; do
        [ "${row#*  }" != "$1" ] || return 0
    done < "$manifest"
    return 1
}
check_parent() {
    checked_parent=${1%/*}
    while [ "$checked_parent" != "$prefix" ]; do
        [ ! -L "$checked_parent" ] || fail "Symlink in payload destination: $checked_parent"
        if [ -e "$checked_parent" ]; then
            [ -d "$checked_parent" ] || fail 'Payload ancestor is not a directory'
            checked_mode=$(stat -c %a "$checked_parent")
            [ "$((0$checked_mode & 022))" -eq 0 ] || fail "Writable payload ancestry: $checked_parent"
            if [ "$(id -u)" -eq 0 ]; then
                [ "$(stat -c %u "$checked_parent")" -eq 0 ] || fail "Nonroot-owned payload ancestry: $checked_parent"
            fi
        fi
        checked_parent=${checked_parent%/*}
    done
}
if [ -e "$manifest" ] || [ -L "$manifest" ]; then
    [ -f "$manifest" ] || fail 'Missing installation manifest'
    [ ! -L "$manifest" ] || fail 'Unsafe installation manifest'
    [ "$(stat -c %u "$manifest")" -eq "$(id -u)" ] || fail 'Installation manifest belongs to another identity'
    [ "$(stat -c %h "$manifest")" -eq 1 ] || fail 'Hardlinked installation manifest'
    [ "$(stat -c %a "$manifest")" = 600 ] || fail 'Installation manifest must be private'
    while IFS= read -r row; do
        digest=${row%%  *}; file=${row#*  }
        [ "${#digest}" -eq 64 ] || fail 'Invalid installation manifest'
        case "$digest" in *[!0-9a-f]*) fail 'Invalid manifest digest' ;; esac
        case "$file" in bin/tired|libexec/tired/tired-helper|share/tired/*|share/doc/tired/*|share/man/man1/tired.1) ;; *) fail 'Unknown manifest path' ;; esac
        case "$file" in *../*|*/..|*\\*|*'
'*) fail 'Unsafe manifest path' ;; esac
        check_parent "$prefix/$file"
        [ -f "$prefix/$file" ] || fail "Missing payload path: $file"
        [ ! -L "$prefix/$file" ] || fail "Changed payload path: $file"
        [ "$(stat -c %h "$prefix/$file")" -eq 1 ] || fail "Hardlinked payload path: $file"
        [ "$(stat -c %u "$prefix/$file")" -eq "$(id -u)" ] || fail "Changed payload ownership: $file"
    done < "$manifest"
    (cd "$prefix" && sha256sum --check --strict "$manifest" >/dev/null) || fail 'Installed payload changed; inspect it before replacing/removing it'
elif "$uninstall"; then
    fail 'No direct installation manifest was found'
fi
if "$uninstall"; then
    while IFS= read -r row; do
        file=${row#*  }
        rm -- "$prefix/$file"
    done < "$manifest"
    rm -- "$manifest"
    printf '%s\n' 'Removed the direct-install payload. Generated services and state remain available to systemd.'
    exit 0
fi
work=$(mktemp -d) || fail 'Cannot create private download storage'
staging=
trap 'rm -rf -- "$work"; if [ -n "$staging" ]; then rm -rf -- "$staging"; fi' EXIT HUP INT TERM
artifact=tired-$version-linux-$arch.tar.gz
url=${base%/}/v$version
curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 20 --max-time 600 \
    --silent --show-error "$url/$artifact" --output "$work/$artifact" || fail 'Artifact download failed'
curl --fail --location --proto '=https' --proto-redir '=https' --tlsv1.2 --connect-timeout 20 --max-time 60 \
    --silent --show-error "$url/SHA256SUMS" --output "$work/SHA256SUMS" || fail 'Checksum download failed'
expected=
while IFS= read -r row; do
    case "$row" in *"  $artifact") [ -z "$expected" ] || fail 'Duplicate artifact checksum'; expected=${row%%  *} ;; esac
done < "$work/SHA256SUMS"
[ "${#expected}" -eq 64 ] || fail 'Release checksum is missing or malformed'
case "$expected" in *[!0-9a-f]*) fail 'Malformed checksum' ;; esac
printf '%s  %s\n' "$expected" "$artifact" > "$work/check"
(cd "$work" && sha256sum --check --strict check >/dev/null) || fail 'Artifact checksum mismatch'
tar -tzf "$work/$artifact" > "$work/entries" || fail 'Invalid release archive'
while IFS= read -r entry; do
    case "$entry" in "$artifact") fail 'Invalid archive root' ;; esac
    case "$entry" in "tired-$version-linux-$arch"|"tired-$version-linux-$arch/"|"tired-$version-linux-$arch/usr/"|"tired-$version-linux-$arch/usr/local/"|"tired-$version-linux-$arch/usr/local/"*) ;; *) fail 'Unexpected archive layout' ;; esac
    case "$entry" in *../*|*/..|*\\*|*'
'*) fail 'Unsafe archive path' ;; esac
done < "$work/entries"
tar -tvzf "$work/$artifact" > "$work/types" || fail 'Cannot inspect archive types'
while IFS= read -r entry; do
    case "$entry" in -*|d*) ;; *) fail 'Release archive contains a link or special file' ;; esac
done < "$work/types"
tar -xzf "$work/$artifact" -C "$work" || fail 'Archive extraction failed'
payload=$work/tired-$version-linux-$arch/usr/local
if ! { [ -x "$payload/bin/tired" ] && [ -x "$payload/libexec/tired/tired-helper" ] &&
       [ -d "$payload/share/tired/profiles.d" ]; }; then fail 'Incomplete release payload'; fi
# Literal build metadata detects an incorrectly named artifact before installation.
[ -f "$payload/share/tired/release.txt" ] || fail 'Missing platform metadata'
grep -Fx "architecture=$arch" "$payload/share/tired/release.txt" >/dev/null || fail 'Wrong artifact architecture'
grep -Fx "version=$version" "$payload/share/tired/release.txt" >/dev/null || fail 'Wrong artifact version'
for binary in bin/tired libexec/tired/tired-helper; do
    header=$(od -An -tx1 -N6 "$payload/$binary" | tr -d ' \n')
    [ "$header" = 7f454c460201 ] || fail 'Artifact is not a little-endian 64-bit ELF executable'
    machine=$(od -An -tu1 -j18 -N2 "$payload/$binary" | tr -d ' \n')
    case "$arch:$machine" in amd64:620|arm64:1830) ;; *) fail 'Wrong ELF artifact architecture' ;; esac
done
find "$payload" -type f > "$work/files"
while IFS= read -r source; do
    relative=${source#"$payload"/}
    case "$relative" in bin/tired|libexec/tired/tired-helper|share/tired/*|share/doc/tired/*|share/man/man1/tired.1) ;; *) fail 'Unknown release payload path' ;; esac
    destination=$prefix/$relative
    check_parent "$destination"
    if [ -e "$destination" ] || [ -L "$destination" ]; then
        owned_path "$relative" || fail "Refusing to overwrite an unrelated file: $destination"
    fi
done < "$work/files"
# Finish all destination-filesystem writes before replacing any existing payload.
mkdir -p -- "$prefix" || fail 'Cannot create installation prefix'
staging=$(mktemp -d "$prefix/.tired-install.XXXXXX") || fail 'Cannot stage installation'
while IFS= read -r source; do
    relative=${source#"$payload"/}
    case "$relative" in bin/tired|libexec/tired/tired-helper) mode=0755 ;; *) mode=0644 ;; esac
    install -D -m "$mode" -- "$source" "$staging/$relative" || fail 'Payload staging failed'
done < "$work/files"
: > "$work/manifest"
while IFS= read -r source; do
    relative=${source#"$payload"/}
    (cd "$staging" && sha256sum -- "$relative") >> "$work/manifest"
done < "$work/files"
while IFS= read -r source; do
    relative=${source#"$payload"/}
    parent=${relative%/*}
    mkdir -p -- "$prefix/$parent" || fail 'Cannot prepare payload destination'
    mv -T -- "$staging/$relative" "$prefix/$relative" || fail 'Payload installation failed'
done < "$work/files"
if [ -f "$manifest" ]; then
    while IFS= read -r row; do
        relative=${row#*  }
        if [ ! -f "$payload/$relative" ]; then
            rm -- "$prefix/$relative" || fail 'Cannot remove obsolete package payload'
        fi
    done < "$manifest"
fi
install -m 0600 -- "$work/manifest" "$manifest"
printf '%s\n' "Installed tired $version for $arch in $prefix." \
    'Run tired ./program to review a service. No services were changed during installation.'
