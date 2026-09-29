# Parsing unit values for display

`tired_unit_words_parse` decodes one logical, quote-aware unit directive value into
owned words and their original half-open byte spans. The spans include the source
quotes and escape sequences. Display code can use them to mask classified words
without reconstructing the rest of the installed file.

Whitespace separates words outside quotes. Single and double quotes group values;
adjacent quoted/unquoted segments form one word, matching the baseline command
parser. Empty quoted words remain present. Supported escapes include the documented
control characters, quotes, backslash, space, two-digit hexadecimal bytes,
three-digit octal bytes, and four/eight-digit Unicode escapes. Byte escapes preserve
arbitrary nonzero bytes, including non-UTF-8. Unicode escapes reject NUL, surrogates,
out-of-range code points and reserved noncharacters. Unknown/truncated escapes and
unclosed quotes produce an error rather than a guessed interpretation.

The parser performs no expansion. Percent sequences, dollar references, command
prefixes, semicolons and shell-looking text remain literal decoded values. Callers
must handle unit sections, assignments, physical-line continuation and comment
rules before passing a logical value. This component does not decide command
boundaries, validate a complete unit or execute anything.

Input is bounded to 4 MiB, with at most 4096 words and 1 MiB of decoded storage
including terminators. Failure preserves the previous output. Destruction clears
the retained decoded bytes before releasing them; callers should treat those bytes
as sensitive and must not print them without appropriate redaction/escaping.

Tests cover source spans, empty words, quote concatenation, byte and Unicode escapes,
renderer-token decoding, literal expansion syntax, malformed inputs, count/byte
limits and failure preservation. Complete installed-file redaction and the `show`
command remain separate integration work.
