#include "pyp-topics.hh"
//#include "mt19937ar.h"

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <sys/time.h>
#include "clock_gettime_stub.c"


struct Timer {
  Timer() { Reset(); }
  void Reset() 
  { 
    clock_gettime(CLOCK_MONOTONIC, &start_t); 
  }
  double Elapsed() const {
    timespec end_t;
    clock_gettime(CLOCK_MONOTONIC, &end_t); 
    const double elapsed = (end_t.tv_sec - start_t.tv_sec) 
                + (end_t.tv_nsec - start_t.tv_nsec) / 1000000000.0;
    return elapsed;
  }
 private:
  timespec start_t;
};

void PYPTopics::sample(const Corpus& corpus, int samples) {
  Timer timer;

  if (!m_backoff.get()) {
    m_word_pyps.clear();
    m_word_pyps.push_back(PYPs());
  }

  std::cerr << " Training with " << m_word_pyps.size()-1 << " backoff level" 
    << (m_word_pyps.size()==2 ? ":" : "s:") << std::endl;

  for (int i=0; i<(int)m_word_pyps.size(); ++i)
    m_word_pyps.at(i).resize(m_num_topics, PYP<int>(0.5, 1.0));
  std::cerr << std::endl;

  m_document_pyps.resize(corpus.num_documents(), PYP<int>(0.5, 1.0));

  m_topic_p0 = 1.0/m_num_topics;
  m_term_p0 = 1.0/corpus.num_types();
  m_backoff_p0 = 1.0/corpus.num_documents();

  std::cerr << " Documents: " << corpus.num_documents() << " Terms: " 
    << corpus.num_types() << std::endl;

  timer.Reset();
  // Initialisation pass
  int document_id=0, topic_counter=0;
  for (Corpus::const_iterator corpusIt=corpus.begin(); 
       corpusIt != corpus.end(); ++corpusIt, ++document_id) {
    m_corpus_topics.push_back(DocumentTopics(corpusIt->size(), 0));

    int term_index=0;
    for (Document::const_iterator docIt=corpusIt->begin();
         docIt != corpusIt->end(); ++docIt, ++term_index) {
      topic_counter++;
      Term term = *docIt;

      // sample a new_topic
      //int new_topic = (topic_counter % m_num_topics);
      int new_topic = (document_id % m_num_topics);

      // add the new topic to the PYPs
      m_corpus_topics[document_id][term_index] = new_topic;
      increment(term, new_topic);

      if (m_use_topic_pyp) {
        F p0 = m_topic_pyp.prob(new_topic, m_topic_p0);
        int table_delta = m_document_pyps[document_id].increment(new_topic, p0);
        if (table_delta)
          m_topic_pyp.increment(new_topic, m_topic_p0);
      }
      else m_document_pyps[document_id].increment(new_topic, m_topic_p0);
    }
  }
  std::cerr << "  Initialized in " << timer.Elapsed() << " seconds\n";

  int* randomDocIndices = new int[corpus.num_documents()];
  for (int i = 0; i < corpus.num_documents(); ++i)
	  randomDocIndices[i] = i;

  // Sampling phase
  for (int curr_sample=0; curr_sample < samples; ++curr_sample) {
    std::cerr << "\n  -- Sample " << curr_sample << " "; std::cerr.flush();

    // Randomize the corpus indexing array
    int tmp;
    for (int i = corpus.num_documents()-1; i > 0; --i)
    {
    	int j = (int)(mt_genrand_real1() * i);
    	tmp = randomDocIndices[i];
    	randomDocIndices[i] = randomDocIndices[j];
    	randomDocIndices[j] = tmp;
    }

    // for each document in the corpus
    int document_id;
    for (int i=0; i<corpus.num_documents(); ++i)
    {
    	document_id = randomDocIndices[i];

      // for each term in the document
      int term_index=0;
      Document::const_iterator docEnd = corpus.at(document_id).end();
      for (Document::const_iterator docIt=corpus.at(document_id).begin();
           docIt != docEnd; ++docIt, ++term_index) {
        Term term = *docIt;

        // remove the prevous topic from the PYPs
        int current_topic = m_corpus_topics[document_id][term_index];
        decrement(term, current_topic);

        int table_delta = m_document_pyps[document_id].decrement(current_topic);
        if (m_use_topic_pyp && table_delta < 0) 
          m_topic_pyp.decrement(current_topic);

        // sample a new_topic
        int new_topic = sample(document_id, term);

        // add the new topic to the PYPs
        m_corpus_topics[document_id][term_index] = new_topic;
        increment(term, new_topic);

        if (m_use_topic_pyp) {
          F p0 = m_topic_pyp.prob(new_topic, m_topic_p0);
          int table_delta = m_document_pyps[document_id].increment(new_topic, p0);
          if (table_delta)
            m_topic_pyp.increment(new_topic, m_topic_p0);
        }
        else m_document_pyps[document_id].increment(new_topic, m_topic_p0);
      }
      if (document_id && document_id % 10000 == 0) {
        std::cerr << "."; std::cerr.flush();
      }
    }

    if (curr_sample != 0 && curr_sample % 10 == 0) {
      std::cerr << " ||| time=" << (timer.Elapsed() / 10.0) << " sec/sample" << std::endl;
      timer.Reset();
      std::cerr << "     ... Resampling hyperparameters "; std::cerr.flush();
      // resample the hyperparamters
      F log_p=0.0; int resample_counter=0;
      for (std::vector<PYPs>::iterator levelIt=m_word_pyps.begin();
           levelIt != m_word_pyps.end(); ++levelIt) {
        for (PYPs::iterator pypIt=levelIt->begin();
             pypIt != levelIt->end(); ++pypIt) {
          pypIt->resample_prior();
          log_p += pypIt->log_restaurant_prob();
        }
      }

      resample_counter=0;
      for (PYPs::iterator pypIt=m_document_pyps.begin();
           pypIt != m_document_pyps.end(); ++pypIt, ++resample_counter) {
        pypIt->resample_prior();
        log_p += pypIt->log_restaurant_prob();
        if (resample_counter++ % 10000 == 0) {
          std::cerr << "."; std::cerr.flush();
        }
      }
      if (m_use_topic_pyp) {
        m_topic_pyp.resample_prior();
        log_p += m_topic_pyp.log_restaurant_prob();
      }

      std::cerr.precision(10);
      std::cerr << " ||| LLH=" << log_p << " ||| resampling time=" << timer.Elapsed() << " sec" << std::endl;
      timer.Reset();

      int k=0;
      std::cerr << "Topics distribution: ";
      std::cerr.precision(2);
      for (PYPs::iterator pypIt=m_word_pyps.front().begin();
           pypIt != m_word_pyps.front().end(); ++pypIt, ++k) {
        std::cerr << "<" << k << ":" << pypIt->num_customers() << "," 
          << pypIt->num_types() << "," << m_topic_pyp.prob(k, m_topic_p0) << "> ";
        if (k % 5 == 0) std::cerr << std::endl << '\t';
      }
      std::cerr.precision(4);
      std::cerr << std::endl;
    }
  }
  delete [] randomDocIndices;
}

