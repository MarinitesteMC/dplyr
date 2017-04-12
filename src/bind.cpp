#include "pch.h"
#include <dplyr/main.h>

#include <boost/scoped_ptr.hpp>

#include <tools/all_na.h>
#include <tools/collapse.h>
#include <tools/DotsOf.h>
#include <tools/pointer_vector.h>
#include <tools/utils.h>

#include <dplyr/GroupedDataFrame.h>
#include <dplyr/Collecter.h>

using namespace Rcpp;
using namespace dplyr;


// From Rcpp::DataFrame
static
int df_rows_length(SEXP df) {
  SEXP n = R_NilValue;
  SEXP attrs = ATTRIB(df);
  while (attrs != R_NilValue) {
    if (TAG(attrs) == R_RowNamesSymbol) {
      n = CAR(attrs) ;
      break ;
    }
    attrs = CDR(attrs) ;
  }

  if (n == R_NilValue)
    return 0;
  else if (TYPEOF(n) == INTSXP && LENGTH(n) == 2 && INTEGER(n)[0] == NA_INTEGER)
    return abs(INTEGER(n)[1]);
  else
    return LENGTH(n);
}

static
size_t rows_length(SEXP x, bool rowwise) {
  if (TYPEOF(x) == VECSXP) {
    if (Rf_inherits(x, "data.frame"))
      return df_rows_length(x);
    else if (Rf_length(x) > 0)
      return Rf_length(VECTOR_ELT(x, 0));
    else
      return 0;
  } else {
    if (rowwise)
      return 1;
    else
      return Rf_length(x);
  }
}
static
size_t cols_length(SEXP x) {
  if (TYPEOF(x) == VECSXP)
    return Rf_length(x);
  else
    return 1;
}

static
void inner_vector_check(SEXP x, int nrows, const char* fn) {
  if (!is_vector(x))
    stop("`%s()` expects data frames and named atomic vectors 2", fn);

  if (OBJECT(x)) {
    if (Rf_inherits(x, "data.frame"))
      stop("`%s()` does not support nested data frames", fn);
    if (Rf_inherits(x, "POSIXlt"))
      stop("`%s()` does not support POSIXlt columns", fn);
  }

  if (Rf_length(x) != nrows)
    stop("incompatible sizes (%d != %s)", nrows, Rf_length(x));
}
static
void rbind_vector_check(SEXP x, size_t nrows) {
  if (rows_length(x, true) != nrows)
    stop("incompatible sizes (%d != %s)", nrows, rows_length(x, true));

  switch (TYPEOF(x)) {
  case LGLSXP:
  case INTSXP:
  case REALSXP:
  case CPLXSXP:
  case STRSXP:
  case RAWSXP: {
    if (vec_names(x) != R_NilValue)
      break;
    stop("`bind_rows()` expects data frames and named atomic vectors");
  }
  case VECSXP: {
    if (!OBJECT(x) || Rf_inherits(x, "data.frame"))
      break;
  }
  default:
    stop("`bind_rows()` expects data frames and named atomic vectors");
  }
}
static
void cbind_vector_check(SEXP x, size_t nrows, SEXP contr, int i) {
  if (is_atomic(x) && !has_name_at(contr, i))
    stop("`bind_cols()` expects data frames and named atomic vectors");
  if (rows_length(x, false) != nrows)
    stop("incompatible sizes (%d != %s)", nrows, rows_length(x, false));
}

static
void rbind_type_check(SEXP x, int nrows) {
  int n = Rf_length(x);
  if (n == 0)
    return;

  rbind_vector_check(x, nrows);

  if (TYPEOF(x) == VECSXP) {
    for (int i = 0; i < n; i++)
      inner_vector_check(VECTOR_ELT(x, i), nrows, "bind_rows");
  }
}
static
void cbind_type_check(SEXP x, int nrows, SEXP contr, int i) {
  int n = Rf_length(x);
  if (n == 0)
    return;

  cbind_vector_check(x, nrows, contr, i);

  if (TYPEOF(x) == VECSXP) {
    if (OBJECT(x) && !Rf_inherits(x, "data.frame"))
      stop("`bind_cols()` expects data frames and named atomic vectors");
    for (int i = 0; i < n; i++)
      inner_vector_check(VECTOR_ELT(x, i), nrows, "bind_cols");
  }
}

