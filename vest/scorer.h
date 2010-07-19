#ifndef SCORER_H_
#define SCORER_H_
#include <vector>
#include <string>
#include <boost/shared_ptr.hpp>

#include "wordid.h"

class ViterbiEnvelope;
class ErrorSurface;
class Hypergraph;  // needed for alignment

enum ScoreType { IBM_BLEU, NIST_BLEU, Koehn_BLEU, TER, BLEU_minus_TER_over_2, SER, AER, IBM_BLEU_3 };
ScoreType ScoreTypeFromString(const std::string& st);
std::string StringFromScoreType(ScoreType st);

class Score {
 public:
  typedef boost::shared_ptr<Score> ScoreP;
  virtual ~Score();
  virtual float ComputeScore() const = 0;
  virtual float ComputePartialScore() const =0;
  virtual void ScoreDetails(std::string* details) const = 0;
  std::string ScoreDetails() {
    std::string d;
    ScoreDetails(&d);
    return d;
  }
  virtual void PlusEquals(const Score& rhs, const float scale) = 0;
  virtual void PlusEquals(const Score& rhs) = 0;
  virtual void PlusPartialEquals(const Score& rhs, int oracle_e_cover, int oracle_f_cover, int src_len) = 0;
  virtual void Subtract(const Score& rhs, Score* res) const = 0;
  virtual Score* GetZero() const = 0;
  virtual Score* GetOne() const = 0;
  virtual bool IsAdditiveIdentity() const = 0; // returns true if adding this delta
                                      // to another score results in no score change
				      // under any circumstances
  virtual void Encode(std::string* out) const = 0;
  static Score* GetZero(ScoreType type);
  static Score* GetOne(ScoreType type);
};

class SentenceScorer {
 public:
  typedef boost::shared_ptr<Score> ScoreP;
  typedef boost::shared_ptr<SentenceScorer> ScorerP;
  typedef std::vector<WordID> Sentence;
  typedef std::vector<Sentence> Sentences;
  std::string desc;
  Sentences refs;
  SentenceScorer(std::string desc="SentenceScorer_unknown", Sentences const& refs=Sentences()) : desc(desc),refs(refs) {  }
  std::string verbose_desc() const;
  virtual float ComputeRefLength(const Sentence& hyp) const; // default: avg of refs.length
  virtual ~SentenceScorer();
  virtual Score* GetOne() const;
  virtual Score* GetZero() const;
  void ComputeErrorSurface(const ViterbiEnvelope& ve, ErrorSurface* es, const ScoreType type, const Hypergraph& hg) const;
  virtual Score* ScoreCandidate(const Sentence& hyp) const = 0;
  virtual Score* ScoreCCandidate(const Sentence& hyp) const =0;
  virtual const std::string* GetSource() const;
  static Score* CreateScoreFromString(const ScoreType type, const std::string& in);
  static SentenceScorer* CreateSentenceScorer(const ScoreType type,
    const std::vector<Sentence >& refs,
    const std::string& src = "");
};

//TODO: should be able to GetOne GetZero without supplying sentence (just type)
class DocScorer {
 public:
  ~DocScorer();
  DocScorer() {  }
  void Init(const ScoreType type,
            const std::vector<std::string>& ref_files,
            const std::string& src_file = "");
  DocScorer(const ScoreType type,
            const std::vector<std::string>& ref_files,
            const std::string& src_file = "")
  {
    Init(type,ref_files,src_file);
  }

  int size() const { return scorers_.size(); }
  typedef boost::shared_ptr<SentenceScorer> ScorerP;
  ScorerP operator[](size_t i) const { return scorers_[i]; }
 private:
  std::vector<ScorerP> scorers_;
};

#endif
