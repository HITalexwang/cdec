#ifndef DICT_H_
#define DICT_H_

#include <cassert>
#include <cstring>
#include <tr1/unordered_map>
#include <string>
#include <vector>

#include <boost/functional/hash.hpp>

#include "wordid.h"

class Dict {
 typedef std::tr1::unordered_map<std::string, WordID, boost::hash<std::string> > Map;

 public:
  Dict() : b0_("<bad0>") { words_.reserve(1000); }

  inline int max() const { return words_.size(); }

  inline WordID Convert(const std::string& word, bool frozen = false) {
    Map::iterator i = d_.find(word);
    if (i == d_.end()) {
      if (frozen)
        return 0;
      words_.push_back(word);
      d_[word] = words_.size();
      return words_.size();
    } else {
      return i->second;
    }
  }

  inline WordID Convert(const std::vector<std::string>& words, bool frozen = false) {
    std::string word= "";
    for (std::vector<std::string>::const_iterator it=words.begin();
         it != words.end(); ++it) {
      if (it != words.begin()) word += "__";
      word += *it;
    }

    return Convert(word, frozen);
  }

  inline const std::string& Convert(const WordID& id) const {
    if (id == 0) return b0_;
    assert(id <= words_.size());
    return words_[id-1];
  }

  void clear() { words_.clear(); d_.clear(); }

 private:
  const std::string b0_;
  std::vector<std::string> words_;
  Map d_;
};

#endif
