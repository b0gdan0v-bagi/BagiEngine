# The compiler launcher of a Clang build with ccache on Windows (cmake/BagiCompilerCache.cmake): no `sh` there, and no batch file
# (its quoting breaks an argument with a space or a `;`). Started as `python -S -E`, standard library only.
#
#   BagiCcacheLauncher.py <ccache> <base_dir or "-"> <compiler> <arguments...>
#
# The same rule as cmake/BagiCcacheLauncher.sh (its header says why): a compile that is not a PCH goes through ccache with the
# settings the PCH needs; the compile of a PCH (-emit-pch) bypasses ccache and writes <pch>.sum with a constant, which ccache
# (pch_external_checksum) hashes instead of the PCH. Keep the two files in step: the sloppiness, the environment variables, the way
# the -o of the PCH is found (the argument after the last "-o") and the exit code (the child's).
import os
import subprocess
import sys

SLOPPINESS = "pch_defines,time_macros,include_file_mtime,include_file_ctime"
PCH_SUM_CONTENT = "bagi-pch\n"
NO_BASE_DIR = "-"


def pch_output(arguments):
    output = None
    for previous, argument in zip(arguments, arguments[1:]):
        if previous == "-o":
            output = argument
    return output


def main(argv):
    ccache, base_dir, command = argv[1], argv[2], argv[3:]

    if "-emit-pch" in command:
        code = subprocess.call(command)
        if code != 0:
            return code
        output = pch_output(command)
        if output:
            with open(output + ".sum", "w", newline="\n") as sum_file:
                sum_file.write(PCH_SUM_CONTENT)
        return 0

    environment = dict(os.environ)
    environment["CCACHE_SLOPPINESS"] = SLOPPINESS
    environment["CCACHE_PCH_EXTSUM"] = "1"
    if base_dir != NO_BASE_DIR:
        environment["CCACHE_BASEDIR"] = base_dir
        environment["CCACHE_NOHASHDIR"] = "1"
    return subprocess.call([ccache] + command, env=environment)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
