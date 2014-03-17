// gpp_test_utils.hpp
/*
  This file declares functions and classes that are useful for unit testing.  This includes some relative/absolute precision
  checks and a few mathematical utilities.

  The "big stuff" in this file is a class that defines the interface for a pingable function: PingableMatrixInputVectorOutputInterface
  Then there is a PingDerivative() function that can conduct ping testing on any implementer of this interface.

  There's also a mock environment class that sets up quantities commonly needed by tests of GP functionality.
*/

#ifndef OPTIMAL_LEARNING_EPI_SRC_CPP_GPP_TEST_UTILS_HPP_
#define OPTIMAL_LEARNING_EPI_SRC_CPP_GPP_TEST_UTILS_HPP_

#include <vector>

#include "gpp_common.hpp"
#include "gpp_random.hpp"

namespace optimal_learning {

/*
  Class to enable numerical and analytic differentiation of functions of the form:
  f_{k} = f(X_{d,i})
  with derivatives taken wrt each member of X_{d,i},
  gradf_{k,d,i} = \frac{\partial f_k}{\partial X_{d,i}}
  In the nomenclature used in the class:
    d indexes over num_rows (set in GetInputSizes())
    i indexes over num_cols (set in GetInputSizes())
    k indexes over GetOutputSize()

  Typically 'd' is the spatial_dimension of the problem.  So if 'i' ranges over 1 .. num_points,
  then X_{d,i} is a matrix of num_points points each with dimension spatial_dim.
  And 'k' refers to num_outputs.

  X_{d,i} can of course be any arbitrary matrix; d, i need not refer to spatial dimension and num_points.  But
  that is currently the most common use case.

  This class enables easy pinging of a multitude of f, X combinations.  Since it abstracts away indexing, it does not limit
  how implementations store/compute f() and its gradient.

  Generally, usage goes as follows:
  <> Use GetInputSizes(), GetOutputSize(), and possibly GetGradientsSize() to inspect the dimensions of the problem
  <> EvaluateAndStoreAnalyticGradient(): compute and internally store the gradient evaluated at a given input*
  <> GetAnalyticGradient: returns the value of the analytic gradient for a given output (k), wrt a given point (d,i)
  <> EvaluateFunction: returns all outputs of the function for a given input

  * It is not necessary to fully evaluate the gradient here.  Instead, the input point can be stored and evaluation can
  happen on-the-fly in GetAnalyticGradient() if desired.

  So to ping a derivative, you can:
  f_p = EvaluateFunction(X + h), f_m = EvaluateFunction(X - h)
  Compare:
  (f_p - f_m)/(2h)
  to
  GetAnalyticGradient
  See PingDerivative() docs for more details.
*/
class PingableMatrixInputVectorOutputInterface {
 public:
  /*
    Number of rows and columns of the input, X_{d,i}, to f().

    For example, the input might be a N_d X N_i matrix, "points_to_sample," where N_d = spatial dimension (rows)
    and N_i = number of points (columns).

    OUTPUTS:
    num_rows[1]: the number of rows of the input matrix X
    num_cols[1]: the number of columns of the input matrix X
  */
  virtual void GetInputSizes(int * num_rows, int * num_cols) const noexcept OL_NONNULL_POINTERS = 0;

  /*
    Number of outputs of the function f_k = f(X_{d,i}); i.e., legnth(f_k)

    RETURNS:
    The number of entries in f_k aka number of outputs of f()
  */
  virtual int GetOutputSize() const noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT = 0;

  /*
    Number of entries in the gradient of the output wrt each entry of the input.

    This should generally not be used unless you require direct access to the analytic gradient.

    RETURNS:
    MUST be num_rows*num_cols*GetOutputSize() or undefined behavior may result.
  */
  virtual int GetGradientsSize() const noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT = 0;

  /*
    Setup so that GetAnalyticGradient(row, column, output) will be able to return
    gradf[row][column][output] evaluated at X, "input_matrix."

    Typically this will entail computing and storing the analytic gradient.  But the only thing that needs
    to be saved is the contents of input_matrix for later access.

    MUST BE CALLED before using GetAnalyticGradient!

    INPUTS:
    input_matrix[num_rows][num_cols]: the input, X_{d,i}
    OUTPUTS:
    gradients[num_rows][num_cols][num_outputs]: filled with the gradient evaluated at
      input_matrix.  Ignored if nullptr.  IMPLEMENTATION NOT REQUIRED.
  */
  virtual void EvaluateAndStoreAnalyticGradient(double const * restrict input_matrix, double * restrict gradients) noexcept OL_NONNULL_POINTERS_LIST(2) = 0;

  /*
    The gradients are indexed by: dA[input_row][input_column][output_index], where row, column index
    the input matrix and output_index indexes the output.

    Returns the gradient computed/stored by EvaluateAndStoreAnalyticGradient().

    INPUTS:
    row_index: row_index (d) of the input to be differentiated with respect to
    column_index: column_index (i) of the input to be differentiated with respect to
    output_index:
    RETURNS:
    The [row_index][column_index][output_index]'th entry of the analytic gradient evaluated at input_matrix (where
    input matrix was specified in EvaluateAndStoreAnalyticGradient()).
  */
  virtual double GetAnalyticGradient(int row_index, int column_index, int output_index) const OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT = 0;

