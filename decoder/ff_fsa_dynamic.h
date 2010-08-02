#ifndef FF_FSA_DYNAMIC_H
#define FF_FSA_DYNAMIC_H

struct SentenceMetadata;
#include "ff_fsa_data.h"
#include "hg.h" // can't forward declare nested Hypergraph::Edge class
#include <sstream>


// the type-erased interface

struct FsaFeatureFunction : public FsaFeatureFunctionData {
  static const bool simple_phrase_score=false;
  virtual int markov_order() const = 0;

  // see ff_fsa.h - FsaFeatureFunctionBase<Impl> gives you reasonable impls of these if you override just ScanAccum
  virtual void ScanAccum(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,
                        WordID w,void const* state,void *next_state,Accum *a) const = 0;
  virtual void ScanPhraseAccum(SentenceMetadata const& smeta,Hypergraph::Edge const & edge,
                              WordID const* i, WordID const* end,
                              void const* state,void *next_state,Accum *accum) const = 0;
  virtual void ScanPhraseAccumOnly(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,
                           WordID const* i, WordID const* end,
                           void const* state,Accum *accum) const = 0;
  virtual void *ScanPhraseAccumBounce(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,WordID const* i, WordID const* end,void *cs,void *ns,Accum *accum) const = 0;

  virtual int early_score_words(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,WordID const* i, WordID const* end,Accum *accum) const { return 0; }
  virtual std::string usage(bool param,bool verbose) const {
    return FeatureFunction::usage_helper("unnamed_dynamic_fsa_feature","","",param,verbose);
  }

  virtual void print_state(std::ostream &o,void const*state) const {
    FsaFeatureFunctionData::print_state(o,state);
  }
  //end_phrase()
  virtual ~FsaFeatureFunction() {}

  // no need to override:
  std::string describe_state(void const* state) const {
    std::ostringstream o;
    print_state(o,state);
    return o.str();
  }
};

// conforming to above interface, type erases FsaImpl
// you might be wondering: why do this?  answer: it's cool, and it means that the bottom-up ff over ff_fsa wrapper doesn't go through multiple layers of dynamic dispatch
// usage: struct My : public FsaFeatureFunctionDynamic<My>
template <class Impl>
struct FsaFeatureFunctionDynamic : public FsaFeatureFunction {
  static const bool simple_phrase_score=Impl::simple_phrase_score;
  Impl& d() { return static_cast<Impl&>(*this); }
  Impl const& d() { return static_cast<Impl const&>(*this); }
  int markov_order() const { return d().markov_order(); }

  virtual void ScanAccum(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,
                        WordID w,void const* state,void *next_state,Accum *a) const {
    return d().ScanAccum(smeta,edge,w,state,next_state,a);
  }

  virtual void ScanPhraseAccum(SentenceMetadata const& smeta,Hypergraph::Edge const & edge,
                              WordID const* i, WordID const* end,
                              void const* state,void *next_state,Accum *a) const {
    return d().ScanPhraseAccum(smeta,edge,i,end,state,next_state,a);
  }

  virtual void ScanPhraseAccumOnly(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,
                           WordID const* i, WordID const* end,
                           void const* state,Accum *a) const {
    return d().ScanPhraseAccumOnly(smeta,edge,i,end,state,a);

  virtual void *ScanPhraseAccumBounce(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,WordID const* i, WordID const* end,void *cs,void *ns,Accum *a) const {
    return d().ScanPhraseAccumBounce(smeta,edge,i,end,cs,ns,a);
  }

  virtual int early_score_words(SentenceMetadata const& smeta,Hypergraph::Edge const& edge,WordID const* i, WordID const* end,Accum *accum) const {
    return d().early_score_words(smeta,edge,i,end,accum);
  }

  virtual std::string usage(bool param,bool verbose) const {
    return Impl::usage(param,verbose);
  }

  virtual void print_state(std::ostream &o,void const*state) const {
    return d().print_state(o,state);
  }
};

//TODO: combine 2 (or N) FsaFeatureFunction (type erased)


#endif
