#ifndef CDEC_HASH_H
#define CDEC_HASH_H

#include <boost/functional/hash.hpp>

#include "murmur_hash.h"

#include "config.h"
#ifdef HAVE_SPARSEHASH
# include <google/dense_hash_map>
# define HASH_MAP google::dense_hash_map
# define HASH_MAP_RESERVED(h,empty,deleted) do { h.set_empty_key(empty); h.set_deleted_key(deleted); } while(0)
# define HASH_MAP_EMPTY(h,empty) do { h.set_empty_key(empty); } while(0)
#else
# include <tr1/unordered_map>
# define HASH_MAP std::tr1::unordered_map
# define HASH_MAP_RESERVED(h,empty,deleted)
# define HASH_MAP_EMPTY(h,empty)
#endif


// assumes C is POD
template <class C>
struct murmur_hash
{
  typedef MurmurInt return_type;
  typedef C /*const&*/ argument_type;
  return_type operator()(argument_type const& c) const {
    return MurmurHash((void*)&c,sizeof(c));
  }
};

// murmur_hash_array isn't std guaranteed safe (you need to use string::data())
template <>
struct murmur_hash<std::string>
{
  typedef MurmurInt return_type;
  typedef std::string /*const&*/ argument_type;
  return_type operator()(argument_type const& c) const {
    return MurmurHash(c.data(),c.size());
  }
};

// uses begin(),size() assuming contiguous layout and POD
template <class C>
struct murmur_hash_array
{
  typedef MurmurInt return_type;
  typedef C /*const&*/ argument_type;
  return_type operator()(argument_type const& c) const {
    return MurmurHash(&*c.begin(),c.size()*sizeof(*c.begin()));
  }
};


// adds default val to table if key wasn't found, returns ref to val
template <class H,class K>
typename H::mapped_type & get_default(H &ht,K const& k,typename H::mapped_type const& v) {
  return const_cast<typename H::mapped_type &>(ht.insert(typename H::value_type(k,v)).first->second);
}

// the below could also return a ref to the mapped max/min.  they have the advantage of not falsely claiming an improvement when an equal value already existed.  otherwise you could just modify the get_default and if equal assume new.
template <class H,class K>
bool improve_mapped_max(H &ht,K const& k,typename H::mapped_type const& v) {
  std::pair<typename H::iterator,bool> inew=ht.insert(typename H::value_type(k,v));
  if (inew.second) return true;
  typedef typename H::mapped_type V;
  V &oldv=const_cast<V&>(inew.first->second);
  if (oldv<v) {
    oldv=v;
    return true;
  }
  return false;
}

template <class H,class K>
bool improve_mapped_min(H &ht,K const& k,typename H::mapped_type const& v) {
  std::pair<typename H::iterator,bool> inew=ht.insert(typename H::value_type(k,v));
  if (inew.second) return true;
  typedef typename H::mapped_type V;
  V &oldv=const_cast<V&>(inew.first->second);
  if (v<oldv) { // the only difference from above
    oldv=v;
    return true;
  }
  return false;
}

#endif