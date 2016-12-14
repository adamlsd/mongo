#!/usr/bin/env python

from os import walk, path
import subprocess
import sys


# Modified from burn_in_test.py
def get_err(*popenargs):
    """Run command with arguments and return its output as a byte string.

    The arguments are the same as for the Popen constructor.  Example:

    >>> check_output(["ls", "-l", "/dev/null"])
    'crw-rw-rw- 1 root root 1, 3 Oct 18  2007 /dev/null\n'
    """
    process = subprocess.Popen(stdout=subprocess.PIPE, stderr=subprocess.PIPE, *popenargs)
    unused_output, err = process.communicate()
    return err


def parse_command_line():
    if len(sys.argv) != 2:
        print "usage: %s build_dir" % sys.argv[0]
        return None
    if not path.isdir(sys.argv[1]):
        print "error: '%s' is not a valid directory" % sys.argv[1]
    return sys.argv[1]


def main():
    print "Checking for multiply defined symbols..."
    build_dir = parse_command_line()
    if build_dir is None:
        sys.exit(1)

    build_files = []
    for folder, subs, files in walk(build_dir):
        for f in files:
            if f.endswith('.os'):
                build_files.append(path.join(folder, f))

    command = ["ld", "--shared"] + build_files

    output = get_err(command).strip()
    multiples = [line.partition(' ')[2] for line in output.split('\n') if "multiple" in line]
    uniq = [line for line in set(multiples) if "main" not in line]

    if len(uniq) != 0:
        print "error: found the following multiply defined symbols"
        for multi_def in sorted(uniq):
            print multi_def
        sys.exit(1)

    print "No multiply defined symbols found."
    sys.exit(0)


if __name__ == "__main__":
    main()
