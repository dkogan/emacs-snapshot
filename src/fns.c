/* Random utility Lisp functions.

Copyright (C) 1985-2025 Free Software Foundation, Inc.

This file is part of GNU Emacs.

GNU Emacs is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or (at
your option) any later version.

GNU Emacs is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Emacs.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>

#include <stdlib.h>
#include <sys/random.h>
#include <unistd.h>
#include <filevercmp.h>
#include <intprops.h>
#include <vla.h>
#include <errno.h>
#include <math.h>

#include "lisp.h"
#include "bignum.h"
#include "character.h"
#include "coding.h"
#include "composite.h"
#include "buffer.h"
#include "intervals.h"
#include "window.h"
#include "gnutls.h"

#ifdef HAVE_TREE_SITTER
#include "treesit.h"
#endif

enum equal_kind { EQUAL_NO_QUIT, EQUAL_PLAIN, EQUAL_INCLUDING_PROPERTIES };
static bool internal_equal (Lisp_Object, Lisp_Object,
			    enum equal_kind, int, Lisp_Object);
static EMACS_UINT sxhash_obj (Lisp_Object, int);

DEFUN ("identity", Fidentity, Sidentity, 1, 1, 0,
       doc: /* Return the ARGUMENT unchanged.  */
       attributes: const)
  (Lisp_Object argument)
{
  return argument;
}

/* Return a random Lisp fixnum I in the range 0 <= I < LIM,
   where LIM is taken from a positive fixnum.  */
static Lisp_Object
get_random_fixnum (EMACS_INT lim)
{
  /* Return the remainder of a random integer R (in range 0..INTMASK)
     divided by LIM, except reject the rare case where R is so close
     to INTMASK that the remainder isn't random.  */
  EMACS_INT difflim = INTMASK - lim + 1, diff, remainder;
  do
    {
      EMACS_INT r = get_random ();
      remainder = r % lim;
      diff = r - remainder;
    }
  while (difflim < diff);

  return make_fixnum (remainder);
}

DEFUN ("random", Frandom, Srandom, 0, 1, 0,
       doc: /* Return a pseudo-random integer.
By default, return a fixnum; all fixnums are equally likely.
With positive integer LIMIT, return random integer in interval [0,LIMIT).
With argument t, set the random number seed from the system's entropy
pool if available, otherwise from less-random volatile data such as the time.
With a string argument, set the seed based on the string's contents.

See Info node `(elisp)Random Numbers' for more details.  */)
  (Lisp_Object limit)
{
  if (EQ (limit, Qt))
    init_random ();
  else if (STRINGP (limit))
    seed_random (SSDATA (limit), SBYTES (limit));
  else if (FIXNUMP (limit))
    {
      EMACS_INT lim = XFIXNUM (limit);
      if (lim <= 0)
        xsignal1 (Qargs_out_of_range, limit);
      return get_random_fixnum (lim);
    }
  else if (BIGNUMP (limit))
    {
      struct Lisp_Bignum *lim = XBIGNUM (limit);
      if (mpz_sgn (*bignum_val (lim)) <= 0)
        xsignal1 (Qargs_out_of_range, limit);
      return get_random_bignum (lim);
    }

  return make_ufixnum (get_random ());
}

/* Random data-structure functions.  */

/* Return LIST's length.  Signal an error if LIST is not a proper list.  */

ptrdiff_t
list_length (Lisp_Object list)
{
  ptrdiff_t i = 0;
  FOR_EACH_TAIL (list)
    i++;
  CHECK_LIST_END (list, list);
  return i;
}


DEFUN ("length", Flength, Slength, 1, 1, 0,
       doc: /* Return the length of vector, list or string SEQUENCE.
A byte-code function object is also allowed.

If the string contains multibyte characters, this is not necessarily
the number of bytes in the string; it is the number of characters.
To get the number of bytes, use `string-bytes'.

If the length of a list is being computed to compare to a (small)
number, the `length<', `length>' and `length=' functions may be more
efficient.  */)
  (Lisp_Object sequence)
{
  EMACS_INT val;

  if (STRINGP (sequence))
    val = SCHARS (sequence);
  else if (CONSP (sequence))
    val = list_length (sequence);
  else if (NILP (sequence))
    val = 0;
  else if (VECTORP (sequence))
    val = ASIZE (sequence);
  else if (CHAR_TABLE_P (sequence))
    val = MAX_CHAR + 1;
  else if (BOOL_VECTOR_P (sequence))
    val = bool_vector_size (sequence);
  else if (CLOSUREP (sequence) || RECORDP (sequence))
    val = PVSIZE (sequence);
  else
    wrong_type_argument (Qsequencep, sequence);

  return make_fixnum (val);
}

DEFUN ("safe-length", Fsafe_length, Ssafe_length, 1, 1, 0,
       doc: /* Return the length of a list, but avoid error or infinite loop.
This function never gets an error.  If LIST is not really a list,
it returns 0.  If LIST is circular, it returns an integer that is at
least the number of distinct elements.  */)
  (Lisp_Object list)
{
  ptrdiff_t len = 0;
  FOR_EACH_TAIL_SAFE (list)
    len++;
  return make_fixnum (len);
}

static inline
EMACS_INT length_internal (Lisp_Object sequence, int len)
{
  /* If LENGTH is short (arbitrarily chosen cut-off point), use a
     fast loop that doesn't care about whether SEQUENCE is
     circular or not. */
  if (len < 0xffff)
    while (CONSP (sequence))
      {
	if (--len <= 0)
	  return -1;
	sequence = XCDR (sequence);
      }
  /* Signal an error on circular lists. */
  else
    FOR_EACH_TAIL (sequence)
      if (--len <= 0)
	return -1;
  return len;
}

DEFUN ("length<", Flength_less, Slength_less, 2, 2, 0,
       doc: /* Return non-nil if SEQUENCE is shorter than LENGTH.
See `length' for allowed values of SEQUENCE and how elements are
counted.  */)
  (Lisp_Object sequence, Lisp_Object length)
{
  CHECK_FIXNUM (length);
  EMACS_INT len = XFIXNUM (length);

  if (CONSP (sequence))
    return length_internal (sequence, len) == -1? Qnil: Qt;
  else
    return XFIXNUM (Flength (sequence)) < len? Qt: Qnil;
}

DEFUN ("length>", Flength_greater, Slength_greater, 2, 2, 0,
       doc: /* Return non-nil if SEQUENCE is longer than LENGTH.
See `length' for allowed values of SEQUENCE and how elements are
counted.  */)
  (Lisp_Object sequence, Lisp_Object length)
{
  CHECK_FIXNUM (length);
  EMACS_INT len = XFIXNUM (length);

  if (CONSP (sequence))
    return length_internal (sequence, len + 1) == -1? Qt: Qnil;
  else
    return XFIXNUM (Flength (sequence)) > len? Qt: Qnil;
}

DEFUN ("length=", Flength_equal, Slength_equal, 2, 2, 0,
       doc: /* Return non-nil if SEQUENCE has length equal to LENGTH.
See `length' for allowed values of SEQUENCE and how elements are
counted.  */)
  (Lisp_Object sequence, Lisp_Object length)
{
  CHECK_FIXNUM (length);
  EMACS_INT len = XFIXNUM (length);

  if (len < 0)
    return Qnil;

  if (CONSP (sequence))
    return length_internal (sequence, len + 1) == 1? Qt: Qnil;
  else
    return XFIXNUM (Flength (sequence)) == len? Qt: Qnil;
}

DEFUN ("proper-list-p", Fproper_list_p, Sproper_list_p, 1, 1, 0,
       doc: /* Return OBJECT's length if it is a proper list, nil otherwise.
A proper list is neither circular nor dotted (i.e., its last cdr is nil).  */
       attributes: const)
  (Lisp_Object object)
{
  ptrdiff_t len = 0;
  Lisp_Object last_tail = object;
  Lisp_Object tail = object;
  FOR_EACH_TAIL_SAFE (tail)
    {
      len++;
      rarely_quit (len);
      last_tail = XCDR (tail);
    }
  if (!NILP (last_tail))
    return Qnil;
  return make_fixnum (len);
}

DEFUN ("string-bytes", Fstring_bytes, Sstring_bytes, 1, 1, 0,
       doc: /* Return the number of bytes in STRING.
If STRING is multibyte, this may be greater than the length of STRING.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);
  return make_fixnum (SBYTES (string));
}

DEFUN ("string-distance", Fstring_distance, Sstring_distance, 2, 3, 0,
       doc: /* Return Levenshtein distance between STRING1 and STRING2.
The distance is the number of deletions, insertions, and substitutions
required to transform STRING1 into STRING2.
If BYTECOMPARE is nil or omitted, compute distance in terms of characters.
If BYTECOMPARE is non-nil, compute distance in terms of bytes.
Letter-case is significant, but text properties are ignored. */)
  (Lisp_Object string1, Lisp_Object string2, Lisp_Object bytecompare)

{
  CHECK_STRING (string1);
  CHECK_STRING (string2);

  bool use_byte_compare =
    !NILP (bytecompare)
    || (!STRING_MULTIBYTE (string1) && !STRING_MULTIBYTE (string2));
  ptrdiff_t len1 = use_byte_compare ? SBYTES (string1) : SCHARS (string1);
  ptrdiff_t len2 = use_byte_compare ? SBYTES (string2) : SCHARS (string2);
  ptrdiff_t x, y, lastdiag, olddiag;

  USE_SAFE_ALLOCA;
  ptrdiff_t *column;
  SAFE_NALLOCA (column, 1, len1 + 1);
  for (y = 0; y <= len1; y++)
    column[y] = y;

  if (use_byte_compare)
    {
      char *s1 = SSDATA (string1);
      char *s2 = SSDATA (string2);

      for (x = 1; x <= len2; x++)
        {
          column[0] = x;
          for (y = 1, lastdiag = x - 1; y <= len1; y++)
            {
              olddiag = column[y];
              column[y] = min (min (column[y] + 1, column[y-1] + 1),
			       lastdiag + (s1[y-1] == s2[x-1] ? 0 : 1));
              lastdiag = olddiag;
            }
        }
    }
  else
    {
      int c1, c2;
      ptrdiff_t i1, i1_byte, i2 = 0, i2_byte = 0;
      for (x = 1; x <= len2; x++)
        {
          column[0] = x;
          c2 = fetch_string_char_advance (string2, &i2, &i2_byte);
          i1 = i1_byte = 0;
          for (y = 1, lastdiag = x - 1; y <= len1; y++)
            {
              olddiag = column[y];
              c1 = fetch_string_char_advance (string1, &i1, &i1_byte);
              column[y] = min (min (column[y] + 1, column[y-1] + 1),
			       lastdiag + (c1 == c2 ? 0 : 1));
              lastdiag = olddiag;
            }
        }
    }

  SAFE_FREE ();
  return make_fixnum (column[len1]);
}

DEFUN ("string-equal", Fstring_equal, Sstring_equal, 2, 2, 0,
       doc: /* Return t if two strings have identical contents.
Case is significant, but text properties are ignored.
Symbols are also allowed; their print names are used instead.

See also `string-equal-ignore-case'.  */)
  (register Lisp_Object s1, Lisp_Object s2)
{
  if (SYMBOLP (s1))
    s1 = SYMBOL_NAME (s1);
  if (SYMBOLP (s2))
    s2 = SYMBOL_NAME (s2);
  CHECK_STRING (s1);
  CHECK_STRING (s2);

  if (SCHARS (s1) != SCHARS (s2)
      || SBYTES (s1) != SBYTES (s2)
      || memcmp (SDATA (s1), SDATA (s2), SBYTES (s1)))
    return Qnil;
  return Qt;
}

DEFUN ("compare-strings", Fcompare_strings, Scompare_strings, 6, 7, 0,
       doc: /* Compare the contents of two strings, converting to multibyte if needed.
The arguments START1, END1, START2, and END2, if non-nil, are
positions specifying which parts of STR1 or STR2 to compare.  In
string STR1, compare the part between START1 (inclusive) and END1
\(exclusive).  If START1 is nil, it defaults to 0, the beginning of
the string; if END1 is nil, it defaults to the length of the string.
Likewise, in string STR2, compare the part between START2 and END2.
Like in `substring', negative values are counted from the end.

The strings are compared by the numeric values of their characters.
For instance, STR1 is "less than" STR2 if its first differing
character has a smaller numeric value.  If IGNORE-CASE is non-nil,
characters are converted to upper-case before comparing them.  Unibyte
strings are converted to multibyte for comparison.

The value is t if the strings (or specified portions) match.
If string STR1 is less, the value is a negative number N;
  - 1 - N is the number of characters that match at the beginning.
If string STR1 is greater, the value is a positive number N;
  N - 1 is the number of characters that match at the beginning.  */)
  (Lisp_Object str1, Lisp_Object start1, Lisp_Object end1, Lisp_Object str2,
   Lisp_Object start2, Lisp_Object end2, Lisp_Object ignore_case)
{
  ptrdiff_t from1, to1, from2, to2, i1, i1_byte, i2, i2_byte;

  CHECK_STRING (str1);
  CHECK_STRING (str2);

  /* For backward compatibility, silently bring too-large positive end
     values into range.  */
  if (FIXNUMP (end1) && SCHARS (str1) < XFIXNUM (end1))
    end1 = make_fixnum (SCHARS (str1));
  if (FIXNUMP (end2) && SCHARS (str2) < XFIXNUM (end2))
    end2 = make_fixnum (SCHARS (str2));

  validate_subarray (str1, start1, end1, SCHARS (str1), &from1, &to1);
  validate_subarray (str2, start2, end2, SCHARS (str2), &from2, &to2);

  i1 = from1;
  i2 = from2;

  i1_byte = string_char_to_byte (str1, i1);
  i2_byte = string_char_to_byte (str2, i2);

  while (i1 < to1 && i2 < to2)
    {
      /* When we find a mismatch, we must compare the
	 characters, not just the bytes.  */
      int c1 = fetch_string_char_as_multibyte_advance (str1, &i1, &i1_byte);
      int c2 = fetch_string_char_as_multibyte_advance (str2, &i2, &i2_byte);

      if (c1 == c2)
	continue;

      if (! NILP (ignore_case))
	{
	  c1 = XFIXNUM (Fupcase (make_fixnum (c1)));
	  c2 = XFIXNUM (Fupcase (make_fixnum (c2)));
	}

      if (c1 == c2)
	continue;

      /* Note that I1 has already been incremented
	 past the character that we are comparing;
	 hence we don't add or subtract 1 here.  */
      if (c1 < c2)
	return make_fixnum (- i1 + from1);
      else
	return make_fixnum (i1 - from1);
    }

  if (i1 < to1)
    return make_fixnum (i1 - from1 + 1);
  if (i2 < to2)
    return make_fixnum (- i1 + from1 - 1);

  return Qt;
}

/* Check whether the platform allows access to unaligned addresses for
   size_t integers without trapping or undue penalty (a few cycles is OK),
   and that a word-sized memcpy can be used to generate such an access.

   This whitelist is incomplete but since it is only used to improve
   performance, omitting cases is safe.  */
#if (defined __x86_64__|| defined __amd64__		\
     || defined __i386__ || defined __i386		\
     || defined __arm64__ || defined __aarch64__	\
     || defined __powerpc__ || defined __powerpc	\
     || defined __ppc__ || defined __ppc		\
     || defined __s390__ || defined __s390x__)		\
  && defined __OPTIMIZE__
#define HAVE_FAST_UNALIGNED_ACCESS 1
#else
#define HAVE_FAST_UNALIGNED_ACCESS 0
#endif

/* Load a word from a possibly unaligned address.  */
static inline size_t
load_unaligned_size_t (const void *p)
{
  size_t x;
  memcpy (&x, p, sizeof x);
  return x;
}

/* Return -1/0/1 to indicate the relation </=/> between string1 and string2.  */
static int
string_cmp (Lisp_Object string1, Lisp_Object string2)
{
  ptrdiff_t n = min (SCHARS (string1), SCHARS (string2));

  if ((!STRING_MULTIBYTE (string1) || SCHARS (string1) == SBYTES (string1))
      && (!STRING_MULTIBYTE (string2) || SCHARS (string2) == SBYTES (string2)))
    {
      /* Each argument is either unibyte or all-ASCII multibyte:
	 we can compare bytewise.  */
      int d = memcmp (SSDATA (string1), SSDATA (string2), n);
      if (d)
	return d;
      return n < SCHARS (string2) ? -1 : n < SCHARS (string1);
    }
  else if (STRING_MULTIBYTE (string1) && STRING_MULTIBYTE (string2))
    {
      /* Two arbitrary multibyte strings: we cannot use memcmp because
	 the encoding for raw bytes would sort those between U+007F and U+0080
	 which isn't where we want them.
	 Instead, we skip the longest common prefix and look at
	 what follows.  */
      ptrdiff_t nb1 = SBYTES (string1);
      ptrdiff_t nb2 = SBYTES (string2);
      ptrdiff_t nb = min (nb1, nb2);
      ptrdiff_t b = 0;

      /* String data is normally allocated with word alignment, but
	 there are exceptions (notably pure strings) so we restrict the
	 wordwise skipping to safe architectures.  */
      if (HAVE_FAST_UNALIGNED_ACCESS)
	{
	  /* First compare entire machine words.  */
	  int ws = sizeof (size_t);
	  const char *w1 = SSDATA (string1);
	  const char *w2 = SSDATA (string2);
	  while (b < nb - ws + 1 &&    load_unaligned_size_t (w1 + b)
		                    == load_unaligned_size_t (w2 + b))
	    b += ws;
	}

      /* Scan forward to the differing byte.  */
      while (b < nb && SREF (string1, b) == SREF (string2, b))
	b++;

      if (b >= nb)
	/* One string is a prefix of the other.  */
	return b < nb2 ? -1 : b < nb1;

      /* Now back up to the start of the differing characters:
	 it's the last byte not having the bit pattern 10xxxxxx.  */
      while ((SREF (string1, b) & 0xc0) == 0x80)
	b--;

      /* Compare the differing characters.  */
      ptrdiff_t i1 = 0, i2 = 0;
      ptrdiff_t i1_byte = b, i2_byte = b;
      int c1 = fetch_string_char_advance_no_check (string1, &i1, &i1_byte);
      int c2 = fetch_string_char_advance_no_check (string2, &i2, &i2_byte);
      return c1 < c2 ? -1 : c1 > c2;
    }
  else if (STRING_MULTIBYTE (string1))
    {
      /* string1 multibyte, string2 unibyte */
      ptrdiff_t i1 = 0, i1_byte = 0, i2 = 0;
      while (i1 < n)
	{
	  int c1 = fetch_string_char_advance_no_check (string1, &i1, &i1_byte);
	  int c2 = SREF (string2, i2++);
	  if (c1 != c2)
	    return c1 < c2 ? -1 : 1;
	}
      return i1 < SCHARS (string2) ? -1 : i1 < SCHARS (string1);
    }
  else
    {
      /* string1 unibyte, string2 multibyte */
      ptrdiff_t i1 = 0, i2 = 0, i2_byte = 0;
      while (i1 < n)
	{
	  int c1 = SREF (string1, i1++);
	  int c2 = fetch_string_char_advance_no_check (string2, &i2, &i2_byte);
	  if (c1 != c2)
	    return c1 < c2 ? -1 : 1;
	}
      return i1 < SCHARS (string2) ? -1 : i1 < SCHARS (string1);
    }
}

DEFUN ("string-lessp", Fstring_lessp, Sstring_lessp, 2, 2, 0,
       doc: /* Return non-nil if STRING1 is less than STRING2 in lexicographic order.
Case is significant.
Symbols are also allowed; their print names are used instead.  */)
  (Lisp_Object string1, Lisp_Object string2)
{
  if (SYMBOLP (string1))
    string1 = SYMBOL_NAME (string1);
  else
    CHECK_STRING (string1);
  if (SYMBOLP (string2))
    string2 = SYMBOL_NAME (string2);
  else
    CHECK_STRING (string2);

  return string_cmp (string1, string2) < 0 ? Qt : Qnil;
}

DEFUN ("string-version-lessp", Fstring_version_lessp,
       Sstring_version_lessp, 2, 2, 0,
       doc: /* Return non-nil if S1 is less than S2, as version strings.

This function compares version strings S1 and S2:
   1) By prefix lexicographically.
   2) Then by version (similarly to version comparison of Debian's dpkg).
      Leading zeros in version numbers are ignored.
   3) If both prefix and version are equal, compare as ordinary strings.

For example, \"foo2.png\" compares less than \"foo12.png\".
Case is significant.
Symbols are also allowed; their print names are used instead.  */)
  (Lisp_Object string1, Lisp_Object string2)
{
  if (SYMBOLP (string1))
    string1 = SYMBOL_NAME (string1);
  if (SYMBOLP (string2))
    string2 = SYMBOL_NAME (string2);
  CHECK_STRING (string1);
  CHECK_STRING (string2);
  int cmp = filenvercmp (SSDATA (string1), SBYTES (string1),
			 SSDATA (string2), SBYTES (string2));
  return cmp < 0 ? Qt : Qnil;
}

DEFUN ("string-collate-lessp", Fstring_collate_lessp, Sstring_collate_lessp, 2, 4, 0,
       doc: /* Return t if first arg string is less than second in collation order.
Symbols are also allowed; their print names are used instead.

This function obeys the conventions for collation order in your
locale settings.  For example, punctuation and whitespace characters
might be considered less significant for sorting:

\(sort \\='("11" "12" "1 1" "1 2" "1.1" "1.2") \\='string-collate-lessp)
  => ("11" "1 1" "1.1" "12" "1 2" "1.2")

The optional argument LOCALE, a string, overrides the setting of your
current locale identifier for collation.  The value is system
dependent; a LOCALE \"en_US.UTF-8\" is applicable on POSIX systems,
while it would be, e.g., \"enu_USA.1252\" on MS-Windows systems.

If IGNORE-CASE is non-nil, characters are converted to lower-case
before comparing them.

To emulate Unicode-compliant collation on MS-Windows systems,
bind `w32-collate-ignore-punctuation' to a non-nil value, since
the codeset part of the locale cannot be \"UTF-8\" on MS-Windows.

Some operating systems do not implement correct collation (in specific
locale environments or at all).  Then, this functions falls back to
case-sensitive `string-lessp' and IGNORE-CASE argument is ignored.  */)
  (Lisp_Object s1, Lisp_Object s2, Lisp_Object locale, Lisp_Object ignore_case)
{
#if defined __STDC_ISO_10646__ || defined WINDOWSNT
  /* Check parameters.  */
  if (SYMBOLP (s1))
    s1 = SYMBOL_NAME (s1);
  if (SYMBOLP (s2))
    s2 = SYMBOL_NAME (s2);
  CHECK_STRING (s1);
  CHECK_STRING (s2);
  if (!NILP (locale))
    CHECK_STRING (locale);

  return (str_collate (s1, s2, locale, ignore_case) < 0) ? Qt : Qnil;

#else  /* !__STDC_ISO_10646__, !WINDOWSNT */
  return Fstring_lessp (s1, s2);
#endif /* !__STDC_ISO_10646__, !WINDOWSNT */
}

DEFUN ("string-collate-equalp", Fstring_collate_equalp, Sstring_collate_equalp, 2, 4, 0,
       doc: /* Return t if two strings have identical contents.
Symbols are also allowed; their print names are used instead.

This function obeys the conventions for collation order in your locale
settings.  For example, characters with different coding points but
the same meaning might be considered as equal, like different grave
accent Unicode characters:

\(string-collate-equalp (string ?\\uFF40) (string ?\\u1FEF))
  => t

The optional argument LOCALE, a string, overrides the setting of your
current locale identifier for collation.  The value is system
dependent; a LOCALE \"en_US.UTF-8\" is applicable on POSIX systems,
while it would be \"enu_USA.1252\" on MS Windows systems.

If IGNORE-CASE is non-nil, characters are converted to lower-case
before comparing them.

To emulate Unicode-compliant collation on MS-Windows systems,
bind `w32-collate-ignore-punctuation' to a non-nil value, since
the codeset part of the locale cannot be \"UTF-8\" on MS-Windows.

If your system does not support a locale environment, this function
behaves like `string-equal', and in that case the IGNORE-CASE argument
is ignored.

Do NOT use this function to compare file names for equality.  */)
  (Lisp_Object s1, Lisp_Object s2, Lisp_Object locale, Lisp_Object ignore_case)
{
#if defined __STDC_ISO_10646__ || defined WINDOWSNT
  /* Check parameters.  */
  if (SYMBOLP (s1))
    s1 = SYMBOL_NAME (s1);
  if (SYMBOLP (s2))
    s2 = SYMBOL_NAME (s2);
  CHECK_STRING (s1);
  CHECK_STRING (s2);
  if (!NILP (locale))
    CHECK_STRING (locale);

  return (str_collate (s1, s2, locale, ignore_case) == 0) ? Qt : Qnil;

#else  /* !__STDC_ISO_10646__, !WINDOWSNT */
  return Fstring_equal (s1, s2);
#endif /* !__STDC_ISO_10646__, !WINDOWSNT */
}

static Lisp_Object concat_to_list (ptrdiff_t nargs, Lisp_Object *args,
				   Lisp_Object last_tail);
static Lisp_Object concat_to_vector (ptrdiff_t nargs, Lisp_Object *args);
static Lisp_Object concat_to_string (ptrdiff_t nargs, Lisp_Object *args);

Lisp_Object
concat2 (Lisp_Object s1, Lisp_Object s2)
{
  return concat_to_string (2, ((Lisp_Object []) {s1, s2}));
}

Lisp_Object
concat3 (Lisp_Object s1, Lisp_Object s2, Lisp_Object s3)
{
  return concat_to_string (3, ((Lisp_Object []) {s1, s2, s3}));
}

DEFUN ("append", Fappend, Sappend, 0, MANY, 0,
       doc: /* Concatenate all the arguments and make the result a list.
The result is a list whose elements are the elements of all the arguments.
Each argument may be a list, vector or string.

All arguments except the last argument are copied.  The last argument
is just used as the tail of the new list.  If the last argument is not
a list, this results in a dotted list.

As an exception, if all the arguments except the last are nil, and the
last argument is not a list, the return value is that last argument
unaltered, not a list.

usage: (append &rest SEQUENCES)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  if (nargs == 0)
    return Qnil;
  return concat_to_list (nargs - 1, args, args[nargs - 1]);
}

DEFUN ("concat", Fconcat, Sconcat, 0, MANY, 0,
       doc: /* Concatenate all the arguments and make the result a string.
The result is a string whose elements are the elements of all the arguments.
Each argument may be a string or a list or vector of characters (integers).

Values of the `composition' property of the result are not guaranteed
to be `eq'.
usage: (concat &rest SEQUENCES)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  return concat_to_string (nargs, args);
}

DEFUN ("vconcat", Fvconcat, Svconcat, 0, MANY, 0,
       doc: /* Concatenate all the arguments and make the result a vector.
The result is a vector whose elements are the elements of all the arguments.
Each argument may be a list, vector or string.
usage: (vconcat &rest SEQUENCES)   */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  return concat_to_vector (nargs, args);
}


DEFUN ("copy-sequence", Fcopy_sequence, Scopy_sequence, 1, 1, 0,
       doc: /* Return a copy of a list, vector, string, char-table or record.
The elements of a list, vector or record are not copied; they are
shared with the original.  See Info node `(elisp) Sequence Functions'
for more details about this sharing and its effects.
If the original sequence is empty, this function may return
the same empty object instead of its copy.  */)
  (Lisp_Object arg)
{
  if (NILP (arg)) return arg;

  if (CONSP (arg))
    {
      Lisp_Object val = Fcons (XCAR (arg), Qnil);
      Lisp_Object prev = val;
      Lisp_Object tail = XCDR (arg);
      FOR_EACH_TAIL (tail)
	{
	  Lisp_Object c = Fcons (XCAR (tail), Qnil);
	  XSETCDR (prev, c);
	  prev = c;
	}
      CHECK_LIST_END (tail, tail);
      return val;
    }

  if (STRINGP (arg))
    {
      ptrdiff_t bytes = SBYTES (arg);
      ptrdiff_t chars = SCHARS (arg);
      Lisp_Object val = STRING_MULTIBYTE (arg)
	? make_uninit_multibyte_string (chars, bytes)
	: make_uninit_string (bytes);
      memcpy (SDATA (val), SDATA (arg), bytes);
      INTERVAL ivs = string_intervals (arg);
      if (ivs)
	{
	  INTERVAL copy = copy_intervals (ivs, 0, chars);
	  set_interval_object (copy, val);
	  set_string_intervals (val, copy);
	}
      return val;
    }

  if (VECTORP (arg))
    return Fvector (ASIZE (arg), XVECTOR (arg)->contents);

  if (RECORDP (arg))
    return Frecord (PVSIZE (arg), XVECTOR (arg)->contents);

  if (CHAR_TABLE_P (arg))
    return copy_char_table (arg);

  if (BOOL_VECTOR_P (arg))
    {
      EMACS_INT nbits = bool_vector_size (arg);
      ptrdiff_t nbytes = bool_vector_bytes (nbits);
      Lisp_Object val = make_uninit_bool_vector (nbits);
      memcpy (bool_vector_data (val), bool_vector_data (arg), nbytes);
      return val;
    }

  wrong_type_argument (Qsequencep, arg);
}

/* This structure holds information of an argument of `concat_to_string'
   that is a string and has text properties to be copied.  */