void PYPTopics::decrement(const Term& term, int topic, int level) {
  //std::cerr << "PYPTopics::decrement(" << term << "," << topic << "," << level << ")" << std::endl;
  m_word_pyps.at(level).at(topic).decrement(term);
  if (m_backoff.get()) {
    Term backoff_term = (*m_backoff)[term];
    if (!m_backoff->is_null(backoff_term))
      decrement(backoff_term, topic, level+1);
  }
}

void PYPTopics::increment(const Term& term, int topic, int level) {
  //std::cerr << "PYPTopics::increment(" << term << "," << topic << "," << level << ")" << std::endl;
  m_word_pyps.at(level).at(topic).increment(term, word_pyps_p0(term, topic, level));

  if (m_backoff.get()) {
    Term backoff_term = (*m_backoff)[term];
    if (!m_backoff->is_null(backoff_term))
      increment(backoff_term, topic, level+1);
  }
}

int PYPTopics::sample(const DocumentId& doc, const Term& term) {
  // First pass: collect probs
  F sum=0.0;
  std::vector<F> sums;
  for (int k=0; k<m_num_topics; ++k) {
    F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp) topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    //F p_k_d = m_document_pyps[doc].prob(k, topic_prob);
    F p_k_d = m_document_pyps[doc].unnormalised_prob(k, topic_prob);

    sum += (p_w_k*p_k_d);
    sums.push_back(sum);
  }
  // Second pass: sample a topic
  F cutoff = mt_genrand_res53() * sum;
  for (int k=0; k<m_num_topics; ++k) {
    if (cutoff <= sums[k]) 
      return k;
  }
  assert(false);
}

