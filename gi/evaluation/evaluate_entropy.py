#!/usr/bin/env python

import sys, math, itertools

ginfile = open(sys.argv[1])
pinfile = open(sys.argv[2])
if len(sys.argv) >= 4:
    slash_threshold = int(sys.argv[3])
else:
    slash_threshold = 99999

# evaluating: H(G | P) = sum_{g,p} p(g,p) log { p(p) / p(g,p) }
#                      = sum_{g,p} c(g,p)/N { log c(p) - log N - log c(g,p) + log N }
#                      = 1/N sum_{g,p} c(g,p) { log c(p) - log c(g,p) }
# where G = gold, P = predicted, N = number of events

N = 0
gold_frequencies = {}
predict_frequencies = {}
joint_frequencies = {}

for gline, pline in itertools.izip(ginfile, pinfile):
    gparts = gline.split('||| ')[1].split()
    pparts = pline.split('||| ')[1].split()
    assert len(gparts) == len(pparts)

    for gpart, ppart in zip(gparts, pparts):
        gtag = gpart.split(':',1)[1]
        ptag = ppart.split(':',1)[1]

        if gtag.count('/') <= slash_threshold:
            joint_frequencies.setdefault((gtag, ptag), 0)
            joint_frequencies[gtag,ptag] += 1

            predict_frequencies.setdefault(ptag, 0)
            predict_frequencies[ptag] += 1

            gold_frequencies.setdefault(gtag, 0)
            gold_frequencies[gtag] += 1

            N += 1

hg2p = 0
hp2g = 0
for (gtag, ptag), cgp in joint_frequencies.items():
    hp2g += cgp * (math.log(predict_frequencies[ptag], 2) - math.log(cgp, 2))
    hg2p += cgp * (math.log(gold_frequencies[gtag], 2) - math.log(cgp, 2))
hg2p /= N
hp2g /= N

print 'H(P|G)', hg2p, 'H(G|P)', hp2g, 'VI', hg2p + hp2g
