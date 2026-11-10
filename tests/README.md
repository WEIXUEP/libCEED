# libCEED: Tests

This page provides a brief description of the tests for the libCEED library.

The tests are organized by API object, and some tests are further organized, as required.

0. Ceed Tests
1. CeedVector Tests\
    0. CeedVector general tests\
    1. CeedVector error tests
2. CeedElemRestriction Tests
3. CeedBasis Tests\
    0. CeedBasis utility tests\
    1. CeedBasis tensor basis tests\
    2. CeedBasis simplex basis tests\
    3. CeedBasis non-tensor H(div) basis tests\
    4. CeedBasis non-tensor H(curl) basis tests\
    5. CeedBasis evaluation at arbitrary points tests\
    6. CeedBasis ApplyAdd tests
4. CeedQFunction Tests\
    0. CeedQFunction user code tests\
    1. CeedQFunction gallery code tests
5. CeedOperator Tests\
    0. CeedOperator with tensor bases tests\
    1. CeedOperator with simplex bases tests\
    2. CeedOperator with operator composition tests\
    3. CeedOperator diagonal and CeedQFunction assembly tests\
    4. CeedOperator element inverse tests\
    5. CeedOperator multigrid level tests\
    6. CeedOperator full assembly tests\
    7. CeedOperator full assembly tests (extended)\
    8. CeedOperator H(div) and H(curl) tests\
    9. CeedOperatorAtPoints tests

GPU regression controls (also runnable on CPU backends):

- `t527-operator` assembles a coupled interpolation/gradient QFunction, applies the
  operator, then forces three reassemblies with changing passive coefficients.
  It checks that all three active components are reset before coordinate probing.
- `t366-basis` checks non-tensor H1 interpolation and gradients with P=1, Q=24,
  two components, and batches of 512 and 87000 elements against tabulated algebra.
  Explicit operator restrictions handle backend-specific vector layouts. Two
  shared output fields exercise both transpose and transpose-add basis calls;
  operator apply/add checks accumulation. The large batch exceeds the tuned
  MAGMA kernel's shared-memory limit on devices such as RTX 4070 and must use GEMM.

Both tests are discovered by the Makefile suite and included in the native CMake
CTest suite for each configured backend. A successful run prints nothing and
returns zero; numerical mismatches return nonzero.

