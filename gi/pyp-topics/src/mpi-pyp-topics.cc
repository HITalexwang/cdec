#include <boost/mpi/communicator.hpp>

#include "timing.h"
#include "mpi-pyp-topics.hh"

//#include <boost/date_time/posix_time/posix_time_types.hpp>
void MPIPYPTopics::sample_corpus(const Corpus& corpus, int samples,
                              int freq_cutoff_start, int freq_cutoff_end,
                              int freq_cutoff_interval,
                              int max_contexts_per_document) {
  Timer timer;
  std::cout << m_am_root << std::endl;

  int documents = corpus.num_documents();
  m_mpi_start = 0;
  m_mpi_end = documents;
  if (m_size != 1) {
      assert(documents < std::numeric_limits<int>::max());
      m_mpi_start = (documents / m_size) * m_rank;
      if (m_rank == m_size-1) m_mpi_end = documents;
      else m_mpi_end = (documents / m_size)*(m_rank+1);
  }
  int local_documents = m_mpi_end - m_mpi_start;

  if (!m_backoff.get()) {
    m_word_pyps.clear();
    m_word_pyps.push_back(MPIPYPs());
  }

  if (m_am_root) std::cerr << "\n Training with " << m_word_pyps.size()-1 << " backoff level"
    << (m_word_pyps.size()>1 ? ":" : "s:") << std::endl;

  for (int i=0; i<(int)m_word_pyps.size(); ++i) {
    m_word_pyps.at(i).reserve(m_num_topics);
    for (int j=0; j<m_num_topics; ++j)
      m_word_pyps.at(i).push_back(new MPIPYP<int>(0.5, 1.0));
  }
  if (m_am_root) std::cerr << std::endl;

  m_document_pyps.reserve(local_documents);
  //m_document_pyps.reserve(corpus.num_documents());
  //for (int j=0; j<corpus.num_documents(); ++j)
  for (int j=0; j<local_documents; ++j)
    m_document_pyps.push_back(new PYP<int>(0.5, 1.0));

  m_topic_p0 = 1.0/m_num_topics;
  m_term_p0 = 1.0/corpus.num_types();
  m_backoff_p0 = 1.0/corpus.num_documents();

  if (m_am_root) std::cerr << " Documents: " << corpus.num_documents() << "(" 
    << local_documents << ")" << " Terms: " << corpus.num_types() << std::endl;

  int frequency_cutoff = freq_cutoff_start;
  if (m_am_root) std::cerr << " Context frequency cutoff set to " << frequency_cutoff << std::endl;

  timer.Reset();
  // Initialisation pass
  int document_id=0, topic_counter=0;
  for (int i=0; i<local_documents; ++i) {
    document_id = i+m_mpi_start;

  //for (Corpus::const_iterator corpusIt=corpus.begin();
  //     corpusIt != corpus.end(); ++corpusIt, ++document_id) {
    m_corpus_topics.push_back(DocumentTopics(corpus.at(document_id).size(), 0));

    int term_index=0;
    for (Document::const_iterator docIt=corpus.at(document_id).begin();
         docIt != corpus.at(document_id).end(); ++docIt, ++term_index) {
      topic_counter++;
      Term term = *docIt;

      // sample a new_topic
      //int new_topic = (topic_counter % m_num_topics);
      int freq = corpus.context_count(term);
      int new_topic = -1;
      if (freq > frequency_cutoff
          && (!max_contexts_per_document || term_index < max_contexts_per_document)) {
        new_topic = document_id % m_num_topics;

        // add the new topic to the PYPs
        increment(term, new_topic);

        if (m_use_topic_pyp) {
          F p0 = m_topic_pyp.prob(new_topic, m_topic_p0);
          int table_delta = m_document_pyps.at(i).increment(new_topic, p0);
          if (table_delta)
            m_topic_pyp.increment(new_topic, m_topic_p0, rnd);
        }
        else m_document_pyps.at(i).increment(new_topic, m_topic_p0);
      }

      m_corpus_topics.at(i).at(term_index) = new_topic;
    }
  }

  // Synchronise the topic->word counds across the processes.
  synchronise();

  if (m_am_root) std::cerr << "  Initialized in " << timer.Elapsed() << " seconds\n";

  int* randomDocIndices = new int[local_documents];
  for (int i = 0; i < local_documents; ++i)
	  randomDocIndices[i] = i;

  // Sampling phase
  for (int curr_sample=0; curr_sample < samples; ++curr_sample) {
    if (freq_cutoff_interval > 0 && curr_sample != 1
        && curr_sample % freq_cutoff_interval == 1
        && frequency_cutoff > freq_cutoff_end) {
      frequency_cutoff--;
      if (m_am_root) std::cerr << "\n Context frequency cutoff set to " << frequency_cutoff << std::endl;
    }

    if (m_am_root) std::cerr << "\n  -- Sample " << curr_sample << " "; std::cerr.flush();

    // Randomize the corpus indexing array
    int tmp;
    int processed_terms=0;
    for (int i = (local_documents-1); i > 0; --i) {
      //i+1 since j \in [0,i] but rnd() \in [0,1)
    	int j = (int)(rnd() * (i+1));
      assert(j >= 0 && j <= i);
     	tmp = randomDocIndices[i];
    	randomDocIndices[i] = randomDocIndices[j];
    	randomDocIndices[j] = tmp;
    }

    // for each document in the corpus
    for (int rand_doc=0; rand_doc<local_documents; ++rand_doc) {
    	int doc_index = randomDocIndices[rand_doc];
    	int document_id = doc_index + m_mpi_start;
      const Document& doc = corpus.at(document_id);

      // for each term in the document
      int term_index=0;
      Document::const_iterator docEnd = doc.end();
      for (Document::const_iterator docIt=doc.begin();
           docIt != docEnd; ++docIt, ++term_index) {

        if (max_contexts_per_document && term_index > max_contexts_per_document)
          break;
        
        Term term = *docIt;
        int freq = corpus.context_count(term);
        if (freq < frequency_cutoff)
          continue;

        processed_terms++;

        // remove the prevous topic from the PYPs
        int current_topic = m_corpus_topics.at(doc_index).at(term_index);
        // a negative label mean that term hasn't been sampled yet
        if (current_topic >= 0) {
          decrement(term, current_topic);

          int table_delta = m_document_pyps.at(doc_index).decrement(current_topic);
          if (m_use_topic_pyp && table_delta < 0)
            m_topic_pyp.decrement(current_topic, rnd);
        }

        // sample a new_topic
        int new_topic = sample(doc_index, term);

        // add the new topic to the PYPs
        m_corpus_topics.at(doc_index).at(term_index) = new_topic;
        increment(term, new_topic);

        if (m_use_topic_pyp) {
          F p0 = m_topic_pyp.prob(new_topic, m_topic_p0);
          int table_delta = m_document_pyps.at(doc_index).increment(new_topic, p0);
          if (table_delta)
            m_topic_pyp.increment(new_topic, m_topic_p0, rnd);
        }
        else m_document_pyps.at(doc_index).increment(new_topic, m_topic_p0);
      }
      if (document_id && document_id % 10000 == 0) {
        if (m_am_root) std::cerr << "."; std::cerr.flush();
      }
    }
    std::cerr << "|"; std::cerr.flush();  

    // Synchronise the topic->word counds across the processes.
    synchronise();

    if (m_am_root) std::cerr << " ||| sampled " << processed_terms << " terms.";

    if (curr_sample != 0 && curr_sample % 10 == 0) {
      if (m_am_root) std::cerr << " ||| time=" << (timer.Elapsed() / 10.0) << " sec/sample" << std::endl;
      timer.Reset();
      if (m_am_root) std::cerr << "     ... Resampling hyperparameters"; std::cerr.flush();

      // resample the hyperparamters
      F log_p=0.0;
      for (std::vector<MPIPYPs>::iterator levelIt=m_word_pyps.begin();
           levelIt != m_word_pyps.end(); ++levelIt) {
        for (MPIPYPs::iterator pypIt=levelIt->begin();
             pypIt != levelIt->end(); ++pypIt) {
          pypIt->resample_prior(rnd);
          log_p += pypIt->log_restaurant_prob();
        }
      }

      for (PYPs::iterator pypIt=m_document_pyps.begin();
           pypIt != m_document_pyps.end(); ++pypIt) {
        pypIt->resample_prior(rnd);
        log_p += pypIt->log_restaurant_prob();
      }

      if (m_use_topic_pyp) {
        m_topic_pyp.resample_prior(rnd);
        log_p += m_topic_pyp.log_restaurant_prob();
      }

      std::cerr.precision(10);
      if (m_am_root) std::cerr << " ||| LLH=" << log_p << " ||| resampling time=" << timer.Elapsed() << " sec" << std::endl;
      timer.Reset();

      int k=0;
      if (m_am_root) std::cerr << "Topics distribution: ";
      std::cerr.precision(2);
      for (MPIPYPs::iterator pypIt=m_word_pyps.front().begin();
           pypIt != m_word_pyps.front().end(); ++pypIt, ++k) {
        if (m_am_root && k % 5 == 0) std::cerr << std::endl << '\t';
        if (m_am_root) std::cerr << "<" << k << ":" << pypIt->num_customers() << ","
          << pypIt->num_types() << "," << m_topic_pyp.prob(k, m_topic_p0) << "> ";
      }
      std::cerr.precision(4);
      if (m_am_root) std::cerr << std::endl;
    }
  }
  delete [] randomDocIndices;
}

