    /**
 * Printf configuration for the Luma3DS Discord RPC module.
 *
 * Only %s, %u, %llX/%016llX, %llu format specifiers are used.
 * All floating-point/exponential/writeback features are disabled to reduce code size.
 */

#ifndef PRINTF_CONFIG_H_
#define PRINTF_CONFIG_H_

/* Enable soft aliases so that snprintf() in source code maps to snprintf_() */
#define PRINTF_ALIAS_STANDARD_FUNCTION_NAMES_SOFT 1

/* Disable floating-point specifiers %f, %F, %e, %E, %g, %G, %a, %A */
#define PRINTF_SUPPORT_DECIMAL_SPECIFIERS      0
#define PRINTF_SUPPORT_EXPONENTIAL_SPECIFIERS  0

/* Disable the %n writeback specifier (not used) */
#define PRINTF_SUPPORT_WRITEBACK_SPECIFIER     0

/* No floating-point is used, so we do not need double internally */
#define PRINTF_USE_DOUBLE_INTERNALLY          0

/* long long is needed for %016llX and %llu */
#define PRINTF_SUPPORT_LONG_LONG              1

#endif /* PRINTF_CONFIG_H_ */