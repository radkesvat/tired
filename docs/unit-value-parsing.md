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

`tired_unit_document_parse` supplies the logical assignment layer. It retains each
section/key/value and every repeated or empty reset assignment in file order.
Boundary whitespace is trimmed. CR, LF and paired CR/LF delimit physical lines;
an odd run of trailing backslashes continues a line, replacing the final backslash
with a space. Comment lines are skipped even during continuation. Blank lines end
continuation, and an unfinished continuation at EOF is parsed after its trailing
space is trimmed. A UTF-8 BOM is accepted before a section header.

Each logical value byte has an original file offset. Ordinary bytes map directly;
an inserted continuation space maps to its replaced backslash. Source offsets are
strictly increasing but may skip newlines and comments. Combining those offsets
with a decoded word's start/end yields a complete original span, including any
continuation/comment bytes inside that word. No file rewrite occurs during parsing.

The document layer accepts at most 4 MiB of source, 1 MiB per logical line,
4096 assignments and 255 bytes per section/key. Empty/comment-only files have no
assignments. Malformed headers, assignments outside sections, missing keys or
equals signs, invalid UTF-8 and NUL fail atomically. This deliberately reports
syntax it cannot safely map instead of reproducing the manager's warning-and-ignore
behavior for malformed lines. It is not full semantic unit validation.

Document tests cover continued quoted words across comments, exact source mapping,
mixed line endings, escaped trailing backslashes, EOF/blank-line continuation,
reset assignments, syntax failures and size/count limits. Classification of values,
span replacement, installed-file reading and `show` integration are still pending.
