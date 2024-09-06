#!/usr/bin/env python                                                                                                              
import subprocess
import sys
import os

n = len(sys.argv)

olddir = "validate_outputs/"
newdir = "test_outputs/"
if len(sys.argv) != 2:
    print("Usage: python check_regressions testfile.out\n")
    sys.exit()
testname = sys.argv[1]

oldname = olddir + testname
newname = newdir + testname

print("COMPARING RESULTS FOR ", testname)

try:
    of = open(oldname, "r")
    nf = open(newname, "r")
except FileNotFoundError as error:
    print(error)
    sys.exit()

c = 0
experiment = "UNDEFINED"
fail = False
warn = False

while True:
    c += 1
    oline = of.readline()
    nline = nf.readline()

    if not oline and not nline:
        break
    if not oline:
        print("ERROR: " + oldname + " is shorter than " + newname);
        sys.exit()
    if not nline:
        print("ERROR: " + newname + " is shorter than " + oldname);
        sys.exit()

#    print(oline, end='')
    otokens = oline.split()
    ntokens = nline.split()
    if len(otokens) <= 1: #skip empty lines
        continue

    if otokens[0] == "Experiment:":
        if oline != nline:
            print("ERROR: Experiment name mismatch at line", c)
            print("  Original: ", oline)
            print("  New:      ", nline)
            sys.exit()
        experiment = oline

    if otokens[0] == "[PASS]":
        if ntokens[0] == "[FAIL]":
            print("FAIL: test previously passes, now fails at line", c)
            print("   ", oline)
            print("   ", nline)
            fail = True
        elif ntokens[0] != "[PASS]":
            print("ERROR: format mismatch at line", c, otokens[0], " vs ", ntokens[0])
            print("   ", oline)
            print("   ", nline)
            sys.exit()

    if otokens[0] == "[FAIL]":
        if ntokens[0] == "[PASS]":
            print("NOTE: test previously failed, now passes at line", c)
            print("   ", oline)
            print("   ", nline)
        elif ntokens[0] != "[FAIL]":
            print("ERROR: format mismatch at line", c, otokens[0], " vs ", ntokens[0])
            sys.exit()

    if otokens[0] == "[PASS]" or otokens[0] == "[FAIL]":
        if otokens[1] == "Connection":
            if ntokens[1] != "Connection":
                print("ERROR: format mismatch at line", c, otokens[1], " vs ", ntokens[1])
                sys.exit()
            ocount = int(otokens[3])
            ncount = int(ntokens[3])
            if ocount != ncount:
                if otokens[0] == "[PASS]" and otokens[0] == "[PASS]":
                    print(experiment)
                    print("FAIL: mismatch in completed connections of passing test at line", c)
                    print("   ", oline)
                    print("   ", nline)
            else:
                #print("completed connections match", ocount)
                pass
        if otokens[1] == "Tail":
            if ntokens[1] != "Tail":
                print("ERROR: format mismatch at line", c, otokens[1], " vs ", ntokens[1])
                sys.exit()
            ofct = float(otokens[3])
            nfct = float(ntokens[3])
            if nfct < ofct * 0.95:
                print(experiment)
                print("NOTE: tail FCT decreased significantly at line", c)
                print("   ", oline)
                print("   ", nline)
            elif nfct < ofct * 0.99:
                print(experiment)
                print("NOTE: tail FCT decreased slightly at line", c)
                print("   ", oline)
                print("   ", nline)
            elif ofct * 1.05 < nfct:
                print(experiment)
                print("FAIL: tail FCT increased significantly at line", c)
                print("   ", oline)
                print("   ", nline)
                fail = True
            elif ofct * 1.01 < nfct:
                print(experiment)
                print("WARNING: tail FCT increased slightly at line", c)
                print("   ", oline)
                print("   ", nline)
                warn = True
            elif ofct == nfct:
                #print("FCT matches", ofct)
                pass
                
        

    if otokens[0] == "Summary:":
        if ntokens[0] != "Summary:":
            print("ERROR: format mismatch at line", c, otokens[0], " vs ", ntokens[0])
            sys.exit()
        if otokens[1] == "New:":
            if ntokens[1] != "New:":
                print("ERROR: format mismatch at line", c, otokens[1], " vs ", ntokens[1])
                sys.exit()
            onew = int(otokens[2])
            nnew = int(ntokens[2])
            if onew != nnew:
                print("VALIDATION FAILURE: different number of new packets sent at line", c)
            else:
                #print("new pkts match")
                pass
        if otokens[3] == "Rtx:":
            if ntokens[3] != "Rtx:":
                print("ERROR: format mismatch at line", c, otokens[3], " vs ", ntokens[3])
                sys.exit()
            ortx = int(otokens[4])
            nrtx = int(ntokens[4])
            if ortx > nrtx * 1.2:
                print(experiment)
                print("NOTE: Significant reduction in retransmissions at line", c)
                print("   ", oline)
                print("   ", nline)
            if nrtx > ortx * 1.2:
                print(experiment)
                print("FAIL: Significant increase in retransmissions at line", c)
                print("   ", oline)
                print("   ", nline)
                fail = True
            if nrtx == ortx:
                #print("rtx matches")
                pass
        if otokens[9] == "ACKs:":
            if ntokens[9] != "ACKs:":
                print("ERROR: format mismatch at line", c, otokens[3], " vs ", ntokens[3])
                sys.exit()
            oacks = int(otokens[10])
            nacks = int(ntokens[10])
            if oacks > nacks * 1.2:
                print(experiment)
                print("NOTE: Significant reduction in ACKs at line", c)
                print("   ", oline)
                print("   ", nline)
            if nacks > oacks* 1.2:
                print(experiment)
                print("WARNING: Significant increase in ACKs at line", c)
                print("   ", oline)
                print("   ", nline)
                warn = True
            if nacks == oacks:
                #print("ack count matches")
                pass

print("SUMMARY: ", end='')
if fail:
    print(" SOME TESTS FAILED - REQUIRES MANUAL FOLLOWUP")
elif warn:
    print(" ALL TESTS PASSED, BUT INVESTIGATE WARNINGS")
else:
    print(" ALL TESTS PASSED")
print("-------------------------------")
