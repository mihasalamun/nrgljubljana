#ifndef _traits_hpp_
#define _traits_hpp_

//#include <concepts> // C++20
#include <complex>
#include <type_traits> // is_same_v, is_floating_point_v

#include <boost/numeric/ublas/matrix.hpp>

namespace NRG {

using namespace boost::numeric;

template <typename T>
     concept floating_point = std::is_floating_point_v<T>;

template <typename T>
     struct is_complex : std::false_type {};

template <floating_point T>
     struct is_complex<std::complex<T>> : std::true_type {};

template <typename T>
     concept scalar = floating_point<T> || is_complex<T>::value;

template <typename T>
  concept matrix = requires(T a, T b, size_t i, size_t j) {
     { a.size1() }; // -> std::convertible_to<std::size_t>;
     { a.size2() }; // -> std::convertible_to<std::size_t>;
     { a(i,j) };
     { a = b };
     { a.swap(b) };
//     { typename T::value_type };
  };

// We encapsulate the differences between real-value and complex-value versions of the code in class traits.

template <scalar S> struct traits {};

template <> struct traits<double> {
  using t_matel = double;  // type for the matrix elements
  using t_coef = double;   // type for the Wilson chain coefficients & various prefactors
  using t_expv = double;   // type for expectation values of operators
  using t_eigen = double;  // type for the eigenvalues (always real)
  using t_temp = t_eigen;  // type for temperatures
  using t_weight = std::complex<double>;  // spectral weight accumulators (always complex)
  using evec = std::vector<double>;     // vector of eigenvalues type (always real) // YYY
  using RVector = std::vector<double>;    // vector of eigenvalues type (always real)
  using Matrix = ublas::matrix<t_matel>;  // matrix type
};

template <> struct traits<std::complex<double>> {
  using t_matel = std::complex<double>;
  using t_coef = std::complex<double>;
  using t_expv = std::complex<double>;     // we allow the calculation of expectation values of non-Hermitian operators!
  using t_eigen = double;
  using t_temp = t_eigen;
  using t_weight = std::complex<double>;
  using evec = std::vector<double>;
  using RVector = std::vector<double>;
  using Matrix = ublas::matrix<t_matel>;
};

template <scalar S> using matel_traits   = typename traits<S>::t_matel;
template <scalar S> using coef_traits    = typename traits<S>::t_coef;
template <scalar S> using expv_traits    = typename traits<S>::t_expv;
template <scalar S> using eigen_traits   = typename traits<S>::t_eigen;
template <scalar S> using weight_traits  = typename traits<S>::t_weight;
template <scalar S> using evec_traits    = typename traits<S>::evec;
template <scalar S> using RVector_traits = typename traits<S>::RVector;
template <scalar S> using Matrix_traits  = typename traits<S>::Matrix;

} // namespace

#endif
