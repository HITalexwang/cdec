#ifndef _DTRAIN_KSAMPLER_H_
#define _DTRAIN_KSAMPLER_H_

#include "kbestget.h"
#include "hgsampler.h"
#include <vector>
#include <string>

using namespace std;

#include "kbest.h" // cdec
#include "sampler.h"

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
  NotifyTranslationForest(const SentenceMetadata& smeta, Hypergraph* hg)
  {
    Sample(*hg);
  }

  vector<ScoredHyp>* GetSamples() { return &s_; }

  void Sample(const Hypergraph& forest) {
    s_.clear();
    std::vector<HypergraphSampler::Hypothesis> samples;
    HypergraphSampler::sample_hypotheses(forest, k_, prng_, &samples);
    for (unsigned i = 0; i < k_; ++i) {
      ScoredHyp h;
      h.w = samples[i].words;
      h.f = samples[i].fmap;
      h.model = log(samples[i].model_score); 
      h.rank = i;
      s_.push_back(h);
    }
  }
};


} // namespace

#endif

