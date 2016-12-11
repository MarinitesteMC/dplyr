#ifndef dplyr_GroupedHybridCall_H
#define dplyr_GroupedHybridCall_H

#include <boost/scoped_ptr.hpp>

#include <tools/Call.h>

#include <dplyr/Result/Result.h>

#include <bindrcpp.h>

namespace dplyr {

  class IHybridCallback {
  protected:
    virtual ~IHybridCallback() {}

  public:
    virtual SEXP get_subset(const Symbol& name) const = 0;
  };

  class GroupedHybridEnv {
  public:
    GroupedHybridEnv(const CharacterVector& names_, const Environment& env_, const IHybridCallback* callback_) :
      names(names_), env(env_), callback(callback_), has_eval_env(false)
    {
      LOG_VERBOSE;
    }

  public:
    const Environment& get_eval_env() const {
      provide_eval_env();
      return eval_env;
    }

  private:
    void provide_eval_env() const {
      if (has_eval_env)
        return;

      // Environment::new_child() performs an R callback, creating the environment
      // in R should be slightly faster
      Environment active_env =
        create_env_symbol(
          names, &GroupedHybridEnv::hybrid_get_callback,
          PAYLOAD(const_cast<void*>(reinterpret_cast<const void*>(callback))), env);

      // If bindr (via bindrcpp) supported the creation of a child environment, we could save the
      // call to Rcpp_eval() triggered by active_env.new_child()
      eval_env = active_env.new_child(true);
      eval_env[".data"] = active_env;
      eval_env[".env"] = env;

      has_eval_env = true;
    }

    static SEXP hybrid_get_callback(const Symbol& name, bindrcpp::PAYLOAD payload) {
      LOG_VERBOSE;
      IHybridCallback* callback_ = reinterpret_cast<IHybridCallback*>(payload.p);
      return callback_->get_subset(name);
    }

  private:
    const CharacterVector names;
    const Environment env;
    const IHybridCallback* callback;

    mutable Environment eval_env;
    mutable bool has_eval_env;
  };

  template <typename Subsets>
  class GroupedHybridCall : public IHybridCallback {
  public:
    GroupedHybridCall(const Call& call_, Subsets& subsets_, const Environment& env_) :
      call(call_), indices(NULL), subsets(subsets_), env(env_),
      hybrid_env(subsets_.get_variable_names(), env_, this)
    {
      LOG_VERBOSE;
    }

    const SlicingIndex& get_indices() const {
      return *indices;
    }

  public: // IHybridCallback
    SEXP get_subset(const Symbol& name) const {
      LOG_VERBOSE;
      return subsets.get(name, get_indices());
    }

  public:
    SEXP eval(const SlicingIndex& indices_) {
      set_indices(indices_);
      SEXP ret = eval_with_indices();
      clear_indices();
      return ret;
    }

  private:
    void set_indices(const SlicingIndex& indices_) {
      indices = &indices_;
    }

    void clear_indices() {
      indices = NULL;
    }

    SEXP eval_with_indices() {
      Call call_ = clone(call);
      while (simplified(call_)) {}
      return eval_with_indices_simplified(call_);
    }

    SEXP eval_with_indices_simplified(const Call& call) {
      LOG_INFO << type2name(call);
      if (TYPEOF(call) == LANGSXP) {
        LOG_VERBOSE << "performing evaluation in eval_env";
        return Rcpp_eval(call, hybrid_env.get_eval_env());
      } else if (TYPEOF(call) == SYMSXP) {
        if (subsets.count(call)) {
          return subsets.get(call, get_indices());
        }
        return env.find(CHAR(PRINTNAME(call)));
      }
      return call;
    }

    bool simplified(Call& call) {
      LOG_VERBOSE;
      // initial
      if (TYPEOF(call) == LANGSXP) {
        boost::scoped_ptr<Result> res(get_handler(call, subsets, env));
        if (res) {
          // replace the call by the result of process
          call = res->process(get_indices());

          // no need to go any further, we simplified the top level
          return true;
        }
        return replace(CDR(call));
      }
      return false;
    }

    bool replace(SEXP p) {
      LOG_VERBOSE;
      SEXP obj = CAR(p);
      if (TYPEOF(obj) == LANGSXP) {
        boost::scoped_ptr<Result> res(get_handler(obj, subsets, env));
        if (res) {
          SETCAR(p, res->process(get_indices()));
          return true;
        }

        if (replace(CDR(obj))) return true;
      }

      if (TYPEOF(p) == LISTSXP) {
        return replace(CDR(p));
      }

      return false;
    }

  private:
    const Call& call;
    const SlicingIndex* indices;
    Subsets& subsets;
    Environment env;
    const GroupedHybridEnv hybrid_env;
  };

}
#endif
