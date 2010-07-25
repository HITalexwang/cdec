#ifndef _HG_H_
#define _HG_H_


//FIXME: is the edge given to ffs the coarse (previous forest) edge?  if so, then INFO_EDGE is effectively not working.  supposed to have logging associated with each edge and see how it fits together in kbest afterwards.
#define USE_INFO_EDGE 1
#if USE_INFO_EDGE
# include <sstream>
# define INFO_EDGE(e,msg) do { std::ostringstream &o=(e.info_);o<<msg; } while(0)
# define INFO_EDGEw(e,msg) do { std::ostringstream &o(e.info_);if (o.empty()) o<<' ';o<<msg; } while(0)
#else
# define INFO_EDGE(e,msg)
# define INFO_EDGEw(e,msg)
#endif
#define INFO_EDGEln(e,msg) INFO_EDGE(e,msg<<'\n')

#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>

#include "feature_vector.h"
#include "small_vector.h"
#include "wordid.h"
#include "tdict.h"
#include "trule.h"
#include "prob.h"
#include "indices_after.h"

// if you define this, edges_ will be sorted
// (normally, just nodes_ are - root must be nodes_.back()), but this can be quite
// slow
#undef HG_EDGES_TOPO_SORTED

class Hypergraph;
typedef boost::shared_ptr<Hypergraph> HypergraphP;

// class representing an acyclic hypergraph
//  - edges have 1 head, 0..n tails
class Hypergraph {
public:
  Hypergraph() : is_linear_chain_(false) {}

  // SmallVector is a fast, small vector<int> implementation for sizes <= 2
  typedef SmallVectorInt TailNodeVector;
  typedef std::vector<int> EdgesVector;

  // TODO get rid of cat_?
  // TODO keep cat_ and add span and/or state? :)
  struct Node {
    Node() : id_(), cat_(), promise(1) {}
    int id_; // equal to this object's position in the nodes_ vector
    WordID cat_;  // non-terminal category if <0, 0 if not set
    EdgesVector in_edges_;   // contents refer to positions in edges_
    EdgesVector out_edges_;  // contents refer to positions in edges_
    double promise; // set in global pruning; in [0,infty) so that mean is 1.  use: e.g. scale cube poplimit
    void copy_fixed(Node const& o) { // nonstructural fields only - structural ones are managed by sorting/pruning/subsetting
      cat_=o.cat_;
      promise=o.promise;
    }
    void copy_reindex(Node const& o,indices_after const& n2,indices_after const& e2) {
      copy_fixed(o);
      id_=n2[id_];
      e2.reindex_push_back(o.in_edges_,in_edges_);
      e2.reindex_push_back(o.out_edges_,out_edges_);
    }

  };

  // TODO get rid of edge_prob_? (can be computed on the fly as the dot
  // product of the weight vector and the feature values)
  struct Edge {
    Edge() : i_(-1), j_(-1), prev_i_(-1), prev_j_(-1) {}
    inline int Arity() const { return tail_nodes_.size(); }
    int head_node_;               // refers to a position in nodes_
    TailNodeVector tail_nodes_;   // contents refer to positions in nodes_
    TRulePtr rule_;
    SparseVector<double> feature_values_;
    prob_t edge_prob_;             // dot product of weights and feat_values
    int id_;   // equal to this object's position in the edges_ vector

    // span info. typically, i_ and j_ refer to indices in the source sentence
    // if a synchronous parse has been executed i_ and j_ will refer to indices
    // in the target sentence / lattice and prev_i_ prev_j_ will refer to
    // positions in the source.  Note: it is up to the translator implementation
    // to properly set these values.  For some models (like the Forest-input
    // phrase based model) it may not be straightforward to do.  if these values
    // are not properly set, most things will work but alignment and any features
    // that depend on them will be broken.
    short int i_;
    short int j_;
    short int prev_i_;
    short int prev_j_;

