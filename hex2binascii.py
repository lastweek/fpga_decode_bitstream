#!/usr/bin/env python3
import sys

s = sys.argv[1]
n = int(s, 16)

binascii = format(n, '032b')

print(s)
print(binascii)
