#!/usr/bin/env python

import re
import sys

def extract_config(f):
    var_patt = re.compile('libbcc_([A-Z_]+)\\s*:=\\s*([01])')

    for line in f:
        match = var_patt.match(line.strip())
        if match:
            print '#define', match.group(1), match.group(2)

def main():
    if len(sys.argv) != 1:
        print >> sys.stderr, 'USAGE:', sys.argv[0]
        sys.exit(1)

    extract_config(sys.stdin)


if __name__ == '__main__':
    main()
