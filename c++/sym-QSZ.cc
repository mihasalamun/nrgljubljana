#include "nrg-general.h"
#include "sym-QSZ-impl.h"
#include "sym-QSZ.h" // include for consistency

template <>
std::unique_ptr<Symmetry<double>> mk_QSZ(const Params &P, Allfields &allfields)
{
  return std::make_unique<SymmetryQSZ<double>>(P, allfields);
}

template <>
std::unique_ptr<Symmetry<cmpl>> mk_QSZ(const Params &P, Allfields &allfields)
{
  return std::make_unique<SymmetryQSZ<cmpl>>(P, allfields);
}
