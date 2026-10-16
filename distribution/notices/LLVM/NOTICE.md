# LLVM compiler-rt builtins

The AROS guest uses compiler-rt builtins from the configured AROS LLVM compiler
prefix. Its exact prefix, compiler hash and unrestricted guest ABI are recorded
in the guest build provenance. LLVM licence terms and exceptions are retained
verbatim in LLVM-COMPILER-RT-LICENSE.txt.

Corresponding compiler-rt sources, AROS patches and build instructions accompany
the release source assets. The hosted target builds its reserved-x18 runtime
separately; that hosted runtime is not imported into this guest.

Upstream: https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.0
