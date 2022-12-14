#!/usr/bin/env python

from __future__ import print_function
import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from vm import VMManager
from testLibrary import TestLib

VM_PREFIX="aos"

if __name__ == '__main__':
    manager = VMManager()
    vms = manager.getRunningVMNames(VM_PREFIX)
    for vmname in vms:
        manager.pinVCpuToPCpu(vmname,0,0)
    ips = TestLib.getIps(vms)
    ipsAndVals = { ip : 100000 for ip in ips }
    TestLib.startTestCase("~/cpu/test/testcases/2/iambusy {}",ipsAndVals)