PYPTopics::F PYPTopics::word_pyps_p0(const Term& term, int topic, int level) const {
  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "PYPTopics::word_pyps_p0(" << term << "," << topic << "," << level << ")" << std::endl;

  F p0 = m_term_p0;
  if (m_backoff.get()) {
    //static F fudge=m_backoff_p0; // TODO

    Term backoff_term = (*m_backoff)[term];
    if (!m_backoff->is_null(backoff_term)) {
      assert (level < m_backoff->order());
      p0 = (1.0/(double)m_backoff->terms_at_level(level))*prob(backoff_term, topic, level+1);
    }
    else
      p0 = m_term_p0;
  }
  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "PYPTopics::word_pyps_p0(" << term << "," << topic << "," << level << ") = " << p0 << std::endl;
  return p0;
}

PYPTopics::F PYPTopics::prob(const Term& term, int topic, int level) const {
  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "PYPTopics::prob(" << term << "," << topic << "," << level << " " << factor << ")" << std::endl;

  F p0 = word_pyps_p0(term, topic, level);
  F p_w_k = m_word_pyps.at(level).at(topic).prob(term, p0);

  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "PYPTopics::prob(" << term << "," << topic << "," << level << ") = " << p_w_k << std::endl;

  return p_w_k;
}

int PYPTopics::max_topic() const {
  if (!m_use_topic_pyp)
    return -1;

  F current_max=0.0;
  int current_topic=-1;
  for (int k=0; k<m_num_topics; ++k) {
    F prob = m_topic_pyp.prob(k, m_topic_p0);
    if (prob > current_max) {
      current_max = prob;
      current_topic = k;
    }
  }
  assert(current_topic >= 0);
  return current_topic;
}

int PYPTopics::max(const DocumentId& doc) const {
  //std::cerr << "PYPTopics::max(" << doc << "," << term << ")" << std::endl;
  // collect probs
  F current_max=0.0;
  int current_topic=-1;
  for (int k=0; k<m_num_topics; ++k) {
    //F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp) 
      topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    F prob = 0;
    if (doc < 0) prob = topic_prob;
    else         prob = m_document_pyps[doc].prob(k, topic_prob);

    if (prob > current_max) {
      current_max = prob;
      current_topic = k;
    }
  }
  assert(current_topic >= 0);
  return current_topic;
}

int PYPTopics::max(const DocumentId& doc, const Term& term) const {
  //std::cerr << "PYPTopics::max(" << doc << "," << term << ")" << std::endl;
  // collect probs
  F current_max=0.0;
  int current_topic=-1;
  for (int k=0; k<m_num_topics; ++k) {
    F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp) 
      topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    F p_k_d = 0;
    if (doc < 0) p_k_d = topic_prob;
    else         p_k_d = m_document_pyps[doc].prob(k, topic_prob);

    F prob = (p_w_k*p_k_d);
    if (prob > current_max) {
      current_max = prob;
      current_topic = k;
    }
  }
  assert(current_topic >= 0);
  return current_topic;
}

std::ostream& PYPTopics::print_document_topics(std::ostream& out) const {
  for (CorpusTopics::const_iterator corpusIt=m_corpus_topics.begin(); 
       corpusIt != m_corpus_topics.end(); ++corpusIt) {
    int term_index=0;
    for (DocumentTopics::const_iterator docIt=corpusIt->begin();
         docIt != corpusIt->end(); ++docIt, ++term_index) {
      if (term_index) out << " ";
      out << *docIt;
    }
    out << std::endl;
  }
  return out;
}

std::ostream& PYPTopics::print_topic_terms(std::ostream& out) const {
  for (PYPs::const_iterator pypsIt=m_word_pyps.front().begin(); 
       pypsIt != m_word_pyps.front().end(); ++pypsIt) {
    int term_index=0;
    for (PYP<int>::const_iterator termIt=pypsIt->begin();
         termIt != pypsIt->end(); ++termIt, ++term_index) {
      if (term_index) out << " ";
      out << termIt->first << ":" << termIt->second;
    }
    out << std::endl;
  }
  return out;
}
