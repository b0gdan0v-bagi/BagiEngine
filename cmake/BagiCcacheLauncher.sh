#!/bin/sh
# The compiler launcher of a Clang build with ccache (cmake/BagiCompilerCache.cmake):
#
#   BagiCcacheLauncher.sh <ccache> <base_dir or "-"> <compiler> <arguments...>
#
# A compile that is not a PCH goes through ccache with the settings the PCH needs. The compile of a PCH (-emit-pch) bypasses ccache
# and writes <pch>.sum beside it: ccache (pch_external_checksum) hashes that file instead of the PCH. Why: Clang records the
# absolute path of every header in the PCH (the working folder plus the relative path), so the PCH of two checkouts is never
# byte-equal and a hash of its content would miss in every object that uses it. The headers themselves are in the manifest of
# every object (ccache reads them from the preprocessor), so the checksum can be a constant: a header that changed is a miss anyway.
# The PCH is not taken from the cache either: a PCH of another checkout names that checkout's files and Clang refuses it
# ("file has been modified since the precompiled header was built"); compiling it takes seconds.
ccache=$1
base_dir=$2
shift 2

for argument in "$@"; do
    if [ "$argument" = "-emit-pch" ]; then
        "$@" || exit $?
        output=
        previous=
        for argument in "$@"; do
            if [ "$previous" = "-o" ]; then
                output=$argument
            fi
            previous=$argument
        done
        if [ -n "$output" ]; then
            printf 'bagi-pch\n' >"$output.sum"
        fi
        exit 0
    fi
done

CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime
CCACHE_PCH_EXTSUM=1
export CCACHE_SLOPPINESS CCACHE_PCH_EXTSUM
if [ "$base_dir" != "-" ]; then
    CCACHE_BASEDIR=$base_dir
    CCACHE_NOHASHDIR=1
    export CCACHE_BASEDIR CCACHE_NOHASHDIR
fi
exec "$ccache" "$@"
