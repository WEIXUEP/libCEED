// Copyright (c) 2026, Lawrence Livermore National Security, LLC and other CEED contributors.
// SPDX-License-Identifier: BSD-2-Clause
#include <ceed.h>
#include <ceed/backend.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

static int HostScale(void *, CeedInt q, const CeedScalar *const *input, CeedScalar *const *output) {
  for (CeedInt i = 0; i < q; i++) output[0][i] = 2 * input[0][i];
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4) return 1;
  const auto directory = std::filesystem::u8path(argv[2]);
  const auto header    = directory / "external.h";
  if (std::string(argv[1]) == "write") {
    std::filesystem::create_directories(directory);
    std::ofstream coefficient(directory / "coefficient.h");
    coefficient << "#define FACTOR " << std::atoi(argv[3]) << "\n";
    std::ofstream source(header);
    if (argc > 4) source << "#define COEFFICIENT_HEADER \"coefficient.h\"\n#include COEFFICIENT_HEADER\n";
    else source << "#include \"coefficient.h\"\n";
    source << "CEED_QFUNCTION(ExternalScale)(void *ctx, CeedInt Q, const CeedScalar *const *in, CeedScalar *const *out) {\n"
              "for (CeedInt i = 0; i < Q; i++) out[0][i] = FACTOR * in[0][i]; return 0; }\n";
    source.close();
    coefficient.close();
    return coefficient && source ? 0 : 1;
  }
  Ceed                ceed;
  CeedQFunction       qf;
  CeedOperator        op;
  CeedElemRestriction restriction;
  CeedVector          input, output;
  const CeedInt       strides[3] = {1, 4, 4};
  const auto          location   = header.generic_u8string() + ":ExternalScale";
  CeedCall(CeedInit(argv[1], &ceed));
  CeedCall(CeedQFunctionCreateInterior(ceed, 1, HostScale, location.c_str(), &qf));
  CeedCall(CeedQFunctionAddInput(qf, "u", 1, CEED_EVAL_NONE));
  CeedCall(CeedQFunctionAddOutput(qf, "v", 1, CEED_EVAL_NONE));
  CeedCall(CeedElemRestrictionCreateStrided(ceed, 1, 4, 1, 4, strides, &restriction));
  CeedCall(CeedOperatorCreate(ceed, qf, CEED_QFUNCTION_NONE, CEED_QFUNCTION_NONE, &op));
  CeedCall(CeedOperatorSetField(op, "u", restriction, CEED_BASIS_NONE, CEED_VECTOR_ACTIVE));
  CeedCall(CeedOperatorSetField(op, "v", restriction, CEED_BASIS_NONE, CEED_VECTOR_ACTIVE));
  CeedCall(CeedVectorCreate(ceed, 4, &input));
  CeedCall(CeedVectorCreate(ceed, 4, &output));
  CeedCall(CeedVectorSetValue(input, 3));
  CeedCall(CeedOperatorApply(op, input, output, CEED_REQUEST_IMMEDIATE));
  const CeedScalar *values;
  CeedCall(CeedVectorGetArrayRead(output, CEED_MEM_HOST, &values));
  for (int i = 0; i < 4; i++)
    if (values[i] != std::atoi(argv[3])) return 1;
  CeedCall(CeedVectorRestoreArrayRead(output, &values));
  CeedCall(CeedVectorDestroy(&input));
  CeedCall(CeedVectorDestroy(&output));
  CeedCall(CeedOperatorDestroy(&op));
  CeedCall(CeedElemRestrictionDestroy(&restriction));
  CeedCall(CeedQFunctionDestroy(&qf));
  CeedCall(CeedDestroy(&ceed));
  return 0;
}
