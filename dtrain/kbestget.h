#ifndef _DTRAIN_KBESTGET_H_
#define _DTRAIN_KBESTGET_H_

#include "kbest.h"

namespace dtrain
{


/*
 * KBestList
 *
 */
struct KBestList {
  vector<SparseVector<double> > feats;
  vector<vector<WordID> > sents;
  vector<double> scores;
};


/*
 * KBestGetter
 *
 */
struct KBestGetter : public DecoderObserver
{
  KBestGetter( const size_t k ) : k_(k) {}
  const size_t k_;
  KBestList kb;

  virtual void
  NotifyTranslationForest(const SentenceMetadata& smeta, Hypergraph* hg)
  {
    GetKBest(smeta.GetSentenceID(), *hg);
  }

  KBestList* GetKBest() { return &kb; }

  void
  GetKBest(int sid, const Hypergraph& forest)
  {
    kb.scores.clear();
    kb.sents.clear();
    kb.feats.clear();
    // FIXME TODO FIXME TODO
    KBest::KBestDerivations<vector<WordID>, ESentenceTraversal, KBest::FilterUnique, prob_t, EdgeProb> kbest( forest, k_ );
    for ( size_t i = 0; i < k_; ++i ) {
      const KBest::KBestDerivations<vector<WordID>, ESentenceTraversal, KBest::FilterUnique, prob_t, EdgeProb>::Derivation* d =
        kbest.LazyKthBest( forest.nodes_.size() - 1, i );
      if (!d) break;
      kb.sents.push_back( d->yield);
      kb.feats.push_back( d->feature_values );
      kb.scores.push_back( d->score );
    }
  }
};


} // namespace


#endif

