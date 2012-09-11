#ifndef SEARCH_VERTEX__
#define SEARCH_VERTEX__

#include "lm/left.hh"
#include "search/final.hh"
#include "search/types.hh"

#include <boost/unordered_set.hpp>

#include <queue>
#include <vector>

#include <stdint.h>

namespace search {

class ContextBase;

class Edge;

class VertexNode {
  public:
    VertexNode() : end_(NULL) {}

    void InitRoot() {
      extend_.clear();
      state_.left.full = false;
      state_.left.length = 0;
      state_.right.length = 0;
      right_full_ = false;
      bound_ = -kScoreInf;
      end_ = NULL;
    }

    lm::ngram::ChartState &MutableState() { return state_; }
    bool &MutableRightFull() { return right_full_; }

    void AddExtend(VertexNode *next) {
      extend_.push_back(next);
    }

    void SetEnd(Final *end) { end_ = end; }
    
    Final &MutableEnd() { return *end_; }

    void SortAndSet(ContextBase &context, VertexNode **parent_pointer);

    // Should only happen to a root node when the entire vertex is empty.   
    bool Empty() const {
      return !end_ && extend_.empty();
    }

    bool Complete() const {
      return end_;
    }

    const lm::ngram::ChartState &State() const { return state_; }
    bool RightFull() const { return right_full_; }

    Score Bound() const {
      return bound_;
    }

    unsigned char Length() const {
      return state_.left.length + state_.right.length;
    }

    // May be NULL.
    const Final *End() const { return end_; }

    const VertexNode &operator[](size_t index) const {
      return *extend_[index];
    }

    size_t Size() const {
      return extend_.size();
    }

  private:
    std::vector<VertexNode*> extend_;

    lm::ngram::ChartState state_;
    bool right_full_;

    Score bound_;
    Final *end_;
};

class PartialVertex {
  public:
    PartialVertex() {}

    explicit PartialVertex(const VertexNode &back) : back_(&back), index_(0) {}

    bool Empty() const { return back_->Empty(); }

    bool Complete() const { return back_->Complete(); }

    const lm::ngram::ChartState &State() const { return back_->State(); }
    bool RightFull() const { return back_->RightFull(); }

    Score Bound() const { return Complete() ? back_->End()->Bound() : (*back_)[index_].Bound(); }

    unsigned char Length() const { return back_->Length(); }

    // Split into continuation and alternative, rendering this the alternative.
    bool Split(PartialVertex &continuation) {
      assert(!Complete());
      continuation.back_ = &((*back_)[index_]);
      continuation.index_ = 0;
      if (index_ + 1 < back_->Size()) {
        ++index_;
        return true;
      }
      return false;
    }

    const Final &End() const {
      return *back_->End();
    }

  private:
    const VertexNode *back_;
    unsigned int index_;
};

extern PartialVertex kBlankPartialVertex;

class Vertex {
  public:
    Vertex() 
#ifdef DEBUG
      : finished_adding_(false)
#endif
    {}

    void Add(Edge &edge) {
#ifdef DEBUG
      assert(!finished_adding_);
#endif
      edges_.push_back(&edge);
    }

    void FinishedAdding() {
#ifdef DEBUG
      assert(!finished_adding_);
      finished_adding_ = true;
#endif
    }

    PartialVertex RootPartial() const { return PartialVertex(root_); }

  private:
    friend class VertexGenerator;
    std::vector<Edge*> edges_;

#ifdef DEBUG
    bool finished_adding_;
#endif

    VertexNode root_;
};

} // namespace search
#endif // SEARCH_VERTEX__