extern "C"
bool bind_spliceable(SEXP x) {
  if (TYPEOF(x) != VECSXP)
    return false;

  if (OBJECT(x))
    return Rf_inherits(x, "spliced");

  for (size_t i = 0; i != Rf_length(x); ++i) {
    if (is_atomic(VECTOR_ELT(x, i)))
      return false;
  }

  return true;
}

List rbind__impl(List dots, SEXP id = R_NilValue) {
  int ndata = dots.size();
  int n = 0;
  std::vector<SEXP> chunks;
  std::vector<int> df_nrows;
  std::vector<String> dots_names;

  chunks.reserve(ndata);
  df_nrows.reserve(ndata);
  dots_names.reserve(ndata);

  int k = 0;
  for (int i = 0; i < ndata; i++) {
    SEXP obj = dots[i];
    if (Rf_isNull(obj)) continue;
    chunks.push_back(obj);
    size_t nrows = rows_length(chunks[k], true);
    df_nrows.push_back(nrows);
    n += nrows;
    if (!Rf_isNull(id)) {
      dots_names.push_back(name_at(dots, i));
    }
    k++;
  }
  ndata = chunks.size();
  pointer_vector<Collecter> columns;

  std::vector<String> names;

  k = 0;
  Function enc2native("enc2native");
  for (int i = 0; i < ndata; i++) {
    Rcpp::checkUserInterrupt();

    SEXP df = chunks[i];
    int nrows = df_nrows[i];
    rbind_type_check(df, nrows);

    CharacterVector df_names = enc2native(vec_names(df));
    for (int j = 0; j < Rf_length(df); j++) {

      SEXP source;
      int offset;
      if (TYPEOF(df) == VECSXP) {
        source = VECTOR_ELT(df, j);
        offset = 0;
      } else {
        source = df;
        offset = j;
      }

      String name = df_names[j];

      Collecter* coll = 0;
      size_t index = 0;
      for (; index < names.size(); index++) {
        if (name == names[index]) {
          coll = columns[index];
          break;
        }
      }
      if (!coll) {
        coll = collecter(source, n);
        columns.push_back(coll);
        names.push_back(name);
      }
      if (coll->compatible(source)) {
        // if the current source is compatible, collect
        coll->collect(OffsetSlicingIndex(k, nrows), source, offset);
      } else if (coll->can_promote(source)) {
        // setup a new Collecter
        Collecter* new_collecter = promote_collecter(source, n, coll);

        // import data from this chunk
        new_collecter->collect(OffsetSlicingIndex(k, nrows), source, offset);

        // import data from previous collecter
        new_collecter->collect(NaturalSlicingIndex(k), coll->get());

        // dispose the previous collecter and keep the new one.
        delete coll;
        columns[index] = new_collecter;

      } else if (all_na(source)) {
        // do nothing, the collecter already initialized data with the
        // right NA
      } else if (coll->is_logical_all_na()) {
        Collecter* new_collecter = collecter(source, n);
        new_collecter->collect(OffsetSlicingIndex(k, nrows), source, offset);
        delete coll;
        columns[index] = new_collecter;
      } else {
        std::string column_name(name);
        stop(
          "Can not automatically convert from %s to %s in column \"%s\".",
          coll->describe(), get_single_class(source), column_name
        );
      }

    }

    k += nrows;
  }

  int nc = columns.size();
  int has_id = Rf_isNull(id) ? 0 : 1;

  List out(nc + has_id);
  CharacterVector out_names(nc + has_id);
  for (int i = 0; i < nc; i++) {
    out[i + has_id] = columns[i]->get();
    out_names[i + has_id] = names[i];
  }

  // Add vector of identifiers if .id is supplied
  if (!Rf_isNull(id)) {
    CharacterVector id_col = no_init(n);

    CharacterVector::iterator it = id_col.begin();
    for (int i = 0; i < ndata; ++i) {
      std::fill(it, it + df_nrows[i], dots_names[i]);
      it += df_nrows[i];
    }
    out[0] = id_col;
    out_names[0] = Rcpp::as<std::string>(id);
  }
  out.attr("names") = out_names;
  set_rownames(out, n);

  // infer the classes and extra info (groups, etc ) from the first (#1692)
  if (ndata) {
    SEXP first = chunks[0];
    if (Rf_inherits(first, "data.frame")) {
      set_class(out, get_class(first));
      if (Rf_inherits(first, "grouped_df")) {
        copy_vars(out, first);
        out = GroupedDataFrame(out).data();
      }
    } else {
      set_class(out, classes_not_grouped());
    }
  } else {
    set_class(out, classes_not_grouped());
  }

  return out;
}

