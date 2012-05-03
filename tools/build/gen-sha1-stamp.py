#!/usr/bin/env python
#
# Copyright (C) 2011-2012 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import sys

try:
    import hashlib
    sha1 = hashlib.sha1
except ImportError, e:
    import sha
    sha1 = sha.sha

def compute_sha1(h, path):
    f = open(path, 'rb')
    while True:
        buf = f.read(1024)
        h.update(buf)
        if len(buf) < 1024:
            break
    f.close()

def compute_sha1_list(path_list):
    h = sha1()
    for path in path_list:
        compute_sha1(h, path)
    return h.digest()

def main():
    if len(sys.argv) < 2:
        print 'USAGE:', sys.argv[0], '[OUTPUT] [INPUTs]'
        sys.exit(1)

    f = open(sys.argv[1], 'wb')
    f.write(compute_sha1_list(sys.argv[2:]))
    f.close()

if __name__ == '__main__':
    main()