    void copy_fixed(Edge const& o) {
      rule_=o.rule_;
      feature_values_ = o.feature_values_;
      edge_prob_ = o.edge_prob_;
      i_ = o.i_; j_ = o.j_; prev_i_ = o.prev_i_; prev_j_ = o.prev_j_;
#if USE_INFO_EDGE
      info_.str(o.info_.str());
#endif
    }
    void copy_reindex(Edge const& o,indices_after const& n2,indices_after const& e2) {
      copy_fixed(o);
      head_node_=n2[o.head_node_];
      id_=e2[o.id_];
      n2.reindex_push_back(o.tail_nodes_,tail_nodes_);
    }

#if USE_INFO_EDGE
    std::ostringstream info_;

    Edge(Edge const& o) : head_node_(o.head_node_),tail_nodes_(o.tail_nodes_),rule_(o.rule_),feature_values_(o.feature_values_),edge_prob_(o.edge_prob_),id_(o.id_),i_(o.i_),j_(o.j_),prev_i_(o.prev_i_),prev_j_(o.prev_j_), info_(o.info_.str()) {  }
    void operator=(Edge const& o) {
      head_node_ = o.head_node_; tail_nodes_ = o.tail_nodes_; rule_ = o.rule_; feature_values_ = o.feature_values_; edge_prob_ = o.edge_prob_; id_ = o.id_; i_ = o.i_; j_ = o.j_; prev_i_ = o.prev_i_; prev_j_ = o.prev_j_;  info_.str(o.info_.str());
    }
    std::string info() const { return info_.str(); }
    void reset_info() { info_.str(""); info_.clear(); }
#else
    std::string info() const { return std::string(); }
    void reset_info() {  }
#endif
    void show(std::ostream &o,unsigned mask=SPAN|RULE) const {
      o<<'{';
      if (mask&CATEGORY)
        o<<TD::Convert(rule_->GetLHS());
      if (mask&PREV_SPAN)
        o<<'<'<<prev_i_<<','<<prev_j_<<'>';
      if (mask&SPAN)
        o<<'<'<<i_<<','<<j_<<'>';
      if (mask&PROB)
        o<<" p="<<edge_prob_;
      if (mask&FEATURES)
        o<<" "<<feature_values_;
      if (mask&RULE)
        o<<rule_->AsString(mask&RULE_LHS);
      if (USE_INFO_EDGE) {
        if (mask) o << ' ';
        o<<info();
      }
      o<<'}';
    }
    std::string show(unsigned mask=SPAN|RULE) const {
      std::ostringstream o;
      show(o,mask);
      return o.str();
    }
    /* generic recursion re: child_handle=re(tail_nodes_[i],i,parent_handle)

       FIXME: make kbest create a simple derivation-tree structure (could be a
       hg), and replace the list-of-edges viterbi.h with a tree-structured one.
       CreateViterbiHypergraph can do for 1best, though.
    */
    template <class EdgeRecurse,class EdgeHandle>
    std::string derivation_tree(EdgeRecurse const& re,EdgeHandle const& eh,bool indent=true,int show_mask=SPAN|RULE,int maxdepth=0x7FFFFFFF,int depth=0) const {
      std::ostringstream o;
      derivation_tree_stream(re,eh,o,indent,show_mask,maxdepth,depth);
      return o.str();
    }
    template <class EdgeRecurse,class EdgeHandle>
    void derivation_tree_stream(EdgeRecurse const& re,EdgeHandle const& eh,std::ostream &o,bool indent=true,int show_mask=SPAN|RULE,int maxdepth=0x7FFFFFFF,int depth=0) const {
      if (depth>maxdepth) return;
      if (indent) for (int i=0;i<depth;++i) o<<' ';
      o<<'(';
      show(o,show_mask);
      if (indent) o<<'\n';
      for (int i=0;i<tail_nodes_.size();++i) {
        EdgeHandle c=re(tail_nodes_[i],i,eh);
        Edge const* cp=c;
        if (cp) {
          cp->derivation_tree_stream(re,c,o,indent,show_mask,maxdepth,depth+1);
          if (!indent) o<<' ';
        }
      }
      if (indent) for (int i=0;i<depth;++i) o<<' ';
      o<<")";
      if (indent) o<<"\n";
    }
  };

  std::string show_viterbi_tree(bool indent=true,int show_mask=SPAN|RULE,int maxdepth=0x7FFFFFFF,int depth=0) const;

