#ifndef ORACLE_BLEU_H
#define ORACLE_BLEU_H

#include <sstream>
#include <iostream>
#include <string>
#include <vector>
#include <boost/program_options.hpp>
#include <boost/program_options/variables_map.hpp>
#include "../vest/scorer.h"
#include "hg.h"
#include "ff_factory.h"
#include "ff_bleu.h"
#include "sparse_vector.h"
#include "viterbi.h"
#include "sentence_metadata.h"
#include "apply_models.h"
#include "kbest.h"

//TODO: put function impls into .cc
//TODO: disentangle
struct Translation {
  typedef std::vector<WordID> Sentence;
  Sentence sentence;
  FeatureVector features;
  Translation() {  }
  Translation(Hypergraph const& hg,WeightVector *feature_weights=0)
  {
    Viterbi(hg,feature_weights);
  }
  void Viterbi(Hypergraph const& hg,WeightVector *feature_weights=0) // weights are only for checking that scoring is correct
  {
    ViterbiESentence(hg,&sentence);
    features=ViterbiFeatures(hg,feature_weights,true);
  }
  void Print(std::ostream &out,std::string pre="   +Oracle BLEU ") {
    out<<pre<<"Viterbi: "<<TD::GetString(sentence)<<"\n";
    out<<pre<<"features: "<<features<<std::endl;
  }

};


struct OracleBleu {
  typedef std::vector<std::string> Refs;
  Refs refs;
  WeightVector feature_weights_;
  DocScorer ds;

  static void AddOptions(boost::program_options::options_description *opts) {
    using namespace boost::program_options;
    using namespace std;
    opts->add_options()
      ("references,R", value<Refs >(), "Translation reference files")
      ("oracle_loss", value<string>(), "IBM_BLEU_3 (default), IBM_BLEU etc")
      ;
  }
  int order;

  //TODO: move cdec.cc kbest output files function here

  //TODO: provide for loading most recent translation for every sentence (no more scale.. etc below? it's possible i messed the below up; i assume it's supposed to gracefully figure out the document 1bests as you go, then keep them up to date as you make multiple MIRA passes.  provide alternative loading for MERT
  double scale_oracle;
  int oracle_doc_size;
  double tmp_src_length;
  double doc_src_length;
  void set_oracle_doc_size(int size) {
    oracle_doc_size=size;
    scale_oracle=  1-1./oracle_doc_size;\
    doc_src_length=0;
  }
  OracleBleu(int doc_size=10) {
    set_oracle_doc_size(doc_size);
  }

  boost::shared_ptr<Score> doc_score,sentscore; // made from factory, so we delete them

  void UseConf(boost::program_options::variables_map const& conf) {
    using namespace std;
    set_loss(conf["oracle_loss"].as<string>());
    set_refs(conf["references"].as<Refs>());
  }

  ScoreType loss;
//  std::string loss_name;
  boost::shared_ptr<FeatureFunction> pff;

  void set_loss(std::string const& lossd="IBM_BLEU_3") {
//    loss_name=lossd;
    loss=ScoreTypeFromString(lossd);
    order=(loss==IBM_BLEU_3)?3:4;
    std::ostringstream param;
    param<<"-o "<<order;
    pff=global_ff_registry->Create("BLEUModel",param.str());
  }

  void set_refs(Refs const& r) {
    refs=r;
    assert(refs.size());
    ds=DocScorer(loss,refs);
    doc_score.reset();
//    doc_score=sentscore
    std::cerr << "Loaded " << ds.size() << " references for scoring with " << StringFromScoreType(loss) << std::endl;
  }

  SentenceMetadata MakeMetadata(Hypergraph const& forest,int sent_id) {
    std::vector<WordID> srcsent;
    ViterbiFSentence(forest,&srcsent);
    SentenceMetadata sm(sent_id,Lattice()); //TODO: make reference from refs?
    sm.SetSourceLength(srcsent.size());
    return sm;
  }

