// STL
#include <iostream>
#include <fstream>
#include <algorithm>
#include <iterator>

// Boost
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/mpi/environment.hpp>
#include <boost/mpi/communicator.hpp>

// Local
#include "mpi-pyp-topics.hh"
#include "corpus.hh"
#include "contexts_corpus.hh"
#include "gzstream.hh"

static const char *REVISION = "$Rev: 170 $";

// Namespaces
using namespace boost;
using namespace boost::program_options;
using namespace std;

int main(int argc, char **argv)
{
  mpi::environment env(argc, argv);
  mpi::communicator world;
  bool am_root = (world.rank() == 0);
  if (am_root) std::cout << "I am process " << world.rank() << " of " << world.size() << "." << std::endl;
  if (am_root) cout << "Pitman Yor topic models: Copyright 2010 Phil Blunsom\n";
  if (am_root) cout << REVISION << '\n' <<endl;

  ////////////////////////////////////////////////////////////////////////////////////////////
  // Command line processing
  variables_map vm; 

  // Command line processing
  {
    options_description cmdline_specific("Command line specific options");
    cmdline_specific.add_options()
      ("help,h", "print help message")
      ("config,c", value<string>(), "config file specifying additional command line options")
      ;
    options_description config_options("Allowed options");
    config_options.add_options()
      ("help,h", "print help message")
      ("data,d", value<string>(), "file containing the documents and context terms")
      ("topics,t", value<int>()->default_value(50), "number of topics")
      ("document-topics-out,o", value<string>(), "file to write the document topics to")
      ("default-topics-out", value<string>(), "file to write default term topic assignments.")
      ("topic-words-out,w", value<string>(), "file to write the topic word distribution to")
      ("samples,s", value<int>()->default_value(10), "number of sampling passes through the data")
      ("backoff-type", value<string>(), "backoff type: none|simple")
//      ("filter-singleton-contexts", "filter singleton contexts")
      ("hierarchical-topics", "Use a backoff hierarchical PYP as the P0 for the document topics distribution.")
      ("freq-cutoff-start", value<int>()->default_value(0), "initial frequency cutoff.")
      ("freq-cutoff-end", value<int>()->default_value(0), "final frequency cutoff.")
      ("freq-cutoff-interval", value<int>()->default_value(0), "number of iterations between frequency decrement.")
      ("max-contexts-per-document", value<int>()->default_value(0), "Only sample the n most frequent contexts for a document.")
      ;

    cmdline_specific.add(config_options);

    store(parse_command_line(argc, argv, cmdline_specific), vm); 
    notify(vm);

    if (vm.count("config") > 0) {
      ifstream config(vm["config"].as<string>().c_str());
      store(parse_config_file(config, config_options), vm); 
    }

    if (vm.count("help")) { 
      cout << cmdline_specific << "\n"; 
      return 1; 
    }
  }
  ////////////////////////////////////////////////////////////////////////////////////////////

  if (!vm.count("data")) {
    cerr << "Please specify a file containing the data." << endl;
    return 1;
  }

  // seed the random number generator: 0 = automatic, specify value otherwise
  unsigned long seed = 0; 
  MPIPYPTopics model(vm["topics"].as<int>(), vm.count("hierarchical-topics"), seed);

  // read the data
  BackoffGenerator* backoff_gen=0;
  if (vm.count("backoff-type")) {
    if (vm["backoff-type"].as<std::string>() == "none") {
      backoff_gen = 0;
    }
    else if (vm["backoff-type"].as<std::string>() == "simple") {
      backoff_gen = new SimpleBackoffGenerator();
    }
    else {
     cerr << "Backoff type (--backoff-type) must be one of none|simple." <<endl;
      return(1);
    }
  }

  ContextsCorpus contexts_corpus;
  contexts_corpus.read_contexts(vm["data"].as<string>(), backoff_gen, /*vm.count("filter-singleton-contexts")*/ false);
  model.set_backoff(contexts_corpus.backoff_index());

  if (backoff_gen) 
    delete backoff_gen;

  // train the sampler
  model.sample_corpus(contexts_corpus, vm["samples"].as<int>(),
                      vm["freq-cutoff-start"].as<int>(),
                      vm["freq-cutoff-end"].as<int>(),
                      vm["freq-cutoff-interval"].as<int>(),
                      vm["max-contexts-per-document"].as<int>());

  if (world.rank() == 0) {
    if (vm.count("document-topics-out")) {
      ogzstream documents_out(vm["document-topics-out"].as<string>().c_str());

      int document_id=0;
      map<int,int> all_terms;
      for (Corpus::const_iterator corpusIt=contexts_corpus.begin(); 
           corpusIt != contexts_corpus.end(); ++corpusIt, ++document_id) {
        vector<int> unique_terms;
        for (Document::const_iterator docIt=corpusIt->begin();
             docIt != corpusIt->end(); ++docIt) {
          if (unique_terms.empty() || *docIt != unique_terms.back())
            unique_terms.push_back(*docIt);
          // increment this terms frequency
          pair<map<int,int>::iterator,bool> insert_result = all_terms.insert(make_pair(*docIt,1));
          if (!insert_result.second) 
            all_terms[*docIt] = all_terms[*docIt] + 1;
          //insert_result.first++;
        }
        documents_out << contexts_corpus.key(document_id) << '\t';
        documents_out << model.max(document_id) << " " << corpusIt->size() << " ||| ";
        for (std::vector<int>::const_iterator termIt=unique_terms.begin();
             termIt != unique_terms.end(); ++termIt) {
          if (termIt != unique_terms.begin())
            documents_out << " ||| ";
          vector<std::string> strings = contexts_corpus.context2string(*termIt);
          copy(strings.begin(), strings.end(),ostream_iterator<std::string>(documents_out, " "));
          documents_out << "||| C=" << model.max(document_id, *termIt);

        }
        documents_out <<endl;
      }
      documents_out.close();

      if (vm.count("default-topics-out")) {
        ofstream default_topics(vm["default-topics-out"].as<string>().c_str());
        default_topics << model.max_topic() <<endl;
        for (std::map<int,int>::const_iterator termIt=all_terms.begin(); termIt != all_terms.end(); ++termIt) {
          vector<std::string> strings = contexts_corpus.context2string(termIt->first);
          default_topics << model.max(-1, termIt->first) << " ||| " << termIt->second << " ||| ";
          copy(strings.begin(), strings.end(),ostream_iterator<std::string>(default_topics, " "));
          default_topics <<endl;
        }
      }
    }

    if (vm.count("topic-words-out")) {
      ogzstream topics_out(vm["topic-words-out"].as<string>().c_str());
      model.print_topic_terms(topics_out);
      topics_out.close();
    }

    cout <<endl;
  }

  return 0;
}