  typedef Edge const* EdgeHandle;
  EdgeHandle operator()(int tailn,int /*taili*/,EdgeHandle /*parent*/) const {
    return viterbi_edge(tailn);
  }

  Edge const* viterbi_edge(int node) const { // even if edges are sorted by edgeweights, this doesn't give viterbi
    EdgesVector const& v=nodes_[node].in_edges_;
    return v.empty() ? 0 : &edges_[v.front()];
  }

  enum {
    NONE=0,CATEGORY=1,SPAN=2,PROB=4,FEATURES=8,RULE=16,RULE_LHS=32,PREV_SPAN=64,ALL=0xFFFFFFFF
  };

  // returns edge with rule_.IsGoal, returns 0 if none found.  otherwise gives best edge_prob_ - note: I don't think edge_prob_ is viterbi cumulative, so this would just be the best local probability.
  Edge const* ViterbiGoalEdge() const;

  void swap(Hypergraph& other) {
    other.nodes_.swap(nodes_);
    std::swap(is_linear_chain_, other.is_linear_chain_);
    other.edges_.swap(edges_);
  }

  void ResizeNodes(int size) {
    nodes_.resize(size);
    for (int i = 0; i < size; ++i) nodes_[i].id_ = i;
  }

  // reserves space in the nodes vector to prevent memory locations
  // from changing
  void ReserveNodes(size_t n, size_t e = 0) {
    nodes_.reserve(n);
    if (e) edges_.reserve(e);
  }

  Edge* AddEdge(const TRulePtr& rule, const TailNodeVector& tail) {
    edges_.push_back(Edge());
    Edge* edge = &edges_.back();
    edge->rule_ = rule;
    edge->tail_nodes_ = tail;
    edge->id_ = edges_.size() - 1;
    for (int i = 0; i < edge->tail_nodes_.size(); ++i)
      nodes_[edge->tail_nodes_[i]].out_edges_.push_back(edge->id_);
    return edge;
  }

  Node* AddNode(const WordID& cat) {
    nodes_.push_back(Node());
    nodes_.back().cat_ = cat;
    nodes_.back().id_ = nodes_.size() - 1;
    return &nodes_.back();
  }

  void ConnectEdgeToHeadNode(const int edge_id, const int head_id) {
    edges_[edge_id].head_node_ = head_id;
    nodes_[head_id].in_edges_.push_back(edge_id);
  }

  // TODO remove this - use the version that takes indices
  void ConnectEdgeToHeadNode(Edge* edge, Node* head) {
    edge->head_node_ = head->id_;
    head->in_edges_.push_back(edge->id_);
  }

  // merge the goal node from other with this goal node
  void Union(const Hypergraph& other);

  void PrintGraphviz() const;

  // compute the total number of paths in the forest
  double NumberOfPaths() const;

  // BEWARE. this assumes that the source and target language
  // strings are identical and that there are no loops.
  // It assumes a bunch of other things about where the
  // epsilons will be.  It tries to assert failure if you
  // break these assumptions, but it may not.
  // TODO - make this work
  void EpsilonRemove(WordID eps);

  // multiple the weights vector by the edge feature vector
  // (inner product) to set the edge probabilities
  template <typename V>
  void Reweight(const V& weights) {
    for (int i = 0; i < edges_.size(); ++i) {
      Edge& e = edges_[i];
      e.edge_prob_.logeq(e.feature_values_.dot(weights));
    }
  }

  typedef std::vector<prob_t> EdgeProbs;
  typedef std::vector<bool> EdgeMask;
  typedef std::vector<bool> NodeMask;

  // computes inside and outside scores for each
  // edge in the hypergraph
  // alpha->size = edges_.size = beta->size
  // returns inside prob of goal node
  prob_t ComputeEdgePosteriors(double scale,
                               EdgeProbs* posts) const;

  // find the score of the very best path passing through each edge
  prob_t ComputeBestPathThroughEdges(EdgeProbs* posts) const;


  /* for all of the below subsets, the hg Nodes must be topo sorted already*/

