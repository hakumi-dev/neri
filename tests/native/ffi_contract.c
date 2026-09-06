/* Independent C ABI oracle; this library is not part of the Neri runtime. */
double neri_ffi_double(double value) { return value * 2.0; }
double neri_ffi_half(double value) { return value / 2.0; }
