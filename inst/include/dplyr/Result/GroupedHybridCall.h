#ifndef dplyr_GroupedHybridCall_H
#define dplyr_GroupedHybridCall_H

#include <boost/scoped_ptr.hpp>

#include <tools/Call.h>

#include <dplyr/Result/Result.h>

namespace dplyr {

  template <typename Subsets>
  class GroupedHybridCall {
  public:
    GroupedHybridCall(const Call& call_, const SlicingIndex& indices_, Subsets& subsets_, const Environment& env_) :
      call(clone(call_)), indices(indices_), subsets(subsets_), env(create_subset_env(subsets_, env_))
    {
      LOG_VERBOSE;
      while (simplified()) {}
    }

    static Environment create_subset_env(const Subsets& subsets, Environment parent) {
      return parent;
    }

    SEXP eval() {
      LOG_INFO << type2name(call);
      if (TYPEOF(call) == LANGSXP) {
        substitute(call);
        return Rcpp_eval(call, env);
      } else if (TYPEOF(call) == SYMSXP) {
        if (subsets.count(call)) {
          return subsets.get(call, indices);
        }
        return env.find(CHAR(PRINTNAME(call)));
      }
      return call;
    }

  private:

    void substitute(SEXP obj) {
      LOG_VERBOSE << "obj: " << type2name(obj);
      if (! Rf_isNull(obj)) {
        SEXP head = CAR(obj);
        LOG_VERBOSE << "head: " << type2name(head);

        switch (TYPEOF(head)) {
        case LISTSXP:
          substitute(CDR(head));
          break;

        case LANGSXP:
        {
          SEXP symb = CAR(head);
          LOG_VERBOSE << "symb: " << CHAR(PRINTNAME(symb));
          if (symb == R_DollarSymbol /* "$" */ || symb == Rf_install("@") || symb == Rf_install("::") || symb == Rf_install(":::")) {

            if (TYPEOF(CADR(head)) == LANGSXP) {
              substitute(CDR(head));
            }

            // deal with foo$bar( bla = boom )
            if (TYPEOF(CADDR(head)) == LANGSXP) {
              substitute(CDDR(head));
            }

            break;
          }

          substitute(CDR(head));
          break;
        }
        case SYMSXP:
          if (TYPEOF(obj) != LANGSXP) {
            if (subsets.count(head)) {
              SETCAR(obj, subsets.get(head, indices));
            }
          }
          break;
        }
        substitute(CDR(obj));
      }
    }

    bool simplified() {
      LOG_VERBOSE;
      // initial
      if (TYPEOF(call) == LANGSXP) {
        boost::scoped_ptr<Result> res(get_handler(call, subsets, env));
        if (res) {
          // replace the call by the result of process
          call = res->process(indices);

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
          SETCAR(p, res->process(indices));
          return true;
        }

        if (replace(CDR(obj))) return true;
      }

      if (TYPEOF(p) == LISTSXP) {
        return replace(CDR(p));
      }

      return false;
    }

    Call call;
    const SlicingIndex& indices;
    Subsets& subsets;
    const Environment env;
  };

}
#endif