void MPIPYPTopics::synchronise() {
  // Synchronise the topic->word counds across the processes.
  //for (std::vector<MPIPYPs>::iterator levelIt=m_word_pyps.begin();
  //     levelIt != m_word_pyps.end(); ++levelIt) {
//  std::vector<MPIPYPs>::iterator levelIt=m_word_pyps.begin(); 
//  {
//    for (MPIPYPs::iterator pypIt=levelIt->begin(); pypIt != levelIt->end(); ++pypIt) {
    for (size_t label=0; label < m_word_pyps.at(0).size(); ++label) {
      MPIPYP<int>& pyp = m_word_pyps.at(0).at(label);

      //if (!m_am_root) boost::mpi::communicator().barrier();
      //std::cerr << "Before Sync Process " << m_rank << ":";
      //pyp.debug_info(std::cerr); std::cerr << std::endl;
      //if (m_am_root) boost::mpi::communicator().barrier();

      MPIPYP<int>::dish_delta_type delta;
      pyp.synchronise(&delta);

      for (MPIPYP<int>::dish_delta_type::const_iterator it=delta.begin(); it != delta.end(); ++it) {
        int count = it->second;
        if (count > 0)
          for (int i=0; i < count; ++i) increment(it->first, label);
        if (count < 0)
          for (int i=0; i > count; --i) decrement(it->first, label);
      }
      pyp.reset_deltas();

      //if (!m_am_root) boost::mpi::communicator().barrier();
      //std::cerr << "After Sync Process " << m_rank << ":";
      //pyp.debug_info(std::cerr); std::cerr << std::endl;
      //if (m_am_root) boost::mpi::communicator().barrier();
    }
//  }
    // Synchronise the hierarchical topic pyp
    MPIPYP<int>::dish_delta_type topic_delta;
    m_topic_pyp.synchronise(&topic_delta);
    for (MPIPYP<int>::dish_delta_type::const_iterator it=topic_delta.begin(); it != topic_delta.end(); ++it) {
      int count = it->second;
      if (count > 0)
        for (int i=0; i < count; ++i) 
          m_topic_pyp.increment(it->first, m_topic_p0, rnd);
      if (count < 0)
        for (int i=0; i > count; --i) 
          m_topic_pyp.decrement(it->first, rnd);
    }
    m_topic_pyp.reset_deltas();
}