struct textprop_rec
{
  ptrdiff_t argnum;		/* refer to ARGS (arguments of `concat') */
  ptrdiff_t to;			/* refer to VAL (the target string) */
};

static Lisp_Object
concat_to_string (ptrdiff_t nargs, Lisp_Object *args)
{
  USE_SAFE_ALLOCA;

  /* Check types and compute total length in chars of arguments in RESULT_LEN,
     length in bytes in RESULT_LEN_BYTE, and determine in DEST_MULTIBYTE
     whether the result should be a multibyte string.  */
  EMACS_INT result_len = 0;
  EMACS_INT result_len_byte = 0;
  bool dest_multibyte = false;
  bool some_unibyte = false;
  for (ptrdiff_t i = 0; i < nargs; i++)
    {
      Lisp_Object arg = args[i];
      EMACS_INT len;

      /* We must count the number of bytes needed in the string
	 as well as the number of characters.  */

      if (STRINGP (arg))
	{
	  ptrdiff_t arg_len_byte = SBYTES (arg);
	  len = SCHARS (arg);
	  if (STRING_MULTIBYTE (arg))
	    dest_multibyte = true;
	  else
	    some_unibyte = true;
	  if (STRING_BYTES_BOUND - result_len_byte < arg_len_byte)
	    string_overflow ();
	  result_len_byte += arg_len_byte;
	}
      else if (VECTORP (arg))
	{
	  len = ASIZE (arg);
	  ptrdiff_t arg_len_byte = 0;
	  for (ptrdiff_t j = 0; j < len; j++)
	    {
	      Lisp_Object ch = AREF (arg, j);
	      CHECK_CHARACTER (ch);
	      int c = XFIXNAT (ch);
	      arg_len_byte += CHAR_BYTES (c);
	      if (!ASCII_CHAR_P (c) && !CHAR_BYTE8_P (c))
		dest_multibyte = true;
	    }
	  if (STRING_BYTES_BOUND - result_len_byte < arg_len_byte)
	    string_overflow ();
	  result_len_byte += arg_len_byte;
	}
      else if (NILP (arg))
	continue;
      else if (CONSP (arg))
	{
	  len = XFIXNAT (Flength (arg));
	  ptrdiff_t arg_len_byte = 0;
	  for (; CONSP (arg); arg = XCDR (arg))
	    {
	      Lisp_Object ch = XCAR (arg);
	      CHECK_CHARACTER (ch);
	      int c = XFIXNAT (ch);
	      arg_len_byte += CHAR_BYTES (c);
	      if (!ASCII_CHAR_P (c) && !CHAR_BYTE8_P (c))
		dest_multibyte = true;
	    }
	  if (STRING_BYTES_BOUND - result_len_byte < arg_len_byte)
	    string_overflow ();
	  result_len_byte += arg_len_byte;
	}
      else
	wrong_type_argument (Qsequencep, arg);

      result_len += len;
      if (MOST_POSITIVE_FIXNUM < result_len)
	memory_full (SIZE_MAX);
    }

  if (dest_multibyte && some_unibyte)
    {
      /* Non-ASCII characters in unibyte strings take two bytes when
	 converted to multibyte -- count them and adjust the total.  */
      for (ptrdiff_t i = 0; i < nargs; i++)
	{
	  Lisp_Object arg = args[i];
	  if (STRINGP (arg) && !STRING_MULTIBYTE (arg))
	    {
	      ptrdiff_t bytes = SCHARS (arg);
	      const unsigned char *s = SDATA (arg);
	      ptrdiff_t nonascii = 0;
	      for (ptrdiff_t j = 0; j < bytes; j++)
		nonascii += s[j] >> 7;
	      if (STRING_BYTES_BOUND - result_len_byte < nonascii)
		string_overflow ();
	      result_len_byte += nonascii;
	    }
	}
    }

  if (!dest_multibyte)
    result_len_byte = result_len;

  /* Create the output object.  */
  Lisp_Object result = dest_multibyte
    ? make_uninit_multibyte_string (result_len, result_len_byte)
    : make_uninit_string (result_len);

  /* Copy the contents of the args into the result.  */
  ptrdiff_t toindex = 0;
  ptrdiff_t toindex_byte = 0;

  /* When we make a multibyte string, we can't copy text properties
     while concatenating each string because the length of resulting
     string can't be decided until we finish the whole concatenation.
     So, we record strings that have text properties to be copied
     here, and copy the text properties after the concatenation.  */
  struct textprop_rec *textprops;
  /* Number of elements in textprops.  */
  ptrdiff_t num_textprops = 0;
  SAFE_NALLOCA (textprops, 1, nargs);

  for (ptrdiff_t i = 0; i < nargs; i++)
    {
      Lisp_Object arg = args[i];
      if (STRINGP (arg))
	{
	  if (string_intervals (arg))
	    {
	      textprops[num_textprops].argnum = i;
	      textprops[num_textprops].to = toindex;
	      num_textprops++;
	    }
	  ptrdiff_t nchars = SCHARS (arg);
	  if (STRING_MULTIBYTE (arg) == dest_multibyte)
	    {
	      /* Between strings of the same kind, copy fast.  */
	      ptrdiff_t arg_len_byte = SBYTES (arg);
	      memcpy (SDATA (result) + toindex_byte, SDATA (arg), arg_len_byte);
	      toindex_byte += arg_len_byte;
	    }
	  else
	    {
	      /* Copy a single-byte string to a multibyte string.  */
	      toindex_byte += str_to_multibyte (SDATA (result) + toindex_byte,
						SDATA (arg), nchars);
	    }
	  toindex += nchars;
	}
      else if (VECTORP (arg))
	{
	  ptrdiff_t len = ASIZE (arg);
	  for (ptrdiff_t j = 0; j < len; j++)
	    {
	      int c = XFIXNAT (AREF (arg, j));
	      if (dest_multibyte)
		toindex_byte += CHAR_STRING (c, SDATA (result) + toindex_byte);
	      else
		SSET (result, toindex_byte++, c);
	      toindex++;
	    }
	}
      else
	for (Lisp_Object tail = arg; !NILP (tail); tail = XCDR (tail))
	  {
	    int c = XFIXNAT (XCAR (tail));
	    if (dest_multibyte)
	      toindex_byte += CHAR_STRING (c, SDATA (result) + toindex_byte);
	    else
	      SSET (result, toindex_byte++, c);
	    toindex++;
	  }
    }

  if (num_textprops > 0)
    {
      ptrdiff_t last_to_end = -1;
      for (ptrdiff_t i = 0; i < num_textprops; i++)
	{
	  Lisp_Object arg = args[textprops[i].argnum];
	  Lisp_Object props = text_property_list (arg,
						  make_fixnum (0),
						  make_fixnum (SCHARS (arg)),
						  Qnil);
	  /* If successive arguments have properties, be sure that the
	     value of `composition' property be the copy.  */
	  if (last_to_end == textprops[i].to)
	    make_composition_value_copy (props);
	  add_text_properties_from_list (result, props,
					 make_fixnum (textprops[i].to));
	  last_to_end = textprops[i].to + SCHARS (arg);
	}
    }

  SAFE_FREE ();
  return result;
}

/* Concatenate sequences into a list. */
Lisp_Object
concat_to_list (ptrdiff_t nargs, Lisp_Object *args, Lisp_Object last_tail)
{
  /* Copy the contents of the args into the result.  */
  Lisp_Object result = Qnil;
  Lisp_Object last = Qnil;	/* Last cons in result if nonempty.  */

  for (ptrdiff_t i = 0; i < nargs; i++)
    {
      Lisp_Object arg = args[i];
      /* List arguments are treated specially since this is the common case.  */
      if (CONSP (arg))
	{
	  Lisp_Object head = Fcons (XCAR (arg), Qnil);
	  Lisp_Object prev = head;
	  arg = XCDR (arg);
	  FOR_EACH_TAIL (arg)
	    {
	      Lisp_Object next = Fcons (XCAR (arg), Qnil);
	      XSETCDR (prev, next);
	      prev = next;
	    }
	  CHECK_LIST_END (arg, arg);
	  if (NILP (result))
	    result = head;
	  else
	    XSETCDR (last, head);
	  last = prev;
	}
      else if (NILP (arg))
	;
      else if (VECTORP (arg) || STRINGP (arg)
	       || BOOL_VECTOR_P (arg) || CLOSUREP (arg))
	{
	  ptrdiff_t arglen = XFIXNUM (Flength (arg));
	  ptrdiff_t argindex_byte = 0;

	  /* Copy element by element.  */
	  for (ptrdiff_t argindex = 0; argindex < arglen; argindex++)
	    {
	      /* Fetch next element of `arg' arg into `elt', or break if
		 `arg' is exhausted. */
	      Lisp_Object elt;
	      if (STRINGP (arg))
		{
		  int c;
		  if (STRING_MULTIBYTE (arg))
		    {
		      ptrdiff_t char_idx = argindex;
		      c = fetch_string_char_advance_no_check (arg, &char_idx,
							      &argindex_byte);
		    }
		  else
		    c = SREF (arg, argindex);
		  elt = make_fixed_natnum (c);
		}
	      else if (BOOL_VECTOR_P (arg))
		elt = bool_vector_ref (arg, argindex);
	      else
		elt = AREF (arg, argindex);

	      /* Store this element into the result.  */
	      Lisp_Object node = Fcons (elt, Qnil);
	      if (NILP (result))
		result = node;
	      else
		XSETCDR (last, node);
	      last = node;
	    }
	}
      else
	wrong_type_argument (Qsequencep, arg);
    }

  if (NILP (result))
    result = last_tail;
  else
    XSETCDR (last, last_tail);

  return result;
}

/* Concatenate sequences into a vector.  */
Lisp_Object
concat_to_vector (ptrdiff_t nargs, Lisp_Object *args)
{
  /* Check argument types and compute total length of arguments.  */
  EMACS_INT result_len = 0;
  for (ptrdiff_t i = 0; i < nargs; i++)
    {
      Lisp_Object arg = args[i];
      if (!(VECTORP (arg) || CONSP (arg) || NILP (arg) || STRINGP (arg)
	    || BOOL_VECTOR_P (arg) || CLOSUREP (arg)))
	wrong_type_argument (Qsequencep, arg);
      EMACS_INT len = XFIXNAT (Flength (arg));
      result_len += len;
      if (MOST_POSITIVE_FIXNUM < result_len)
	memory_full (SIZE_MAX);
    }

  /* Create the output vector.  */
  Lisp_Object result = make_uninit_vector (result_len);
  Lisp_Object *dst = XVECTOR (result)->contents;

  /* Copy the contents of the args into the result.  */

  for (ptrdiff_t i = 0; i < nargs; i++)
    {
      Lisp_Object arg = args[i];
      if (VECTORP (arg))
	{
	  ptrdiff_t size = ASIZE (arg);
	  memcpy (dst, XVECTOR (arg)->contents, size * sizeof *dst);
	  dst += size;
	}
      else if (CONSP (arg))
	do
	  {
	    *dst++ = XCAR (arg);
	    arg = XCDR (arg);
	  }
	while (!NILP (arg));
      else if (NILP (arg))
	;
      else if (STRINGP (arg))
	{
	  ptrdiff_t size = SCHARS (arg);
	  if (STRING_MULTIBYTE (arg))
	    {
	      ptrdiff_t byte = 0;
	      for (ptrdiff_t i = 0; i < size;)
		{
		  int c = fetch_string_char_advance_no_check (arg, &i, &byte);
		  *dst++ = make_fixnum (c);
		}
	    }
	  else
	    for (ptrdiff_t i = 0; i < size; i++)
	      *dst++ = make_fixnum (SREF (arg, i));
	}
      else if (BOOL_VECTOR_P (arg))
	{
	  ptrdiff_t size = bool_vector_size (arg);
	  for (ptrdiff_t i = 0; i < size; i++)
	    *dst++ = bool_vector_ref (arg, i);
	}
      else
	{
	  eassert (CLOSUREP (arg));
	  ptrdiff_t size = PVSIZE (arg);
	  memcpy (dst, XVECTOR (arg)->contents, size * sizeof *dst);
	  dst += size;
	}
    }
  eassert (dst == XVECTOR (result)->contents + result_len);

  return result;
}

static Lisp_Object string_char_byte_cache_string;
static ptrdiff_t string_char_byte_cache_charpos;
static ptrdiff_t string_char_byte_cache_bytepos;

void
clear_string_char_byte_cache (void)
{
  string_char_byte_cache_string = Qnil;
}

/* Return the byte index corresponding to CHAR_INDEX in STRING.  */

ptrdiff_t
string_char_to_byte (Lisp_Object string, ptrdiff_t char_index)
{
  ptrdiff_t i_byte;
  ptrdiff_t best_below, best_below_byte;
  ptrdiff_t best_above, best_above_byte;

  best_below = best_below_byte = 0;
  best_above = SCHARS (string);
  best_above_byte = SBYTES (string);
  if (best_above == best_above_byte)
    return char_index;

  if (BASE_EQ (string, string_char_byte_cache_string))
    {
      if (string_char_byte_cache_charpos < char_index)
	{
	  best_below = string_char_byte_cache_charpos;
	  best_below_byte = string_char_byte_cache_bytepos;
	}
      else
	{
	  best_above = string_char_byte_cache_charpos;
	  best_above_byte = string_char_byte_cache_bytepos;
	}
    }

  if (char_index - best_below < best_above - char_index)
    {
      unsigned char *p = SDATA (string) + best_below_byte;

      while (best_below < char_index)
	{
	  p += BYTES_BY_CHAR_HEAD (*p);
	  best_below++;
	}
      i_byte = p - SDATA (string);
    }
  else
    {
      unsigned char *p = SDATA (string) + best_above_byte;

      while (best_above > char_index)
	{
	  p--;
	  while (!CHAR_HEAD_P (*p)) p--;
	  best_above--;
	}
      i_byte = p - SDATA (string);
    }

  string_char_byte_cache_bytepos = i_byte;
  string_char_byte_cache_charpos = char_index;
  string_char_byte_cache_string = string;

  return i_byte;
}

/* Return the character index corresponding to BYTE_INDEX in STRING.  */

ptrdiff_t
string_byte_to_char (Lisp_Object string, ptrdiff_t byte_index)
{
  ptrdiff_t i, i_byte;
  ptrdiff_t best_below, best_below_byte;
  ptrdiff_t best_above, best_above_byte;

  best_below = best_below_byte = 0;
  best_above = SCHARS (string);
  best_above_byte = SBYTES (string);
  if (best_above == best_above_byte)
    return byte_index;

  if (BASE_EQ (string, string_char_byte_cache_string))
    {
      if (string_char_byte_cache_bytepos < byte_index)
	{
	  best_below = string_char_byte_cache_charpos;
	  best_below_byte = string_char_byte_cache_bytepos;
	}
      else
	{
	  best_above = string_char_byte_cache_charpos;
	  best_above_byte = string_char_byte_cache_bytepos;
	}
    }

  if (byte_index - best_below_byte < best_above_byte - byte_index)
    {
      unsigned char *p = SDATA (string) + best_below_byte;
      unsigned char *pend = SDATA (string) + byte_index;

      while (p < pend)
	{
	  p += BYTES_BY_CHAR_HEAD (*p);
	  best_below++;
	}
      i = best_below;
      i_byte = p - SDATA (string);
    }
  else
    {
      unsigned char *p = SDATA (string) + best_above_byte;
      unsigned char *pbeg = SDATA (string) + byte_index;

      while (p > pbeg)
	{
	  p--;
	  while (!CHAR_HEAD_P (*p)) p--;
	  best_above--;
	}
      i = best_above;
      i_byte = p - SDATA (string);
    }

  string_char_byte_cache_bytepos = i_byte;
  string_char_byte_cache_charpos = i;
  string_char_byte_cache_string = string;

  return i;
}

/* Convert STRING (if unibyte) to a multibyte string without changing
   the number of characters.  Characters 0x80..0xff are interpreted as
   raw bytes. */

Lisp_Object
string_to_multibyte (Lisp_Object string)
{
  if (STRING_MULTIBYTE (string))
    return string;

  ptrdiff_t nchars = SCHARS (string);
  ptrdiff_t nbytes = count_size_as_multibyte (SDATA (string), nchars);
  /* If all the chars are ASCII, they won't need any more bytes once
     converted.  */
  if (nbytes == nchars)
    return make_multibyte_string (SSDATA (string), nbytes, nbytes);

  Lisp_Object ret = make_uninit_multibyte_string (nchars, nbytes);
  str_to_multibyte (SDATA (ret), SDATA (string), nchars);
  return ret;
}


/* Convert STRING to a single-byte string.  */

Lisp_Object
string_make_unibyte (Lisp_Object string)
{
  ptrdiff_t nchars;
  unsigned char *buf;
  Lisp_Object ret;
  USE_SAFE_ALLOCA;

  if (! STRING_MULTIBYTE (string))
    return string;

  nchars = SCHARS (string);

  buf = SAFE_ALLOCA (nchars);
  copy_text (SDATA (string), buf, SBYTES (string),
	     1, 0);

  ret = make_unibyte_string ((char *) buf, nchars);
  SAFE_FREE ();

  return ret;
}

DEFUN ("string-make-multibyte", Fstring_make_multibyte, Sstring_make_multibyte,
       1, 1, 0,
       doc: /* Return the multibyte equivalent of STRING.
If STRING is unibyte and contains non-ASCII characters, the function
`unibyte-char-to-multibyte' is used to convert each unibyte character
to a multibyte character.  In this case, the returned string is a
newly created string with no text properties.  If STRING is multibyte
or entirely ASCII, it is returned unchanged.  In particular, when
STRING is unibyte and entirely ASCII, the returned string is unibyte.
\(When the characters are all ASCII, Emacs primitives will treat the
string the same way whether it is unibyte or multibyte.)  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  if (STRING_MULTIBYTE (string))
    return string;

  ptrdiff_t nchars = SCHARS (string);
  ptrdiff_t nbytes = count_size_as_multibyte (SDATA (string), nchars);
  if (nbytes == nchars)
    return string;

  Lisp_Object ret = make_uninit_multibyte_string (nchars, nbytes);
  str_to_multibyte (SDATA (ret), SDATA (string), nchars);
  return ret;
}

DEFUN ("string-make-unibyte", Fstring_make_unibyte, Sstring_make_unibyte,
       1, 1, 0,
       doc: /* Return the unibyte equivalent of STRING.
Multibyte character codes above 255 are converted to unibyte
by taking just the low 8 bits of each character's code.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  return string_make_unibyte (string);
}

DEFUN ("string-as-unibyte", Fstring_as_unibyte, Sstring_as_unibyte,
       1, 1, 0,
       doc: /* Return a unibyte string with the same individual bytes as STRING.
If STRING is unibyte, the result is STRING itself.
Otherwise it is a newly created string, with no text properties.
If STRING is multibyte and contains a character of charset
`eight-bit', it is converted to the corresponding single byte.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  if (STRING_MULTIBYTE (string))
    {
      unsigned char *str = (unsigned char *) xlispstrdup (string);
      ptrdiff_t bytes = str_as_unibyte (str, SBYTES (string));

      string = make_unibyte_string ((char *) str, bytes);
      xfree (str);
    }
  return string;
}

DEFUN ("string-as-multibyte", Fstring_as_multibyte, Sstring_as_multibyte,
       1, 1, 0,
       doc: /* Return a multibyte string with the same individual bytes as STRING.
If STRING is multibyte, the result is STRING itself.
Otherwise it is a newly created string, with no text properties.

If STRING is unibyte and contains an individual 8-bit byte (i.e. not
part of a correct utf-8 sequence), it is converted to the corresponding
multibyte character of charset `eight-bit'.
See also `string-to-multibyte'.

Beware, this often doesn't really do what you think it does.
It is similar to (decode-coding-string STRING \\='utf-8-emacs).
If you're not sure, whether to use `string-as-multibyte' or
`string-to-multibyte', use `string-to-multibyte'.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  if (! STRING_MULTIBYTE (string))
    {
      Lisp_Object new_string;
      ptrdiff_t nchars, nbytes;

      parse_str_as_multibyte (SDATA (string),
			      SBYTES (string),
			      &nchars, &nbytes);
      new_string = make_uninit_multibyte_string (nchars, nbytes);
      memcpy (SDATA (new_string), SDATA (string), SBYTES (string));
      if (nbytes != SBYTES (string))
	str_as_multibyte (SDATA (new_string), nbytes,
			  SBYTES (string), NULL);
      string = new_string;
      set_string_intervals (string, NULL);
    }
  return string;
}

DEFUN ("string-to-multibyte", Fstring_to_multibyte, Sstring_to_multibyte,
       1, 1, 0,
       doc: /* Return a multibyte string with the same individual chars as STRING.
If STRING is multibyte, the result is STRING itself.
Otherwise it is a newly created string, with no text properties.

If STRING is unibyte and contains an 8-bit byte, it is converted to
the corresponding multibyte character of charset `eight-bit'.

This differs from `string-as-multibyte' by converting each byte of a correct
utf-8 sequence to an eight-bit character, not just bytes that don't form a
correct sequence.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);

  return string_to_multibyte (string);
}

DEFUN ("string-to-unibyte", Fstring_to_unibyte, Sstring_to_unibyte,
       1, 1, 0,
       doc: /* Return a unibyte string with the same individual chars as STRING.
If STRING is unibyte, the result is STRING itself.
Otherwise it is a newly created string, with no text properties,
where each `eight-bit' character is converted to the corresponding byte.
If STRING contains a non-ASCII, non-`eight-bit' character,
an error is signaled.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);
  if (!STRING_MULTIBYTE (string))
    return string;

  ptrdiff_t chars = SCHARS (string);
  Lisp_Object ret = make_uninit_string (chars);
  unsigned char *src = SDATA (string);
  unsigned char *dst = SDATA (ret);
  for (ptrdiff_t i = 0; i < chars; i++)
    {
      unsigned char b = *src++;
      if (b <= 0x7f)
	*dst++ = b;					 /* ASCII */
      else if (CHAR_BYTE8_HEAD_P (b))
	*dst++ = 0x80 | (b & 1) << 6 | (*src++ & 0x3f);	 /* raw byte */
      else
	error ("Cannot convert character at index %"pD"d to unibyte", i);
    }
  return ret;
}


DEFUN ("copy-alist", Fcopy_alist, Scopy_alist, 1, 1, 0,
       doc: /* Return a copy of ALIST.
This is an alist which represents the same mapping from objects to objects,
but does not share the alist structure with ALIST.
The objects mapped (cars and cdrs of elements of the alist)
are shared, however.
Elements of ALIST that are not conses are also shared.  */)
  (Lisp_Object alist)
{
  CHECK_LIST (alist);
  if (NILP (alist))
    return alist;
  alist = Fcopy_sequence (alist);
  for (Lisp_Object tem = alist; !NILP (tem); tem = XCDR (tem))
    {
      Lisp_Object car = XCAR (tem);
      if (CONSP (car))
	XSETCAR (tem, Fcons (XCAR (car), XCDR (car)));
    }
  return alist;
}

/* Check that ARRAY can have a valid subarray [FROM..TO),
   given that its size is SIZE.
   If FROM is nil, use 0; if TO is nil, use SIZE.
   Count negative values backwards from the end.
   Set *IFROM and *ITO to the two indexes used.  */

void
validate_subarray (Lisp_Object array, Lisp_Object from, Lisp_Object to,
		   ptrdiff_t size, ptrdiff_t *ifrom, ptrdiff_t *ito)
{
  EMACS_INT f, t;

  if (FIXNUMP (from))
    {
      f = XFIXNUM (from);
      if (f < 0)
	f += size;
    }
  else if (NILP (from))
    f = 0;
  else
    wrong_type_argument (Qintegerp, from);

  if (FIXNUMP (to))
    {
      t = XFIXNUM (to);
      if (t < 0)
	t += size;
    }
  else if (NILP (to))
    t = size;
  else
    wrong_type_argument (Qintegerp, to);

  if (! (0 <= f && f <= t && t <= size))
    args_out_of_range_3 (array, from, to);

  *ifrom = f;
  *ito = t;
}

DEFUN ("substring", Fsubstring, Ssubstring, 1, 3, 0,
       doc: /* Return a new string whose contents are a substring of STRING.
The returned string consists of the characters between index FROM
\(inclusive) and index TO (exclusive) of STRING.  FROM and TO are
zero-indexed: 0 means the first character of STRING.  Negative values
are counted from the end of STRING.  If TO is nil, the substring runs
to the end of STRING.

The STRING argument may also be a vector.  In that case, the return
value is a new vector that contains the elements between index FROM
\(inclusive) and index TO (exclusive) of that vector argument.

With one argument, just copy STRING (with properties, if any).  */)
  (Lisp_Object string, Lisp_Object from, Lisp_Object to)
{
  Lisp_Object res;
  ptrdiff_t size, ifrom, ito;

  size = CHECK_VECTOR_OR_STRING (string);
  validate_subarray (string, from, to, size, &ifrom, &ito);

  if (STRINGP (string))
    {
      ptrdiff_t from_byte
	= !ifrom ? 0 : string_char_to_byte (string, ifrom);
      ptrdiff_t to_byte
	= ito == size ? SBYTES (string) : string_char_to_byte (string, ito);
      res = make_specified_string (SSDATA (string) + from_byte,
				   ito - ifrom, to_byte - from_byte,
				   STRING_MULTIBYTE (string));
      copy_text_properties (make_fixnum (ifrom), make_fixnum (ito),
			    string, make_fixnum (0), res, Qnil);
    }
  else
    res = Fvector (ito - ifrom, aref_addr (string, ifrom));

  return res;
}


DEFUN ("substring-no-properties", Fsubstring_no_properties, Ssubstring_no_properties, 1, 3, 0,
       doc: /* Return a substring of STRING, without text properties.
It starts at index FROM and ends before TO.
TO may be nil or omitted; then the substring runs to the end of STRING.
If FROM is nil or omitted, the substring starts at the beginning of STRING.
If FROM or TO is negative, it counts from the end.

With one argument, just copy STRING without its properties.  */)
  (Lisp_Object string, register Lisp_Object from, Lisp_Object to)
{
  ptrdiff_t from_char, to_char, from_byte, to_byte, size;

  CHECK_STRING (string);

  size = SCHARS (string);
  validate_subarray (string, from, to, size, &from_char, &to_char);

  from_byte = !from_char ? 0 : string_char_to_byte (string, from_char);
  to_byte =
    to_char == size ? SBYTES (string) : string_char_to_byte (string, to_char);
  return make_specified_string (SSDATA (string) + from_byte,
				to_char - from_char, to_byte - from_byte,
				STRING_MULTIBYTE (string));
}

/* Extract a substring of STRING, giving start and end positions
   both in characters and in bytes.  */

Lisp_Object
substring_both (Lisp_Object string, ptrdiff_t from, ptrdiff_t from_byte,
		ptrdiff_t to, ptrdiff_t to_byte)
{
  Lisp_Object res;
  ptrdiff_t size = CHECK_VECTOR_OR_STRING (string);

  if (!(0 <= from && from <= to && to <= size))
    args_out_of_range_3 (string, make_fixnum (from), make_fixnum (to));

  if (STRINGP (string))
    {
      res = make_specified_string (SSDATA (string) + from_byte,
				   to - from, to_byte - from_byte,
				   STRING_MULTIBYTE (string));
      copy_text_properties (make_fixnum (from), make_fixnum (to),
			    string, make_fixnum (0), res, Qnil);
    }
  else
    res = Fvector (to - from, aref_addr (string, from));

  return res;
}

DEFUN ("take", Ftake, Stake, 2, 2, 0,
       doc: /* Return the first N elements of LIST.
If N is zero or negative, return nil.
If N is greater or equal to the length of LIST, return LIST (or a copy).  */)
  (Lisp_Object n, Lisp_Object list)
{
  EMACS_INT m;
  if (FIXNUMP (n))
    {
      m = XFIXNUM (n);
      if (m <= 0)
	return Qnil;
    }
  else if (BIGNUMP (n))
    {
      if (mpz_sgn (*xbignum_val (n)) < 0)
	return Qnil;
      m = MOST_POSITIVE_FIXNUM;
    }
  else
    wrong_type_argument (Qintegerp, n);
  CHECK_LIST (list);
  if (NILP (list))
    return Qnil;
  Lisp_Object ret = Fcons (XCAR (list), Qnil);
  Lisp_Object prev = ret;
  m--;
  list = XCDR (list);
  while (m > 0 && CONSP (list))
    {
      Lisp_Object p = Fcons (XCAR (list), Qnil);
      XSETCDR (prev, p);
      prev = p;
      m--;
      list = XCDR (list);
    }
  if (m > 0 && !NILP (list))
    wrong_type_argument (Qlistp, list);
  return ret;
}

DEFUN ("ntake", Fntake, Sntake, 2, 2, 0,
       doc: /* Modify LIST to keep only the first N elements.
If N is zero or negative, return nil.
If N is greater or equal to the length of LIST, return LIST unmodified.
Otherwise, return LIST after truncating it.  */)
  (Lisp_Object n, Lisp_Object list)
{
  EMACS_INT m;
  if (FIXNUMP (n))
    {
      m = XFIXNUM (n);
      if (m <= 0)
	return Qnil;
    }
  else if (BIGNUMP (n))
    {
      if (mpz_sgn (*xbignum_val (n)) < 0)
	return Qnil;
      m = MOST_POSITIVE_FIXNUM;
    }
  else
    wrong_type_argument (Qintegerp, n);
  CHECK_LIST (list);
  Lisp_Object tail = list;
  --m;
  while (m > 0 && CONSP (tail))
    {
      tail = XCDR (tail);
      m--;
    }
  if (CONSP (tail))
    XSETCDR (tail, Qnil);
  else if (!NILP (tail))
    wrong_type_argument (Qlistp, list);
  return list;
}

