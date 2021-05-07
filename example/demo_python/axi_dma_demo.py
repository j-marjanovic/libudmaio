#!/usr/bin/env python3

import sys
import os
import numpy as np
from itertools import zip_longest

sys.path.append(os.getcwd())
from lfsr_demo import LfsrIo


def grouper(n, iterable):
    "grouper(3, 'abcdef') -> ('a','b','c'), ('d','e','f')"
    return zip_longest(*[iter(iterable)]*n)


# Implements LFSR as described in "AXI Traffic Generator v3.0"
class Lfsr(object):
    def __init__(self, seed):
        self.state = seed

    def advance(self):
        new_bit = 1 ^ (self.state) ^ (self.state >> 1) ^ (self.state >> 3) ^ (self.state >> 12)
        self.state = (new_bit << 15) | (self.state >> 1)
        self.state &= 0xffff


# Checks array against expected LFSR values
class LfsrChecker(object):
    def __init__(self):
        self.lfsr = None
    
    def check(self, arr):
        if self.lfsr is None:
            self.lfsr = Lfsr(arr[0])
        
        pos = 0
        for chunk in grouper(8, arr):
            for rcv_val in chunk:
                if rcv_val != self.lfsr.state:
                    print(f'mismatch at {pos}: rcv {rcv_val}, exp {self.lfsr.state}')
                    return False
                pos += 1
            self.lfsr.advance()
        return True


print('Creating LfsrIo instance')
l = LfsrIo('/dev/xdma/slot4')

print('Starting LfsrIo')
l.start(1024, 10, 60000)

checker = LfsrChecker()

words_total = 0

while True:
    result = l.read(10)
    if not result.size:
        break
    if not checker.check(result):
        break
    words_total += result.size

print(f'{words_total} words OK')

l.stop()