  // keep_nodes is an output-only param, keep_edges is in-out (more may be pruned than you asked for if the tails can't be built)
  HypergraphP CreateEdgeSubset(EdgeMask & keep_edges_in_out,NodeMask &keep_nodes_out) const;
  HypergraphP CreateEdgeSubset(EdgeMask & keep_edges) const;
  // node keeping is final (const), edge keeping is an additional constraint which will sometimes be made stricter (and output back to you) by the absence of nodes.  you may end up with some empty nodes.  that is, kept edges will be made consistent with kept nodes first (but empty nodes are allowed)
  HypergraphP CreateNodeSubset(NodeMask const& keep_nodes,EdgeMask &keep_edges_in_out) const {
    TightenEdgeMask(keep_edges_in_out,keep_nodes);
    return CreateNodeEdgeSubset(keep_nodes,keep_edges_in_out);
  }
  HypergraphP CreateNodeSubset(NodeMask const& keep_nodes) const {
    EdgeMask ke(edges_.size(),true);
    return CreateNodeSubset(keep_nodes,ke);
  }

  void TightenEdgeMask(EdgeMask &ke,NodeMask const& kn) const;
  // kept edges are consistent with kept nodes already:
  HypergraphP CreateNodeEdgeSubset(NodeMask const& keep_nodes,EdgeMask const&keep_edges_in_out) const;
  HypergraphP CreateNodeEdgeSubset(NodeMask const& keep_nodes) const;


  // create a new hypergraph consisting only of the nodes / edges
  // in the Viterbi derivation of this hypergraph
  // if edges is set, use the EdgeSelectEdgeWeightFunction
  // NOTE: last edge/node index are goal
  HypergraphP CreateViterbiHypergraph(const EdgeMask* edges = NULL) const;

  // move weights as near to the source as possible, resulting in a
  // stochastic automaton.  ONLY FUNCTIONAL FOR *LATTICES*.
  // See M. Mohri and M. Riley. A Weight Pushing Algorithm for Large
  //   Vocabulary Speech Recognition. 2001.
  // the log semiring (NOT tropical) is used
  void PushWeightsToSource(double scale = 1.0);
  // same, except weights are pushed to the goal, works for HGs,
  // not just lattices
  void PushWeightsToGoal(double scale = 1.0);

//  void SortInEdgesByEdgeWeights(); // local sort only - pretty useless

  void PruneUnreachable(int goal_node_id); // DEPRECATED

  void RemoveNoncoaccessibleStates(int goal_node_id = -1);

  // remove edges from the hypergraph if prune_edge[edge_id] is true
  // note: if run_inside_algorithm is false, then consumers may be unhappy if you pruned nodes that are built on by nodes that are kept.
  void PruneEdges(const EdgeMask& prune_edge, bool run_inside_algorithm = false);

  /// drop edge i if edge_margin[i] < prune_below, unless preserve_mask[i]
  void MarginPrune(EdgeProbs const& edge_margin,prob_t prune_below,EdgeMask const* preserve_mask=0,bool safe_inside=false,bool verbose=false);
  // promise[i]=((max_marginal[i]/viterbi)^power).todouble.  if normalize, ensure that avg promise is 1.
  typedef EdgeProbs NodeProbs;
  void SetPromise(NodeProbs const& inside,NodeProbs const& outside, double power=1, bool normalize=true);

  //TODO: in my opinion, looking at the ratio of logprobs (features \dot weights) rather than the absolute difference generalizes more nicely across sentence lengths and weight vectors that are constant multiples of one another.  at least make that an option.  i worked around this a little in cdec by making "beam alpha per source word" but that's not helping with different tuning runs.  this would also make me more comfortable about allocating promise

  // beam_alpha=0 means don't beam prune, otherwise drop things that are e^beam_alpha times worse than best -   // prunes any edge whose prob_t on the best path taking that edge is more than e^alpha times
  //density=0 means don't density prune:   // for density>=1.0, keep this many times the edges needed for the 1best derivation
  // worse than the score of the global best past (or the highest edge posterior)
  // scale is for use_sum_prod_semiring (sharpens distribution?)
  // promise_power is for a call to SetPromise (no call happens if power=0)
  // returns true if density pruning was tighter than beam
  // safe_inside would be a redundant anti-rounding error second bottom-up reachability before actually removing edges, to prevent stranded edges.  shouldn't be needed - if the hyperedges occur in defined-before-use (all edges with head h occur before h is used as a tail) order, then a grace margin for keeping edges that starts leniently and becomes more forbidding will make it impossible for this to occur, i.e. safe_inside=true is not needed.
  bool PruneInsideOutside(double beam_alpha,double density,const EdgeMask* preserve_mask = NULL,const bool use_sum_prod_semiring=false, const double scale=1,double promise_power=0,bool safe_inside=false);