  /*
    Evalutes f_k = f(X_{d,i}).  X_{d,i} is the "input_matrix" and f_k is in "function_values."

    INPUTS:
    input_matrix[num_rows][num_cols]: the matrix of inputs
    OUTPUTS:
    function_values[num_outputs]: vector of outputs of f()
  */
  virtual void EvaluateFunction(double const * restrict input_matrix, double * restrict function_values) const noexcept OL_NONNULL_POINTERS = 0;

  OL_DISALLOW_COPY_AND_ASSIGN(PingableMatrixInputVectorOutputInterface);

 protected:
  PingableMatrixInputVectorOutputInterface() = default;

  virtual ~PingableMatrixInputVectorOutputInterface() = default;
};

/*
  Class to conveniently hold and generate random data that are commonly needed for testing functions in gpp_math.cpp.  In
  particular, this mock is used for testing GP mean, GP variance, and expected improvement (and their gradients).

  This class holds arrays: points_to_sample, points_sampled, points_sampled_value, and current_point
  which are sized according to the parameters specified in Initialize(), and filled with random numbers.

  TODO: we currently generate the point sets by repeated calls to rand().  This is generally unwise since the distribution
  of points is not particularly random.  Additionally, our current covariance functions are all stationary, so we would rather
  generate a random base point x, and then a random (direction, radius) pair so that y = x + direction*radius.  We better cover
  the different behavioral regimes of our code in this case, since it's the radius value that actually correlates to results.
  (Ticket: #44278)
*/
class MockExpectedImprovementEnvironment {
 public:
  using EngineType = UniformRandomGenerator::EngineType;

  static constexpr EngineType::result_type kDefaultSeed = 314;
  static constexpr double range_min = -5.0;
  static constexpr double range_max = 5.0;

  /*
    Construct a MockExpectedImprovementEnvironment and set invalid values for all size parameters
    (so that Initialize must be called to do anything useful) and pre-allocate some space.
  */
  MockExpectedImprovementEnvironment() noexcept :
      dim(-1),
      num_to_sample(-1),
      num_sampled(-1),
      points_to_sample_(20*4),
      points_sampled_(20*4),
      points_sampled_value_(20),
      current_point_(4),
      uniform_generator_(kDefaultSeed),
      uniform_double_(range_min, range_max) {
  }

  /*
    (Re-)initializes the data data in this function: this includes space allocation and random number generation.

    If any of the size parameters are changed from their current values, space will be realloc'd.
    Then it re-draws another set of uniform random points (in [-5, 5]) for the member arrays points_to_sample,
    points_sampled, points_sampled_value, and current_point.

    INPUTS:
    dim: the spatial dimension of a point (i.e., number of independent params in experiment)
    num_to_sample: number of points being sampled concurrently
    num_sampled: number of already-sampled points
  */
  void Initialize(int dim_in, int num_to_sample_in, int num_sampled_in) noexcept {
    Initialize(dim_in, num_to_sample_in, num_sampled_in, &uniform_generator_);
  }

  void Initialize(int dim_in, int num_to_sample_in, int num_sampled_in, UniformRandomGenerator * uniform_generator) noexcept {
    if (dim_in != dim || num_to_sample_in != num_to_sample || num_sampled_in != num_sampled) {
      dim = dim_in;
      num_to_sample = num_to_sample_in;
      num_sampled = num_sampled_in;

      points_to_sample_.resize(num_to_sample*dim);
      points_sampled_.resize(num_sampled*dim);
      points_sampled_value_.resize(num_sampled);
      current_point_.resize(dim);
    }

    for (int i = 0; i < dim*num_to_sample; ++i) {
      points_to_sample_[i] = uniform_double_(uniform_generator->engine);
    }

    for (int i = 0; i < dim*num_sampled; ++i) {
      points_sampled_[i] = uniform_double_(uniform_generator->engine);
    }

    for (int i = 0; i < num_sampled; ++i) {
      points_sampled_value_[i] = uniform_double_(uniform_generator->engine);
    }

    for (int i = 0; i < dim; ++i) {
      current_point_[i] = uniform_double_(uniform_generator->engine);
    }
  }

  int dim;
  int num_to_sample;
  int num_sampled;

  double * points_to_sample() {
    return points_to_sample_.data();
  }

  double * points_sampled() {
    return points_sampled_.data();
  }

  double * points_sampled_value() {
    return points_sampled_value_.data();
  }

  double * current_point() {
    return current_point_.data();
  }

  OL_DISALLOW_COPY_AND_ASSIGN(MockExpectedImprovementEnvironment);

 private:
  std::vector<double> points_to_sample_;
  std::vector<double> points_sampled_;
  std::vector<double> points_sampled_value_;
  std::vector<double> current_point_;