void MPIPYPTopics::decrement(const Term& term, int topic, int level) {
  //std::cerr << "MPIPYPTopics::decrement(" << term << "," << topic << "," << level << ")" << std::endl;
  m_word_pyps.at(level).at(topic).decrement(term, rnd);
  if (m_backoff.get()) {
    Term backoff_term = (*m_backoff)[term];
    if (!m_backoff->is_null(backoff_term))
      decrement(backoff_term, topic, level+1);
  }
}

void MPIPYPTopics::increment(const Term& term, int topic, int level) {
  //std::cerr << "MPIPYPTopics::increment(" << term << "," << topic << "," << level << ")" << std::endl;
  m_word_pyps.at(level).at(topic).increment(term, word_pyps_p0(term, topic, level), rnd);

  if (m_backoff.get()) {
    Term backoff_term = (*m_backoff)[term];
    if (!m_backoff->is_null(backoff_term))
      increment(backoff_term, topic, level+1);
  }
}

int MPIPYPTopics::sample(const DocumentId& doc, const Term& term) {
  // First pass: collect probs
  F sum=0.0;
  std::vector<F> sums;
  for (int k=0; k<m_num_topics; ++k) {
    F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp) topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    //F p_k_d = m_document_pyps[doc].prob(k, topic_prob);
    F p_k_d = m_document_pyps.at(doc).unnormalised_prob(k, topic_prob);

    sum += (p_w_k*p_k_d);
    sums.push_back(sum);
  }
  // Second pass: sample a topic
  F cutoff = rnd() * sum;
  for (int k=0; k<m_num_topics; ++k) {
    if (cutoff <= sums[k])
      return k;
  }
  std::cerr << cutoff << " " << sum << std::endl;
  assert(false);
}