// [[Rcpp::export]]
List bind_rows_(List dots, SEXP id = R_NilValue) {
  return rbind__impl(dots, id);
}

// [[Rcpp::export]]
List rbind_list__impl(List dots) {
  return rbind__impl(dots);
}

List cbind__impl(List dots) {
  int n_dots = dots.size();

  // First check that the number of rows is the same based on first
  // nonnull element
  int first_i = -1;
  for (int i = 0; i != n_dots; ++i) {
    if (dots[i] != R_NilValue) {
      first_i = i;
      break;
    }
  }

  if (!n_dots || first_i == -1)
    return DataFrame();

  SEXP first = dots[first_i];
  const size_t nrows = rows_length(first, false);
  cbind_type_check(first, nrows, dots, 0);

  size_t nv = cols_length(first);

  for (int i = first_i + 1; i < n_dots; i++) {
    SEXP current = dots[i];
    if (Rf_isNull(current))
      continue;

    cbind_type_check(current, nrows, dots, i);
    nv += cols_length(current);
  }

  // collect columns
  List out(nv);
  CharacterVector out_names(nv);
  SEXP dots_names = vec_names(dots);

  // then do the subsequent dfs
  for (int i = first_i, k = 0; i < n_dots; i++) {
    SEXP current = dots[i];
    if (Rf_isNull(current))
      continue;

    if (TYPEOF(current) == VECSXP) {
      CharacterVector current_names = vec_names(current);
      int nc = Rf_length(current);
      for (int j = 0; j < nc; j++, k++) {
        out[k] = shared_SEXP(VECTOR_ELT(current, j));
        out_names[k] = current_names[j];
      }
    } else {
      out[k] = current;
      out_names[k] = STRING_ELT(dots_names, i);
      k++;
    }

    Rcpp::checkUserInterrupt();
  }

  // infer the classes and extra info (groups, etc ) from the first (#1692)
  if (Rf_inherits(first, "data.frame")) {
    copy_most_attributes(out, first);
  } else {
    set_class(out, classes_not_grouped());
  }

  out.names() = out_names;
  set_rownames(out, nrows);

  return out;
}

// [[Rcpp::export]]
List cbind_all(List dots) {
  return cbind__impl(dots);
}

// [[Rcpp::export]]
SEXP combine_all(List data) {
  int nv = data.size();
  if (nv == 0) stop("combine_all needs at least one vector");

  // get the size of the output
  int n = 0;
  for (int i = 0; i < nv; i++) {
    n += Rf_length(data[i]);
  }

  // go to the first non NULL
  int i = 0;
  for (; i < nv; i++) {
    if (!Rf_isNull(data[i])) break;
  }
  if (i == nv) stop("no data to combine, all elements are NULL");

  // collect
  boost::scoped_ptr<Collecter> coll(collecter(data[i], n));
  int k = Rf_length(data[i]);
  coll->collect(NaturalSlicingIndex(k), data[i]);
  i++;
  for (; i < nv; i++) {
    SEXP current = data[i];
    if (Rf_isNull(current)) continue;
    int n_current = Rf_length(current);

    if (coll->compatible(current)) {
      coll->collect(OffsetSlicingIndex(k, n_current), current);
    } else if (coll->can_promote(current)) {
      Collecter* new_coll = promote_collecter(current, n, coll.get());
      new_coll->collect(OffsetSlicingIndex(k, n_current), current);
      new_coll->collect(NaturalSlicingIndex(k), coll->get());
      coll.reset(new_coll);
    } else {
      stop(
        "Can not automatically convert from %s to %s.",
        get_single_class(coll->get()), get_single_class(current)
      );
    }
    k += n_current;
  }

  return coll->get();
}