DEFUN ("nthcdr", Fnthcdr, Snthcdr, 2, 2, 0,
       doc: /* Take cdr N times on LIST, return the result.  */)
  (Lisp_Object n, Lisp_Object list)
{
  Lisp_Object tail = list;

  CHECK_INTEGER (n);

  /* A huge but in-range EMACS_INT that can be substituted for a
     positive bignum while counting down.  It does not introduce
     miscounts because a list or cycle cannot possibly be this long,
     and any counting error is fixed up later.  */
  EMACS_INT large_num = EMACS_INT_MAX;

  EMACS_INT num;
  if (FIXNUMP (n))
    {
      num = XFIXNUM (n);

      /* Speed up small lists by omitting circularity and quit checking.  */
      if (num <= SMALL_LIST_LEN_MAX)
	{
	  for (; 0 < num; num--, tail = XCDR (tail))
	    if (! CONSP (tail))
	      {
		CHECK_LIST_END (tail, list);
		return Qnil;
	      }
	  return tail;
	}
    }
  else
    {
      if (mpz_sgn (*xbignum_val (n)) < 0)
	return tail;
      num = large_num;
    }

  EMACS_INT tortoise_num = num;
  Lisp_Object saved_tail = tail;
  FOR_EACH_TAIL_SAFE (tail)
    {
      /* If the tortoise just jumped (which is rare),
	 update TORTOISE_NUM accordingly.  */
      if (BASE_EQ (tail, li.tortoise))
	tortoise_num = num;

      saved_tail = XCDR (tail);
      num--;
      if (num == 0)
	return saved_tail;
      rarely_quit (num);
    }

  tail = saved_tail;
  if (! CONSP (tail))
    {
      CHECK_LIST_END (tail, list);
      return Qnil;
    }

  /* TAIL is part of a cycle.  Reduce NUM modulo the cycle length to
     avoid going around this cycle repeatedly.  */
  intptr_t cycle_length = tortoise_num - num;
  if (! FIXNUMP (n))
    {
      /* Undo any error introduced when LARGE_NUM was substituted for
	 N, by adding N - LARGE_NUM to NUM, using arithmetic modulo
	 CYCLE_LENGTH.  */
      /* Add N mod CYCLE_LENGTH to NUM.  */
      if (cycle_length <= ULONG_MAX)
	num += mpz_tdiv_ui (*xbignum_val (n), cycle_length);
      else
	{
	  mpz_set_intmax (mpz[0], cycle_length);
	  mpz_tdiv_r (mpz[0], *xbignum_val (n), mpz[0]);
	  intptr_t iz;
	  mpz_export (&iz, NULL, -1, sizeof iz, 0, 0, mpz[0]);
	  num += iz;
	}
      num += cycle_length - large_num % cycle_length;
    }
  num %= cycle_length;

  /* One last time through the cycle.  */
  for (; 0 < num; num--)
    {
      tail = XCDR (tail);
      rarely_quit (num);
    }
  return tail;
}

DEFUN ("nth", Fnth, Snth, 2, 2, 0,
       doc: /* Return the Nth element of LIST.
N counts from zero.  If LIST is not that long, nil is returned.  */)
  (Lisp_Object n, Lisp_Object list)
{
  return Fcar (Fnthcdr (n, list));
}

DEFUN ("elt", Felt, Selt, 2, 2, 0,
       doc: /* Return element of SEQUENCE at index N.  */)
  (Lisp_Object sequence, Lisp_Object n)
{
  if (CONSP (sequence) || NILP (sequence))
    return Fcar (Fnthcdr (n, sequence));

  /* Faref signals a "not array" error, so check here.  */
  CHECK_ARRAY (sequence, Qsequencep);
  return Faref (sequence, n);
}

enum { WORDS_PER_DOUBLE = (sizeof (double) / sizeof (EMACS_UINT)
                          + (sizeof (double) % sizeof (EMACS_UINT) != 0)) };
union double_and_words
{
  double val;
  EMACS_UINT word[WORDS_PER_DOUBLE];
};

/* Return true if the floats X and Y have the same value.
   This looks at X's and Y's representation, since (unlike '==')
   it returns true if X and Y are the same NaN.  */
static bool
same_float (Lisp_Object x, Lisp_Object y)
{
  union double_and_words
    xu = { .val = XFLOAT_DATA (x) },
    yu = { .val = XFLOAT_DATA (y) };
  EMACS_UINT neql = 0;
  for (int i = 0; i < WORDS_PER_DOUBLE; i++)
    neql |= xu.word[i] ^ yu.word[i];
  return !neql;
}

/* True if X can be compared using `eq'.
   This predicate is approximative, for maximum speed.  */
static bool
eq_comparable_value (Lisp_Object x)
{
  return SYMBOLP (x) || FIXNUMP (x);
}

DEFUN ("member", Fmember, Smember, 2, 2, 0,
       doc: /* Return non-nil if ELT is an element of LIST.  Comparison done with `equal'.
The value is actually the tail of LIST whose car is ELT.  */)
  (Lisp_Object elt, Lisp_Object list)
{
  if (eq_comparable_value (elt))
    return Fmemq (elt, list);
  Lisp_Object tail = list;
  FOR_EACH_TAIL (tail)
    if (! NILP (Fequal (elt, XCAR (tail))))
      return tail;
  CHECK_LIST_END (tail, list);
  return Qnil;
}

DEFUN ("memq", Fmemq, Smemq, 2, 2, 0,
       doc: /* Return non-nil if ELT is an element of LIST.  Comparison done with `eq'.
The value is actually the tail of LIST whose car is ELT.  */)
  (Lisp_Object elt, Lisp_Object list)
{
  Lisp_Object tail = list;
  FOR_EACH_TAIL (tail)
    if (EQ (XCAR (tail), elt))
      return tail;
  CHECK_LIST_END (tail, list);
  return Qnil;
}

Lisp_Object
memq_no_quit (Lisp_Object elt, Lisp_Object list)
{
  for (; CONSP (list); list = XCDR (list))
    if (EQ (XCAR (list), elt))
      return list;
  return Qnil;
}

DEFUN ("memql", Fmemql, Smemql, 2, 2, 0,
       doc: /* Return non-nil if ELT is an element of LIST.  Comparison done with `eql'.
The value is actually the tail of LIST whose car is ELT.  */)
  (Lisp_Object elt, Lisp_Object list)
{
  Lisp_Object tail = list;

  if (FLOATP (elt))
    {
      FOR_EACH_TAIL (tail)
        {
          Lisp_Object tem = XCAR (tail);
          if (FLOATP (tem) && same_float (elt, tem))
            return tail;
        }
    }
  else if (BIGNUMP (elt))
    {
      FOR_EACH_TAIL (tail)
        {
          Lisp_Object tem = XCAR (tail);
          if (BIGNUMP (tem)
	      && mpz_cmp (*xbignum_val (elt), *xbignum_val (tem)) == 0)
            return tail;
        }
    }
  else
    return Fmemq (elt, list);

  CHECK_LIST_END (tail, list);
  return Qnil;
}

DEFUN ("assq", Fassq, Sassq, 2, 2, 0,
       doc: /* Return non-nil if KEY is `eq' to the car of an element of ALIST.
The value is actually the first element of ALIST whose car is KEY.
Elements of ALIST that are not conses are ignored.  */)
  (Lisp_Object key, Lisp_Object alist)
{
  Lisp_Object tail = alist;
  FOR_EACH_TAIL (tail)
    if (CONSP (XCAR (tail)) && EQ (XCAR (XCAR (tail)), key))
      return XCAR (tail);
  CHECK_LIST_END (tail, alist);
  return Qnil;
}

/* Like Fassq but never report an error and do not allow quits.
   Use only on objects known to be non-circular lists.  */

Lisp_Object
assq_no_quit (Lisp_Object key, Lisp_Object alist)
{
  for (; ! NILP (alist); alist = XCDR (alist))
    if (CONSP (XCAR (alist)) && EQ (XCAR (XCAR (alist)), key))
      return XCAR (alist);
  return Qnil;
}

/* Assq but doesn't signal.  Unlike assq_no_quit, this function still
   detects circular lists; like assq_no_quit, this function does not
   allow quits and never signals.  If anything goes wrong, it returns
   Qnil.  */
Lisp_Object
assq_no_signal (Lisp_Object key, Lisp_Object alist)
{
  Lisp_Object tail = alist;
  FOR_EACH_TAIL_SAFE (tail)
    if (CONSP (XCAR (tail)) && EQ (XCAR (XCAR (tail)), key))
      return XCAR (tail);
  return Qnil;
}

DEFUN ("assoc", Fassoc, Sassoc, 2, 3, 0,
       doc: /* Return non-nil if KEY is equal to the car of an element of ALIST.
The value is actually the first element of ALIST whose car equals KEY.

Equality is defined by the function TESTFN, defaulting to `equal'.
TESTFN is called with 2 arguments: a car of an alist element and KEY.  */)
     (Lisp_Object key, Lisp_Object alist, Lisp_Object testfn)
{
  if (eq_comparable_value (key) && NILP (testfn))
    return Fassq (key, alist);
  Lisp_Object tail = alist;
  FOR_EACH_TAIL (tail)
    {
      Lisp_Object car = XCAR (tail);
      if (!CONSP (car))
	continue;
      if ((NILP (testfn)
	   ? (EQ (XCAR (car), key) || !NILP (Fequal
					     (XCAR (car), key)))
	   : !NILP (calln (testfn, XCAR (car), key))))
	return car;
    }
  CHECK_LIST_END (tail, alist);
  return Qnil;
}

/* Like Fassoc but never report an error and do not allow quits.
   Use only on keys and lists known to be non-circular, and on keys
   that are not too deep and are not window configurations.  */

Lisp_Object
assoc_no_quit (Lisp_Object key, Lisp_Object alist)
{
  for (; ! NILP (alist); alist = XCDR (alist))
    {
      Lisp_Object car = XCAR (alist);
      if (CONSP (car)
	  && (EQ (XCAR (car), key) || equal_no_quit (XCAR (car), key)))
	return car;
    }
  return Qnil;
}

DEFUN ("rassq", Frassq, Srassq, 2, 2, 0,
       doc: /* Return non-nil if KEY is `eq' to the cdr of an element of ALIST.
The value is actually the first element of ALIST whose cdr is KEY.  */)
  (Lisp_Object key, Lisp_Object alist)
{
  Lisp_Object tail = alist;
  FOR_EACH_TAIL (tail)
    if (CONSP (XCAR (tail)) && EQ (XCDR (XCAR (tail)), key))
      return XCAR (tail);
  CHECK_LIST_END (tail, alist);
  return Qnil;
}

DEFUN ("rassoc", Frassoc, Srassoc, 2, 2, 0,
       doc: /* Return non-nil if KEY is `equal' to the cdr of an element of ALIST.
The value is actually the first element of ALIST whose cdr equals KEY.  */)
  (Lisp_Object key, Lisp_Object alist)
{
  if (eq_comparable_value (key))
    return Frassq (key, alist);
  Lisp_Object tail = alist;
  FOR_EACH_TAIL (tail)
    {
      Lisp_Object car = XCAR (tail);
      if (CONSP (car)
	  && (EQ (XCDR (car), key) || !NILP (Fequal (XCDR (car), key))))
	return car;
    }
  CHECK_LIST_END (tail, alist);
  return Qnil;
}

DEFUN ("delq", Fdelq, Sdelq, 2, 2, 0,
       doc: /* Delete members of LIST which are `eq' to ELT, and return the result.
More precisely, this function skips any members `eq' to ELT at the
front of LIST, then removes members `eq' to ELT from the remaining
sublist by modifying its list structure, then returns the resulting
list.

Write `(setq foo (delq element foo))' to be sure of correctly changing
the value of a list `foo'.  See also `remq', which does not modify the
argument.  */)
  (Lisp_Object elt, Lisp_Object list)
{
  Lisp_Object prev = Qnil, tail = list;

  FOR_EACH_TAIL (tail)
    {
      Lisp_Object tem = XCAR (tail);
      if (EQ (elt, tem))
	{
	  if (NILP (prev))
	    list = XCDR (tail);
	  else
	    Fsetcdr (prev, XCDR (tail));
	}
      else
	prev = tail;
    }
  CHECK_LIST_END (tail, list);
  return list;
}

DEFUN ("delete", Fdelete, Sdelete, 2, 2, 0,
       doc: /* Delete members of SEQ which are `equal' to ELT, and return the result.
SEQ must be a sequence (i.e. a list, a vector, or a string).
The return value is a sequence of the same type.

If SEQ is a list, this behaves like `delq', except that it compares
with `equal' instead of `eq'.  In particular, it may remove elements
by altering the list structure.

If SEQ is not a list, deletion is never performed destructively;
instead this function creates and returns a new vector or string.

Write `(setq foo (delete element foo))' to be sure of correctly
changing the value of a sequence `foo'.  See also `remove', which
does not modify the argument.  */)
  (Lisp_Object elt, Lisp_Object seq)
{
  if (NILP (seq))
    ;
  else if (CONSP (seq))
    {
      Lisp_Object prev = Qnil, tail = seq;

      FOR_EACH_TAIL (tail)
	{
	  if (!NILP (Fequal (elt, XCAR (tail))))
	    {
	      if (NILP (prev))
		seq = XCDR (tail);
	      else
		Fsetcdr (prev, XCDR (tail));
	    }
	  else
	    prev = tail;
	}
      CHECK_LIST_END (tail, seq);
    }
  else if (VECTORP (seq))
    {
      ptrdiff_t n = 0;
      ptrdiff_t size = ASIZE (seq);
      USE_SAFE_ALLOCA;
      Lisp_Object *kept = SAFE_ALLOCA (size * sizeof *kept);

      for (ptrdiff_t i = 0; i < size; i++)
	{
	  kept[n] = AREF (seq, i);
	  n += NILP (Fequal (AREF (seq, i), elt));
	}

      if (n != size)
	seq = Fvector (n, kept);

      SAFE_FREE ();
    }
  else if (STRINGP (seq))
    {
      if (!CHARACTERP (elt))
	return seq;

      ptrdiff_t i, ibyte, nchars, nbytes, cbytes;
      int c;

      for (i = nchars = nbytes = ibyte = 0;
	   i < SCHARS (seq);
	   ++i, ibyte += cbytes)
	{
	  if (STRING_MULTIBYTE (seq))
	    {
	      c = STRING_CHAR (SDATA (seq) + ibyte);
	      cbytes = CHAR_BYTES (c);
	    }
	  else
	    {
	      c = SREF (seq, i);
	      cbytes = 1;
	    }

	  if (c != XFIXNUM (elt))
	    {
	      ++nchars;
	      nbytes += cbytes;
	    }
	}

      if (nchars != SCHARS (seq))
	{
	  Lisp_Object tem;

	  tem = make_uninit_multibyte_string (nchars, nbytes);
	  if (!STRING_MULTIBYTE (seq))
	    STRING_SET_UNIBYTE (tem);

	  for (i = nchars = nbytes = ibyte = 0;
	       i < SCHARS (seq);
	       ++i, ibyte += cbytes)
	    {
	      if (STRING_MULTIBYTE (seq))
		{
		  c = STRING_CHAR (SDATA (seq) + ibyte);
		  cbytes = CHAR_BYTES (c);
		}
	      else
		{
		  c = SREF (seq, i);
		  cbytes = 1;
		}

	      if (c != XFIXNUM (elt))
		{
		  unsigned char *from = SDATA (seq) + ibyte;
		  unsigned char *to   = SDATA (tem) + nbytes;
		  ptrdiff_t n;

		  ++nchars;
		  nbytes += cbytes;

		  for (n = cbytes; n--; )
		    *to++ = *from++;
		}
	    }

	  seq = tem;
	}
    }
  else
    wrong_type_argument (Qsequencep, seq);

  return seq;
}

DEFUN ("nreverse", Fnreverse, Snreverse, 1, 1, 0,
       doc: /* Reverse order of items in a list, vector or string SEQ.
If SEQ is a list, it should be nil-terminated.
This function may destructively modify SEQ to produce the value.  */)
  (Lisp_Object seq)
{
  if (NILP (seq))
    return seq;
  else if (CONSP (seq))
    {
      Lisp_Object prev, tail, next;

      for (prev = Qnil, tail = seq; CONSP (tail); tail = next)
	{
	  next = XCDR (tail);
	  /* If SEQ contains a cycle, attempting to reverse it
	     in-place will inevitably come back to SEQ.  */
	  if (BASE_EQ (next, seq))
	    circular_list (seq);
	  Fsetcdr (tail, prev);
	  prev = tail;
	}
      CHECK_LIST_END (tail, seq);
      seq = prev;
    }
  else if (VECTORP (seq))
    {
      ptrdiff_t i, size = ASIZE (seq);

      for (i = 0; i < size / 2; i++)
	{
	  Lisp_Object tem = AREF (seq, i);
	  ASET (seq, i, AREF (seq, size - i - 1));
	  ASET (seq, size - i - 1, tem);
	}
    }
  else if (BOOL_VECTOR_P (seq))
    {
      ptrdiff_t i, size = bool_vector_size (seq);

      for (i = 0; i < size / 2; i++)
	{
	  bool tem = bool_vector_bitref (seq, i);
	  bool_vector_set (seq, i, bool_vector_bitref (seq, size - i - 1));
	  bool_vector_set (seq, size - i - 1, tem);
	}
    }
  else if (STRINGP (seq))
    return Freverse (seq);
  else
    wrong_type_argument (Qarrayp, seq);
  return seq;
}

DEFUN ("reverse", Freverse, Sreverse, 1, 1, 0,
       doc: /* Return the reversed copy of list, vector, or string SEQ.
See also the function `nreverse', which is used more often.  */)
  (Lisp_Object seq)
{
  Lisp_Object new;

  if (NILP (seq))
    return Qnil;
  else if (CONSP (seq))
    {
      new = Qnil;
      FOR_EACH_TAIL (seq)
	new = Fcons (XCAR (seq), new);
      CHECK_LIST_END (seq, seq);
    }
  else if (VECTORP (seq))
    {
      ptrdiff_t i, size = ASIZE (seq);

      new = make_uninit_vector (size);
      for (i = 0; i < size; i++)
	ASET (new, i, AREF (seq, size - i - 1));
    }
  else if (BOOL_VECTOR_P (seq))
    {
      EMACS_INT nbits = bool_vector_size (seq);

      new = make_clear_bool_vector (nbits, true);
      for (ptrdiff_t i = 0; i < nbits; i++)
	if (bool_vector_bitref (seq, nbits - i - 1))
	  bool_vector_set (new, i, true);
    }
  else if (STRINGP (seq))
    {
      ptrdiff_t size = SCHARS (seq), bytes = SBYTES (seq);

      if (size == bytes)
	{
	  ptrdiff_t i;

	  new = make_uninit_string (size);
	  for (i = 0; i < size; i++)
	    SSET (new, i, SREF (seq, size - i - 1));
	}
      else
	{
	  unsigned char *p, *q;

	  new = make_uninit_multibyte_string (size, bytes);
	  p = SDATA (seq), q = SDATA (new) + bytes;
	  while (q > SDATA (new))
	    {
	      int len, ch = string_char_and_length (p, &len);
	      p += len, q -= len;
	      CHAR_STRING (ch, q);
	    }
	}
    }
  else
    wrong_type_argument (Qsequencep, seq);
  return new;
}


/* Stably sort LIST ordered by PREDICATE and KEYFUNC, optionally reversed.
   This converts the list to a vector, sorts the vector, and returns the
   result converted back to a list.  If INPLACE, the input list is
   reused to hold the sorted result; otherwise a new list is returned.  */
static Lisp_Object
sort_list (Lisp_Object list, Lisp_Object predicate, Lisp_Object keyfunc,
	   bool reverse, bool inplace)
{
  ptrdiff_t length = list_length (list);
  if (length < 2)
    return inplace ? list : list1 (XCAR (list));
  else
    {
      Lisp_Object *result;
      USE_SAFE_ALLOCA;
      SAFE_ALLOCA_LISP (result, length);
      Lisp_Object tail = list;
      for (ptrdiff_t i = 0; i < length; i++)
	{
	  result[i] = Fcar (tail);
	  tail = XCDR (tail);
	}
      tim_sort (predicate, keyfunc, result, length, reverse);

      if (inplace)
	{
	  /* Copy sorted vector contents back onto the original list.  */
	  ptrdiff_t i = 0;
	  tail = list;
	  while (CONSP (tail))
	    {
	      XSETCAR (tail, result[i]);
	      tail = XCDR (tail);
	      i++;
	    }
	}
      else
	{
	  /* Create a new list for the sorted vector contents.  */
	  list = Qnil;
	  for (ptrdiff_t i = length - 1; i >= 0; i--)
	    list = Fcons (result[i], list);
	}
      SAFE_FREE ();
      return list;
    }
}

/* Stably sort VECTOR in-place ordered by PREDICATE and KEYFUNC,
   optionally reversed.  */
static Lisp_Object
sort_vector (Lisp_Object vector, Lisp_Object predicate, Lisp_Object keyfunc,
	     bool reverse)
{
  ptrdiff_t length = ASIZE (vector);
  if (length >= 2)
    tim_sort (predicate, keyfunc, XVECTOR (vector)->contents, length, reverse);
  return vector;
}

DEFUN ("sort", Fsort, Ssort, 1, MANY, 0,
       doc: /* Sort SEQ, stably, and return the sorted sequence.
SEQ should be a list or vector.
Optional arguments are specified as keyword/argument pairs.  The following
arguments are defined:

:key FUNC -- FUNC is a function that takes a single element from SEQ and
  returns the key value to be used in comparison.  If absent or nil,
  `identity' is used.

:lessp FUNC -- FUNC is a function that takes two arguments and returns
  non-nil if the first element should come before the second.
  If absent or nil, `value<' is used.

:reverse BOOL -- if BOOL is non-nil, the sorting order implied by FUNC is
  reversed.  This does not affect stability: equal elements still retain
  their order in the input sequence.

:in-place BOOL -- if BOOL is non-nil, SEQ is sorted in-place and returned.
  Otherwise, a sorted copy of SEQ is returned and SEQ remains unmodified;
  this is the default.

For compatibility, the calling convention (sort SEQ LESSP) can also be used;
in this case, sorting is always done in-place.

usage: (sort SEQ &key KEY LESSP REVERSE IN-PLACE)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  Lisp_Object seq = args[0];
  Lisp_Object key = Qnil;
  Lisp_Object lessp = Qnil;
  bool inplace = false;
  bool reverse = false;
  if (nargs == 2)
    {
      /* old-style invocation without keywords */
      lessp = args[1];
      inplace = true;
    }
  else if ((nargs & 1) == 0)
    error ("Invalid argument list");
  else
    for (ptrdiff_t i = 1; i < nargs - 1; i += 2)
      {
	if (EQ (args[i], QCkey))
	  key = args[i + 1];
	else if (EQ (args[i], QClessp))
	  lessp = args[i + 1];
	else if (EQ (args[i], QCin_place))
	  inplace = !NILP (args[i + 1]);
	else if (EQ (args[i], QCreverse))
	  reverse = !NILP (args[i + 1]);
	else
	  signal_error ("Invalid keyword argument", args[i]);
      }

  if (CONSP (seq))
    return sort_list (seq, lessp, key, reverse, inplace);
  else if (NILP (seq))
    return seq;
  else if (VECTORP (seq))
    return sort_vector (inplace ? seq : Fcopy_sequence (seq),
			lessp, key, reverse);
  else
    wrong_type_argument (Qlist_or_vector_p, seq);
}

Lisp_Object
merge (Lisp_Object org_l1, Lisp_Object org_l2, Lisp_Object pred)
{
  Lisp_Object l1 = org_l1;
  Lisp_Object l2 = org_l2;
  Lisp_Object tail = Qnil;
  Lisp_Object value = Qnil;

  while (1)
    {
      if (NILP (l1))
	{
	  if (NILP (tail))
	    return l2;
	  Fsetcdr (tail, l2);
	  return value;
	}
      if (NILP (l2))
	{
	  if (NILP (tail))
	    return l1;
	  Fsetcdr (tail, l1);
	  return value;
	}

      Lisp_Object tem;
      if (!NILP (calln (pred, Fcar (l1), Fcar (l2))))
	{
	  tem = l1;
	  l1 = Fcdr (l1);
	  org_l1 = l1;
	}
      else
	{
	  tem = l2;
	  l2 = Fcdr (l2);
	  org_l2 = l2;
	}
      if (NILP (tail))
	value = tem;
      else
	Fsetcdr (tail, tem);
      tail = tem;
    }
}

Lisp_Object
merge_c (Lisp_Object org_l1, Lisp_Object org_l2, bool (*less) (Lisp_Object, Lisp_Object))
{
  Lisp_Object l1 = org_l1;
  Lisp_Object l2 = org_l2;
  Lisp_Object tail = Qnil;
  Lisp_Object value = Qnil;

  while (1)
    {
      if (NILP (l1))
	{
	  if (NILP (tail))
	    return l2;
	  Fsetcdr (tail, l2);
	  return value;
	}
      if (NILP (l2))
	{
	  if (NILP (tail))
	    return l1;
	  Fsetcdr (tail, l1);
	  return value;
	}

      Lisp_Object tem;
      if (less (Fcar (l1), Fcar (l2)))
	{
	  tem = l1;
	  l1 = Fcdr (l1);
	  org_l1 = l1;
	}
      else
	{
	  tem = l2;
	  l2 = Fcdr (l2);
	  org_l2 = l2;
	}
      if (NILP (tail))
	value = tem;
      else
	Fsetcdr (tail, tem);
      tail = tem;
    }
}


/* This does not check for quits.  That is safe since it must terminate.  */

DEFUN ("plist-get", Fplist_get, Splist_get, 2, 3, 0,
       doc: /* Extract a value from a property list.
PLIST is a property list, which is a list of the form
\(PROP1 VALUE1 PROP2 VALUE2...).

This function returns the value corresponding to the given PROP, or
nil if PROP is not one of the properties on the list.  The comparison
with PROP is done using PREDICATE, which defaults to `eq'.

This function doesn't signal an error if PLIST is invalid.  */)
  (Lisp_Object plist, Lisp_Object prop, Lisp_Object predicate)
{
  if (NILP (predicate))
    return plist_get (plist, prop);

  Lisp_Object tail = plist;
  FOR_EACH_TAIL_SAFE (tail)
    {
      if (! CONSP (XCDR (tail)))
	break;
      if (!NILP (calln (predicate, XCAR (tail), prop)))
	return XCAR (XCDR (tail));
      tail = XCDR (tail);
    }

  return Qnil;
}

/* Faster version of Fplist_get that works with EQ only.  */
Lisp_Object
plist_get (Lisp_Object plist, Lisp_Object prop)
{
  Lisp_Object tail = plist;
  FOR_EACH_TAIL_SAFE (tail)
    {
      if (! CONSP (XCDR (tail)))
	break;
      if (EQ (XCAR (tail), prop))
	return XCAR (XCDR (tail));
      tail = XCDR (tail);
    }
  return Qnil;
}

DEFUN ("get", Fget, Sget, 2, 2, 0,
       doc: /* Return the value of SYMBOL's PROPNAME property.
This is the last value stored with `(put SYMBOL PROPNAME VALUE)'.  */)
  (Lisp_Object symbol, Lisp_Object propname)
{
  CHECK_SYMBOL (symbol);
  Lisp_Object propval = plist_get (CDR (Fassq (symbol,
					       Voverriding_plist_environment)),
				   propname);
  if (!NILP (propval))
    return propval;
  return plist_get (XSYMBOL (symbol)->u.s.plist, propname);
}

DEFUN ("plist-put", Fplist_put, Splist_put, 3, 4, 0,
       doc: /* Change value in PLIST of PROP to VAL.
PLIST is a property list, which is a list of the form
\(PROP1 VALUE1 PROP2 VALUE2 ...).

The comparison with PROP is done using PREDICATE, which defaults to `eq'.

If PROP is already a property on the list, its value is set to VAL,
otherwise the new PROP VAL pair is added.  The new plist is returned;
use `(setq x (plist-put x prop val))' to be sure to use the new value.
The PLIST is modified by side effects.  */)
  (Lisp_Object plist, Lisp_Object prop, Lisp_Object val, Lisp_Object predicate)
{
  if (NILP (predicate))
    return plist_put (plist, prop, val);
  Lisp_Object prev = Qnil, tail = plist;
  FOR_EACH_TAIL (tail)
    {
      if (! CONSP (XCDR (tail)))
	break;

      if (!NILP (calln (predicate, XCAR (tail), prop)))
	{
	  Fsetcar (XCDR (tail), val);
	  return plist;
	}

      prev = tail;
      tail = XCDR (tail);
    }
  CHECK_TYPE (NILP (tail), Qplistp, plist);
  Lisp_Object newcell
    = Fcons (prop, Fcons (val, NILP (prev) ? plist : XCDR (XCDR (prev))));
  if (NILP (prev))
    return newcell;
  Fsetcdr (XCDR (prev), newcell);
  return plist;
}