MPIPYPTopics::F MPIPYPTopics::word_pyps_p0(const Term& term, int topic, int level) const {
  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "MPIPYPTopics::word_pyps_p0(" << term << "," << topic << "," << level << ")" << std::endl;

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
  //std::cerr << "MPIPYPTopics::word_pyps_p0(" << term << "," << topic << "," << level << ") = " << p0 << std::endl;
  return p0;
}

MPIPYPTopics::F MPIPYPTopics::prob(const Term& term, int topic, int level) const {
  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "MPIPYPTopics::prob(" << term << "," << topic << "," << level << " " << factor << ")" << std::endl;

  F p0 = word_pyps_p0(term, topic, level);
  F p_w_k = m_word_pyps.at(level).at(topic).prob(term, p0);

  //for (int i=0; i<level+1; ++i) std::cerr << "  ";
  //std::cerr << "MPIPYPTopics::prob(" << term << "," << topic << "," << level << ") = " << p_w_k << std::endl;

  return p_w_k;
}

int MPIPYPTopics::max_topic() const {
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

int MPIPYPTopics::max(const DocumentId& true_doc) const {
  //std::cerr << "MPIPYPTopics::max(" << doc << "," << term << ")" << std::endl;
  // collect probs
  F current_max=0.0;
  DocumentId local_doc = true_doc - m_mpi_start;
  int current_topic=-1;
  for (int k=0; k<m_num_topics; ++k) {
    //F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp)
      topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    F prob = 0;
    if (local_doc < 0) prob = topic_prob;
    else               prob = m_document_pyps.at(local_doc).prob(k, topic_prob);

    if (prob > current_max) {
      current_max = prob;
      current_topic = k;
    }
  }
  assert(current_topic >= 0);
  return current_topic;
}

int MPIPYPTopics::max(const DocumentId& true_doc, const Term& term) const {
  //std::cerr << "MPIPYPTopics::max(" << doc << "," << term << ")" << std::endl;
  // collect probs
  F current_max=0.0;
  DocumentId local_doc = true_doc - m_mpi_start;
  int current_topic=-1;
  for (int k=0; k<m_num_topics; ++k) {
    F p_w_k = prob(term, k);

    F topic_prob = m_topic_p0;
    if (m_use_topic_pyp)
      topic_prob = m_topic_pyp.prob(k, m_topic_p0);

    F p_k_d = 0;
    if (local_doc < 0) p_k_d = topic_prob;
    else               p_k_d = m_document_pyps.at(local_doc).prob(k, topic_prob);

    F prob = (p_w_k*p_k_d);
    if (prob > current_max) {
      current_max = prob;
      current_topic = k;
    }
  }
  assert(current_topic >= 0);
  return current_topic;
}

std::ostream& MPIPYPTopics::print_document_topics(std::ostream& out) const {
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

std::ostream& MPIPYPTopics::print_topic_terms(std::ostream& out) const {
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
