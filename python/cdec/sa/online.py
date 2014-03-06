from __future__ import division

import collections
import gzip
import itertools

from cdec.sa._sa import gzip_or_text

# Same as Cython implementation.  Collisions with NULL in bitext?
NULL_WORD = 'NULL'

def learn_vocab(text_f):
    vocab = set()
    for line in gzip_or_text(text_f):
        for word in line.strip().split():
            vocab.add(word)
    return vocab

def write_vocab(vocab, out_f):
    with gzip.open(out_f, 'wb') as out:
        for word in sorted(vocab):
            out.write('{}\n'.format(word))

def read_vocab(in_f):
    return set(line.strip() for line in gzip_or_text(in_f))

class Bilex:

    def __init__(self, in_f=None, alignment_f=None, text_f=None, target_text_f=None):
        self.f = collections.defaultdict(int)
        self.e = collections.defaultdict(int)
        self.fe = collections.defaultdict(lambda: collections.defaultdict(int))
        self.ef = collections.defaultdict(lambda: collections.defaultdict(int))

        # Read from file
        if in_f:
            self.read(in_f)
        # Build from aligned bitext
        elif alignment_f:
            # Allow one or two args for bitext
            if target_text_f:
                t = itertools.izip((line.strip() for line in gzip_or_text(text_f)), (line.strip() for line in gzip_or_text(target_text_f)))
            else:
                t = (line.strip().split(' ||| ') for line in gzip_or_text(text_f))
            a = (line.strip() for line in gzip_or_text(alignment_f))
            for (source, target) in t:
                links = sorted(tuple(int(link) for link in link_str.split('-')) for link_str in a.next().split())
                self.update(source.split(), target.split(), links)

    # Add bilex counts from new aligned sentence pair
    def update(self, f_words, e_words, links):
        aligned_fe = [list() for _ in range(len(f_words))]
        aligned_ef = [list() for _ in range(len(e_words))]
        for (i, j) in links:
            aligned_fe[i].append(j)
            aligned_ef[j].append(i)
        for f_i in range(len(f_words)):
            self.f[f_words[f_i]] += 1
            e_i_aligned = aligned_fe[f_i]
            lc = len(e_i_aligned)
            if lc > 0:
                for e_i in e_i_aligned:
                    self.fe[f_words[f_i]][e_words[e_i]] += (1 / lc)
            else:
                self.fe[f_words[f_i]][NULL_WORD] += 1
        for e_i in range(len(e_words)):
            self.e[e_words[e_i]] += 1
            f_i_aligned = aligned_ef[e_i]
            lc = len(f_i_aligned)
            if lc > 0:
                for f_i in f_i_aligned:
                    self.ef[e_words[e_i]][f_words[f_i]] += (1 / lc)
            else:
                self.ef[e_words[e_i]][NULL_WORD] += 1

    def write(self, out_f):
        fv = sorted(self.f)
        ev = sorted(self.e)
        with gzip.open(out_f, 'wb') as out:
            for f in fv:
                out.write('{} {}\n'.format(f, self.f[f]))
            out.write('\n')
            for e in ev:
                out.write('{} {}\n'.format(e, self.e[e]))
            out.write('\n')
            for f in fv:
                for (e, c) in sorted(self.fe[f].iteritems()):
                    out.write('{} {} {}\n'.format(f, e, c))
            for e in ev:
                for (f, c) in sorted(self.ef[e].iteritems()):
                    out.write('{} {} {}\n'.format(e, f, c))

    def read(self, in_f):
        with gzip_or_text(in_f) as inp:
            pass