/* Faster version of Fplist_put that works with EQ only.  */
Lisp_Object
plist_put (Lisp_Object plist, Lisp_Object prop, Lisp_Object val)
{
  Lisp_Object prev = Qnil, tail = plist;
  FOR_EACH_TAIL (tail)
    {
      if (! CONSP (XCDR (tail)))
	break;

      if (EQ (XCAR (tail), prop))
	{
	  Fsetcar (XCDR (tail), val);
	  return plist;
	}

      prev = tail;
      tail = XCDR (tail);
    }
  CHECK_TYPE (NILP (tail), Qplistp, plist);
  Lisp_Object newcell
    = Fcons (prop, Fcons (val, NILP (prev) ? plist : XCDR (XCDR (prev))));
  if (NILP (prev))
    return newcell;
  Fsetcdr (XCDR (prev), newcell);
  return plist;
}

DEFUN ("put", Fput, Sput, 3, 3, 0,
       doc: /* Store SYMBOL's PROPNAME property with value VALUE.
It can be retrieved with `(get SYMBOL PROPNAME)'.  */)
  (Lisp_Object symbol, Lisp_Object propname, Lisp_Object value)
{
  CHECK_SYMBOL (symbol);
  set_symbol_plist
    (symbol, plist_put (XSYMBOL (symbol)->u.s.plist, propname, value));
  return value;
}

DEFUN ("plist-member", Fplist_member, Splist_member, 2, 3, 0,
       doc: /* Return non-nil if PLIST has the property PROP.
PLIST is a property list, which is a list of the form
\(PROP1 VALUE1 PROP2 VALUE2 ...).

The comparison with PROP is done using PREDICATE, which defaults to
`eq'.

Unlike `plist-get', this allows you to distinguish between a missing
property and a property with the value nil.
The value is actually the tail of PLIST whose car is PROP.  */)
  (Lisp_Object plist, Lisp_Object prop, Lisp_Object predicate)
{
  if (NILP (predicate))
    return plist_member (plist, prop);
  Lisp_Object tail = plist;
  FOR_EACH_TAIL (tail)
    {
      if (!NILP (calln (predicate, XCAR (tail), prop)))
	return tail;
      tail = XCDR (tail);
      if (! CONSP (tail))
	break;
    }
  CHECK_TYPE (NILP (tail), Qplistp, plist);
  return Qnil;
}

/* Faster version of Fplist_member that works with EQ only.  */
Lisp_Object
plist_member (Lisp_Object plist, Lisp_Object prop)
{
  Lisp_Object tail = plist;
  FOR_EACH_TAIL (tail)
    {
      if (EQ (XCAR (tail), prop))
	return tail;
      tail = XCDR (tail);
      if (! CONSP (tail))
	break;
    }
  CHECK_TYPE (NILP (tail), Qplistp, plist);
  return Qnil;
}

DEFUN ("eql", Feql, Seql, 2, 2, 0,
       doc: /* Return t if the two args are `eq' or are indistinguishable numbers.
Integers with the same value are `eql'.
Floating-point values with the same sign, exponent and fraction are `eql'.
This differs from numeric comparison: (eql 0.0 -0.0) returns nil and
\(eql 0.0e+NaN 0.0e+NaN) returns t, whereas `=' does the opposite.  */)
  (Lisp_Object obj1, Lisp_Object obj2)
{
  if (FLOATP (obj1))
    return FLOATP (obj2) && same_float (obj1, obj2) ? Qt : Qnil;
  else if (BIGNUMP (obj1))
    return ((BIGNUMP (obj2)
	     && mpz_cmp (*xbignum_val (obj1), *xbignum_val (obj2)) == 0)
	    ? Qt : Qnil);
  else
    return EQ (obj1, obj2) ? Qt : Qnil;
}

DEFUN ("equal", Fequal, Sequal, 2, 2, 0,
       doc: /* Return t if two Lisp objects have similar structure and contents.
They must have the same data type.
Conses are compared by comparing the cars and the cdrs.
Vectors and strings are compared element by element.
Numbers are compared via `eql', so integers do not equal floats.
\(Use `=' if you want integers and floats to be able to be equal.)
Symbols must match exactly.  */)
  (Lisp_Object o1, Lisp_Object o2)
{
  return internal_equal (o1, o2, EQUAL_PLAIN, 0, Qnil) ? Qt : Qnil;
}

DEFUN ("equal-including-properties", Fequal_including_properties, Sequal_including_properties, 2, 2, 0,
       doc: /* Return t if two Lisp objects have similar structure and contents.
This is like `equal' except that it compares the text properties
of strings.  (`equal' ignores text properties.)  */)
  (Lisp_Object o1, Lisp_Object o2)
{
  return (internal_equal (o1, o2, EQUAL_INCLUDING_PROPERTIES, 0, Qnil)
	  ? Qt : Qnil);
}

/* Return true if O1 and O2 are equal.  Do not quit or check for cycles.
   Use this only on arguments that are cycle-free and not too large and
   are not window configurations.  */

bool
equal_no_quit (Lisp_Object o1, Lisp_Object o2)
{
  return internal_equal (o1, o2, EQUAL_NO_QUIT, 0, Qnil);
}

static ptrdiff_t hash_find_with_hash (struct Lisp_Hash_Table *h,
				      Lisp_Object key, hash_hash_t hash);


/* Return true if O1 and O2 are equal.  EQUAL_KIND specifies what kind
   of equality test to use: if it is EQUAL_NO_QUIT, do not check for
   cycles or large arguments or quits; if EQUAL_PLAIN, do ordinary
   Lisp equality; and if EQUAL_INCLUDING_PROPERTIES, do
   equal-including-properties.

   If DEPTH is the current depth of recursion; signal an error if it
   gets too deep.  HT is a hash table used to detect cycles; if nil,
   it has not been allocated yet.  But ignore the last two arguments
   if EQUAL_KIND == EQUAL_NO_QUIT.  */

static bool
internal_equal_1 (Lisp_Object o1, Lisp_Object o2, enum equal_kind equal_kind,
		  int depth, Lisp_Object *ht)
{
 tail_recurse:
  if (depth > 10)
    {
      eassert (equal_kind != EQUAL_NO_QUIT);
      if (depth > 200)
	error ("Stack overflow in equal");
      if (NILP (*ht))
	*ht = CALLN (Fmake_hash_table, QCtest, Qeq);
      switch (XTYPE (o1))
	{
	case Lisp_Cons: case Lisp_Vectorlike:
	  {
	    struct Lisp_Hash_Table *h = XHASH_TABLE (*ht);
	    hash_hash_t hash = hash_from_key (h, o1);
	    ptrdiff_t i = hash_find_with_hash (h, o1, hash);
	    if (i >= 0)
	      { /* `o1' was seen already.  */
		Lisp_Object o2s = HASH_VALUE (h, i);
		if (!NILP (Fmemq (o2, o2s)))
		  return true;
		else
		  set_hash_value_slot (h, i, Fcons (o2, o2s));
	      }
	    else
	      hash_put (h, o1, Fcons (o2, Qnil), hash);
	  }
	default: ;
	}
    }

  /* A symbol with position compares the contained symbol, and is
     `equal' to the corresponding ordinary symbol.  */
  o1 = maybe_remove_pos_from_symbol (o1);
  o2 = maybe_remove_pos_from_symbol (o2);

  if (BASE_EQ (o1, o2))
    return true;
  if (XTYPE (o1) != XTYPE (o2))
    return false;

  switch (XTYPE (o1))
    {
    case Lisp_Float:
      return same_float (o1, o2);

    case Lisp_Cons:
      if (equal_kind == EQUAL_NO_QUIT)
	for (; CONSP (o1); o1 = XCDR (o1))
	  {
	    if (! CONSP (o2))
	      return false;
	    if (! equal_no_quit (XCAR (o1), XCAR (o2)))
	      return false;
	    o2 = XCDR (o2);
	    if (EQ (XCDR (o1), o2))
	      return true;
	  }
      else
	FOR_EACH_TAIL (o1)
	  {
	    if (! CONSP (o2))
	      return false;
	    if (! internal_equal_1 (XCAR (o1), XCAR (o2),
				    equal_kind, depth + 1, ht))
	      return false;
	    o2 = XCDR (o2);
	    if (EQ (XCDR (o1), o2))
	      return true;
	  }
      depth++;
      goto tail_recurse;

    case Lisp_Vectorlike:
      {
	ptrdiff_t size = ASIZE (o1);
	/* Pseudovectors have the type encoded in the size field, so this test
	   actually checks that the objects have the same type as well as the
	   same size.  */
	if (ASIZE (o2) != size)
	  return false;

	/* Compare bignums, overlays, markers, boolvectors, and
	   symbols with position specially, by comparing their values.  */
	if (BIGNUMP (o1))
	  return mpz_cmp (*xbignum_val (o1), *xbignum_val (o2)) == 0;
	if (OVERLAYP (o1))
	  {
	    if (OVERLAY_BUFFER (o1) != OVERLAY_BUFFER (o2)
		|| OVERLAY_START (o1) != OVERLAY_START (o2)
		|| OVERLAY_END (o1) != OVERLAY_END (o2))
	      return false;
	    o1 = XOVERLAY (o1)->plist;
	    o2 = XOVERLAY (o2)->plist;
	    depth++;
	    goto tail_recurse;
	  }
	if (MARKERP (o1))
	  {
	    return (XMARKER (o1)->buffer == XMARKER (o2)->buffer
		    && (XMARKER (o1)->buffer == 0
			|| XMARKER (o1)->bytepos == XMARKER (o2)->bytepos));
	  }
	if (BOOL_VECTOR_P (o1))
	  {
	    EMACS_INT size = bool_vector_size (o1);
	    return (size == bool_vector_size (o2)
		    && !memcmp (bool_vector_data (o1), bool_vector_data (o2),
			        bool_vector_bytes (size)));
	  }

#ifdef HAVE_TREE_SITTER
	if (TS_NODEP (o1))
	  return treesit_node_eq (o1, o2);
#endif
	if (SYMBOL_WITH_POS_P (o1))
	  {
	    eassert (!symbols_with_pos_enabled);
	    return (BASE_EQ (XSYMBOL_WITH_POS_SYM (o1),
			     XSYMBOL_WITH_POS_SYM (o2))
		    && BASE_EQ (XSYMBOL_WITH_POS_POS (o1),
				XSYMBOL_WITH_POS_POS (o2)));
	  }

	/* Aside from them, only true vectors, char-tables, compiled
	   functions, and fonts (font-spec, font-entity, font-object)
	   are sensible to compare, so eliminate the others now.  */
	if (size & PSEUDOVECTOR_FLAG)
	  {
	    if (((size & PVEC_TYPE_MASK) >> PSEUDOVECTOR_AREA_BITS)
		< PVEC_CLOSURE)
	      return false;
	    size &= PSEUDOVECTOR_SIZE_MASK;
	  }
	for (ptrdiff_t i = 0; i < size; i++)
	  {
	    Lisp_Object v1, v2;
	    v1 = AREF (o1, i);
	    v2 = AREF (o2, i);
	    if (!internal_equal_1 (v1, v2, equal_kind, depth + 1, ht))
	      return false;
	  }
	return true;
      }
      break;

    case Lisp_String:
      return (SCHARS (o1) == SCHARS (o2)
	      && SBYTES (o1) == SBYTES (o2)
	      && !memcmp (SDATA (o1), SDATA (o2), SBYTES (o1))
	      && (equal_kind != EQUAL_INCLUDING_PROPERTIES
	          || compare_string_intervals (o1, o2)));

    default:
      break;
    }

  return false;
}

static bool
internal_equal (Lisp_Object o1, Lisp_Object o2, enum equal_kind equal_kind,
		int depth, Lisp_Object ht)
{
  return internal_equal_1 (o1, o2, equal_kind, depth, &ht);
}

/* Return -1/0/1 for the </=/> lexicographic relation between bool-vectors.  */
static int
bool_vector_cmp (Lisp_Object a, Lisp_Object b)
{
  ptrdiff_t na = bool_vector_size (a);
  ptrdiff_t nb = bool_vector_size (b);
  /* Skip equal words.  */
  ptrdiff_t words_min = min (na, nb) / BITS_PER_BITS_WORD;
  bits_word *ad = bool_vector_data (a);
  bits_word *bd = bool_vector_data (b);
  ptrdiff_t i = 0;
  while (i < words_min && ad[i] == bd[i])
    i++;
  na -= i * BITS_PER_BITS_WORD;
  nb -= i * BITS_PER_BITS_WORD;
  eassume (na >= 0 && nb >= 0);
  if (nb == 0)
    return na != 0;
  if (na == 0)
    return -1;

  bits_word aw = bits_word_to_host_endian (ad[i]);
  bits_word bw = bits_word_to_host_endian (bd[i]);
  bits_word xw = aw ^ bw;
  if (xw == 0)
    return na < nb ? -1 : na > nb;

  bits_word d = xw & -xw;	/* Isolate first difference.  */
  eassume (d != 0);
  return (d & aw) ? 1 : -1;
}

/* Return -1 if a<b, 1 if a>b, 0 if a=b or if b is NaN (a must be a fixnum).  */
static inline int
fixnum_float_cmp (EMACS_INT a, double b)
{
  double fa = (double)a;
  if (fa == b)
    {
      /* This doesn't mean that a=b because the conversion may have rounded.
	 However, b must be an integer that fits in an EMACS_INT,
	 because |b| <= 2|a| and EMACS_INT has at least one bit more than
	 needed to represent any fixnum.
	 Thus we can compare in the integer domain instead.  */
      EMACS_INT ib = b;		/* lossless conversion */
      return a < ib ? -1 : a > ib;
    }
  else
    return fa < b ? -1 : fa > b;   /* return 0 if b is NaN */
}

/* Return -1, 0 or 1 to indicate whether a<b, a=b or a>b in the sense of value<.
   In particular 0 does not mean equality in the sense of Fequal, only
   that the arguments cannot be ordered yet they can be compared (same
   type).  */
static int
value_cmp (Lisp_Object a, Lisp_Object b, int maxdepth)
{
  if (maxdepth < 0)
    error ("Maximum depth exceeded in comparison");

 tail_recurse:
  /* Shortcut for a common case.  */
  if (BASE_EQ (a, b))
    return 0;

  switch (XTYPE (a))
    {
    case Lisp_Int0:
    case Lisp_Int1:
      {
	EMACS_INT ia = XFIXNUM (a);
	if (FIXNUMP (b))
	  return ia < XFIXNUM (b) ? -1 : 1;   /* we know that a != b */
	if (FLOATP (b))
	  return fixnum_float_cmp (ia, XFLOAT_DATA (b));
	if (BIGNUMP (b))
	  return -mpz_sgn (*xbignum_val (b));
      }
      goto type_mismatch;

    case Lisp_Symbol:
      if (BARE_SYMBOL_P (b))
	return string_cmp (XBARE_SYMBOL (a)->u.s.name,
			   XBARE_SYMBOL (b)->u.s.name);
      if (CONSP (b) && NILP (a))
	return -1;
      if (SYMBOLP (b))
	/* Slow-path branch when B is a symbol-with-pos.  */
	return string_cmp (XBARE_SYMBOL (a)->u.s.name, XSYMBOL (b)->u.s.name);
      goto type_mismatch;

    case Lisp_String:
      if (STRINGP (b))
	return string_cmp (a, b);
      goto type_mismatch;

    case Lisp_Cons:
      /* FIXME: Optimize for difference in the first element? */
      FOR_EACH_TAIL (b)
	{
	  int cmp = value_cmp (XCAR (a), XCAR (b), maxdepth - 1);
	  if (cmp != 0)
	    return cmp;
	  a = XCDR (a);
	  if (!CONSP (a))
	    {
	      b = XCDR (b);
	      goto tail_recurse;
	    }
	}
      if (NILP (b))
	return 1;
      goto type_mismatch;

    case Lisp_Vectorlike:
      if (VECTORLIKEP (b))
	{
	  enum pvec_type ta = PSEUDOVECTOR_TYPE (XVECTOR (a));
	  enum pvec_type tb = PSEUDOVECTOR_TYPE (XVECTOR (b));
	  if (ta == tb)
	    switch (ta)
	      {
	      case PVEC_NORMAL_VECTOR:
	      case PVEC_RECORD:
		{
		  ptrdiff_t len_a = ASIZE (a);
		  ptrdiff_t len_b = ASIZE (b);
		  if (ta == PVEC_RECORD)
		    {
		      len_a &= PSEUDOVECTOR_SIZE_MASK;
		      len_b &= PSEUDOVECTOR_SIZE_MASK;
		    }
		  ptrdiff_t len_min = min (len_a, len_b);
		  for (ptrdiff_t i = 0; i < len_min; i++)
		    {
		      int cmp = value_cmp (AREF (a, i), AREF (b, i),
					   maxdepth - 1);
		      if (cmp != 0)
			return cmp;
		    }
		  return len_a < len_b ? -1 : len_a > len_b;
		}

	      case PVEC_BOOL_VECTOR:
		return bool_vector_cmp (a, b);

	      case PVEC_MARKER:
		{
		  Lisp_Object buf_a = Fmarker_buffer (a);
		  Lisp_Object buf_b = Fmarker_buffer (b);
		  if (NILP (buf_a))
		    return NILP (buf_b) ? 0 : -1;
		  if (NILP (buf_b))
		    return 1;
		  int cmp = value_cmp (buf_a, buf_b, maxdepth - 1);
		  if (cmp != 0)
		    return cmp;
		  ptrdiff_t pa = XMARKER (a)->charpos;
		  ptrdiff_t pb = XMARKER (b)->charpos;
		  return pa < pb ? -1 : pa > pb;
		}

#ifdef subprocesses
	      case PVEC_PROCESS:
		a = Fprocess_name (a);
		b = Fprocess_name (b);
		goto tail_recurse;
#endif /* subprocesses */

	      case PVEC_BUFFER:
		{
		  /* Killed buffers lack names and sort before those alive.  */
		  Lisp_Object na = Fbuffer_name (a);
		  Lisp_Object nb = Fbuffer_name (b);
		  if (NILP (na))
		    return NILP (nb) ? 0 : -1;
		  if (NILP (nb))
		    return 1;
		  a = na;
		  b = nb;
		  goto tail_recurse;
		}

	      case PVEC_BIGNUM:
		return mpz_cmp (*xbignum_val (a), *xbignum_val (b));

	      case PVEC_SYMBOL_WITH_POS:
		/* Compare by name, enabled or not.  */
		a = XSYMBOL_WITH_POS_SYM (a);
		b = XSYMBOL_WITH_POS_SYM (b);
		goto tail_recurse;

	      default:
		/* Treat other types as unordered.  */
		return 0;
	      }
	}
      else if (BIGNUMP (a))
	return -value_cmp (b, a, maxdepth);
      else if (SYMBOL_WITH_POS_P (a) && symbols_with_pos_enabled)
	{
	  a = XSYMBOL_WITH_POS_SYM (a);
	  goto tail_recurse;
	}

      goto type_mismatch;

    case Lisp_Float:
      {
	double fa = XFLOAT_DATA (a);
	if (FLOATP (b))
	  return fa < XFLOAT_DATA (b) ? -1 : fa > XFLOAT_DATA (b);
	if (FIXNUMP (b))
	  return -fixnum_float_cmp (XFIXNUM (b), fa);
	if (BIGNUMP (b))
	  {
	    if (isnan (fa))
	      return 0;
	    return -mpz_cmp_d (*xbignum_val (b), fa);
	  }
      }
      goto type_mismatch;

    default:
      eassume (0);
    }
 type_mismatch:
  xsignal2 (Qtype_mismatch, a, b);
}

DEFUN ("value<", Fvaluelt, Svaluelt, 2, 2, 0,
       doc: /* Return non-nil if A precedes B in standard value order.
A and B must have the same basic type.
Numbers are compared with `<'.
Strings and symbols are compared with `string-lessp'.
Lists, vectors, bool-vectors and records are compared lexicographically.
Markers are compared lexicographically by buffer and position.
Buffers and processes are compared by name.
Other types are considered unordered and the return value will be `nil'.  */)
  (Lisp_Object a, Lisp_Object b)
{
  int maxdepth = 200;		  /* FIXME: arbitrary value */
  return value_cmp (a, b, maxdepth) < 0 ? Qt : Qnil;
}



DEFUN ("fillarray", Ffillarray, Sfillarray, 2, 2, 0,
       doc: /* Store each element of ARRAY with ITEM.
ARRAY is a vector, string, char-table, or bool-vector.  */)
  (Lisp_Object array, Lisp_Object item)
{
  register ptrdiff_t size, idx;

  if (VECTORP (array))
    for (idx = 0, size = ASIZE (array); idx < size; idx++)
      ASET (array, idx, item);
  else if (CHAR_TABLE_P (array))
    {
      int i;

      for (i = 0; i < (1 << CHARTAB_SIZE_BITS_0); i++)
	set_char_table_contents (array, i, item);
      set_char_table_defalt (array, item);
    }
  else if (STRINGP (array))
    {
      unsigned char *p = SDATA (array);
      CHECK_CHARACTER (item);
      int charval = XFIXNAT (item);
      size = SCHARS (array);
      if (size != 0)
	{
	  unsigned char str[MAX_MULTIBYTE_LENGTH];
	  int len;
	  if (STRING_MULTIBYTE (array))
	    len = CHAR_STRING (charval, str);
	  else
	    {
	      str[0] = charval;
	      len = 1;
	    }

	  ptrdiff_t size_byte = SBYTES (array);
	  if (len == 1 && size == size_byte)
	    memset (p, str[0], size);
	  else
	    {
	      ptrdiff_t product;
	      if (ckd_mul (&product, size, len) || product != size_byte)
		error ("Attempt to change byte length of a string");
	      for (idx = 0; idx < size_byte; idx++)
		*p++ = str[idx % len];
	    }
	}
    }
  else if (BOOL_VECTOR_P (array))
    return bool_vector_fill (array, item);
  else
    wrong_type_argument (Qarrayp, array);
  return array;
}

DEFUN ("clear-string", Fclear_string, Sclear_string, 1, 1, 0,
       doc: /* Clear the contents of STRING.
This makes STRING unibyte, clears its contents to null characters, and
removes all text properties.  This may change its length.  */)
  (Lisp_Object string)
{
  CHECK_STRING (string);
  ptrdiff_t len = SBYTES (string);
  Fset_text_properties (make_fixnum (0), make_fixnum (SCHARS (string)),
			Qnil, string);
  if (len != 0 || STRING_MULTIBYTE (string))
    {
      memset (SDATA (string), 0, len);
      STRING_SET_CHARS (string, len);
      STRING_SET_UNIBYTE (string);
    }
  return Qnil;
}

Lisp_Object
nconc2 (Lisp_Object s1, Lisp_Object s2)
{
  return CALLN (Fnconc, s1, s2);
}

