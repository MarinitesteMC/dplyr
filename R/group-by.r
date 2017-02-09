#' Group a tbl by one or more variables.
#'
#' Most data operations are useful done on groups defined by variables in the
#' the dataset. The `group_by()` function takes an existing tbl
#' and converts it into a grouped tbl where operations are performed
#' "by group".
#'
#' @section Tbl types:
#'
#' `group_by()` is an S3 generic with methods for the three built-in
#' tbls. See the help for the corresponding classes and their manip
#' methods for more details:
#'
#' \itemize{
#'   \item data.frame: [grouped_df]
#'   \item data.table: [dtplyr::grouped_dt]
#'   \item SQLite: [src_sqlite()]
#'   \item PostgreSQL: [src_postgres()]
#'   \item MySQL: [src_mysql()]
#' }
#'
#' @seealso [ungroup()] for the inverse operation,
#'   [groups()] for accessors that don't do special evaluation.
#' @param .data a tbl
#' @param ... variables to group by. All tbls accept variable names,
#'   some will also accept functions of variables. Duplicated groups
#'   will be silently dropped.
#' @param add By default, when `add = FALSE`, `group_by()` will
#'   override existing groups. To instead add to the existing groups,
#'   use `add = TRUE`
#' @inheritParams filter
#' @export
#' @examples
#' by_cyl <- group_by(mtcars, cyl)
#' summarise(by_cyl, mean(disp), mean(hp))
#' filter(by_cyl, disp == max(disp))
#'
#' # summarise peels off a single layer of grouping
#' by_vs_am <- group_by(mtcars, vs, am)
#' by_vs <- summarise(by_vs_am, n = n())
#' by_vs
#' summarise(by_vs, n = sum(n))
#' # use ungroup() to remove if not wanted
#' summarise(ungroup(by_vs), n = sum(n))
#'
#' # You can group by expressions: this is just short-hand for
#' # a mutate/rename followed by a simple group_by
#' group_by(mtcars, vsam = vs + am)
#' group_by(mtcars, vs2 = vs)
#'
#' # You can also group by a constant, but it's not very useful
#' group_by(mtcars, "vs")
#'
#' # By default, group_by sets groups. Use add = TRUE to add groups
#' groups(group_by(by_cyl, vs, am))
#' groups(group_by(by_cyl, vs, am, add = TRUE))
#'
#' # Duplicate groups are silently dropped
#' groups(group_by(by_cyl, cyl, cyl))
#' @aliases regroup
group_by <- function(.data, ..., add = FALSE) {
  group_by_(.data, .dots = lazyeval::lazy_dots(...), add = add)
}

#' @export
#' @rdname group_by
group_by_ <- function(.data, ..., .dots, add = FALSE) {
  UseMethod("group_by_")
}

#' Prepare for grouping.
#'
#' Performs standard operations that should happen before individual methods
#' process the data. This includes mutating the tbl to add new grouping columns
#' and updating the groups (based on add)
#'
#' @return A list
#'   \item{data}{Modified tbl}
#'   \item{groups}{Modified groups}
#' @export
#' @keywords internal
group_by_prepare <- function(.data, ..., .dots, add = FALSE) {
  new_groups <- lazyeval::all_dots(.dots, ...)
  new_groups <- resolve_vars(new_groups, tbl_vars(.data))

  # If any calls, use mutate to add new columns, then group by those
  is_name <- vapply(new_groups, function(x) is.name(x$expr), logical(1))
  has_name <- names2(new_groups) != ""

  needs_mutate <- has_name | !is_name
  if (any(needs_mutate)) {
    .data <- mutate_(.data, .dots = new_groups[needs_mutate])
  }

  # Once we've done the mutate, we no longer need lazy objects, and
  # can instead just use symbols
  new_groups <- lazyeval::auto_name(new_groups)
  group_names <- names(new_groups)
  if (add) {
    group_names <- c(group_vars(.data), group_names)
  }
  group_names <- unique(group_names)

  list(data = .data, groups = lapply(group_names, as.name), group_names = group_names)
}

#' @rdname group_by
#' @description `groups()` returns the current grouping
#'   as a list of [name()].
#' @param x data [tbl()]
#' @param ... Additional arguments that maybe used by methods.
#' @export
#' @examples
#' grouped <- group_by(mtcars, cyl)
#' groups(grouped)
#' groups(ungroup(grouped))
groups <- function(x) {
  UseMethod("groups")
}

#' @rdname group_by
#' @description `group_vars()` returns the current grouping
#'   as a character vector.
#' @export
group_vars <- function(x) {
  UseMethod("group_vars")
}

#' @export
group_vars.default <- function(x) {
  deparse_names(groups(x))
}

#' @export
regroup <- function(x, value) {
  .Deprecated("group_by_")
  group_by_(x, .dots = value)
}

#' @rdname group_by
#' @description `ungroup()` removes an existing grouping.
#' @export
ungroup <- function(x, ...) {
  UseMethod("ungroup")
}
