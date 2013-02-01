#include "scorer.h"

#include "features/feature.h"

Scorer::Scorer(const vector<shared_ptr<Feature> >& features) :
    features(features) {}

vector<double> Scorer::Score(const FeatureContext& context) const {
  vector<double> scores;
  for (auto feature: features) {
    scores.push_back(feature->Score(context));
  }
  return scores;
}

vector<string> Scorer::GetFeatureNames() const {
  vector<string> feature_names;
  for (auto feature: features) {
    feature_names.push_back(feature->GetName());
  }
  return feature_names;
}
