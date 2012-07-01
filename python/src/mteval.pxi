cimport mteval

cdef char* as_str(sentence, error_msg='Cannot convert type %s to str'):
    cdef bytes ret
    if isinstance(sentence, unicode):
        ret = sentence.encode('utf8')
    elif isinstance(sentence, str):
        ret = sentence
    else:
        raise TypeError(error_msg % type(sentence))
    return ret

cdef class Candidate:
    cdef mteval.Candidate* candidate
    cdef public float score

    property words:
        def __get__(self):
            return unicode(GetString(self.candidate.ewords).c_str(), encoding='utf8')

    property fmap:
        def __get__(self):
            cdef SparseVector fmap = SparseVector()
            fmap.vector = new FastSparseVector[weight_t](self.candidate.fmap)
            return fmap

cdef class SufficientStats:
    cdef mteval.SufficientStats* stats
    cdef mteval.EvaluationMetric* metric

    def __dealloc__(self):
        del self.stats

    property score:
        def __get__(self):
            return self.metric.ComputeScore(self.stats[0])

    property detail:
        def __get__(self):
            return self.metric.DetailedScore(self.stats[0]).c_str()

    def __len__(self):
        return self.stats.size()

    def __iter__(self):
        for i in range(len(self)):
            yield self.stats[0][i]

    def __iadd__(SufficientStats self, SufficientStats other):
        self.stats[0] += other.stats[0]
        return self

    def __add__(SufficientStats x, SufficientStats y):
        cdef SufficientStats result = SufficientStats()
        result.stats = new mteval.SufficientStats(mteval.add(x.stats[0], y.stats[0]))
        result.metric = x.metric
        return result

cdef class SegmentEvaluator:
    cdef shared_ptr[mteval.SegmentEvaluator]* scorer
    cdef mteval.EvaluationMetric* metric
    
    def __dealloc__(self):
        del self.scorer

    def evaluate(self, sentence):
        cdef vector[WordID] hyp
        cdef SufficientStats sf = SufficientStats()
        sf.metric = self.metric
        sf.stats = new mteval.SufficientStats()
        ConvertSentence(string(as_str(sentence.strip())), &hyp)
        self.scorer.get().Evaluate(hyp, sf.stats)
        return sf

    def candidate_set(self, Hypergraph hypergraph, unsigned k):
        cdef mteval.CandidateSet* cs = new mteval.CandidateSet()
        cs.AddKBestCandidates(hypergraph.hg[0], k, self.scorer.get())
        cdef Candidate candidate
        cdef unsigned i
        for i in range(cs.size()):
            candidate = Candidate()
            candidate.candidate = &cs[0][i]
            candidate.score = self.metric.ComputeScore(cs[0][i].eval_feats)
            yield candidate
        del cs

cdef class Scorer:
    cdef string* name

    def __cinit__(self, char* name):
        self.name = new string(name)
    
    def __call__(self, refs):
        cdef mteval.EvaluationMetric* metric = mteval.Instance(self.name[0])
        if isinstance(refs, unicode) or isinstance(refs, str):
            refs = [refs]
        cdef vector[vector[WordID]]* refsv = new vector[vector[WordID]]()
        cdef vector[WordID]* refv
        cdef bytes ref_str
        for ref in refs:
            refv = new vector[WordID]()
            ConvertSentence(string(as_str(ref.strip())), refv)
            refsv.push_back(refv[0])
            del refv
        cdef unsigned i
        cdef SegmentEvaluator evaluator = SegmentEvaluator()
        evaluator.metric = metric
        evaluator.scorer = new shared_ptr[mteval.SegmentEvaluator](metric.CreateSegmentEvaluator(refsv[0]))
        del refsv # in theory should not delete but store in SegmentEvaluator
        return evaluator

    def __str__(self):
        return self.name.c_str()

BLEU = Scorer('IBM_BLEU')
TER = Scorer('TER')