  void Rescore(SentenceMetadata & smeta,Hypergraph const& forest,Hypergraph *dest_forest,WeightVector const& feature_weights,double bleu_weight=1.0) {
    Translation model_trans(forest);
    sentscore.reset(ds[smeta.GetSentenceID()]->ScoreCandidate(model_trans.sentence));
	if (!doc_score) { doc_score.reset(sentscore->GetOne()); }
	tmp_src_length = smeta.GetSourceLength(); //TODO: where does this come from?
	smeta.SetScore(doc_score.get());
	smeta.SetDocLen(doc_src_length);
	smeta.SetDocScorer(&ds);
    using namespace std;
    ModelSet oracle_models(FeatureWeights(bleu_weight,1),vector<FeatureFunction const*>(1,pff.get()));
    const IntersectionConfiguration inter_conf_oracle(0, 0);
	cerr << "Going to call Apply Model " << endl;
	ApplyModelSet(forest,
                  smeta,
                  oracle_models,
                  inter_conf_oracle,
                  dest_forest);
    feature_weights_=feature_weights;
    ReweightBleu(dest_forest,bleu_weight);
  }

  void IncludeLastScore(std::ostream *out=0) {
    double bleu_scale_ = doc_src_length * doc_score->ComputeScore();
    doc_score->PlusEquals(*sentscore, scale_oracle);
	sentscore.reset();
    doc_src_length = (doc_src_length + tmp_src_length) * scale_oracle;
    if (out) {
      std::string d;
      doc_score->ScoreDetails(&d);
      *out << "SCALED SCORE: " << bleu_scale_ << "DOC BLEU " << doc_score->ComputeScore() << " " <<d << std::endl;
    }
  }

  void ReweightBleu(Hypergraph *dest_forest,double bleu_weight=-1.) {
    feature_weights_[0]=bleu_weight;
	dest_forest->Reweight(feature_weights_);
//	dest_forest->SortInEdgesByEdgeWeights();
  }

// TODO decoder output should probably be moved to another file - how about oracle_bleu.h
  void DumpKBest(const int sent_id, const Hypergraph& forest, const int k, const bool unique, std::string const &kbest_out_filename_) {
    using namespace std;
    using namespace boost;
    cerr << "In kbest\n";

    ofstream kbest_out;
    kbest_out.open(kbest_out_filename_.c_str());
    cerr << "Output kbest to " << kbest_out_filename_;

    //add length (f side) src length of this sentence to the psuedo-doc src length count
    float curr_src_length = doc_src_length + tmp_src_length;

    if (unique) {
      KBest::KBestDerivations<vector<WordID>, ESentenceTraversal, KBest::FilterUnique> kbest(forest, k);
      for (int i = 0; i < k; ++i) {
        const KBest::KBestDerivations<vector<WordID>, ESentenceTraversal, KBest::FilterUnique>::Derivation* d =
          kbest.LazyKthBest(forest.nodes_.size() - 1, i);
        if (!d) break;
        //calculate score in context of psuedo-doc
        Score* sentscore = ds[sent_id]->ScoreCandidate(d->yield);
        sentscore->PlusEquals(*doc_score,float(1));
        float bleu = curr_src_length * sentscore->ComputeScore();
        kbest_out << sent_id << " ||| " << TD::GetString(d->yield) << " ||| "
                  << d->feature_values << " ||| " << log(d->score) << " ||| " << bleu << endl;
        // cout << sent_id << " ||| " << TD::GetString(d->yield) << " ||| "
        //     << d->feature_values << " ||| " << log(d->score) << endl;
      }
    } else {
      KBest::KBestDerivations<vector<WordID>, ESentenceTraversal> kbest(forest, k);
      for (int i = 0; i < k; ++i) {
        const KBest::KBestDerivations<vector<WordID>, ESentenceTraversal>::Derivation* d =
          kbest.LazyKthBest(forest.nodes_.size() - 1, i);
        if (!d) break;
        cout << sent_id << " ||| " << TD::GetString(d->yield) << " ||| "
             << d->feature_values << " ||| " << log(d->score) << endl;
      }
    }
  }

  void DumpKBest(boost::program_options::variables_map const& conf,std::string const& suffix,const int sent_id, const Hypergraph& forest, const int k, const bool unique)
  {
    std::ostringstream kbest_string_stream;
    kbest_string_stream << conf["forest_output"].as<std::string>() << "/kbest_"<<suffix<< "." << sent_id;
    DumpKBest(sent_id, forest, k, unique, kbest_string_stream.str());
  }

};


#endif
