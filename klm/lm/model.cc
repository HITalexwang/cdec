#include "lm/model.hh"

#include "lm/blank.hh"
#include "lm/lm_exception.hh"
#include "lm/search_hashed.hh"
#include "lm/search_trie.hh"
#include "lm/read_arpa.hh"
#include "util/murmur_hash.hh"

#include <algorithm>
#include <functional>
#include <numeric>
#include <cmath>

namespace lm {
namespace ngram {

size_t hash_value(const State &state) {
  return util::MurmurHashNative(state.history_, sizeof(WordIndex) * state.valid_length_);
}

namespace detail {

template <class Search, class VocabularyT> size_t GenericModel<Search, VocabularyT>::Size(const std::vector<uint64_t> &counts, const Config &config) {
  return VocabularyT::Size(counts[0], config) + Search::Size(counts, config);
}

template <class Search, class VocabularyT> void GenericModel<Search, VocabularyT>::SetupMemory(void *base, const std::vector<uint64_t> &counts, const Config &config) {
  uint8_t *start = static_cast<uint8_t*>(base);
  size_t allocated = VocabularyT::Size(counts[0], config);
  vocab_.SetupMemory(start, allocated, counts[0], config);
  start += allocated;
  start = search_.SetupMemory(start, counts, config);
  if (static_cast<std::size_t>(start - static_cast<uint8_t*>(base)) != Size(counts, config)) UTIL_THROW(FormatLoadException, "The data structures took " << (start - static_cast<uint8_t*>(base)) << " but Size says they should take " << Size(counts, config));
}

template <class Search, class VocabularyT> GenericModel<Search, VocabularyT>::GenericModel(const char *file, const Config &config) {
  LoadLM(file, config, *this);

  // g++ prints warnings unless these are fully initialized.  
  State begin_sentence = State();
  begin_sentence.valid_length_ = 1;
  begin_sentence.history_[0] = vocab_.BeginSentence();
  begin_sentence.backoff_[0] = search_.unigram.Lookup(begin_sentence.history_[0]).backoff;
  State null_context = State();
  null_context.valid_length_ = 0;
  P::Init(begin_sentence, null_context, vocab_, search_.middle.size() + 2);
}

template <class Search, class VocabularyT> void GenericModel<Search, VocabularyT>::InitializeFromBinary(void *start, const Parameters &params, const Config &config, int fd) {
  SetupMemory(start, params.counts, config);
  vocab_.LoadedBinary(fd, config.enumerate_vocab);
  search_.unigram.LoadedBinary();
  for (typename std::vector<Middle>::iterator i = search_.middle.begin(); i != search_.middle.end(); ++i) {
    i->LoadedBinary();
  }
  search_.longest.LoadedBinary();
}

template <class Search, class VocabularyT> void GenericModel<Search, VocabularyT>::InitializeFromARPA(const char *file, const Config &config) {
  // Backing file is the ARPA.  Steal it so we can make the backing file the mmap output if any.  
  util::FilePiece f(backing_.file.release(), file, config.messages);
  std::vector<uint64_t> counts;
  // File counts do not include pruned trigrams that extend to quadgrams etc.   These will be fixed with search_.VariableSizeLoad
  ReadARPACounts(f, counts);

  if (counts.size() > kMaxOrder) UTIL_THROW(FormatLoadException, "This model has order " << counts.size() << ".  Edit ngram.hh's kMaxOrder to at least this value and recompile.");
  if (counts.size() < 2) UTIL_THROW(FormatLoadException, "This ngram implementation assumes at least a bigram model.");
  if (config.probing_multiplier <= 1.0) UTIL_THROW(ConfigException, "probing multiplier must be > 1.0");

  std::size_t vocab_size = VocabularyT::Size(counts[0], config);
  // Setup the binary file for writing the vocab lookup table.  The search_ is responsible for growing the binary file to its needs.  
  vocab_.SetupMemory(SetupJustVocab(config, counts.size(), vocab_size, backing_), vocab_size, counts[0], config);

  if (config.write_mmap) {
    WriteWordsWrapper wrap(config.enumerate_vocab);
    vocab_.ConfigureEnumerate(&wrap, counts[0]);
    search_.InitializeFromARPA(file, f, counts, config, vocab_, backing_);
    wrap.Write(backing_.file.get());
  } else {
    vocab_.ConfigureEnumerate(config.enumerate_vocab, counts[0]);
    search_.InitializeFromARPA(file, f, counts, config, vocab_, backing_);
  }

  // TODO: fail faster?  
  if (!vocab_.SawUnk()) {
    switch(config.unknown_missing) {
      case Config::THROW_UP:
        {
          SpecialWordMissingException e("<unk>");
          e << " and configuration was set to throw if unknown is missing";
          throw e;
        }
      case Config::COMPLAIN:
        if (config.messages) *config.messages << "Language model is missing <unk>.  Substituting probability " << config.unknown_missing_prob << "." << std::endl; 
        // There's no break;.  This is by design.  
      case Config::SILENT:
        // Default probabilities for unknown.  
        search_.unigram.Unknown().backoff = 0.0;
        search_.unigram.Unknown().prob = config.unknown_missing_prob;
        break;
    }
  }
}

template <class Search, class VocabularyT> FullScoreReturn GenericModel<Search, VocabularyT>::FullScore(const State &in_state, const WordIndex new_word, State &out_state) const {
  FullScoreReturn ret = ScoreExceptBackoff(in_state.history_, in_state.history_ + in_state.valid_length_, new_word, out_state);
  if (ret.ngram_length - 1 < in_state.valid_length_) {
    ret.prob = std::accumulate(in_state.backoff_ + ret.ngram_length - 1, in_state.backoff_ + in_state.valid_length_, ret.prob);
  }
  return ret;
}

template <class Search, class VocabularyT> FullScoreReturn GenericModel<Search, VocabularyT>::FullScoreForgotState(const WordIndex *context_rbegin, const WordIndex *context_rend, const WordIndex new_word, State &out_state) const {
  context_rend = std::min(context_rend, context_rbegin + P::Order() - 1);
  FullScoreReturn ret = ScoreExceptBackoff(context_rbegin, context_rend, new_word, out_state);
  ret.prob += SlowBackoffLookup(context_rbegin, context_rend, ret.ngram_length);
  return ret;
}

template <class Search, class VocabularyT> void GenericModel<Search, VocabularyT>::GetState(const WordIndex *context_rbegin, const WordIndex *context_rend, State &out_state) const {
  // Generate a state from context.  
  context_rend = std::min(context_rend, context_rbegin + P::Order() - 1);
  if (context_rend == context_rbegin) {
    out_state.valid_length_ = 0;
    return;
  }
  float ignored_prob;
  typename Search::Node node;
  search_.LookupUnigram(*context_rbegin, ignored_prob, out_state.backoff_[0], node);
  // Tricky part is that an entry might be blank, but out_state.valid_length_ always has the last non-blank n-gram length.  
  out_state.valid_length_ = 1;
  float *backoff_out = out_state.backoff_ + 1;
  const typename Search::Middle *mid = &*search_.middle.begin();
  for (const WordIndex *i = context_rbegin + 1; i < context_rend; ++i, ++backoff_out, ++mid) {
    if (!search_.LookupMiddleNoProb(*mid, *i, *backoff_out, node)) {
      std::copy(context_rbegin, context_rbegin + out_state.valid_length_, out_state.history_);
      return;
    }
    if (*backoff_out != kBlankBackoff) {
      out_state.valid_length_ = i - context_rbegin + 1;
    } else {
      *backoff_out = 0.0;
    }
  }
  std::copy(context_rbegin, context_rbegin + out_state.valid_length_, out_state.history_);
}

template <class Search, class VocabularyT> float GenericModel<Search, VocabularyT>::SlowBackoffLookup(
    const WordIndex *const context_rbegin, const WordIndex *const context_rend, unsigned char start) const {
  // Add the backoff weights for n-grams of order start to (context_rend - context_rbegin).
  if (context_rend - context_rbegin < static_cast<std::ptrdiff_t>(start)) return 0.0;
  float ret = 0.0;
  if (start == 1) {
    ret += search_.unigram.Lookup(*context_rbegin).backoff;
    start = 2;
  }
  typename Search::Node node;
  if (!search_.FastMakeNode(context_rbegin, context_rbegin + start - 1, node)) {
    return 0.0;
  }
  float backoff;
  // i is the order of the backoff we're looking for.
  for (const WordIndex *i = context_rbegin + start - 1; i < context_rend; ++i) {
    if (!search_.LookupMiddleNoProb(search_.middle[i - context_rbegin - 1], *i, backoff, node)) break;
    if (backoff != kBlankBackoff) ret += backoff;
  }
  return ret;
}

/* Ugly optimized function.  Produce a score excluding backoff.  
 * The search goes in increasing order of ngram length.  
 * Context goes backward, so context_begin is the word immediately preceeding
 * new_word.  
 */
template <class Search, class VocabularyT> FullScoreReturn GenericModel<Search, VocabularyT>::ScoreExceptBackoff(
    const WordIndex *context_rbegin,
    const WordIndex *context_rend,
    const WordIndex new_word,
    State &out_state) const {
  FullScoreReturn ret;
  // ret.ngram_length contains the last known good (non-blank) ngram length.  
  ret.ngram_length = 1;

  typename Search::Node node;
  float *backoff_out(out_state.backoff_);
  search_.LookupUnigram(new_word, ret.prob, *backoff_out, node);
  out_state.history_[0] = new_word;
  if (context_rbegin == context_rend) {
    out_state.valid_length_ = 1;
    return ret;
  }
  ++backoff_out;

  // Ok now we now that the bigram contains known words.  Start by looking it up.

  const WordIndex *hist_iter = context_rbegin;
  typename std::vector<Middle>::const_iterator mid_iter = search_.middle.begin();
  for (; ; ++mid_iter, ++hist_iter, ++backoff_out) {
    if (hist_iter == context_rend) {
      // Ran out of history.  Typically no backoff, but this could be a blank.  
      out_state.valid_length_ = ret.ngram_length;
      std::copy(context_rbegin, context_rbegin + ret.ngram_length - 1, out_state.history_ + 1);
      // ret.prob was already set.
      return ret;
    }

    if (mid_iter == search_.middle.end()) break;

    float revert = ret.prob;
    if (!search_.LookupMiddle(*mid_iter, *hist_iter, ret.prob, *backoff_out, node)) {
      // Didn't find an ngram using hist_iter.  
      std::copy(context_rbegin, context_rbegin + ret.ngram_length - 1, out_state.history_ + 1);
      out_state.valid_length_ = ret.ngram_length;
      // ret.prob was already set.  
      return ret;
    }
    if (*backoff_out == kBlankBackoff) {
      *backoff_out = 0.0;
      ret.prob = revert;
    } else {
      ret.ngram_length = hist_iter - context_rbegin + 2;
    }
  }

  // It passed every lookup in search_.middle.  All that's left is to check search_.longest.  
  
  if (!search_.LookupLongest(*hist_iter, ret.prob, node)) {
    //assert(ret.ngram_length <= P::Order() - 1);
    out_state.valid_length_ = ret.ngram_length;
    std::copy(context_rbegin, context_rbegin + ret.ngram_length - 1, out_state.history_ + 1);
    // ret.prob was already set.  
    return ret;
  }
  // It's an P::Order()-gram.  There is no blank in longest_.  
  // out_state.valid_length_ is still P::Order() - 1 because the next lookup will only need that much.
  std::copy(context_rbegin, context_rbegin + P::Order() - 2, out_state.history_ + 1);
  out_state.valid_length_ = P::Order() - 1;
  ret.ngram_length = P::Order();
  return ret;
}

template class GenericModel<ProbingHashedSearch, ProbingVocabulary>;
template class GenericModel<SortedHashedSearch, SortedVocabulary>;
template class GenericModel<trie::TrieSearch, SortedVocabulary>;

} // namespace detail
} // namespace ngram
} // namespace lm