  UniformRandomGenerator uniform_generator_;
  boost::uniform_real<double> uniform_double_;
};

/*
  Checks if |value - truth| == 0

  INPUTS:
  value: number to be tested
  truth: the exact/desired result
  RETURNS:
  true if value, truth are equal.
*/
bool CheckIntEquals(int64_t value, int64_t truth) noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT;

/*
  Computes ||b - A*x||_2
  The quantity b - A*x is called the "residual."  This is meaningful when x is
  the solution of the linear system A*x = b.

  Coupled with knowledge of the underlying algorithm, having a small residual
  norm is a useful measure of method correctness.  See implementation documentation
  for more details.

  This norm is what is minimzied in least squares problems.  However here we
  are not working with least squares solutions and require that A is square.

  INPUTS:
  A[size][size]: the linear system
  x[size}: the solution vector
  b[size]: the RHS vector
  size: the dimension of the problem
  OUTPUTS:
  the 2-norm of b-A*x
*/
double ResidualNorm(double const * restrict A, double const * restrict x, double const * restrict b, int size) noexcept OL_PURE_FUNCTION OL_NONNULL_POINTERS OL_WARN_UNUSED_RESULT;

/*
  Checks if |value - truth| <= tolerance (error)

  INPUTS:
  value: number to be tested
  truth: the exact/desired result
  tolerance: permissible difference
  RETURNS:
  true if value, truth differ by no more than tolerance.
*/
bool CheckDoubleWithin(double value, double truth, double tolerance) noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT;

/*
  Checks if |value - truth| / |truth| <= tolerance (relative error)

  If truth = 0.0, CheckDoubleWithin() is performed.

  INPUTS:
  value: number to be tested
  truth: the exact/desired result
  tolerance: permissible relative difference
  RETURNS:
  true if value, truth differ relatively by no more than tolerance.
*/
bool CheckDoubleWithinRelative(double value, double truth, double tolerance) noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT;

/*
  Checks that ||A - B||_F <= tolerance

  Note: the user may want to scale this norm by \sqrt(size) because ||I||_F = \sqrt(size),
  and we may desire that the norm of the idenity be 1.

  INPUTS:
  matrix1[size_m][size_n]: matrix A
  matrix2[size_m][size_n]: matrix B
  size_m: rows of A, B
  size_n: columns of A, B
  tolerance: largest permissible norm of the difference A - B
  RETURNS:
  true if A - B are "close"
*/
bool CheckMatrixNormWithin(double const * restrict matrix1, double const * restrict matrix2, int size_m, int size_n, double tolerance) noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT;

/*
  Checks the correctness of analytic gradient calculations using finite differences.

  Since the exact level of error is virtually impossible to compute precisely, we
  use finite differences at two different h values (see implementation docs for
  details) and check the convergence rate.

  Includes logic to skip tests or run at relaxed tolerances when poor
  conditioning or loss precision is detected.

  In general, this function is meant to be used to test analytic gradient computations on a large
  number of random points. The logic to skip tests or relax tolerances is designed to decrease the
  false positive rate to 0. Some true positives are lost in the process. So we obtain "reasonable
  certainty" by testing a large number of points.

  If you are implementing/testing new gradients code, please read the function comments for this as well.
  This is an "expert tool" and not necessarily the most user-friendly at that.

  This function produces the most useful debugging output when in PingableMatrixInputVectorOutputInterface,
  num_rows = spatial dimension (d)
  num_cols = num_points (i)
  GetOutputSize() = num_outputs (k)
  for functions f_k = f(X_{d,i})

  WARNING: this function generates ~10 lines of output (to stdout) PER FAILURE. If your implementation
  is incorrect, expect a large number of lines printed to stdout.

  INPUTS:
  function_and_derivative_evaluator: an object that inherits from
    PingableMatrixInputVectorOutputInterface. This object must define all
    virtual functions in that interface.
  points[num_cols][num_rows]: num_cols points, each with dimension
    num_rows. Assumed that the coordinate-wise magnitues are "around 1.0": say [1.0e-3, 1.0e1].
  epsilon[2]: array[h1, h2] of step sizes to use for finite differencing.
    These should not be too small/too large; 5.0e-3, 1.0e-3 are suggested
    starting values.
    Note that the more ill-conditioned computing f_k is, the larger the tolerances
    will need to be.  For example, "complex" functions (e.g., many math ops) may
    be ill-conditioned.
  rate_tolerance_fine: desired amount of deviation from the exact rate
  rate_tolerance_relaxed: maximum allowable abmount of deviation from the exact rate
  input_output_ratio: for ||analytic_gradient||/||input|| < input_output_ratio, ping testing is not performed
    Suggest values around 1.0e-15 to 1.0e-18 (around machine preciscion).
  RETURNS:
  The number of gradient entries that failed pinging.  Expected to be 0.
*/
int PingDerivative(const PingableMatrixInputVectorOutputInterface& function_and_derivative_evaluator, double const * restrict points, double epsilon[2], double rate_tolerance_fine, double rate_tolerance_relaxed, double input_output_ratio) noexcept OL_PURE_FUNCTION OL_WARN_UNUSED_RESULT;

}  // end namespace optimal_learning

#endif  // OPTIMAL_LEARNING_EPI_SRC_CPP_GPP_TEST_UTILS_HPP_
