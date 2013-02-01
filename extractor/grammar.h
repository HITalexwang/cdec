#ifndef _GRAMMAR_H_
#define _GRAMMAR_H_

#include <iostream>
#include <string>
#include <vector>

using namespace std;

class Rule;

class Grammar {
 public:
  Grammar(const vector<Rule>& rules, const vector<string>& feature_names);

  friend ostream& operator<<(ostream& os, const Grammar& grammar);

 private:
  vector<Rule> rules;
  vector<string> feature_names;
};

#endif
