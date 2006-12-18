#ifndef _GPXE_ANSIESC_H
#define _GPXE_ANSIESC_H

/** @file
 *
 * ANSI escape sequences
 *
 * ANSI X3.64 (aka ECMA-48 or ISO/IEC 6429, available from
 * http://www.ecma-international.org/publications/files/ECMA-ST/Ecma-048.pdf)
 * defines escape sequences consisting of:
 *
 *     A Control Sequence Introducer (CSI)
 *
 *     Zero or more Parameter Bytes (P)
 *
 *     Zero or more Intermediate Bytes (I)
 *
 *     A Final Byte (F)
 *
 * The CSI consists of ESC (0x1b) followed by "[" (0x5b).  The
 * Parameter Bytes, for a standardised (i.e. not private or
 * experimental) sequence, consist of a list of ASCII decimal integers
 * separated by semicolons.  The Intermediate Bytes (in the range 0x20
 * to 0x2f) and the Final Byte (in the range 0x40 to 0x4f) determine
 * the control function.
 * 
 */

/** A handler for an escape sequence */
struct ansiesc_handler {
	/** The control function identifier
	 *
	 * The control function identifier consists of the
	 * Intermediate Bytes (if any) and the Final Byte.  In
	 * practice, no more than one immediate byte is ever used, so
	 * the byte combination can be efficiently expressed as a
	 * single integer, in the obvious way (with the Final Byte
	 * being the least significant byte).
	 */
	unsigned int function;
	/** Handle escape sequence
	 *
	 * @v count		Parameter count
	 * @v params		Parameter list
	 *
	 * A negative parameter value indicates that the parameter was
	 * omitted and that the default value for this control
	 * function should be used.
	 *
	 * Since all parameters are optional, there is no way to
	 * distinguish between "zero parameters" and "single parameter
	 * omitted".  Consequently, the parameter list will always
	 * contain at least one item.
	 */
	void ( * handle ) ( unsigned int count, int params[] );
};

/** Maximum number of parameters within a single escape sequence */
#define ANSIESC_MAX_PARAMS 4

/**
 * ANSI escape sequence context
 *
 * This provides temporary storage for processing escape sequences,
 * and points to the list of escape sequence handlers.
 */
struct ansiesc_context {
	/** Array of handlers
	 *
	 * Must be terminated by a handler with @c function set to
	 * zero.
	 */
	struct ansiesc_handler *handlers;
	/** Parameter count
	 *
	 * Will be zero when not currently in an escape sequence.
	 */
	unsigned int count;
	/** Parameter list */ 
	int params[ANSIESC_MAX_PARAMS];
	/** Control function identifier */
	unsigned int function;
};

/** Escape character */
#define ESC 0x1b

/** Control Sequence Introducer */
#define CSI "\033["

/**
 * @defgroup ansifuncs ANSI escape sequence function identifiers
 * @{
 */

/** Character and line position */
#define ANSIESC_HVP 'f'

/** @} */

extern int ansiesc_process ( struct ansiesc_context *ctx, int c );

#endif /* _GPXE_ANSIESC_H */