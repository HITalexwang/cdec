#ifndef _DTRAIN_KSAMPLER_H_
#define _DTRAIN_KSAMPLER_H_

#include "hg_sampler.h" // cdec
#include "kbestget.h"
#include "score.h"

namespace dtrain
{


struct KSampler : public HypSampler
{
  const unsigned k_;
  vector<ScoredHyp> s_;
  MT19937* prng_;
  score_t (*scorer)(NgramCounts&, const unsigned, const unsigned, unsigned, vector<score_t>);

  explicit KSampler(const unsigned k, MT19937* prng) :
    k_(k), prng_(prng) {}

  virtual void
  NotifyTranslationForest(const SentenceMetadata& /*smeta*/, Hypergraph* hg)
  {
    ScoredSamples(*hg);
  }

  vector<ScoredHyp>* GetSamples() { return &s_; }

  void ScoredSamples(const Hypergraph& forest) {
    s_.clear();
    std::vector<HypergraphSampler::Hypothesis> samples;
    HypergraphSampler::sample_hypotheses(forest, k_, prng_, &samples);
    for (unsigned i = 0; i < k_; ++i) {
      ScoredHyp h;
      h.w = samples[i].words;
      h.f = samples[i].fmap;
      h.model = log(samples[i].model_score); 
      h.rank = i;
      h.score = scorer_->Score(h.w, *ref_, i);
      s_.push_back(h);
    }
  }
};


} // namespace

#endif