  // legacy:
  void DensityPruneInsideOutside(const double scale, const bool use_sum_prod_semiring, const double density,const EdgeMask* preserve_mask = NULL) {
    PruneInsideOutside(0,density,preserve_mask,use_sum_prod_semiring,scale);
  }

  // legacy:
  void BeamPruneInsideOutside(const double scale, const bool use_sum_prod_semiring, const double alpha,const EdgeMask* preserve_mask = NULL) {
    PruneInsideOutside(alpha,0,preserve_mask,use_sum_prod_semiring,scale);
  }

  // report nodes, edges, paths
  std::string stats(std::string const& name="forest") const;

  void clear() {
    nodes_.clear();
    edges_.clear();
  }

  inline size_t NumberOfEdges() const { return edges_.size(); }
  inline size_t NumberOfNodes() const { return nodes_.size(); }
  inline bool empty() const { return nodes_.empty(); }

  // linear chains can be represented in a number of ways in a hypergraph,
  // we define them to consist only of lexical translations and monotonic rules
  inline bool IsLinearChain() const { return is_linear_chain_; }
  bool is_linear_chain_;

  // nodes_ is sorted in topological order
  typedef std::vector<Node> Nodes;
  Nodes nodes_;
  // edges_ is not guaranteed to be in any particular order
  typedef std::vector<Edge> Edges;
  Edges edges_;

  // reorder nodes_ so they are in topological order
  // source nodes at 0 sink nodes at size-1
  void TopologicallySortNodesAndEdges(int goal_idx,
                                      const EdgeMask* prune_edges = NULL);

  void set_ids(); // resync edge,node .id_
  void check_ids() const; // assert that .id_ have been kept in sync

private:
  Hypergraph(int num_nodes, int num_edges, bool is_lc) : is_linear_chain_(is_lc), nodes_(num_nodes), edges_(num_edges) {}

  static TRulePtr kEPSRule;
  static TRulePtr kUnaryRule;
};


// common WeightFunctions, map an edge -> WeightType
// for generic Viterbi/Inside algorithms
struct EdgeProb {
  typedef prob_t Weight;
  inline const prob_t& operator()(const Hypergraph::Edge& e) const { return e.edge_prob_; }
};

struct EdgeSelectEdgeWeightFunction {
  typedef prob_t Weight;
  typedef std::vector<bool> EdgeMask;
  EdgeSelectEdgeWeightFunction(const EdgeMask& v) : v_(v) {}
  inline prob_t operator()(const Hypergraph::Edge& e) const {
    if (v_[e.id_]) return prob_t::One();
    else return prob_t::Zero();
  }
private:
  const EdgeMask& v_;
};

struct ScaledEdgeProb {
  ScaledEdgeProb(const double& alpha) : alpha_(alpha) {}
  inline prob_t operator()(const Hypergraph::Edge& e) const { return e.edge_prob_.pow(alpha_); }
  const double alpha_;
  typedef prob_t Weight;
};

// see Li (2010), Section 3.2.2-- this is 'x_e = p_e*r_e'
struct EdgeFeaturesAndProbWeightFunction {
  typedef SparseVector<prob_t> Weight;
  typedef Weight Result; //TODO: change Result->Weight everywhere?
  inline const Weight operator()(const Hypergraph::Edge& e) const {
    SparseVector<prob_t> res;
    for (SparseVector<double>::const_iterator it = e.feature_values_.begin();
         it != e.feature_values_.end(); ++it)
      res.set_value(it->first, prob_t(it->second) * e.edge_prob_);
    return res;
  }
};

struct TransitionCountWeightFunction {
  typedef double Weight;
  inline double operator()(const Hypergraph::Edge& e) const { (void)e; return 1.0; }
};

#endif
