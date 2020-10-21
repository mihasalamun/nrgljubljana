// numerics.h - Miscelaneous numerical routines
// Copyright (C) 2005-2020 Rok Zitko

#ifndef _numerics_h_
#define _numerics_h_

template <typename T>
  using complex_array_ref_t = T(&)[2];

template<typename T>
  complex_array_ref_t<T> reim(std::complex<T>& z) {
    return reinterpret_cast<T(&)[2]>(z);
  }

template <typename T>
  using complex_array_const_ref_t = const T(&)[2];

template<typename T>
  complex_array_const_ref_t<T> reim(const std::complex<T>& z) {
    return reinterpret_cast<const T(&)[2]>(z);
  }

template<typename U, typename V>
  V sum2(const std::vector<std::pair<U,V>> &v) { // sum second elements of a vector of pairs
    return ranges::accumulate(v, V{}, [](auto sum, const auto el) { return sum+el.second; });
  }

// Accumulator abstraction: automatically initialized to 0, result checked for finiteness.
template <typename T> class generic_bucket {
private:
  T value{};
public:
  generic_bucket() {}
  // Can be constructured from a STL vector of pairs, by summing the second elements.
  template <typename T1> generic_bucket(std::vector<std::pair<T1, T>> v) {
    for (const auto &i : v) value += i.second;
  }
  inline T operator+=(T x) { return value += x; }
  inline operator T() const { return value; }
};
using bucket = generic_bucket<double>;

#define IS_ODD(n) ((n)&1)
#define IS_EVEN(n) (!(IS_ODD(n)))

CONSTFNC int my_fcmp(const double x, const double y, const double small_epsilon, const double rel_epsilon) {
  if (x == 0.0 && y == 0.0) return 0.0; // evidently equal
  if (std::abs(x) < small_epsilon && std::abs(y) < small_epsilon) return 0; // If both x and y are small, we ASSUME them to be equivalent
  if (std::abs(x-y) < rel_epsilon * (std::abs(x)+std::abs(y))) return 0;
  return boost::math::sign(x-y);
}

CONSTFNC int my_fcmp(const double x, const double y, const double epsilon) { return my_fcmp(x, y, epsilon, epsilon); }

// Test if two numbers are equal to within numerical errors. (Use this for comparing values that are expected to be
// of order 1.)
CONSTFNC auto num_equal(const double a, const double b, const double check_precision = 1.e-12) { 
  return my_fcmp(a, b, check_precision) == 0; 
}

CONSTFNC auto num_equal(const cmpl &a, const cmpl &b, const double check_precision = 1.e-12) {
  return (my_fcmp(a.real(), b.real(), check_precision) == 0) && (my_fcmp(a.imag(), b.imag(), check_precision) == 0);
}

CONSTFNC auto are_conjugate(const double a, const double b) { return num_equal(a, b); }

CONSTFNC auto are_conjugate(const cmpl &a, const cmpl &b) { return num_equal(a.real(), b.real()) && num_equal(a.imag(), -b.imag()); }

template<typename M> auto frobenius_norm(const ublas::matrix<M> &m) { // Frobenius norm (without taking the final square root!)
  double sum{};
  for (auto i = 0; i < m.size1(); i++)
    for (auto j = 0; j < m.size2(); j++) sum += pow(abs(m(i, j)),2);
  return sum;
}

// Check if matrix m is upper triangular. In the lower triangle, all elements must be 0. NOTE: we store the upper
// triangular part of the symmetric Hamiltonian matrix. In FORTRAN convention, this is the lower part !!
template<typename M> void check_is_matrix_upper(const ublas::matrix<M> &m) {
  my_assert(m.size1() == m.size2() && m.size1() >= 1);
  for (auto i = 1; i < m.size1(); i++)
    for (auto j = 0; j < i; j++) // j < i
      my_assert(m(i, j) == 0.);
}

// x raised to the power of n
CONSTFNC inline auto pow(const int x, const int n) {
  my_assert(n >= 0);
  auto res = 1;
  for (auto i = 1; i <= n; i++) res *= x;
  return res;
}

// (-1)^n
CONSTFNC inline auto psgn(const int n) { return n % 2 == 0 ? 1.0 : -1.0; }

// Dump a matrix with full numerical precision. The columns are aligned for easier inspection. Expect large output!
template<typename M> inline void dump_matrix(const ublas::matrix<M> &m, std::ostream &F = std::cout) {
  boost::io::ios_base_all_saver ofs(F);
  F << std::setprecision(std::numeric_limits<double>::max_digits10);
  F << fmt::format("Matrix: {}x{}\n", m.size1(), m.size2());
  for (auto r1 = 0; r1 < m.size1(); r1++) {
    F << std::setw(6) << r1 << ":";
    for (auto r2 = 0; r2 < m.size2(); r2++) F << std::setw(23) << m(r1, r2) << " ";
    F << std::endl;
  }
}

template<typename M> inline void dump_diagonal_matrix(const ublas::matrix<M> &m, const size_t max_nr, std::ostream &F = std::cout) {
  for (const auto r : range0(std::min(m.size1(), max_nr))) F << m(r,r) << ' ';
  F << std::endl;
}

// Chop numerical noise
template <typename T> CONSTFNC inline T chop(const T x, const double xlimit = 1.e-8) { return std::abs(x) < xlimit ? 0.0 : x; }

template<typename T>
void assert_issquare(const ublas::matrix<T> &m) { my_assert(m.size1() == m.size2()); }

// Powers, such as (-1)^n, appear in the coupling coefficients.
CONSTFNC inline double Power(const double i, const double nn) { return std::pow(i, nn); }

// Read 'len' values of type T into a ublas vector<T>.
template <typename T> ublas::vector<T> read_vector(std::istream &F, const bool nr_is_max_index = false) {
  my_assert(F);
  size_t nr;
  F >> nr;
  // nr is either vector dimension or the value of maximum index
  const auto len = nr_is_max_index ? nr+1 : nr;
  ublas::vector<T> vec(len);
  for (auto j = 0; j < len; j++)
    F >> vec[j];
  if (F.fail()) throw std::runtime_error("read_vector() error. Input file is corrupted.");
  return vec;
}

// Read 'size1' x 'size2' ublas matrix of type T.
template <typename T> void read_matrix(std::istream &F, ublas::matrix<T> &m, const size_t size1, const size_t size2) {
  my_assert(F);
  m = ublas::matrix<T>(size1, size2);
  for (auto j1 = 0; j1 < size1; j1++)
    for (auto j2 = 0; j2 < size2; j2++) {
      T x;
      F >> x;
      m(j1, j2) = assert_isfinite(x);
    }
  if (F.fail()) std::runtime_error("read_matrix() error. Input file is corrupted.");
}

// Check if the value x is real [for complex number calculations].
CONSTFNC inline auto is_real(const double x) { return x; }
CONSTFNC inline auto is_real(const std::complex<double> z, const double check_real_tolerance = 1e-8) {
  return abs(z.imag()) <= check_real_tolerance;
}

// Check if x is real and return the real part, i.e. x.real().
CONSTFNC inline auto check_real(double x) { return x; }
CONSTFNC inline auto check_real(std::complex<double> z) {
  if (!is_real(z)) std::cout << "Warning: expected real number, but got " << z << std::endl;
  return z.real();
}

template <typename M> CONSTFNC auto trace_real(const ublas::matrix<M> &m) {
  assert_issquare(m);
  return ranges::accumulate(range0(m.size2()), 0.0, [&m](auto sum, const auto i){ return sum+check_real(m(i, i)); });
}

auto csqrt(const cmpl z) { return std::sqrt(z); }

#endif