DEFUN ("nconc", Fnconc, Snconc, 0, MANY, 0,
       doc: /* Concatenate any number of lists by altering them.
Only the last argument is not altered, and need not be a list.
usage: (nconc &rest LISTS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  Lisp_Object val = Qnil;

  for (ptrdiff_t argnum = 0; argnum < nargs; argnum++)
    {
      Lisp_Object tem = args[argnum];
      if (NILP (tem)) continue;

      if (NILP (val))
	val = tem;

      if (argnum + 1 == nargs) break;

      CHECK_CONS (tem);

      Lisp_Object tail UNINIT;
      FOR_EACH_TAIL (tem)
	tail = tem;

      tem = args[argnum + 1];
      Fsetcdr (tail, tem);
      if (NILP (tem))
	args[argnum + 1] = tail;
    }

  return val;
}

/* This is the guts of all mapping functions.
   Apply FN to each element of SEQ, one by one, storing the results
   into elements of VALS, a C vector of Lisp_Objects.  LENI is the
   length of VALS, which should also be the length of SEQ.  Return the
   number of results; although this is normally LENI, it can be less
   if SEQ is made shorter as a side effect of FN.  */

static EMACS_INT
mapcar1 (EMACS_INT leni, Lisp_Object *vals, Lisp_Object fn, Lisp_Object seq)
{
  if (NILP (seq))
    return 0;
  else if (CONSP (seq))
    {
      Lisp_Object tail = seq;
      for (ptrdiff_t i = 0; i < leni; i++)
	{
	  if (! CONSP (tail))
	    return i;
	  Lisp_Object dummy = calln (fn, XCAR (tail));
	  if (vals)
	    vals[i] = dummy;
	  tail = XCDR (tail);
	}
    }
  else if (VECTORP (seq) || CLOSUREP (seq))
    {
      for (ptrdiff_t i = 0; i < leni; i++)
	{
	  Lisp_Object dummy = calln (fn, AREF (seq, i));
	  if (vals)
	    vals[i] = dummy;
	}
    }
  else if (STRINGP (seq))
    {
      ptrdiff_t i_byte = 0;

      for (ptrdiff_t i = 0; i < leni;)
	{
	  ptrdiff_t i_before = i;
	  int c = fetch_string_char_advance (seq, &i, &i_byte);
	  Lisp_Object dummy = calln (fn, make_fixnum (c));
	  if (vals)
	    vals[i_before] = dummy;
	}
    }
  else if (BOOL_VECTOR_P (seq))
    {
      for (EMACS_INT i = 0; i < leni; i++)
	{
	  Lisp_Object dummy = calln (fn, bool_vector_ref (seq, i));
	  if (vals)
	    vals[i] = dummy;
	}
    }
  else
    wrong_type_argument (Qsequencep, seq);

  return leni;
}

DEFUN ("mapconcat", Fmapconcat, Smapconcat, 2, 3, 0,
       doc: /* Apply FUNCTION to each element of SEQUENCE, and concat the results as strings.
In between each pair of results, stick in SEPARATOR.  Thus, " " as
  SEPARATOR results in spaces between the values returned by FUNCTION.

SEQUENCE may be a list, a vector, a bool-vector, or a string.

Optional argument SEPARATOR must be a string, a vector, or a list of
characters; nil stands for the empty string.

FUNCTION must be a function of one argument, and must return a value
  that is a sequence of characters: either a string, or a vector or
  list of numbers that are valid character codepoints; nil is treated
  as an empty string.  */)
  (Lisp_Object function, Lisp_Object sequence, Lisp_Object separator)
{
  USE_SAFE_ALLOCA;
  EMACS_INT leni = XFIXNAT (Flength (sequence));
  if (CHAR_TABLE_P (sequence))
    wrong_type_argument (Qlistp, sequence);
  EMACS_INT args_alloc = 2 * leni - 1;
  if (args_alloc < 0)
    return empty_unibyte_string;
  Lisp_Object *args;
  SAFE_ALLOCA_LISP (args, args_alloc);
  if (BASE_EQ (function, Qidentity))
    {
      /* Fast path when no function call is necessary.  */
      if (CONSP (sequence))
	{
	  Lisp_Object src = sequence;
	  Lisp_Object *dst = args;
	  do
	    {
	      *dst++ = XCAR (src);
	      src = XCDR (src);
	    }
	  while (!NILP (src));
	  goto concat;
	}
      else if (VECTORP (sequence))
	{
	  memcpy (args, XVECTOR (sequence)->contents, leni * sizeof *args);
	  goto concat;
	}
    }
  ptrdiff_t nmapped = mapcar1 (leni, args, function, sequence);
  eassert (nmapped == leni);

 concat: ;
  ptrdiff_t nargs = args_alloc;
  if (NILP (separator) || (STRINGP (separator) && SCHARS (separator) == 0))
    nargs = leni;
  else
    {
      for (ptrdiff_t i = leni - 1; i > 0; i--)
        args[i + i] = args[i];

      for (ptrdiff_t i = 1; i < nargs; i += 2)
        args[i] = separator;
    }

  Lisp_Object ret = Fconcat (nargs, args);
  SAFE_FREE ();
  return ret;
}

DEFUN ("mapcar", Fmapcar, Smapcar, 2, 2, 0,
       doc: /* Apply FUNCTION to each element of SEQUENCE, and make a list of the results.
The result is a list just as long as SEQUENCE.
SEQUENCE may be a list, a vector, a bool-vector, or a string.  */)
  (Lisp_Object function, Lisp_Object sequence)
{
  USE_SAFE_ALLOCA;
  EMACS_INT leni = XFIXNAT (Flength (sequence));
  if (CHAR_TABLE_P (sequence))
    wrong_type_argument (Qlistp, sequence);
  Lisp_Object *args;
  SAFE_ALLOCA_LISP (args, leni);
  ptrdiff_t nmapped = mapcar1 (leni, args, function, sequence);
  Lisp_Object ret = Flist (nmapped, args);
  SAFE_FREE ();
  return ret;
}

DEFUN ("mapc", Fmapc, Smapc, 2, 2, 0,
       doc: /* Apply FUNCTION to each element of SEQUENCE for side effects only.
Unlike `mapcar', don't accumulate the results.  Return SEQUENCE.
SEQUENCE may be a list, a vector, a bool-vector, or a string.  */)
  (Lisp_Object function, Lisp_Object sequence)
{
  register EMACS_INT leni;

  leni = XFIXNAT (Flength (sequence));
  if (CHAR_TABLE_P (sequence))
    wrong_type_argument (Qlistp, sequence);
  mapcar1 (leni, 0, function, sequence);

  return sequence;
}

DEFUN ("mapcan", Fmapcan, Smapcan, 2, 2, 0,
       doc: /* Apply FUNCTION to each element of SEQUENCE, and concatenate
the results by altering them (using `nconc').
SEQUENCE may be a list, a vector, a bool-vector, or a string. */)
     (Lisp_Object function, Lisp_Object sequence)
{
  USE_SAFE_ALLOCA;
  EMACS_INT leni = XFIXNAT (Flength (sequence));
  if (CHAR_TABLE_P (sequence))
    wrong_type_argument (Qlistp, sequence);
  Lisp_Object *args;
  SAFE_ALLOCA_LISP (args, leni);
  ptrdiff_t nmapped = mapcar1 (leni, args, function, sequence);
  Lisp_Object ret = Fnconc (nmapped, args);
  SAFE_FREE ();
  return ret;
}

/* This is how C code calls `yes-or-no-p' and allows the user
   to redefine it.  */

Lisp_Object
do_yes_or_no_p (Lisp_Object prompt)
{
  return calln (Qyes_or_no_p, prompt);
}

DEFUN ("yes-or-no-p", Fyes_or_no_p, Syes_or_no_p, 1, 1, 0,
       doc: /* Ask user a yes-or-no question.
Return t if answer is yes, and nil if the answer is no.

PROMPT is the string to display to ask the question; `yes-or-no-p'
appends `yes-or-no-prompt' (default \"(yes or no) \") to it.  If
PROMPT is a non-empty string, and it ends with a non-space character,
a space character will be appended to it.

The user must confirm the answer with RET, and can edit it until it
has been confirmed.

If the `use-short-answers' variable is non-nil, instead of asking for
\"yes\" or \"no\", this function will ask for \"y\" or \"n\" (and
ignore the value of `yes-or-no-prompt').

If dialog boxes are supported, this function will use a dialog box
if `use-dialog-box' is non-nil and the last input event was produced
by a mouse, or by some window-system gesture, or via a menu.  */)
  (Lisp_Object prompt)
{
  Lisp_Object ans, val;

  CHECK_STRING (prompt);

  if (!NILP (last_input_event)
      && (CONSP (last_nonmenu_event)
	  || (NILP (last_nonmenu_event) && CONSP (last_input_event))
	  || (val = find_symbol_value (Qfrom__tty_menu_p),
	      (!NILP (val) && !BASE_EQ (val, Qunbound))))
      && use_dialog_box)
    {
      Lisp_Object pane, menu, obj;
      redisplay_preserve_echo_area (4);
      pane = list2 (Fcons (build_string ("Yes"), Qt),
		    Fcons (build_string ("No"), Qnil));
      menu = Fcons (prompt, pane);
      obj = Fx_popup_dialog (Qt, menu, Qnil);
      return obj;
    }

  if (use_short_answers)
    return calln (Qy_or_n_p, prompt);

  ptrdiff_t promptlen = SCHARS (prompt);
  bool prompt_ends_in_nonspace
    = (0 < promptlen
       && !blankp (XFIXNAT (Faref (prompt, make_fixnum (promptlen - 1)))));
  AUTO_STRING (space_string, " ");
  prompt = CALLN (Fconcat, prompt,
		  prompt_ends_in_nonspace ? space_string : empty_unibyte_string,
		  Vyes_or_no_prompt);

  specpdl_ref count = SPECPDL_INDEX ();
  specbind (Qenable_recursive_minibuffers, Qt);
  /* Preserve the actual command that eventually called `yes-or-no-p'
     (otherwise `repeat' will be repeating `exit-minibuffer').  */
  specbind (Qreal_this_command, Vreal_this_command);

  while (1)
    {
      ans = Fdowncase (Fread_from_minibuffer (prompt, Qnil, Qnil, Qnil,
					      Qyes_or_no_p_history, Qnil,
					      Qnil));
      if (SCHARS (ans) == 3 && !strcmp (SSDATA (ans), "yes"))
	return unbind_to (count, Qt);
      if (SCHARS (ans) == 2 && !strcmp (SSDATA (ans), "no"))
	return unbind_to (count, Qnil);

      Fding (Qnil);
      Fdiscard_input ();
      message1 ("Please answer yes or no.");
      Fsleep_for (make_fixnum (2), Qnil);
    }
}

DEFUN ("load-average", Fload_average, Sload_average, 0, 1, 0,
       doc: /* Return list of 1 minute, 5 minute and 15 minute load averages.

Each of the three load averages is multiplied by 100, then converted
to integer.

When USE-FLOATS is non-nil, floats will be used instead of integers.
These floats are not multiplied by 100.

If the 5-minute or 15-minute load averages are not available, return a
shortened list, containing only those averages which are available.

An error is thrown if the load average can't be obtained.  In some
cases making it work would require Emacs being installed setuid or
setgid so that it can read kernel information, and that usually isn't
advisable.  */)
  (Lisp_Object use_floats)
{
  double load_ave[3];
  int loads = getloadavg (load_ave, 3);
  Lisp_Object ret = Qnil;

  if (loads < 0)
    error ("load-average not implemented for this operating system");

  while (loads-- > 0)
    {
      Lisp_Object load = (NILP (use_floats)
			  ? double_to_integer (100.0 * load_ave[loads])
			  : make_float (load_ave[loads]));
      ret = Fcons (load, ret);
    }

  return ret;
}

DEFUN ("featurep", Ffeaturep, Sfeaturep, 1, 2, 0,
       doc: /* Return t if FEATURE is present in this Emacs.

Use this to conditionalize execution of lisp code based on the
presence or absence of Emacs or environment extensions.
Use `provide' to declare that a feature is available.  This function
looks at the value of the variable `features'.  The optional argument
SUBFEATURE can be used to check a specific subfeature of FEATURE.  */)
  (Lisp_Object feature, Lisp_Object subfeature)
{
  register Lisp_Object tem;
  CHECK_SYMBOL (feature);
  tem = Fmemq (feature, Vfeatures);
  if (!NILP (tem) && !NILP (subfeature))
    tem = Fmember (subfeature, Fget (feature, Qsubfeatures));
  return (NILP (tem)) ? Qnil : Qt;
}

DEFUN ("provide", Fprovide, Sprovide, 1, 2, 0,
       doc: /* Announce that FEATURE is a feature of the current Emacs.
The optional argument SUBFEATURES should be a list of symbols listing
particular subfeatures supported in this version of FEATURE.  */)
  (Lisp_Object feature, Lisp_Object subfeatures)
{
  register Lisp_Object tem;
  CHECK_SYMBOL (feature);
  CHECK_LIST (subfeatures);
  if (!NILP (Vautoload_queue))
    Vautoload_queue = Fcons (Fcons (make_fixnum (0), Vfeatures),
			     Vautoload_queue);
  tem = Fmemq (feature, Vfeatures);
  if (NILP (tem))
    Vfeatures = Fcons (feature, Vfeatures);
  if (!NILP (subfeatures))
    Fput (feature, Qsubfeatures, subfeatures);
  LOADHIST_ATTACH (Fcons (Qprovide, feature));

  /* Run any load-hooks for this file.  */
  tem = Fassq (feature, Vafter_load_alist);
  if (CONSP (tem))
    Fmapc (Qfuncall, XCDR (tem));

  return feature;
}

/* `require' and its subroutines.  */

/* List of features currently being require'd, innermost first.  */

static Lisp_Object require_nesting_list;

static void
require_unwind (Lisp_Object old_value)
{
  require_nesting_list = old_value;
}

DEFUN ("require", Frequire, Srequire, 1, 3, 0,
       doc: /* If FEATURE is not already loaded, load it from FILENAME.
If FEATURE is not a member of the list `features', then the feature was
not yet loaded; so load it from file FILENAME.

If FILENAME is omitted, the printname of FEATURE is used as the file
name, and `load' is called to try to load the file by that name, after
appending the suffix `.elc', `.el', or the system-dependent suffix for
dynamic module files, in that order; but the function will not try to
load the file without any suffix.  See `get-load-suffixes' for the
complete list of suffixes.

To find the file, this function searches the directories in `load-path'.

If the optional third argument NOERROR is non-nil, then, if
the file is not found, the function returns nil instead of signaling
an error.  Normally the return value is FEATURE.

The normal messages issued by `load' at start and end of loading
FILENAME are suppressed.  */)
  (Lisp_Object feature, Lisp_Object filename, Lisp_Object noerror)
{
  Lisp_Object tem;
  bool from_file = load_in_progress;

  CHECK_SYMBOL (feature);

  /* Record the presence of `require' in this file
     even if the feature specified is already loaded.
     But not more than once in any file,
     and not when we aren't loading or reading from a file.  */
  if (!from_file)
    {
      Lisp_Object tail = Vcurrent_load_list;
      FOR_EACH_TAIL_SAFE (tail)
	if (NILP (XCDR (tail)) && STRINGP (XCAR (tail)))
	  from_file = true;
    }

  if (from_file)
    {
      tem = Fcons (Qrequire, feature);
      if (NILP (Fmember (tem, Vcurrent_load_list)))
	LOADHIST_ATTACH (tem);
    }
  tem = Fmemq (feature, Vfeatures);

  if (NILP (tem))
    {
      specpdl_ref count = SPECPDL_INDEX ();
      int nesting = 0;

      /* This is to make sure that loadup.el gives a clear picture
	 of what files are preloaded and when.  */
      if (will_dump_p () && !will_bootstrap_p ())
	{
	  /* Avoid landing here recursively while outputting the
	     backtrace from the error.  */
	  gflags.will_dump = false;
	  error ("(require %s) while preparing to dump",
		 SDATA (SYMBOL_NAME (feature)));
	}

      /* A certain amount of recursive `require' is legitimate,
	 but if we require the same feature recursively 3 times,
	 signal an error.  */
      tem = require_nesting_list;
      while (! NILP (tem))
	{
	  if (! NILP (Fequal (feature, XCAR (tem))))
	    nesting++;
	  tem = XCDR (tem);
	}
      if (nesting > 3)
	error ("Recursive `require' for feature `%s'",
	       SDATA (SYMBOL_NAME (feature)));

      /* Update the list for any nested `require's that occur.  */
      record_unwind_protect (require_unwind, require_nesting_list);
      require_nesting_list = Fcons (feature, require_nesting_list);

      /* Load the file.  */
      tem = load_with_autoload_queue
	(NILP (filename) ? Fsymbol_name (feature) : filename,
	 noerror, Qt, Qnil, (NILP (filename) ? Qt : Qnil));

      /* If load failed entirely, return nil.  */
      if (NILP (tem))
	return unbind_to (count, Qnil);

      tem = Fmemq (feature, Vfeatures);
      if (NILP (tem))
        {
          unsigned char *tem2 = SDATA (SYMBOL_NAME (feature));
          Lisp_Object tem3 = Fcar (Fcar (Vload_history));

          if (NILP (tem3))
            error ("Required feature `%s' was not provided", tem2);
          else
            /* Cf autoload-do-load.  */
            error ("Loading file %s failed to provide feature `%s'",
                   SDATA (tem3), tem2);
        }

      feature = unbind_to (count, feature);
    }

  return feature;
}


#ifdef HAVE_LANGINFO_CODESET
#include <langinfo.h>
#endif

DEFUN ("locale-info", Flocale_info, Slocale_info, 1, 1, 0,
       doc: /* Access locale data ITEM for the current C locale, if available.
ITEM should be one of the following:

`codeset', returning the character set as a string (locale item CODESET);

`days', returning a 7-element vector of day names (locale items DAY_n);

`months', returning a 12-element vector of month names (locale items MON_n);

`paper', returning a list of 2 integers (WIDTH HEIGHT) for the default
  paper size, both measured in millimeters (locale items _NL_PAPER_WIDTH,
  _NL_PAPER_HEIGHT).

If the system can't provide such information through a call to
`nl_langinfo', or if ITEM isn't from the list above, return nil.

See also Info node `(libc)Locales'.

The data read from the system are decoded using `locale-coding-system'.  */)
  (Lisp_Object item)
{
  char *str = NULL;

  /* STR is apparently unused on Android.  */
  ((void) str);

#ifdef HAVE_LANGINFO_CODESET
  if (EQ (item, Qcodeset))
    {
      str = nl_langinfo (CODESET);
      return build_string (str);
    }
# ifdef DAY_1
  if (EQ (item, Qdays))  /* E.g., for calendar-day-name-array.  */
    {
      Lisp_Object v = make_nil_vector (7);
      const int days[7] = {DAY_1, DAY_2, DAY_3, DAY_4, DAY_5, DAY_6, DAY_7};
      int i;
      synchronize_system_time_locale ();
      for (i = 0; i < 7; i++)
	{
	  str = nl_langinfo (days[i]);
	  AUTO_STRING (val, str);
	  /* Fixme: Is this coding system necessarily right, even if
	     it is consistent with CODESET?  If not, what to do?  */
	  ASET (v, i, code_convert_string_norecord (val, Vlocale_coding_system,
						    0));
	}
      return v;
    }
# endif
# ifdef MON_1
  if (EQ (item, Qmonths))  /* E.g., for calendar-month-name-array.  */
    {
      Lisp_Object v = make_nil_vector (12);
      const int months[12] = {MON_1, MON_2, MON_3, MON_4, MON_5, MON_6, MON_7,
			      MON_8, MON_9, MON_10, MON_11, MON_12};
      synchronize_system_time_locale ();
      for (int i = 0; i < 12; i++)
	{
	  str = nl_langinfo (months[i]);
	  AUTO_STRING (val, str);
	  ASET (v, i, code_convert_string_norecord (val, Vlocale_coding_system,
						    0));
	}
      return v;
    }
# endif
# ifdef HAVE_LANGINFO__NL_PAPER_WIDTH
  if (EQ (item, Qpaper))
    /* We have to cast twice here: first to a correctly-sized integer,
       then to int, because that's what nl_langinfo is documented to
       return for _NO_PAPER_{WIDTH,HEIGHT}.  The first cast doesn't
       suffice because it could overflow an Emacs fixnum.  This can
       happen when running under ASan, which fills allocated but
       uninitialized memory with 0xBE bytes.  */
    return list2i ((int) (intptr_t) nl_langinfo (_NL_PAPER_WIDTH),
		   (int) (intptr_t) nl_langinfo (_NL_PAPER_HEIGHT));
# endif
#endif	/* HAVE_LANGINFO_CODESET*/
  return Qnil;
}

/* base64 encode/decode functions (RFC 2045).
   Based on code from GNU recode. */

#define MIME_LINE_LENGTH 76

/* Tables of characters coding the 64 values.  */
static char const base64_value_to_char[2][64] =
{
 /* base64 */
 {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',	/*  0- 9 */
  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',	/* 10-19 */
  'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',	/* 20-29 */
  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',	/* 30-39 */
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',	/* 40-49 */
  'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',	/* 50-59 */
  '8', '9', '+', '/'					/* 60-63 */
 },
 /* base64url */
 {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',	/*  0- 9 */
  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',	/* 10-19 */
  'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd',	/* 20-29 */
  'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',	/* 30-39 */
  'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x',	/* 40-49 */
  'y', 'z', '0', '1', '2', '3', '4', '5', '6', '7',	/* 50-59 */
  '8', '9', '-', '_'					/* 60-63 */
 }
};

/* Tables of base64 values for bytes.  -1 means ignorable, 0 invalid,
   positive means 1 + the represented value.  */
static signed char const base64_char_to_value[2][UCHAR_MAX] =
{
 /* base64 */
 {
  ['\t']= -1, ['\n']= -1, ['\f']= -1, ['\r']= -1, [' '] = -1,
  ['A'] =  1, ['B'] =  2, ['C'] =  3, ['D'] =  4, ['E'] =  5,
  ['F'] =  6, ['G'] =  7, ['H'] =  8, ['I'] =  9, ['J'] = 10,
  ['K'] = 11, ['L'] = 12, ['M'] = 13, ['N'] = 14, ['O'] = 15,
  ['P'] = 16, ['Q'] = 17, ['R'] = 18, ['S'] = 19, ['T'] = 20,
  ['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24, ['Y'] = 25, ['Z'] = 26,
  ['a'] = 27, ['b'] = 28, ['c'] = 29, ['d'] = 30, ['e'] = 31,
  ['f'] = 32, ['g'] = 33, ['h'] = 34, ['i'] = 35, ['j'] = 36,
  ['k'] = 37, ['l'] = 38, ['m'] = 39, ['n'] = 40, ['o'] = 41,
  ['p'] = 42, ['q'] = 43, ['r'] = 44, ['s'] = 45, ['t'] = 46,
  ['u'] = 47, ['v'] = 48, ['w'] = 49, ['x'] = 50, ['y'] = 51, ['z'] = 52,
  ['0'] = 53, ['1'] = 54, ['2'] = 55, ['3'] = 56, ['4'] = 57,
  ['5'] = 58, ['6'] = 59, ['7'] = 60, ['8'] = 61, ['9'] = 62,
  ['+'] = 63, ['/'] = 64
 },
 /* base64url */
 {
  ['\t']= -1, ['\n']= -1, ['\f']= -1, ['\r']= -1, [' '] = -1,
  ['A'] =  1, ['B'] =  2, ['C'] =  3, ['D'] =  4, ['E'] =  5,
  ['F'] =  6, ['G'] =  7, ['H'] =  8, ['I'] =  9, ['J'] = 10,
  ['K'] = 11, ['L'] = 12, ['M'] = 13, ['N'] = 14, ['O'] = 15,
  ['P'] = 16, ['Q'] = 17, ['R'] = 18, ['S'] = 19, ['T'] = 20,
  ['U'] = 21, ['V'] = 22, ['W'] = 23, ['X'] = 24, ['Y'] = 25, ['Z'] = 26,
  ['a'] = 27, ['b'] = 28, ['c'] = 29, ['d'] = 30, ['e'] = 31,
  ['f'] = 32, ['g'] = 33, ['h'] = 34, ['i'] = 35, ['j'] = 36,
  ['k'] = 37, ['l'] = 38, ['m'] = 39, ['n'] = 40, ['o'] = 41,
  ['p'] = 42, ['q'] = 43, ['r'] = 44, ['s'] = 45, ['t'] = 46,
  ['u'] = 47, ['v'] = 48, ['w'] = 49, ['x'] = 50, ['y'] = 51, ['z'] = 52,
  ['0'] = 53, ['1'] = 54, ['2'] = 55, ['3'] = 56, ['4'] = 57,
  ['5'] = 58, ['6'] = 59, ['7'] = 60, ['8'] = 61, ['9'] = 62,
  ['-'] = 63, ['_'] = 64
 }
};

/* The following diagram shows the logical steps by which three octets
   get transformed into four base64 characters.

		 .--------.  .--------.  .--------.
		 |aaaaaabb|  |bbbbcccc|  |ccdddddd|
		 `--------'  `--------'  `--------'
                    6   2      4   4       2   6
	       .--------+--------+--------+--------.
	       |00aaaaaa|00bbbbbb|00cccccc|00dddddd|
	       `--------+--------+--------+--------'

	       .--------+--------+--------+--------.
	       |AAAAAAAA|BBBBBBBB|CCCCCCCC|DDDDDDDD|
	       `--------+--------+--------+--------'

   The octets are divided into 6 bit chunks, which are then encoded into
   base64 characters.  */


static ptrdiff_t base64_encode_1 (const char *, char *, ptrdiff_t, bool, bool,
				  bool, bool);
static ptrdiff_t base64_decode_1 (const char *, char *, ptrdiff_t, bool,
				  bool, bool, ptrdiff_t *);

static Lisp_Object base64_encode_region_1 (Lisp_Object, Lisp_Object, bool,
					   bool, bool);

static Lisp_Object base64_encode_string_1 (Lisp_Object, bool,
					   bool, bool);


DEFUN ("base64-encode-region", Fbase64_encode_region, Sbase64_encode_region,
       2, 3, "r",
       doc: /* Base64-encode the region between BEG and END.
The data in the region is assumed to represent bytes, not text.  If
you want to base64-encode text, the text has to be converted into data
first by using `encode-coding-region' with the appropriate coding
system first.

Return the length of the encoded data.

Optional third argument NO-LINE-BREAK means do not break long lines
into shorter lines.  */)
  (Lisp_Object beg, Lisp_Object end, Lisp_Object no_line_break)
{
  return base64_encode_region_1 (beg, end, NILP (no_line_break), true, false);
}


DEFUN ("base64url-encode-region", Fbase64url_encode_region, Sbase64url_encode_region,
       2, 3, "r",
       doc: /* Base64url-encode the region between BEG and END.
Return the length of the encoded text.
Optional second argument NO-PAD means do not add padding char =.

This produces the URL variant of base 64 encoding defined in RFC 4648.  */)
  (Lisp_Object beg, Lisp_Object end, Lisp_Object no_pad)
{
  return base64_encode_region_1 (beg, end, false, NILP(no_pad), true);
}

static Lisp_Object
base64_encode_region_1 (Lisp_Object beg, Lisp_Object end, bool line_break,
			bool pad, bool base64url)
{
  char *encoded;
  ptrdiff_t allength, length;
  ptrdiff_t ibeg, iend, encoded_length;
  ptrdiff_t old_pos = PT;
  USE_SAFE_ALLOCA;

  validate_region (&beg, &end);

  ibeg = CHAR_TO_BYTE (XFIXNAT (beg));
  iend = CHAR_TO_BYTE (XFIXNAT (end));
  move_gap_both (XFIXNAT (beg), ibeg);

  /* We need to allocate enough room for encoding the text.
     We need 33 1/3% more space, plus a newline every 76
     characters, and then we round up. */
  length = iend - ibeg;
  allength = length + length/3 + 1;
  allength += allength / MIME_LINE_LENGTH + 1 + 6;

  encoded = SAFE_ALLOCA (allength);
  encoded_length = base64_encode_1 ((char *) BYTE_POS_ADDR (ibeg),
				    encoded, length, line_break,
				    pad, base64url,
				    !NILP (BVAR (current_buffer, enable_multibyte_characters)));
  if (encoded_length > allength)
    emacs_abort ();

  if (encoded_length < 0)
    {
      /* The encoding wasn't possible. */
      SAFE_FREE ();
      error ("Multibyte character in data for base64 encoding");
    }

  /* Now we have encoded the region, so we insert the new contents
     and delete the old.  (Insert first in order to preserve markers.)  */
  SET_PT_BOTH (XFIXNAT (beg), ibeg);
  insert (encoded, encoded_length);
  SAFE_FREE ();
  del_range_byte (ibeg + encoded_length, iend + encoded_length);

  /* If point was outside of the region, restore it exactly; else just
     move to the beginning of the region.  */
  if (old_pos >= XFIXNAT (end))
    old_pos += encoded_length - (XFIXNAT (end) - XFIXNAT (beg));
  else if (old_pos > XFIXNAT (beg))
    old_pos = XFIXNAT (beg);
  SET_PT (old_pos);

  /* We return the length of the encoded text. */
  return make_fixnum (encoded_length);
}

DEFUN ("base64-encode-string", Fbase64_encode_string, Sbase64_encode_string,
       1, 2, 0,
       doc: /* Base64-encode STRING and return the result.
Optional second argument NO-LINE-BREAK means do not break long lines
into shorter lines.  */)
  (Lisp_Object string, Lisp_Object no_line_break)
{

  return base64_encode_string_1 (string, NILP (no_line_break), true, false);
}

DEFUN ("base64url-encode-string", Fbase64url_encode_string,
       Sbase64url_encode_string, 1, 2, 0,
       doc: /* Base64url-encode STRING and return the result.
Optional second argument NO-PAD means do not add padding char =.

This produces the URL variant of base 64 encoding defined in RFC 4648.  */)
  (Lisp_Object string, Lisp_Object no_pad)
{

  return base64_encode_string_1 (string, false, NILP(no_pad), true);
}

static Lisp_Object
base64_encode_string_1 (Lisp_Object string, bool line_break,
			bool pad, bool base64url)
{
  ptrdiff_t allength, length, encoded_length;
  char *encoded;
  Lisp_Object encoded_string;
  USE_SAFE_ALLOCA;

  CHECK_STRING (string);

  /* We need to allocate enough room for encoding the text.
     We need 33 1/3% more space, plus a newline every 76
     characters, and then we round up. */
  length = SBYTES (string);
  allength = length + length/3 + 1;
  allength += allength / MIME_LINE_LENGTH + 1 + 6;

  /* We need to allocate enough room for decoding the text. */
  encoded = SAFE_ALLOCA (allength);

  encoded_length = base64_encode_1 (SSDATA (string),
				    encoded, length, line_break,
				    pad, base64url,
				    STRING_MULTIBYTE (string));
  if (encoded_length > allength)
    emacs_abort ();

  if (encoded_length < 0)
    {
      /* The encoding wasn't possible. */
      error ("Multibyte character in data for base64 encoding");
    }

  encoded_string = make_unibyte_string (encoded, encoded_length);
  SAFE_FREE ();

  return encoded_string;
}

static ptrdiff_t
base64_encode_1 (const char *from, char *to, ptrdiff_t length,
		 bool line_break, bool pad, bool base64url,
		 bool multibyte)
{
  int counter = 0;
  ptrdiff_t i = 0;
  char *e = to;
  int c;
  unsigned int value;
  int bytes;
  char const *b64_value_to_char = base64_value_to_char[base64url];

  while (i < length)
    {
      if (multibyte)
	{
	  c = string_char_and_length ((unsigned char *) from + i, &bytes);
	  if (CHAR_BYTE8_P (c))
	    c = CHAR_TO_BYTE8 (c);
	  else if (c >= 128)
	    return -1;
	  i += bytes;
	}
      else
	c = from[i++];

      /* Wrap line every 76 characters.  */

      if (line_break)
	{
	  if (counter < MIME_LINE_LENGTH / 4)
	    counter++;
	  else
	    {
	      *e++ = '\n';
	      counter = 1;
	    }
	}

      /* Process first byte of a triplet.  */

      *e++ = b64_value_to_char[0x3f & c >> 2];
      value = (0x03 & c) << 4;

      /* Process second byte of a triplet.  */

      if (i == length)
	{
	  *e++ = b64_value_to_char[value];
	  if (pad)
	    {
	      *e++ = '=';
	      *e++ = '=';
	    }
	  break;
	}

      if (multibyte)
	{
	  c = string_char_and_length ((unsigned char *) from + i, &bytes);
	  if (CHAR_BYTE8_P (c))
	    c = CHAR_TO_BYTE8 (c);
	  else if (c >= 128)
	    return -1;
	  i += bytes;
	}
      else
	c = from[i++];

      *e++ = b64_value_to_char[value | (0x0f & c >> 4)];
      value = (0x0f & c) << 2;

      /* Process third byte of a triplet.  */

      if (i == length)
	{
	  *e++ = b64_value_to_char[value];
	  if (pad)
	    *e++ = '=';
	  break;
	}

      if (multibyte)
	{
	  c = string_char_and_length ((unsigned char *) from + i, &bytes);
	  if (CHAR_BYTE8_P (c))
	    c = CHAR_TO_BYTE8 (c);
	  else if (c >= 128)
	    return -1;
	  i += bytes;
	}
      else
	c = from[i++];

      *e++ = b64_value_to_char[value | (0x03 & c >> 6)];
      *e++ = b64_value_to_char[0x3f & c];
    }

  return e - to;
}


DEFUN ("base64-decode-region", Fbase64_decode_region, Sbase64_decode_region,
       2, 4, "r",
       doc: /* Base64-decode the region between BEG and END.
Return the length of the decoded data.

Note that after calling this function, the data in the region will
represent bytes, not text.  If you want to end up with text, you have
to call `decode-coding-region' afterwards with an appropriate coding
system.

If the region can't be decoded, signal an error and don't modify the buffer.
Optional third argument BASE64URL determines whether to use the URL variant
of the base 64 encoding, as defined in RFC 4648.
If optional fourth argument IGNORE-INVALID is non-nil invalid characters
are ignored instead of signaling an error.  */)
     (Lisp_Object beg, Lisp_Object end, Lisp_Object base64url,
      Lisp_Object ignore_invalid)
{
  ptrdiff_t ibeg, iend, length, allength;
  char *decoded;
  ptrdiff_t old_pos = PT;
  ptrdiff_t decoded_length;
  ptrdiff_t inserted_chars;
  bool multibyte = !NILP (BVAR (current_buffer, enable_multibyte_characters));
  USE_SAFE_ALLOCA;

  validate_region (&beg, &end);

  ibeg = CHAR_TO_BYTE (XFIXNAT (beg));
  iend = CHAR_TO_BYTE (XFIXNAT (end));

  length = iend - ibeg;

  /* We need to allocate enough room for decoding the text.  If we are
     working on a multibyte buffer, each decoded code may occupy at
     most two bytes.  */
  allength = multibyte ? length * 2 : length;
  decoded = SAFE_ALLOCA (allength);

  move_gap_both (XFIXNAT (beg), ibeg);
  decoded_length = base64_decode_1 ((char *) BYTE_POS_ADDR (ibeg),
				    decoded, length, !NILP (base64url),
				    multibyte, !NILP (ignore_invalid),
				    &inserted_chars);
  if (decoded_length > allength)
    emacs_abort ();

  if (decoded_length < 0)
    {
      /* The decoding wasn't possible. */
      error ("Invalid base64 data");
    }

  /* Now we have decoded the region, so we insert the new contents
     and delete the old.  (Insert first in order to preserve markers.)  */
  TEMP_SET_PT_BOTH (XFIXNAT (beg), ibeg);
  insert_1_both (decoded, inserted_chars, decoded_length, 0, 1, 0);
  signal_after_change (XFIXNAT (beg), 0, inserted_chars);
  SAFE_FREE ();

  /* Delete the original text.  */
  del_range_both (PT, PT_BYTE, XFIXNAT (end) + inserted_chars,
		  iend + decoded_length, 1);

  /* If point was outside of the region, restore it exactly; else just
     move to the beginning of the region.  */
  if (old_pos >= XFIXNAT (end))
    old_pos += inserted_chars - (XFIXNAT (end) - XFIXNAT (beg));
  else if (old_pos > XFIXNAT (beg))
    old_pos = XFIXNAT (beg);
  SET_PT (old_pos > ZV ? ZV : old_pos);

  return make_fixnum (inserted_chars);
}

DEFUN ("base64-decode-string", Fbase64_decode_string, Sbase64_decode_string,
       1, 3, 0,
       doc: /* Base64-decode STRING and return the result as a string.
Optional argument BASE64URL determines whether to use the URL variant of
the base 64 encoding, as defined in RFC 4648.
If optional third argument IGNORE-INVALID is non-nil invalid characters are
ignored instead of signaling an error.  */)
     (Lisp_Object string, Lisp_Object base64url, Lisp_Object ignore_invalid)
{
  char *decoded;
  ptrdiff_t length, decoded_length;
  Lisp_Object decoded_string;
  USE_SAFE_ALLOCA;

  CHECK_STRING (string);

  length = SBYTES (string);
  /* We need to allocate enough room for decoding the text. */
  decoded = SAFE_ALLOCA (length);

  /* The decoded result should be unibyte. */
  ptrdiff_t decoded_chars;
  decoded_length = base64_decode_1 (SSDATA (string), decoded, length,
				    !NILP (base64url), false,
				    !NILP (ignore_invalid), &decoded_chars);
  if (decoded_length > length)
    emacs_abort ();
  else if (decoded_length >= 0)
    decoded_string = make_unibyte_string (decoded, decoded_length);
  else
    decoded_string = Qnil;

  SAFE_FREE ();
  if (!STRINGP (decoded_string))
    error ("Invalid base64 data");

  return decoded_string;
}

/* Base64-decode the data at FROM of LENGTH bytes into TO.  If
   MULTIBYTE, the decoded result should be in multibyte
   form.  If IGNORE_INVALID, ignore invalid base64 characters.
   Store the number of produced characters in *NCHARS_RETURN.  */

static ptrdiff_t
base64_decode_1 (const char *from, char *to, ptrdiff_t length,
		 bool base64url, bool multibyte, bool ignore_invalid,
		 ptrdiff_t *nchars_return)
{
  char const *f = from;
  char const *flim = from + length;
  char *e = to;
  ptrdiff_t nchars = 0;
  signed char const *b64_char_to_value = base64_char_to_value[base64url];
  unsigned char multibyte_bit = multibyte << 7;

  while (true)
    {
      unsigned char c;
      int v1;

      /* Process first byte of a quadruplet. */

      do
	{
	  if (f == flim)
	    {
	      *nchars_return = nchars;
	      return e - to;
	    }
	  c = *f++;
	  v1 = b64_char_to_value[c];
	}
      while (v1 < 0 || (v1 == 0 && ignore_invalid));

      if (v1 == 0)
	return -1;
      unsigned int value = (v1 - 1) << 18;

      /* Process second byte of a quadruplet.  */

      do
	{
	  if (f == flim)
	    return -1;
	  c = *f++;
	  v1 = b64_char_to_value[c];
	}
      while (v1 < 0 || (v1 == 0 && ignore_invalid));

      if (v1 == 0)
	return -1;
      value += (v1 - 1) << 12;

      c = value >> 16 & 0xff;
      if (c & multibyte_bit)
	e += BYTE8_STRING (c, (unsigned char *) e);
      else
	*e++ = c;
      nchars++;

      /* Process third byte of a quadruplet.  */

      do
	{
	  if (f == flim)
	    {
	      if (!base64url && !ignore_invalid)
		return -1;
	      *nchars_return = nchars;
	      return e - to;
	    }
	  c = *f++;
	  v1 = b64_char_to_value[c];
	}
      while (v1 < 0 || (v1 == 0 && ignore_invalid));

      if (c == '=')
	{
	  do
	    {
	      if (f == flim)
		return -1;
	      c = *f++;
	    }
	  while (b64_char_to_value[c] < 0);

	  if (c != '=')
	    return -1;
	  continue;
	}

      if (v1 == 0)
	return -1;
      value += (v1 - 1) << 6;

      c = value >> 8 & 0xff;
      if (c & multibyte_bit)
	e += BYTE8_STRING (c, (unsigned char *) e);
      else
	*e++ = c;
      nchars++;

      /* Process fourth byte of a quadruplet.  */

      do
	{
	  if (f == flim)
	    {
	      if (!base64url && !ignore_invalid)
		return -1;
	      *nchars_return = nchars;
	      return e - to;
	    }
	  c = *f++;
	  v1 = b64_char_to_value[c];
	}
      while (v1 < 0 || (v1 == 0 && ignore_invalid));

      if (c == '=')
	continue;

      if (v1 == 0)
	return -1;
      value += v1 - 1;

      c = value & 0xff;
      if (c & multibyte_bit)
	e += BYTE8_STRING (c, (unsigned char *) e);
      else
	*e++ = c;
      nchars++;
    }
}



/***********************************************************************
 *****                                                             *****
 *****			     Hash Tables                           *****
 *****                                                             *****
 ***********************************************************************/

/* Implemented by gerd@gnu.org.  This hash table implementation was
   inspired by CMUCL hash tables.  */

/* Ideas:

   1. For small tables, association lists are probably faster than
   hash tables because they have lower overhead.

   For uses of hash tables where the O(1) behavior of table
   operations is not a requirement, it might therefore be a good idea
   not to hash.  Instead, we could just do a linear search in the
   key_and_value vector of the hash table.  This could be done
   if a `:linear-search t' argument is given to make-hash-table.  */



/***********************************************************************
			       Utilities
 ***********************************************************************/

static void
CHECK_HASH_TABLE (Lisp_Object x)
{
  CHECK_TYPE (HASH_TABLE_P (x), Qhash_table_p, x);
}

static void
set_hash_next_slot (struct Lisp_Hash_Table *h, ptrdiff_t idx, ptrdiff_t val)
{
  eassert (idx >= 0 && idx < h->table_size);
  h->next[idx] = val;
}
static void
set_hash_hash_slot (struct Lisp_Hash_Table *h, ptrdiff_t idx, hash_hash_t val)
{
  eassert (idx >= 0 && idx < h->table_size);
  h->hash[idx] = val;
}
static void
set_hash_index_slot (struct Lisp_Hash_Table *h, ptrdiff_t idx, ptrdiff_t val)
{
  eassert (idx >= 0 && idx < hash_table_index_size (h));
  h->index[idx] = val;
}

/* If OBJ is a Lisp hash table, return a pointer to its struct
   Lisp_Hash_Table.  Otherwise, signal an error.  */

static struct Lisp_Hash_Table *
check_hash_table (Lisp_Object obj)
{
  CHECK_HASH_TABLE (obj);
  return XHASH_TABLE (obj);
}


/* Value is the next integer I >= N, N >= 0 which is "almost" a prime
   number.  A number is "almost" a prime number if it is not divisible
   by any integer in the range 2 .. (NEXT_ALMOST_PRIME_LIMIT - 1).  */

EMACS_INT
next_almost_prime (EMACS_INT n)
{
  static_assert (NEXT_ALMOST_PRIME_LIMIT == 11);
  for (n |= 1; ; n += 2)
    if (n % 3 != 0 && n % 5 != 0 && n % 7 != 0)
      return n;
}

/* Return a Lisp vector which has the same contents as VEC but has
   at least INCR_MIN more entries, where INCR_MIN is positive.
   If NITEMS_MAX is not -1, do not grow the vector to be any larger
   than NITEMS_MAX.  New entries in the resulting vector are nil.  */

Lisp_Object
larger_vector (Lisp_Object vec, ptrdiff_t incr_min, ptrdiff_t nitems_max)
{
  struct Lisp_Vector *v;
  ptrdiff_t incr, incr_max, old_size, new_size;
  ptrdiff_t C_language_max = min (PTRDIFF_MAX, SIZE_MAX) / sizeof *v->contents;
  ptrdiff_t n_max = (0 <= nitems_max && nitems_max < C_language_max
		     ? nitems_max : C_language_max);
  eassert (VECTORP (vec));
  eassert (0 < incr_min && -1 <= nitems_max);
  old_size = ASIZE (vec);
  incr_max = n_max - old_size;
  incr = max (incr_min, min (old_size >> 1, incr_max));
  if (incr_max < incr)
    memory_full (SIZE_MAX);
  new_size = old_size + incr;
  v = allocate_vector (new_size);
  memcpy (v->contents, XVECTOR (vec)->contents, old_size * sizeof *v->contents);
  memclear (v->contents + old_size, (new_size - old_size) * word_size);
  XSETVECTOR (vec, v);
  return vec;
}


/***********************************************************************
			 Low-level Functions
 ***********************************************************************/

/* Return the index of the next entry in H following the one at IDX,
   or -1 if none.  */

static ptrdiff_t
HASH_NEXT (struct Lisp_Hash_Table *h, ptrdiff_t idx)
{
  eassert (idx >= 0 && idx < h->table_size);
  return h->next[idx];
}

/* Return the index of the element in hash table H that is the start
   of the collision list at index IDX, or -1 if the list is empty.  */

static ptrdiff_t
HASH_INDEX (struct Lisp_Hash_Table *h, ptrdiff_t idx)
{
  eassert (idx >= 0 && idx < hash_table_index_size (h));
  return h->index[idx];
}

/* Restore a hash table's mutability after the critical section exits.  */

static void
restore_mutability (void *ptr)
{
  struct Lisp_Hash_Table *h = ptr;
  h->mutable = true;
}

/* Return the result of calling a user-defined hash or comparison
   function ARGS[0] with arguments ARGS[1] through ARGS[NARGS - 1].
   Signal an error if the function attempts to modify H, which
   otherwise might lead to undefined behavior.  */

static Lisp_Object
hash_table_user_defined_call (ptrdiff_t nargs, Lisp_Object *args,
			      struct Lisp_Hash_Table *h)
{
  if (!h->mutable)
    return Ffuncall (nargs, args);
  specpdl_ref count = inhibit_garbage_collection ();
  record_unwind_protect_ptr (restore_mutability, h);
  h->mutable = false;
  return unbind_to (count, Ffuncall (nargs, args));
}

/* Ignore H and compare KEY1 and KEY2 using 'eql'.
   Value is true if KEY1 and KEY2 are the same.  */

static Lisp_Object
cmpfn_eql (Lisp_Object key1, Lisp_Object key2, struct Lisp_Hash_Table *h)
{
  return Feql (key1, key2);
}

/* Ignore H and compare KEY1 and KEY2 using 'equal'.
   Value is true if KEY1 and KEY2 are the same.  */

static Lisp_Object
cmpfn_equal (Lisp_Object key1, Lisp_Object key2, struct Lisp_Hash_Table *h)
{
  return Fequal (key1, key2);
}


/* Given H, compare KEY1 and KEY2 using H->user_cmp_function.
   Value is true if KEY1 and KEY2 are the same.  */

static Lisp_Object
cmpfn_user_defined (Lisp_Object key1, Lisp_Object key2,
		    struct Lisp_Hash_Table *h)
{
  Lisp_Object args[] = { h->test->user_cmp_function, key1, key2 };
  return hash_table_user_defined_call (ARRAYELTS (args), args, h);
}

static EMACS_INT
sxhash_eq (Lisp_Object key)
{
  Lisp_Object k = maybe_remove_pos_from_symbol (key);
  return XHASH (k) ^ XTYPE (k);
}

static EMACS_INT
sxhash_eql (Lisp_Object key)
{
  return FLOATP (key) || BIGNUMP (key) ? sxhash (key) : sxhash_eq (key);
}

/* Ignore H and return a hash code for KEY which uses 'eq' to compare keys.  */

static hash_hash_t
hashfn_eq (Lisp_Object key, struct Lisp_Hash_Table *h)
{
  return reduce_emacs_uint_to_hash_hash (sxhash_eq (key));
}

/* Ignore H and return a hash code for KEY which uses 'equal' to
   compare keys.  */
static hash_hash_t
hashfn_equal (Lisp_Object key, struct Lisp_Hash_Table *h)
{
  return reduce_emacs_uint_to_hash_hash (sxhash (key));
}

/* Ignore H and return a hash code for KEY which uses 'eql' to compare keys.  */
static hash_hash_t
hashfn_eql (Lisp_Object key, struct Lisp_Hash_Table *h)
{
  return reduce_emacs_uint_to_hash_hash (sxhash_eql (key));
}

/* Given H, return a hash code for KEY which uses a user-defined
   function to compare keys.  */

static hash_hash_t
hashfn_user_defined (Lisp_Object key, struct Lisp_Hash_Table *h)
{
  Lisp_Object args[] = { h->test->user_hash_function, key };
  Lisp_Object hash = hash_table_user_defined_call (ARRAYELTS (args), args, h);
  return reduce_emacs_uint_to_hash_hash (FIXNUMP (hash)
					 ? XUFIXNUM(hash) : sxhash (hash));
}

struct hash_table_test const
  hashtest_eq = { .name = LISPSYM_INITIALLY (Qeq),
		  .cmpfn = 0, .hashfn = hashfn_eq },
  hashtest_eql = { .name = LISPSYM_INITIALLY (Qeql),
		   .cmpfn = cmpfn_eql, .hashfn = hashfn_eql },
  hashtest_equal = { .name = LISPSYM_INITIALLY (Qequal),
		     .cmpfn = cmpfn_equal, .hashfn = hashfn_equal };

/* Allocate basically initialized hash table.  */

static struct Lisp_Hash_Table *
allocate_hash_table (void)
{
  return ALLOCATE_PLAIN_PSEUDOVECTOR (struct Lisp_Hash_Table, PVEC_HASH_TABLE);
}

/* Compute the size of the index (as log2) from the table capacity.  */
static int
compute_hash_index_bits (hash_idx_t size)
{
  /* An upper bound on the size of a hash table index index.  */
  hash_idx_t upper_bound = min (MOST_POSITIVE_FIXNUM,
				min (TYPE_MAXIMUM (hash_idx_t),
				     PTRDIFF_MAX / sizeof (hash_idx_t)));
  /* Use next higher power of 2.  This works even for size=0.  */
  int bits = elogb (size) + 1;
  if (bits >= TYPE_WIDTH (uintmax_t) || ((uintmax_t)1 << bits) > upper_bound)
    error ("Hash table too large");
  return bits;
}

/* Constant hash index vector used when the table size is zero.
   This avoids allocating it from the heap.  */
static const hash_idx_t empty_hash_index_vector[] = {-1};

/* Create and initialize a new hash table.

   TEST specifies the test the hash table will use to compare keys.
   It must be either one of the predefined tests `eq', `eql' or
   `equal' or a symbol denoting a user-defined test named TEST with
   test and hash functions USER_TEST and USER_HASH.

   Give the table initial capacity SIZE, 0 <= SIZE <= MOST_POSITIVE_FIXNUM.

   WEAK specifies the weakness of the table.  */

Lisp_Object
make_hash_table (const struct hash_table_test *test, EMACS_INT size,
		 hash_table_weakness_t weak)
{
  eassert (SYMBOLP (test->name));
  eassert (0 <= size && size <= min (MOST_POSITIVE_FIXNUM, PTRDIFF_MAX));

  struct Lisp_Hash_Table *h = allocate_hash_table ();

  h->test = test;
  h->weakness = weak;
  h->count = 0;
  h->table_size = size;

  if (size == 0)
    {
      h->key_and_value = NULL;
      h->hash = NULL;
      h->next = NULL;
      h->index_bits = 0;
      h->index = (hash_idx_t *)empty_hash_index_vector;
      h->next_free = -1;
    }
  else
    {
      h->key_and_value = hash_table_alloc_bytes (2 * size
						 * sizeof *h->key_and_value);
      for (ptrdiff_t i = 0; i < 2 * size; i++)
	h->key_and_value[i] = HASH_UNUSED_ENTRY_KEY;

      h->hash = hash_table_alloc_bytes (size * sizeof *h->hash);

      h->next = hash_table_alloc_bytes (size * sizeof *h->next);
      for (ptrdiff_t i = 0; i < size - 1; i++)
	h->next[i] = i + 1;
      h->next[size - 1] = -1;

      int index_bits = compute_hash_index_bits (size);
      h->index_bits = index_bits;
      ptrdiff_t index_size = hash_table_index_size (h);
      h->index = hash_table_alloc_bytes (index_size * sizeof *h->index);
      for (ptrdiff_t i = 0; i < index_size; i++)
	h->index[i] = -1;

      h->next_free = 0;
    }

  h->next_weak = NULL;
  h->mutable = true;
  return make_lisp_hash_table (h);
}


/* Return a copy of hash table H1.  Keys and values are not copied,
   only the table itself is.  */

static Lisp_Object
copy_hash_table (struct Lisp_Hash_Table *h1)
{
  struct Lisp_Hash_Table *h2;

  h2 = allocate_hash_table ();
  *h2 = *h1;
  h2->mutable = true;

  if (h1->table_size > 0)
    {
      ptrdiff_t kv_bytes = 2 * h1->table_size * sizeof *h1->key_and_value;
      h2->key_and_value = hash_table_alloc_bytes (kv_bytes);
      memcpy (h2->key_and_value, h1->key_and_value, kv_bytes);

      ptrdiff_t hash_bytes = h1->table_size * sizeof *h1->hash;
      h2->hash = hash_table_alloc_bytes (hash_bytes);
      memcpy (h2->hash, h1->hash, hash_bytes);

      ptrdiff_t next_bytes = h1->table_size * sizeof *h1->next;
      h2->next = hash_table_alloc_bytes (next_bytes);
      memcpy (h2->next, h1->next, next_bytes);

      ptrdiff_t index_bytes = hash_table_index_size (h1) * sizeof *h1->index;
      h2->index = hash_table_alloc_bytes (index_bytes);
      memcpy (h2->index, h1->index, index_bytes);
    }
  return make_lisp_hash_table (h2);
}

/* Compute index into the index vector from a hash value.  */
static inline ptrdiff_t
hash_index_index (struct Lisp_Hash_Table *h, hash_hash_t hash)
{
  return knuth_hash (hash, h->index_bits);
}

/* Resize hash table H if it's too full.  If H cannot be resized
   because it's already too large, throw an error.  */

static void
maybe_resize_hash_table (struct Lisp_Hash_Table *h)
{
  if (h->next_free < 0)
    {
      ptrdiff_t old_size = HASH_TABLE_SIZE (h);
      ptrdiff_t min_size = 6;
      ptrdiff_t base_size = min (max (old_size, min_size), PTRDIFF_MAX / 2);
      /* Grow aggressively at small sizes, then just double.  */
      ptrdiff_t new_size =
	old_size == 0
	? min_size
	: (base_size <= 64 ? base_size * 4 : base_size * 2);

      /* Allocate all the new vectors before updating *H, to
	 avoid problems if memory is exhausted.  */
      hash_idx_t *next = hash_table_alloc_bytes (new_size * sizeof *next);
      for (ptrdiff_t i = old_size; i < new_size - 1; i++)
	next[i] = i + 1;
      next[new_size - 1] = -1;

      Lisp_Object *key_and_value
	= hash_table_alloc_bytes (2 * new_size * sizeof *key_and_value);
      memcpy (key_and_value, h->key_and_value,
	      2 * old_size * sizeof *key_and_value);
      for (ptrdiff_t i = 2 * old_size; i < 2 * new_size; i++)
        key_and_value[i] = HASH_UNUSED_ENTRY_KEY;

      hash_hash_t *hash = hash_table_alloc_bytes (new_size * sizeof *hash);
      memcpy (hash, h->hash, old_size * sizeof *hash);

      ptrdiff_t old_index_size = hash_table_index_size (h);
      ptrdiff_t index_bits = compute_hash_index_bits (new_size);
      ptrdiff_t index_size = (ptrdiff_t)1 << index_bits;
      hash_idx_t *index = hash_table_alloc_bytes (index_size * sizeof *index);
      for (ptrdiff_t i = 0; i < index_size; i++)
	index[i] = -1;

      h->index_bits = index_bits;
      h->table_size = new_size;
      h->next_free = old_size;

      if (old_index_size > 1)
	hash_table_free_bytes (h->index, old_index_size * sizeof *h->index);
      h->index = index;

      hash_table_free_bytes (h->key_and_value,
			     2 * old_size * sizeof *h->key_and_value);
      h->key_and_value = key_and_value;

      hash_table_free_bytes (h->hash, old_size * sizeof *h->hash);
      h->hash = hash;

      hash_table_free_bytes (h->next, old_size * sizeof *h->next);
      h->next = next;

      h->key_and_value = key_and_value;

      /* Rehash: all data occupy entries 0..old_size-1.  */
      for (ptrdiff_t i = 0; i < old_size; i++)
	{
	  hash_hash_t hash_code = HASH_HASH (h, i);
	  ptrdiff_t start_of_bucket = hash_index_index (h, hash_code);
	  set_hash_next_slot (h, i, HASH_INDEX (h, start_of_bucket));
	  set_hash_index_slot (h, start_of_bucket, i);
	}
    }
}

static const struct hash_table_test *
hash_table_test_from_std (hash_table_std_test_t test)
{
  switch (test)
    {
    case Test_eq:    return &hashtest_eq;
    case Test_eql:   return &hashtest_eql;
    case Test_equal: return &hashtest_equal;
    }
  emacs_abort();
}

/* Rebuild a hash table from its frozen (dumped) form.  */
void
hash_table_thaw (Lisp_Object hash_table)
{
  struct Lisp_Hash_Table *h = XHASH_TABLE (hash_table);

  /* Freezing discarded most non-essential information; recompute it.
     The allocation is minimal with no room for growth.  */
  h->test = hash_table_test_from_std (h->frozen_test);
  ptrdiff_t size = h->count;
  h->table_size = size;
  h->next_free = -1;

  if (size == 0)
    {
      h->key_and_value = NULL;
      h->hash = NULL;
      h->next = NULL;
      h->index_bits = 0;
      h->index = (hash_idx_t *)empty_hash_index_vector;
    }
  else
    {
      ptrdiff_t index_bits = compute_hash_index_bits (size);
      h->index_bits = index_bits;

      h->hash = hash_table_alloc_bytes (size * sizeof *h->hash);

      h->next = hash_table_alloc_bytes (size * sizeof *h->next);

      ptrdiff_t index_size = hash_table_index_size (h);
      h->index = hash_table_alloc_bytes (index_size * sizeof *h->index);
      for (ptrdiff_t i = 0; i < index_size; i++)
	h->index[i] = -1;

      /* Recompute the hash codes for each entry in the table.  */
      for (ptrdiff_t i = 0; i < size; i++)
	{
	  Lisp_Object key = HASH_KEY (h, i);
	  hash_hash_t hash_code = hash_from_key (h, key);
	  ptrdiff_t start_of_bucket = hash_index_index (h, hash_code);
	  set_hash_hash_slot (h, i, hash_code);
	  set_hash_next_slot (h, i, HASH_INDEX (h, start_of_bucket));
	  set_hash_index_slot (h, start_of_bucket, i);
	}
    }
}

/* Look up KEY with hash HASH in table H.
   Return entry index or -1 if none.  */
static ptrdiff_t
hash_find_with_hash (struct Lisp_Hash_Table *h,
		     Lisp_Object key, hash_hash_t hash)
{
  ptrdiff_t start_of_bucket = hash_index_index (h, hash);
  for (ptrdiff_t i = HASH_INDEX (h, start_of_bucket);
       0 <= i; i = HASH_NEXT (h, i))
    if (EQ (key, HASH_KEY (h, i))
	|| (h->test->cmpfn
	    && hash == HASH_HASH (h, i)
	    && !NILP (h->test->cmpfn (key, HASH_KEY (h, i), h))))
      return i;

  return -1;
}

/* Look up KEY in table H.  Return entry index or -1 if none.  */
ptrdiff_t
hash_find (struct Lisp_Hash_Table *h, Lisp_Object key)
{
  return hash_find_with_hash (h, key, hash_from_key (h, key));
}

/* Look up KEY in hash table H.  Return its hash value in *PHASH.
   Value is the index of the entry in H matching KEY, or -1 if not found.  */
ptrdiff_t
hash_find_get_hash (struct Lisp_Hash_Table *h, Lisp_Object key,
		    hash_hash_t *phash)
{
  EMACS_UINT hash = hash_from_key (h, key);
  *phash = hash;
  return hash_find_with_hash (h, key, hash);
}

static void
check_mutable_hash_table (Lisp_Object obj, struct Lisp_Hash_Table *h)
{
  if (!h->mutable)
    signal_error ("hash table test modifies table", obj);
}

/* Put an entry into hash table H that associates KEY with VALUE.
   HASH is a previously computed hash code of KEY.
   Value is the index of the entry in H matching KEY.  */

ptrdiff_t
hash_put (struct Lisp_Hash_Table *h, Lisp_Object key, Lisp_Object value,
	  hash_hash_t hash)
{
  eassert (!hash_unused_entry_key_p (key));
  /* Increment count after resizing because resizing may fail.  */
  maybe_resize_hash_table (h);
  h->count++;

  /* Store key/value in the key_and_value vector.  */
  ptrdiff_t i = h->next_free;
  eassert (hash_unused_entry_key_p (HASH_KEY (h, i)));
  h->next_free = HASH_NEXT (h, i);
  set_hash_key_slot (h, i, key);
  set_hash_value_slot (h, i, value);

  /* Remember its hash code.  */
  set_hash_hash_slot (h, i, hash);

  /* Add new entry to its collision chain.  */
  ptrdiff_t start_of_bucket = hash_index_index (h, hash);
  set_hash_next_slot (h, i, HASH_INDEX (h, start_of_bucket));
  set_hash_index_slot (h, start_of_bucket, i);
  return i;
}


/* Remove the entry matching KEY from hash table H, if there is one.  */

void
hash_remove_from_table (struct Lisp_Hash_Table *h, Lisp_Object key)
{
  hash_hash_t hashval = hash_from_key (h, key);
  ptrdiff_t start_of_bucket = hash_index_index (h, hashval);
  ptrdiff_t prev = -1;

  for (ptrdiff_t i = HASH_INDEX (h, start_of_bucket);
       0 <= i;
       i = HASH_NEXT (h, i))
    {
      if (EQ (key, HASH_KEY (h, i))
	  || (h->test->cmpfn
	      && hashval == HASH_HASH (h, i)
	      && !NILP (h->test->cmpfn (key, HASH_KEY (h, i), h))))
	{
	  /* Take entry out of collision chain.  */
	  if (prev < 0)
	    set_hash_index_slot (h, start_of_bucket, HASH_NEXT (h, i));
	  else
	    set_hash_next_slot (h, prev, HASH_NEXT (h, i));

	  /* Clear slots in key_and_value and add the slots to
	     the free list.  */
	  set_hash_key_slot (h, i, HASH_UNUSED_ENTRY_KEY);
	  set_hash_value_slot (h, i, Qnil);
	  set_hash_next_slot (h, i, h->next_free);
	  h->next_free = i;
	  h->count--;
	  eassert (h->count >= 0);
	  break;
	}

      prev = i;
    }
}


/* Clear hash table H.  */

static void
hash_clear (struct Lisp_Hash_Table *h)
{
  if (h->count > 0)
    {
      ptrdiff_t size = HASH_TABLE_SIZE (h);
      for (ptrdiff_t i = 0; i < size; i++)
	{
	  set_hash_next_slot (h, i, i < size - 1 ? i + 1 : -1);
	  set_hash_key_slot (h, i, HASH_UNUSED_ENTRY_KEY);
	  set_hash_value_slot (h, i, Qnil);
	}

      ptrdiff_t index_size = hash_table_index_size (h);
      for (ptrdiff_t i = 0; i < index_size; i++)
	h->index[i] = -1;

      h->next_free = 0;
      h->count = 0;
    }
}



/************************************************************************
			   Weak Hash Tables
 ************************************************************************/

/* Whether to keep an entry whose key and value are known to be retained
   if STRONG_KEY and STRONG_VALUE, respectively, are true.  */
static inline bool
keep_entry_p (hash_table_weakness_t weakness,
	      bool strong_key, bool strong_value)
{
  switch (weakness)
    {
    case Weak_None:          return true;
    case Weak_Key:           return strong_key;
    case Weak_Value:         return strong_value;
    case Weak_Key_Or_Value:  return strong_key || strong_value;
    case Weak_Key_And_Value: return strong_key && strong_value;
    }
  emacs_abort();
}

/* Sweep weak hash table H.  REMOVE_ENTRIES_P means remove
   entries from the table that don't survive the current GC.
   !REMOVE_ENTRIES_P means mark entries that are in use.  Value is
   true if anything was marked.  */

bool
sweep_weak_table (struct Lisp_Hash_Table *h, bool remove_entries_p)
{
  ptrdiff_t n = hash_table_index_size (h);
  bool marked = false;

  for (ptrdiff_t bucket = 0; bucket < n; ++bucket)
    {
      /* Follow collision chain, removing entries that don't survive
         this garbage collection.  */
      ptrdiff_t prev = -1;
      ptrdiff_t next;
      for (ptrdiff_t i = HASH_INDEX (h, bucket); 0 <= i; i = next)
        {
	  bool key_known_to_survive_p = survives_gc_p (HASH_KEY (h, i));
	  bool value_known_to_survive_p = survives_gc_p (HASH_VALUE (h, i));
	  bool remove_p = !keep_entry_p (h->weakness,
					 key_known_to_survive_p,
					 value_known_to_survive_p);

	  next = HASH_NEXT (h, i);

	  if (remove_entries_p)
	    {
              eassert (!remove_p
                       == (key_known_to_survive_p && value_known_to_survive_p));
	      if (remove_p)
		{
		  /* Take out of collision chain.  */
		  if (prev < 0)
		    set_hash_index_slot (h, bucket, next);
		  else
		    set_hash_next_slot (h, prev, next);

		  /* Add to free list.  */
		  set_hash_next_slot (h, i, h->next_free);
		  h->next_free = i;

		  /* Clear key and value.  */
		  set_hash_key_slot (h, i, HASH_UNUSED_ENTRY_KEY);
		  set_hash_value_slot (h, i, Qnil);

                  eassert (h->count != 0);
                  h->count--;
                }
	      else
		{
		  prev = i;
		}
	    }
	  else
	    {
	      if (!remove_p)
		{
		  /* Make sure key and value survive.  */
		  if (!key_known_to_survive_p)
		    {
		      mark_object (HASH_KEY (h, i));
                      marked = true;
		    }

		  if (!value_known_to_survive_p)
		    {
		      mark_object (HASH_VALUE (h, i));
                      marked = true;
		    }
		}
	    }
	}
    }

  return marked;
}


/***********************************************************************
			Hash Code Computation
 ***********************************************************************/

/* Maximum depth up to which to dive into Lisp structures.  */

#define SXHASH_MAX_DEPTH 3

/* Maximum length up to which to take list and vector elements into
   account.  */

#define SXHASH_MAX_LEN   7

/* Return a hash for string PTR which has length LEN.  The hash value
   can be any EMACS_UINT value.  */

EMACS_UINT
hash_char_array (char const *ptr, ptrdiff_t len)
{
  char const *p   = ptr;
  char const *end = ptr + len;
  EMACS_UINT hash = len;
  /* At most 8 steps.  We could reuse SXHASH_MAX_LEN, of course,
   * but dividing by 8 is cheaper.  */
  ptrdiff_t step = max (sizeof hash, ((end - p) >> 3));

  if (p + sizeof hash <= end)
    {
      do
	{
	  EMACS_UINT c;
	  /* We presume that the compiler will replace this `memcpy` with
	     a single load/move instruction when applicable.  */
	  memcpy (&c, p, sizeof hash);
	  p += step;
	  hash = sxhash_combine (hash, c);
	}
      while (p + sizeof hash <= end);
      /* Hash the last word's worth of bytes in the string, because that is
         is often the part where strings differ.  This may cause some
         bytes to be hashed twice but we assume that's not a big problem.  */
      EMACS_UINT c;
      memcpy (&c, end - sizeof c, sizeof c);
      hash = sxhash_combine (hash, c);
    }
  else
    {
      /* String is shorter than an EMACS_UINT.  Use smaller loads.  */
      eassume (p <= end && end - p < sizeof (EMACS_UINT));
      EMACS_UINT tail = 0;
      static_assert (sizeof tail <= 8);
#if EMACS_INT_MAX > INT32_MAX
      if (end - p >= 4)
	{
	  uint32_t c;
	  memcpy (&c, p, sizeof c);
	  tail = (tail << (8 * sizeof c)) + c;
	  p += sizeof c;
	}
#endif
      if (end - p >= 2)
	{
	  uint16_t c;
	  memcpy (&c, p, sizeof c);
	  tail = (tail << (8 * sizeof c)) + c;
	  p += sizeof c;
	}
      if (p < end)
	tail = (tail << 8) + (unsigned char)*p;
      hash = sxhash_combine (hash, tail);
    }

  return hash;
}

/* Return a hash for the floating point value VAL.  */

static EMACS_UINT
sxhash_float (double val)
{
  EMACS_UINT hash = 0;
  union double_and_words u = { .val = val };
  for (int i = 0; i < WORDS_PER_DOUBLE; i++)
    hash = sxhash_combine (hash, u.word[i]);
  return hash;
}

/* Return a hash for list LIST.  DEPTH is the current depth in the
   list.  We don't recurse deeper than SXHASH_MAX_DEPTH in it.  */

static EMACS_UINT
sxhash_list (Lisp_Object list, int depth)
{
  EMACS_UINT hash = 0;
  int i;

  if (depth < SXHASH_MAX_DEPTH)
    for (i = 0;
	 CONSP (list) && i < SXHASH_MAX_LEN;
	 list = XCDR (list), ++i)
      {
	EMACS_UINT hash2 = sxhash_obj (XCAR (list), depth + 1);
	hash = sxhash_combine (hash, hash2);
      }

  if (!NILP (list))
    {
      EMACS_UINT hash2 = sxhash_obj (list, depth + 1);
      hash = sxhash_combine (hash, hash2);
    }

  return hash;
}


/* Return a hash for (pseudo)vector VECTOR.  DEPTH is the current depth in
   the Lisp structure.  */

static EMACS_UINT
sxhash_vector (Lisp_Object vec, int depth)
{
  EMACS_UINT hash = ASIZE (vec);
  int i, n;

  n = min (SXHASH_MAX_LEN, hash & PSEUDOVECTOR_FLAG ? PVSIZE (vec) : hash);
  for (i = 0; i < n; ++i)
    {
      EMACS_UINT hash2 = sxhash_obj (AREF (vec, i), depth + 1);
      hash = sxhash_combine (hash, hash2);
    }

  return hash;
}

/* Return a hash for bool-vector VECTOR.  */

static EMACS_UINT
sxhash_bool_vector (Lisp_Object vec)
{
  EMACS_INT size = bool_vector_size (vec);
  EMACS_UINT hash = size;
  int i, n;

  n = min (SXHASH_MAX_LEN, bool_vector_words (size));
  for (i = 0; i < n; ++i)
    hash = sxhash_combine (hash, bool_vector_data (vec)[i]);

  return hash;
}

/* Return a hash for a bignum.  */

static EMACS_UINT
sxhash_bignum (Lisp_Object bignum)
{
  mpz_t const *n = xbignum_val (bignum);
  size_t i, nlimbs = mpz_size (*n);
  EMACS_UINT hash = mpz_sgn(*n) < 0;

  for (i = 0; i < nlimbs; ++i)
    hash = sxhash_combine (hash, mpz_getlimbn (*n, i));

  return hash;
}

EMACS_UINT
sxhash (Lisp_Object obj)
{
  return sxhash_obj (obj, 0);
}

/* Return a hash code for OBJ.  DEPTH is the current depth in the Lisp
   structure.  */

static EMACS_UINT
sxhash_obj (Lisp_Object obj, int depth)
{
  if (depth > SXHASH_MAX_DEPTH)
    return 0;

  switch (XTYPE (obj))
    {
    case Lisp_Int0:
    case Lisp_Int1:
      return XUFIXNUM (obj);

    case Lisp_Symbol:
      return XHASH (obj);

    case Lisp_String:
      return hash_char_array (SSDATA (obj), SBYTES (obj));

    case Lisp_Vectorlike:
      {
	enum pvec_type pvec_type = PSEUDOVECTOR_TYPE (XVECTOR (obj));
	if (! (PVEC_NORMAL_VECTOR < pvec_type && pvec_type < PVEC_CLOSURE))
	  {
	    /* According to the CL HyperSpec, two arrays are equal only if
	       they are 'eq', except for strings and bit-vectors.  In
	       Emacs, this works differently.  We have to compare element
	       by element.  Same for pseudovectors that internal_equal
	       examines the Lisp contents of.  */
	    return (SUB_CHAR_TABLE_P (obj)
	            /* 'sxhash_vector' can't be applies to a sub-char-table and
	              it's probably not worth looking into them anyway!  */
	            ? 42
	            : sxhash_vector (obj, depth));
	  }
	/* FIXME: Use `switch`.  */
	else if (pvec_type == PVEC_BIGNUM)
	  return sxhash_bignum (obj);
	else if (pvec_type == PVEC_MARKER)
	  {
	    ptrdiff_t bytepos
	      = XMARKER (obj)->buffer ? XMARKER (obj)->bytepos : 0;
	    EMACS_UINT hash
	      = sxhash_combine ((intptr_t) XMARKER (obj)->buffer, bytepos);
	    return hash;
	  }
	else if (pvec_type == PVEC_BOOL_VECTOR)
	  return sxhash_bool_vector (obj);
	else if (pvec_type == PVEC_OVERLAY)
	  {
	    EMACS_UINT hash = OVERLAY_START (obj);
	    hash = sxhash_combine (hash, OVERLAY_END (obj));
	    hash = sxhash_combine (hash, sxhash_obj (XOVERLAY (obj)->plist, depth));
	    return hash;
	  }
	else
	  {
	    if (symbols_with_pos_enabled && pvec_type == PVEC_SYMBOL_WITH_POS)
	      obj = XSYMBOL_WITH_POS_SYM (obj);

	    /* Others are 'equal' if they are 'eq', so take their
	       address as hash.  */
	    return XHASH (obj);
	  }
      }

    case Lisp_Cons:
      return sxhash_list (obj, depth);

    case Lisp_Float:
      return sxhash_float (XFLOAT_DATA (obj));

    default:
      emacs_abort ();
    }
}

static void
hash_interval (INTERVAL interval, void *arg)
{
  EMACS_UINT *phash = arg;
  EMACS_UINT hash = *phash;
  hash = sxhash_combine (hash, interval->position);
  hash = sxhash_combine (hash, LENGTH (interval));
  hash = sxhash_combine (hash, sxhash_obj (interval->plist, 0));
  *phash = hash;
}

static void
collect_interval (INTERVAL interval, void *arg)
{
  Lisp_Object *collector = arg;
  *collector = Fcons (list3 (make_fixnum (interval->position),
			     make_fixnum (interval->position
					  + LENGTH (interval)),
			     interval->plist),
		      *collector);
}



/***********************************************************************
			    Lisp Interface
 ***********************************************************************/

/* Reduce the hash value X to a Lisp fixnum.  */
static inline Lisp_Object
reduce_emacs_uint_to_fixnum (EMACS_UINT x)
{
  return make_ufixnum (SXHASH_REDUCE (x));
}

DEFUN ("sxhash-eq", Fsxhash_eq, Ssxhash_eq, 1, 1, 0,
       doc: /* Return an integer hash code for OBJ suitable for `eq'.
If (eq A B), then (= (sxhash-eq A) (sxhash-eq B)).

Hash codes are not guaranteed to be preserved across Emacs sessions.  */)
  (Lisp_Object obj)
{
  return reduce_emacs_uint_to_fixnum (sxhash_eq (obj));
}

DEFUN ("sxhash-eql", Fsxhash_eql, Ssxhash_eql, 1, 1, 0,
       doc: /* Return an integer hash code for OBJ suitable for `eql'.
If (eql A B), then (= (sxhash-eql A) (sxhash-eql B)), but the opposite
isn't necessarily true.

Hash codes are not guaranteed to be preserved across Emacs sessions.  */)
  (Lisp_Object obj)
{
  return reduce_emacs_uint_to_fixnum (sxhash_eql (obj));
}

DEFUN ("sxhash-equal", Fsxhash_equal, Ssxhash_equal, 1, 1, 0,
       doc: /* Return an integer hash code for OBJ suitable for `equal'.
If (equal A B), then (= (sxhash-equal A) (sxhash-equal B)), but the
opposite isn't necessarily true.

Hash codes are not guaranteed to be preserved across Emacs sessions.  */)
  (Lisp_Object obj)
{
  return reduce_emacs_uint_to_fixnum (sxhash (obj));
}

DEFUN ("sxhash-equal-including-properties", Fsxhash_equal_including_properties,
       Ssxhash_equal_including_properties, 1, 1, 0,
       doc: /* Return an integer hash code for OBJ suitable for
`equal-including-properties'.
If (sxhash-equal-including-properties A B), then
(= (sxhash-equal-including-properties A) (sxhash-equal-including-properties B)).

Hash codes are not guaranteed to be preserved across Emacs sessions.  */)
  (Lisp_Object obj)
{
  EMACS_UINT hash = sxhash (obj);
  if (STRINGP (obj))
    traverse_intervals (string_intervals (obj), 0, hash_interval, &hash);
  return reduce_emacs_uint_to_fixnum (hash);
}


/* This is a cache of hash_table_test structures so that they can be
   shared between hash tables using the same test.
   FIXME: This way of storing and looking up hash_table_test structs
   isn't wonderful.  Find a better solution.  */
struct hash_table_user_test
{
  struct hash_table_test test;
  struct hash_table_user_test *next;
};

static struct hash_table_user_test *hash_table_user_tests = NULL;

void
mark_fns (void)
{
  for (struct hash_table_user_test *ut = hash_table_user_tests;
       ut; ut = ut->next)
    {
      mark_object (ut->test.name);
      mark_object (ut->test.user_cmp_function);
      mark_object (ut->test.user_hash_function);
    }
}

/* Find the hash_table_test object corresponding to the (bare) symbol TEST,
   creating one if none existed.  */
static struct hash_table_test *
get_hash_table_user_test (Lisp_Object test)
{
  Lisp_Object prop = Fget (test, Qhash_table_test);
  if (!CONSP (prop) || !CONSP (XCDR (prop)))
    signal_error ("Invalid hash table test", test);

  Lisp_Object equal_fn = XCAR (prop);
  Lisp_Object hash_fn = XCAR (XCDR (prop));
  struct hash_table_user_test *ut = hash_table_user_tests;
  while (ut && !(BASE_EQ (test, ut->test.name)
		 && EQ (equal_fn, ut->test.user_cmp_function)
		 && EQ (hash_fn, ut->test.user_hash_function)))
    ut = ut->next;
  if (!ut)
    {
      ut = xmalloc (sizeof *ut);
      ut->test.name = test;
      ut->test.user_cmp_function = equal_fn;
      ut->test.user_hash_function = hash_fn;
      ut->test.hashfn = hashfn_user_defined;
      ut->test.cmpfn = cmpfn_user_defined;
      ut->next = hash_table_user_tests;
      hash_table_user_tests = ut;
    }
  return &ut->test;
}

DEFUN ("make-hash-table", Fmake_hash_table, Smake_hash_table, 0, MANY, 0,
       doc: /* Create and return a new hash table.

Arguments are specified as keyword/argument pairs.  The following
arguments are defined:

:test TEST -- TEST must be a symbol that specifies how to compare
keys.  Default is `eql'.  Predefined are the tests `eq', `eql', and
`equal'.  User-supplied test and hash functions can be specified via
`define-hash-table-test'.

:size SIZE -- A hint as to how many elements will be put in the table.
The table will always grow as needed; this argument may help performance
slightly if the size is known in advance but is never required.

:weakness WEAK -- WEAK must be one of nil, t, `key', `value',
`key-or-value', or `key-and-value'.  If WEAK is not nil, the table
returned is a weak table.  Key/value pairs are removed from a weak
hash table when there are no non-weak references pointing to their
key, value, one of key or value, or both key and value, depending on
WEAK.  WEAK t is equivalent to `key-and-value'.  Default value of WEAK
is nil.

The keywords arguments :rehash-threshold, :rehash-size, and :purecopy
are obsolete and ignored.

usage: (make-hash-table &rest KEYWORD-ARGS)  */)
  (ptrdiff_t nargs, Lisp_Object *args)
{
  Lisp_Object test_arg = Qnil;
  Lisp_Object weakness_arg = Qnil;
  Lisp_Object size_arg = Qnil;

  if (nargs & 1)
    error ("Odd number of arguments");
  while (nargs >= 2)
    {
      Lisp_Object arg = maybe_remove_pos_from_symbol (args[--nargs]);
      Lisp_Object kw = maybe_remove_pos_from_symbol (args[--nargs]);
      if (BASE_EQ (kw, QCtest))
	test_arg = arg;
      else if (BASE_EQ (kw, QCweakness))
	weakness_arg = arg;
      else if (BASE_EQ (kw, QCsize))
	size_arg = arg;
      else if (BASE_EQ (kw, QCrehash_threshold) || BASE_EQ (kw, QCrehash_size)
	       || BASE_EQ (kw, QCpurecopy))
	;  /* ignore obsolete keyword arguments */
      else
	signal_error ("Invalid keyword argument", kw);
    }

  const struct hash_table_test *test;
  if (NILP (test_arg) || BASE_EQ (test_arg, Qeql))
    test = &hashtest_eql;
  else if (BASE_EQ (test_arg, Qeq))
    test = &hashtest_eq;
  else if (BASE_EQ (test_arg, Qequal))
    test = &hashtest_equal;
  else
    test = get_hash_table_user_test (test_arg);

  EMACS_INT size;
  if (NILP (size_arg))
    size = DEFAULT_HASH_SIZE;
  else if (FIXNATP (size_arg))
    size = XFIXNAT (size_arg);
  else
    signal_error ("Invalid hash table size", size_arg);

  hash_table_weakness_t weak;
  if (NILP (weakness_arg))
    weak = Weak_None;
  else if (BASE_EQ (weakness_arg, Qkey))
    weak = Weak_Key;
  else if (BASE_EQ (weakness_arg, Qvalue))
    weak = Weak_Value;
  else if (BASE_EQ (weakness_arg, Qkey_or_value))
    weak = Weak_Key_Or_Value;
  else if (BASE_EQ (weakness_arg, Qt) || BASE_EQ (weakness_arg, Qkey_and_value))
    weak = Weak_Key_And_Value;
  else
    signal_error ("Invalid hash table weakness", weakness_arg);

  return make_hash_table (test, size, weak);
}


DEFUN ("copy-hash-table", Fcopy_hash_table, Scopy_hash_table, 1, 1, 0,
       doc: /* Return a copy of hash table TABLE.  */)
  (Lisp_Object table)
{
  return copy_hash_table (check_hash_table (table));
}


DEFUN ("hash-table-count", Fhash_table_count, Shash_table_count, 1, 1, 0,
       doc: /* Return the number of elements in TABLE.  */)
  (Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  return make_fixnum (h->count);
}


DEFUN ("hash-table-rehash-size", Fhash_table_rehash_size,
       Shash_table_rehash_size, 1, 1, 0,
       doc: /* Return the rehash size of TABLE.
This function is for compatibility only; it returns a nominal value
without current significance.  */)
  (Lisp_Object table)
{
  CHECK_HASH_TABLE (table);
  return make_float (1.5);  /* The old default rehash-size value.  */
}


DEFUN ("hash-table-rehash-threshold", Fhash_table_rehash_threshold,
       Shash_table_rehash_threshold, 1, 1, 0,
       doc: /* Return the rehash threshold of TABLE.
This function is for compatibility only; it returns a nominal value
without current significance.  */)
  (Lisp_Object table)
{
  CHECK_HASH_TABLE (table);
  return make_float (0.8125);  /* The old default rehash-threshold value.  */
}


DEFUN ("hash-table-size", Fhash_table_size, Shash_table_size, 1, 1, 0,
       doc: /* Return the current allocation size of TABLE.

This is probably not the function that you are looking for.  To get the
number of entries in a table, use `hash-table-count' instead.

The returned value is the number of entries that TABLE can currently
hold without growing, but since hash tables grow automatically, this
number is rarely of interest.  */)
  (Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  return make_fixnum (HASH_TABLE_SIZE (h));
}


DEFUN ("hash-table-test", Fhash_table_test, Shash_table_test, 1, 1, 0,
       doc: /* Return the test TABLE uses.  */)
  (Lisp_Object table)
{
  return check_hash_table (table)->test->name;
}

Lisp_Object
hash_table_weakness_symbol (hash_table_weakness_t weak)
{
  switch (weak)
    {
    case Weak_None:          return Qnil;
    case Weak_Key:           return Qkey;
    case Weak_Value:         return Qvalue;
    case Weak_Key_And_Value: return Qkey_and_value;
    case Weak_Key_Or_Value:  return Qkey_or_value;
    }
  emacs_abort ();
}

DEFUN ("hash-table-weakness", Fhash_table_weakness, Shash_table_weakness,
       1, 1, 0,
       doc: /* Return the weakness of TABLE.  */)
  (Lisp_Object table)
{
  return hash_table_weakness_symbol (check_hash_table (table)->weakness);
}


DEFUN ("hash-table-p", Fhash_table_p, Shash_table_p, 1, 1, 0,
       doc: /* Return t if OBJ is a Lisp hash table object.  */)
  (Lisp_Object obj)
{
  return HASH_TABLE_P (obj) ? Qt : Qnil;
}


DEFUN ("clrhash", Fclrhash, Sclrhash, 1, 1, 0,
       doc: /* Clear hash table TABLE and return it.  */)
  (Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  check_mutable_hash_table (table, h);
  hash_clear (h);
  /* Be compatible with XEmacs.  */
  return table;
}


DEFUN ("gethash", Fgethash, Sgethash, 2, 3, 0,
       doc: /* Look up KEY in TABLE and return its associated value.
If KEY is not found in table, return DEFAULT, or nil if DEFAULT is not
provided.

usage: (gethash KEY TABLE &optional DEFAULT)  */)
  (Lisp_Object key, Lisp_Object table, Lisp_Object dflt)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  ptrdiff_t i = hash_find (h, key);
  return i >= 0 ? HASH_VALUE (h, i) : dflt;
}


DEFUN ("puthash", Fputhash, Sputhash, 3, 3, 0,
       doc: /* Associate KEY with VALUE in hash table TABLE.
If KEY is already present in table, replace its current value with
VALUE.  In any case, return VALUE.  */)
  (Lisp_Object key, Lisp_Object value, Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  check_mutable_hash_table (table, h);

  EMACS_UINT hash = hash_from_key (h, key);
  ptrdiff_t i = hash_find_with_hash (h, key, hash);
  if (i >= 0)
    set_hash_value_slot (h, i, value);
  else
    hash_put (h, key, value, hash);

  return value;
}


DEFUN ("remhash", Fremhash, Sremhash, 2, 2, 0,
       doc: /* Remove KEY from TABLE.  */)
  (Lisp_Object key, Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  check_mutable_hash_table (table, h);
  hash_remove_from_table (h, key);
  return Qnil;
}


DEFUN ("maphash", Fmaphash, Smaphash, 2, 2, 0,
       doc: /* Call FUNCTION for all entries in hash table TABLE.
FUNCTION is called with two arguments, KEY and VALUE.
It should not alter TABLE in any way other than using `puthash' to
set a new value for KEY, or `remhash' to remove KEY.
`maphash' always returns nil.  */)
  (Lisp_Object function, Lisp_Object table)
{
  struct Lisp_Hash_Table *h = check_hash_table (table);
  /* We can't use DOHASH here since FUNCTION may violate the rules and
     we shouldn't crash as a result (although the effects are
     unpredictable).  */
  DOHASH_SAFE (h, i)
    calln (function, HASH_KEY (h, i), HASH_VALUE (h, i));
  return Qnil;
}


DEFUN ("define-hash-table-test", Fdefine_hash_table_test,
       Sdefine_hash_table_test, 3, 3, 0,
       doc: /* Define a new hash table test with name NAME, a symbol.

In hash tables created with NAME specified as test, use TEST to
compare keys, and HASH for computing hash codes of keys.

TEST must be a function taking two arguments and returning non-nil if
both arguments are the same.  HASH must be a function taking one
argument and returning an object that is the hash code of the argument.
It should be the case that if (eq (funcall HASH x1) (funcall HASH x2))
returns nil, then (funcall TEST x1 x2) also returns nil.  */)
  (Lisp_Object name, Lisp_Object test, Lisp_Object hash)
{
  return Fput (name, Qhash_table_test, list2 (test, hash));
}

DEFUN ("internal--hash-table-histogram",
       Finternal__hash_table_histogram,
       Sinternal__hash_table_histogram,
       1, 1, 0,
       doc: /* Bucket size histogram of HASH-TABLE.  Internal use only. */)
  (Lisp_Object hash_table)
{
  struct Lisp_Hash_Table *h = check_hash_table (hash_table);
  ptrdiff_t size = HASH_TABLE_SIZE (h);
  ptrdiff_t *freq = xzalloc (size * sizeof *freq);
  ptrdiff_t index_size = hash_table_index_size (h);
  for (ptrdiff_t i = 0; i < index_size; i++)
    {
      ptrdiff_t n = 0;
      for (ptrdiff_t j = HASH_INDEX (h, i); j != -1; j = HASH_NEXT (h, j))
	n++;
      if (n > 0)
	freq[n - 1]++;
    }
  Lisp_Object ret = Qnil;
  for (ptrdiff_t i = 0; i < size; i++)
    if (freq[i] > 0)
      ret = Fcons (Fcons (make_int (i + 1), make_int (freq[i])),
		   ret);
  xfree (freq);
  return Fnreverse (ret);
}

DEFUN ("internal--hash-table-buckets",
       Finternal__hash_table_buckets,
       Sinternal__hash_table_buckets,
       1, 1, 0,
       doc: /* (KEY . HASH) in HASH-TABLE, grouped by bucket.
Internal use only. */)
  (Lisp_Object hash_table)
{
  struct Lisp_Hash_Table *h = check_hash_table (hash_table);
  Lisp_Object ret = Qnil;
  ptrdiff_t index_size = hash_table_index_size (h);
  for (ptrdiff_t i = 0; i < index_size; i++)
    {
      Lisp_Object bucket = Qnil;
      for (ptrdiff_t j = HASH_INDEX (h, i); j != -1; j = HASH_NEXT (h, j))
	bucket = Fcons (Fcons (HASH_KEY (h, j), make_int (HASH_HASH (h, j))),
			bucket);
      if (!NILP (bucket))
	ret = Fcons (Fnreverse (bucket), ret);
    }
  return Fnreverse (ret);
}

DEFUN ("internal--hash-table-index-size",
       Finternal__hash_table_index_size,
       Sinternal__hash_table_index_size,
       1, 1, 0,
       doc: /* Index size of HASH-TABLE.  Internal use only. */)
  (Lisp_Object hash_table)
{
  struct Lisp_Hash_Table *h = check_hash_table (hash_table);
  return make_int (hash_table_index_size (h));
}


/************************************************************************
			MD5, SHA-1, and SHA-2
 ************************************************************************/

#include "md5.h"
#include "sha1.h"
#include "sha256.h"
#include "sha512.h"

/* Store into HEXBUF an unterminated hexadecimal character string
   representing DIGEST, which is binary data of size DIGEST_SIZE bytes.
   HEXBUF might equal DIGEST.  */
void
hexbuf_digest (char *hexbuf, void const *digest, int digest_size)
{
  unsigned char const *p = digest;

  for (int i = digest_size - 1; i >= 0; i--)
    {
      static char const hexdigit[16] ATTRIBUTE_NONSTRING = "0123456789abcdef";
      int p_i = p[i];
      hexbuf[2 * i] = hexdigit[p_i >> 4];
      hexbuf[2 * i + 1] = hexdigit[p_i & 0xf];
    }
}

static Lisp_Object
make_digest_string (Lisp_Object digest, int digest_size)
{
  hexbuf_digest (SSDATA (digest), SDATA (digest), digest_size);
  return digest;
}

DEFUN ("secure-hash-algorithms", Fsecure_hash_algorithms,
       Ssecure_hash_algorithms, 0, 0, 0,
       doc: /* Return a list of all the supported `secure-hash' algorithms. */)
  (void)
{
  return list (Qmd5, Qsha1, Qsha224, Qsha256, Qsha384, Qsha512);
}

/* Extract data from a string or a buffer. SPEC is a list of
(BUFFER-OR-STRING-OR-SYMBOL START END CODING-SYSTEM NOERROR) which behave as
specified with `secure-hash' and in Info node
`(elisp)Format of GnuTLS Cryptography Inputs'.  */
char *
extract_data_from_object (Lisp_Object spec,
                          ptrdiff_t *start_byte,
                          ptrdiff_t *end_byte)
{
  Lisp_Object object = XCAR (spec);

  if (CONSP (spec)) spec = XCDR (spec);
  Lisp_Object start = CAR_SAFE (spec);

  if (CONSP (spec)) spec = XCDR (spec);
  Lisp_Object end = CAR_SAFE (spec);

  if (CONSP (spec)) spec = XCDR (spec);
  Lisp_Object coding_system = CAR_SAFE (spec);

  if (CONSP (spec)) spec = XCDR (spec);
  Lisp_Object noerror = CAR_SAFE (spec);

  if (STRINGP (object))
    {
      if (NILP (coding_system))
	{
	  /* Decide the coding-system to encode the data with.  */

	  if (STRING_MULTIBYTE (object))
	    /* use default, we can't guess correct value */
	    coding_system = preferred_coding_system ();
	  else
	    coding_system = Qraw_text;
	}

      if (NILP (Fcoding_system_p (coding_system)))
	{
	  /* Invalid coding system.  */

	  if (!NILP (noerror))
	    coding_system = Qraw_text;
	  else
	    xsignal1 (Qcoding_system_error, coding_system);
	}

      if (STRING_MULTIBYTE (object))
	object = code_convert_string (object, coding_system,
				      Qnil, true, false, true);

      ptrdiff_t size = SCHARS (object), start_char, end_char;
      validate_subarray (object, start, end, size, &start_char, &end_char);

      *start_byte = !start_char ? 0 : string_char_to_byte (object, start_char);
      *end_byte = (end_char == size
                   ? SBYTES (object)
                   : string_char_to_byte (object, end_char));
    }
  else if (BUFFERP (object))
    {
      struct buffer *prev = current_buffer;
      EMACS_INT b, e;

      record_unwind_current_buffer ();

      struct buffer *bp = XBUFFER (object);
      set_buffer_internal (bp);

      b = !NILP (start) ? fix_position (start) : BEGV;
      e = !NILP (end) ? fix_position (end) : ZV;
      if (b > e)
	{
	  EMACS_INT temp = b;
	  b = e;
	  e = temp;
	}

      if (!(BEGV <= b && e <= ZV))
	args_out_of_range (start, end);

      if (NILP (coding_system))
	{
	  /* Decide the coding-system to encode the data with.
	     See fileio.c:Fwrite-region */

	  if (!NILP (Vcoding_system_for_write))
	    coding_system = Vcoding_system_for_write;
	  else
	    {
	      bool force_raw_text = false;

	      coding_system = BVAR (XBUFFER (object), buffer_file_coding_system);
	      if (NILP (coding_system)
		  || NILP (Flocal_variable_p (Qbuffer_file_coding_system, Qnil)))
		{
		  coding_system = Qnil;
		  if (NILP (BVAR (current_buffer, enable_multibyte_characters)))
		    force_raw_text = true;
		}

	      if (NILP (coding_system) && !NILP (Fbuffer_file_name (object)))
		{
		  /* Check file-coding-system-alist.  */
		  Lisp_Object val = CALLN (Ffind_operation_coding_system,
					   Qwrite_region,
					   make_fixnum (b), make_fixnum (e),
					   Fbuffer_file_name (object));
		  if (CONSP (val) && !NILP (XCDR (val)))
		    coding_system = XCDR (val);
		}

	      if (NILP (coding_system)
		  && !NILP (BVAR (XBUFFER (object), buffer_file_coding_system)))
		{
		  /* If we still have not decided a coding system, use the
		     default value of buffer-file-coding-system.  */
		  coding_system = BVAR (XBUFFER (object), buffer_file_coding_system);
		}

	      if (!force_raw_text
		  && !NILP (Ffboundp (Vselect_safe_coding_system_function)))
		/* Confirm that VAL can surely encode the current region.  */
		coding_system = calln (Vselect_safe_coding_system_function,
				       make_fixnum (b), make_fixnum (e),
				       coding_system, Qnil);

	      if (force_raw_text)
		coding_system = Qraw_text;
	    }

	  if (NILP (Fcoding_system_p (coding_system)))
	    {
	      /* Invalid coding system.  */

	      if (!NILP (noerror))
		coding_system = Qraw_text;
	      else
		xsignal1 (Qcoding_system_error, coding_system);
	    }
	}

      object = make_buffer_string (b, e, false);
      set_buffer_internal (prev);
      /* Discard the unwind protect for recovering the current
	 buffer.  */
      specpdl_ptr--;

      if (STRING_MULTIBYTE (object))
	object = code_convert_string (object, coding_system,
				      Qnil, true, false, false);
      *start_byte = 0;
      *end_byte = SBYTES (object);
    }
  else if (EQ (object, Qiv_auto))
    {
      /* Format: (iv-auto REQUIRED-LENGTH).  */

      if (! FIXNATP (start))
        error ("Without a length, `iv-auto' can't be used; see Elisp manual");
      else
        {
	  EMACS_INT start_hold = XFIXNAT (start);
          object = make_uninit_string (start_hold);
	  char *lim = SSDATA (object) + start_hold;
	  for (char *p = SSDATA (object); p < lim; p++)
	    {
	      ssize_t gotten = getrandom (p, lim - p, 0);
	      if (0 <= gotten)
		p += gotten;
	      else if (errno != EINTR)
		report_file_error ("Getting random data", Qnil);
	    }

          *start_byte = 0;
          *end_byte = start_hold;
        }
    }

  if (!STRINGP (object))
    signal_error ("Invalid object argument",
		  NILP (object) ? build_string ("nil") : object);
  return SSDATA (object);
}


/* ALGORITHM is a symbol: md5, sha1, sha224 and so on. */

static Lisp_Object
secure_hash (Lisp_Object algorithm, Lisp_Object object, Lisp_Object start,
	     Lisp_Object end, Lisp_Object coding_system, Lisp_Object noerror,
	     Lisp_Object binary)
{
  ptrdiff_t start_byte, end_byte;
  int digest_size;
  void *(*hash_func) (const char *, size_t, void *);
  Lisp_Object digest;

  CHECK_SYMBOL (algorithm);

  Lisp_Object spec = list5 (object, start, end, coding_system, noerror);

  const char *input = extract_data_from_object (spec, &start_byte, &end_byte);

  if (input == NULL)
    error ("secure_hash: Failed to extract data from object, aborting!");

  if (EQ (algorithm, Qmd5))
    {
      digest_size = MD5_DIGEST_SIZE;
      hash_func	  = md5_buffer;
    }
  else if (EQ (algorithm, Qsha1))
    {
      digest_size = SHA1_DIGEST_SIZE;
      hash_func	  = sha1_buffer;
    }
  else if (EQ (algorithm, Qsha224))
    {
      digest_size = SHA224_DIGEST_SIZE;
      hash_func	  = sha224_buffer;
    }
  else if (EQ (algorithm, Qsha256))
    {
      digest_size = SHA256_DIGEST_SIZE;
      hash_func	  = sha256_buffer;
    }
  else if (EQ (algorithm, Qsha384))
    {
      digest_size = SHA384_DIGEST_SIZE;
      hash_func	  = sha384_buffer;
    }
  else if (EQ (algorithm, Qsha512))
    {
      digest_size = SHA512_DIGEST_SIZE;
      hash_func	  = sha512_buffer;
    }
  else
    error ("Invalid algorithm arg: %s", SDATA (Fsymbol_name (algorithm)));

  /* allocate 2 x digest_size so that it can be reused to hold the
     hexified value */
  digest = make_uninit_string (digest_size * 2);

  hash_func (input + start_byte,
	     end_byte - start_byte,
	     SSDATA (digest));

  if (NILP (binary))
    return make_digest_string (digest, digest_size);
  else
    return make_unibyte_string (SSDATA (digest), digest_size);
}

DEFUN ("md5", Fmd5, Smd5, 1, 5, 0,
       doc: /* Return MD5 message digest of OBJECT, a buffer or string.

A message digest is the string representation of the cryptographic checksum
of a document, and the algorithm to calculate it is defined in RFC 1321.
The MD5 digest is 32-character long.

The two optional arguments START and END are character positions
specifying for which part of OBJECT the message digest should be
computed.  If nil or omitted, the digest is computed for the whole
OBJECT.

The MD5 message digest is computed from the result of encoding the
text in a coding system, not directly from the internal Emacs form of
the text.  The optional fourth argument CODING-SYSTEM specifies which
coding system to encode the text with.  It should be the same coding
system that you used or will use when actually writing the text into a
file.

If CODING-SYSTEM is nil or omitted, the default depends on OBJECT.  If
OBJECT is a buffer, the default for CODING-SYSTEM is whatever coding
system would be chosen by default for writing this text into a file.

If OBJECT is a string, the most preferred coding system (see the
command `prefer-coding-system') is used.

If NOERROR is non-nil, silently assume the `raw-text' coding if the
guesswork fails.  Normally, an error is signaled in such case.

This function is semi-obsolete, since for most purposes it is equivalent
to calling `secure-hash` with the symbol `md5' as the ALGORITHM
argument.  The OBJECT, START and END arguments have the same meanings as
in `secure-hash'.

Note that MD5 is not collision resistant and should not be used for
anything security-related.  See `secure-hash' for alternatives.  */)
  (Lisp_Object object, Lisp_Object start, Lisp_Object end, Lisp_Object coding_system, Lisp_Object noerror)
{
  return secure_hash (Qmd5, object, start, end, coding_system, noerror, Qnil);
}

DEFUN ("secure-hash", Fsecure_hash, Ssecure_hash, 2, 5, 0,
       doc: /* Return the secure hash of OBJECT, a buffer or string.
ALGORITHM is a symbol specifying the hash to use:
- md5    corresponds to MD5, produces a 32-character signature
- sha1   corresponds to SHA-1, produces a 40-character signature
- sha224 corresponds to SHA-2 (SHA-224), produces a 56-character signature
- sha256 corresponds to SHA-2 (SHA-256), produces a 64-character signature
- sha384 corresponds to SHA-2 (SHA-384), produces a 96-character signature
- sha512 corresponds to SHA-2 (SHA-512), produces a 128-character signature

The two optional arguments START and END are positions specifying for
which part of OBJECT to compute the hash.  If nil or omitted, uses the
whole OBJECT.

The full list of algorithms can be obtained with `secure-hash-algorithms'.

If BINARY is non-nil, returns a string in binary form.  In this case,
the function returns a unibyte string whose length is half the number
of characters it returns when BINARY is nil.

Note that MD5 and SHA-1 are not collision resistant and should not be
used for anything security-related.  For these applications, use one
of the other hash types instead, e.g. sha256 or sha512.  */)
  (Lisp_Object algorithm, Lisp_Object object, Lisp_Object start, Lisp_Object end, Lisp_Object binary)
{
  return secure_hash (algorithm, object, start, end, Qnil, Qnil, binary);
}

DEFUN ("buffer-hash", Fbuffer_hash, Sbuffer_hash, 0, 1, 0,
       doc: /* Return a hash of the contents of BUFFER-OR-NAME.
This hash is performed on the raw internal format of the buffer,
disregarding any coding systems.  If nil, use the current buffer.

This function is useful for comparing two buffers running in the same
Emacs, but is not guaranteed to return the same hash between different
Emacs versions.  It should be somewhat more efficient on larger
buffers than `secure-hash' is, and should not allocate more memory.

It should not be used for anything security-related.  See
`secure-hash' for these applications.  */ )
  (Lisp_Object buffer_or_name)
{
  Lisp_Object buffer;
  struct buffer *b;
  struct sha1_ctx ctx;

  if (NILP (buffer_or_name))
    buffer = Fcurrent_buffer ();
  else
    buffer = Fget_buffer (buffer_or_name);
  if (NILP (buffer))
    nsberror (buffer_or_name);

  b = XBUFFER (buffer);
  sha1_init_ctx (&ctx);

  /* Process the first part of the buffer. */
  sha1_process_bytes (BUF_BEG_ADDR (b),
		      BUF_GPT_BYTE (b) - BUF_BEG_BYTE (b),
		      &ctx);

  /* If the gap is before the end of the buffer, process the last half
     of the buffer. */
  if (BUF_GPT_BYTE (b) < BUF_Z_BYTE (b))
    sha1_process_bytes (BUF_GAP_END_ADDR (b),
			BUF_Z_ADDR (b) - BUF_GAP_END_ADDR (b),
			&ctx);

  Lisp_Object digest = make_uninit_string (SHA1_DIGEST_SIZE * 2);
  sha1_finish_ctx (&ctx, SSDATA (digest));
  return make_digest_string (digest, SHA1_DIGEST_SIZE);
}

DEFUN ("buffer-line-statistics", Fbuffer_line_statistics,
       Sbuffer_line_statistics, 0, 1, 0,
       doc: /* Return data about lines in BUFFER.
The data is returned as a list, and the first element is the number of
lines in the buffer, the second is the length of the longest line, and
the third is the mean line length.  The lengths returned are in bytes, not
characters.  */ )
  (Lisp_Object buffer_or_name)
{
  Lisp_Object buffer;
  ptrdiff_t lines = 0, longest = 0;
  double mean = 0;
  struct buffer *b;

  if (NILP (buffer_or_name))
    buffer = Fcurrent_buffer ();
  else
    buffer = Fget_buffer (buffer_or_name);
  if (NILP (buffer))
    nsberror (buffer_or_name);

  b = XBUFFER (buffer);

  unsigned char *start = BUF_BEG_ADDR (b);
  ptrdiff_t area = BUF_GPT_BYTE (b) - BUF_BEG_BYTE (b), pre_gap = 0;

  /* Process the first part of the buffer. */
  while (area > 0)
    {
      unsigned char *n = memchr (start, '\n', area);

      if (n)
	{
	  ptrdiff_t this_line = n - start;
	  if (this_line > longest)
	    longest = this_line;
	  lines++;
	  /* Blame Knuth. */
	  mean = mean + (this_line - mean) / lines;
	  area = area - this_line - 1;
	  start += this_line + 1;
	}
      else
	{
	  /* Didn't have a newline here, so save the rest for the
	     post-gap calculation. */
	  pre_gap = area;
	  area = 0;
	}
    }

  /* If the gap is before the end of the buffer, process the last half
     of the buffer. */
  if (BUF_GPT_BYTE (b) < BUF_Z_BYTE (b))
    {
      start = BUF_GAP_END_ADDR (b);
      area = BUF_Z_ADDR (b) - BUF_GAP_END_ADDR (b);

      while (area > 0)
	{
	  unsigned char *n = memchr (start, '\n', area);
	  ptrdiff_t this_line = n? n - start + pre_gap: area + pre_gap;

	  if (this_line > longest)
	    longest = this_line;
	  lines++;
	  /* Blame Knuth again. */
	  mean = mean + (this_line - mean) / lines;
	  area = area - this_line - 1;
	  start += this_line + 1;
	  pre_gap = 0;
	}
    }
  else if (pre_gap > 0)
    {
      if (pre_gap > longest)
	longest = pre_gap;
      lines++;
      mean = mean + (pre_gap - mean) / lines;
    }

  return list3 (make_int (lines), make_int (longest), make_float (mean));
}

DEFUN ("string-search", Fstring_search, Sstring_search, 2, 3, 0,
       doc: /* Search for the string NEEDLE in the string HAYSTACK.
The return value is the position of the first occurrence of NEEDLE in
HAYSTACK, or nil if no match was found.

The optional START-POS argument says where to start searching in
HAYSTACK and defaults to zero (start at the beginning).
It must be between zero and the length of HAYSTACK, inclusive.

Case is always significant and text properties are ignored. */)
  (register Lisp_Object needle, Lisp_Object haystack, Lisp_Object start_pos)
{
  ptrdiff_t start_byte = 0, haybytes;
  char *res, *haystart;
  EMACS_INT start = 0;

  CHECK_STRING (needle);
  CHECK_STRING (haystack);

  if (!NILP (start_pos))
    {
      CHECK_FIXNUM (start_pos);
      start = XFIXNUM (start_pos);
      if (start < 0 || start > SCHARS (haystack))
        xsignal1 (Qargs_out_of_range, start_pos);
      start_byte = string_char_to_byte (haystack, start);
    }

  /* If NEEDLE is longer than (the remaining length of) haystack, then
     we can't have a match, and return early.  */
  if (SCHARS (needle) > SCHARS (haystack) - start)
    return Qnil;

  haystart = SSDATA (haystack) + start_byte;
  haybytes = SBYTES (haystack) - start_byte;

  /* We can do a direct byte-string search if both strings have the
     same multibyteness, or if the needle consists of ASCII characters only.  */
  if (STRING_MULTIBYTE (haystack)
      ? (STRING_MULTIBYTE (needle)
         || SCHARS (haystack) == SBYTES (haystack) || string_ascii_p (needle))
      : (!STRING_MULTIBYTE (needle)
         || SCHARS (needle) == SBYTES (needle)))
    {
      if (STRING_MULTIBYTE (haystack) && STRING_MULTIBYTE (needle)
          && SCHARS (haystack) == SBYTES (haystack)
          && SCHARS (needle) != SBYTES (needle))
        /* Multibyte non-ASCII needle, multibyte ASCII haystack: impossible.  */
        return Qnil;
      else
        res = memmem (haystart, haybytes,
                      SSDATA (needle), SBYTES (needle));
    }
  else if (STRING_MULTIBYTE (haystack))  /* unibyte non-ASCII needle */
    {
      Lisp_Object multi_needle = string_to_multibyte (needle);
      res = memmem (haystart, haybytes,
		    SSDATA (multi_needle), SBYTES (multi_needle));
    }
  else              /* unibyte haystack, multibyte non-ASCII needle */
    {
      /* The only possible way we can find the multibyte needle in the
	 unibyte stack (since we know that the needle is non-ASCII) is
	 if they contain "raw bytes" (and no other non-ASCII chars.)  */
      ptrdiff_t nbytes = SBYTES (needle);
      for (ptrdiff_t i = 0; i < nbytes; i++)
        {
          int c = SREF (needle, i);
          if (CHAR_BYTE8_HEAD_P (c))
            i++;                /* Skip raw byte.  */
          else if (!ASCII_CHAR_P (c))
            return Qnil;  /* Found a char that can't be in the haystack.  */
        }

      /* "Raw bytes" (aka eighth-bit) are represented differently in
         multibyte and unibyte strings.  */
      Lisp_Object uni_needle = Fstring_to_unibyte (needle);
      res = memmem (haystart, haybytes,
                    SSDATA (uni_needle), SBYTES (uni_needle));
    }

  if (! res)
    return Qnil;

  return make_int (string_byte_to_char (haystack, res - SSDATA (haystack)));
}

DEFUN ("object-intervals", Fobject_intervals, Sobject_intervals, 1, 1, 0,
       doc: /* Return a copy of the text properties of OBJECT.
OBJECT must be a buffer or a string.

Altering this copy does not change the layout of the text properties
in OBJECT.  */)
  (register Lisp_Object object)
{
  INTERVAL intervals;

  if (STRINGP (object))
    intervals = string_intervals (object);
  else if (BUFFERP (object))
    intervals = buffer_intervals (XBUFFER (object));
  else
    wrong_type_argument (Qbuffer_or_string_p, object);

  if (! intervals)
    return Qnil;

  Lisp_Object collector = Qnil;
  traverse_intervals (intervals, 0, collect_interval, &collector);
  return Fnreverse (collector);
}

DEFUN ("line-number-at-pos", Fline_number_at_pos,
       Sline_number_at_pos, 0, 2, 0,
       doc: /* Return the line number at POSITION in the current buffer.
If POSITION is nil or omitted, it defaults to point's position in the
current buffer.

If the buffer is narrowed, the return value by default counts the lines
from the beginning of the accessible portion of the buffer.  But if the
second optional argument ABSOLUTE is non-nil, the value counts the lines
from the absolute start of the buffer, disregarding the narrowing.  */)
  (register Lisp_Object position, Lisp_Object absolute)
{
  ptrdiff_t pos_byte, start_byte = BEGV_BYTE;

  if (!BUFFER_LIVE_P (current_buffer))
    error ("Attempt to count lines in a dead buffer");

  if (MARKERP (position))
    {
      /* We don't trust the byte position if the marker's buffer is
         not the current buffer.  */
      if (XMARKER (position)->buffer != current_buffer)
	pos_byte = CHAR_TO_BYTE (marker_position (position));
      else
	pos_byte = marker_byte_position (position);
    }
  else if (NILP (position))
    pos_byte = PT_BYTE;
  else
    {
      CHECK_FIXNUM (position);
      ptrdiff_t pos = XFIXNUM (position);
      /* Check that POSITION is valid. */
      if (pos < BEG || pos > Z)
	args_out_of_range_3 (position, make_int (BEG), make_int (Z));
      pos_byte = CHAR_TO_BYTE (pos);
    }

  if (!NILP (absolute))
    start_byte = BEG_BYTE;
  else if (NILP (absolute))
    pos_byte = clip_to_bounds (BEGV_BYTE, pos_byte, ZV_BYTE);

  /* Check that POSITION is valid. */
  if (pos_byte < BEG_BYTE || pos_byte > Z_BYTE)
    args_out_of_range_3 (make_int (BYTE_TO_CHAR (pos_byte)),
			 make_int (BEG), make_int (Z));

  return make_int (count_lines (start_byte, pos_byte) + 1);
}


void
syms_of_fns (void)
{
  /* Hash table stuff.  */
  DEFSYM (Qhash_table_p, "hash-table-p");
  DEFSYM (Qeq, "eq");
  DEFSYM (Qeql, "eql");
  DEFSYM (Qequal, "equal");
  DEFSYM (QCtest, ":test");
  DEFSYM (QCsize, ":size");
  DEFSYM (QCpurecopy, ":purecopy");
  DEFSYM (QCrehash_size, ":rehash-size");
  DEFSYM (QCrehash_threshold, ":rehash-threshold");
  DEFSYM (QCweakness, ":weakness");
  DEFSYM (Qkey, "key");
  DEFSYM (Qvalue, "value");
  DEFSYM (Qhash_table_test, "hash-table-test");
  DEFSYM (Qkey_or_value, "key-or-value");
  DEFSYM (Qkey_and_value, "key-and-value");

  defsubr (&Ssxhash_eq);
  defsubr (&Ssxhash_eql);
  defsubr (&Ssxhash_equal);
  defsubr (&Ssxhash_equal_including_properties);
  defsubr (&Smake_hash_table);
  defsubr (&Scopy_hash_table);
  defsubr (&Shash_table_count);
  defsubr (&Shash_table_rehash_size);
  defsubr (&Shash_table_rehash_threshold);
  defsubr (&Shash_table_size);
  defsubr (&Shash_table_test);
  defsubr (&Shash_table_weakness);
  defsubr (&Shash_table_p);
  defsubr (&Sclrhash);
  defsubr (&Sgethash);
  defsubr (&Sputhash);
  defsubr (&Sremhash);
  defsubr (&Smaphash);
  defsubr (&Sdefine_hash_table_test);
  defsubr (&Sinternal__hash_table_histogram);
  defsubr (&Sinternal__hash_table_buckets);
  defsubr (&Sinternal__hash_table_index_size);
  defsubr (&Sstring_search);
  defsubr (&Sobject_intervals);
  defsubr (&Sline_number_at_pos);

  /* Crypto and hashing stuff.  */
  DEFSYM (Qiv_auto, "iv-auto");

  DEFSYM (Qmd5,    "md5");
  DEFSYM (Qsha1,   "sha1");
  DEFSYM (Qsha224, "sha224");
  DEFSYM (Qsha256, "sha256");
  DEFSYM (Qsha384, "sha384");
  DEFSYM (Qsha512, "sha512");

  /* Miscellaneous stuff.  */

  DEFSYM (Qstring_lessp, "string-lessp");
  DEFSYM (Qprovide, "provide");
  DEFSYM (Qrequire, "require");
  DEFSYM (Qyes_or_no_p_history, "yes-or-no-p-history");
  DEFSYM (Qcursor_in_echo_area, "cursor-in-echo-area");
  DEFSYM (Qwidget_type, "widget-type");

  DEFVAR_LISP ("overriding-plist-environment", Voverriding_plist_environment,
               doc: /* An alist that overrides the plists of the symbols which it lists.
Used by the byte-compiler to apply `define-symbol-prop' during
compilation.  */);
  Voverriding_plist_environment = Qnil;
  DEFSYM (Qoverriding_plist_environment, "overriding-plist-environment");

  staticpro (&string_char_byte_cache_string);
  string_char_byte_cache_string = Qnil;

  require_nesting_list = Qnil;
  staticpro (&require_nesting_list);

  Fset (Qyes_or_no_p_history, Qnil);

  DEFVAR_LISP ("features", Vfeatures,
    doc: /* A list of symbols which are the features of the executing Emacs.
Used by `featurep' and `require', and altered by `provide'.  */);
  Vfeatures = list1 (Qemacs);
  DEFSYM (Qfeatures, "features");
  /* Let people use lexically scoped vars named `features'.  */
  Fmake_var_non_special (Qfeatures);
  DEFSYM (Qsubfeatures, "subfeatures");
  DEFSYM (Qfuncall, "funcall");
  DEFSYM (Qplistp, "plistp");
  DEFSYM (Qlist_or_vector_p, "list-or-vector-p");

#ifdef HAVE_LANGINFO_CODESET
  DEFSYM (Qcodeset, "codeset");
  DEFSYM (Qdays, "days");
  DEFSYM (Qmonths, "months");
  DEFSYM (Qpaper, "paper");
#endif	/* HAVE_LANGINFO_CODESET */

  DEFVAR_BOOL ("use-dialog-box", use_dialog_box,
    doc: /* Non-nil means mouse commands use dialog boxes to ask questions.
This applies to `y-or-n-p' and `yes-or-no-p' questions asked by commands
invoked by mouse clicks and mouse menu items.

On some platforms, file selection dialogs are also enabled if this is
non-nil.  */);
  use_dialog_box = true;

  DEFVAR_BOOL ("use-file-dialog", use_file_dialog,
    doc: /* Non-nil means mouse commands use a file dialog to ask for files.
This applies to commands from menus and tool bar buttons even when
they are initiated from the keyboard.  If `use-dialog-box' is nil,
that disables the use of a file dialog, regardless of the value of
this variable.  */);
  use_file_dialog = true;

  DEFVAR_BOOL ("use-short-answers", use_short_answers,
    doc: /* Non-nil means `yes-or-no-p' uses shorter answers "y" or "n".
When non-nil, `yes-or-no-p' will use `y-or-n-p' to read the answer.
We recommend against setting this variable non-nil, because `yes-or-no-p'
is intended to be used when users are expected not to respond too
quickly, but to take their time and perhaps think about the answer.
The same variable also affects the function `read-answer'.  See also
`yes-or-no-prompt'.  */);
  use_short_answers = false;

  DEFVAR_LISP ("yes-or-no-prompt", Vyes_or_no_prompt,
    doc: /* String to append when `yes-or-no-p' asks a question.
For best results this should end in a space.  */);
  Vyes_or_no_prompt = build_unibyte_string ("(yes or no) ");

  defsubr (&Sidentity);
  defsubr (&Srandom);
  defsubr (&Slength);
  defsubr (&Ssafe_length);
  defsubr (&Slength_less);
  defsubr (&Slength_greater);
  defsubr (&Slength_equal);
  defsubr (&Sproper_list_p);
  defsubr (&Sstring_bytes);
  defsubr (&Sstring_distance);
  defsubr (&Sstring_equal);
  defsubr (&Scompare_strings);
  defsubr (&Sstring_lessp);
  defsubr (&Sstring_version_lessp);
  defsubr (&Sstring_collate_lessp);
  defsubr (&Sstring_collate_equalp);
  defsubr (&Sappend);
  defsubr (&Sconcat);
  defsubr (&Svconcat);
  defsubr (&Scopy_sequence);
  defsubr (&Sstring_make_multibyte);
  defsubr (&Sstring_make_unibyte);
  defsubr (&Sstring_as_multibyte);
  defsubr (&Sstring_as_unibyte);
  defsubr (&Sstring_to_multibyte);
  defsubr (&Sstring_to_unibyte);
  defsubr (&Scopy_alist);
  defsubr (&Ssubstring);
  defsubr (&Ssubstring_no_properties);
  defsubr (&Stake);
  defsubr (&Sntake);
  defsubr (&Snthcdr);
  defsubr (&Snth);
  defsubr (&Selt);
  defsubr (&Smember);
  defsubr (&Smemq);
  defsubr (&Smemql);
  defsubr (&Sassq);
  defsubr (&Sassoc);
  defsubr (&Srassq);
  defsubr (&Srassoc);
  defsubr (&Sdelq);
  defsubr (&Sdelete);
  defsubr (&Snreverse);
  defsubr (&Sreverse);
  defsubr (&Ssort);
  defsubr (&Splist_get);
  defsubr (&Sget);
  defsubr (&Splist_put);
  defsubr (&Sput);
  defsubr (&Seql);
  defsubr (&Sequal);
  defsubr (&Sequal_including_properties);
  defsubr (&Svaluelt);
  defsubr (&Sfillarray);
  defsubr (&Sclear_string);
  defsubr (&Snconc);
  defsubr (&Smapcar);
  defsubr (&Smapc);
  defsubr (&Smapcan);
  defsubr (&Smapconcat);
  defsubr (&Syes_or_no_p);
  defsubr (&Sload_average);
  defsubr (&Sfeaturep);
  defsubr (&Srequire);
  defsubr (&Sprovide);
  defsubr (&Splist_member);
  defsubr (&Sbase64_encode_region);
  defsubr (&Sbase64_decode_region);
  defsubr (&Sbase64_encode_string);
  defsubr (&Sbase64_decode_string);
  defsubr (&Sbase64url_encode_region);
  defsubr (&Sbase64url_encode_string);
  defsubr (&Smd5);
  defsubr (&Ssecure_hash_algorithms);
  defsubr (&Ssecure_hash);
  defsubr (&Sbuffer_hash);
  defsubr (&Slocale_info);
  defsubr (&Sbuffer_line_statistics);

  DEFSYM (Qreal_this_command, "real-this-command");
  DEFSYM (Qfrom__tty_menu_p, "from--tty-menu-p");
  DEFSYM (Qyes_or_no_p, "yes-or-no-p");
  DEFSYM (Qy_or_n_p, "y-or-n-p");

  DEFSYM (QCkey, ":key");
  DEFSYM (QClessp, ":lessp");
  DEFSYM (QCin_place, ":in-place");
  DEFSYM (QCreverse, ":reverse");
  DEFSYM (Qvaluelt, "value<");
}
