
# MD4C Change Log


## Version 0.6.0

New Features:

  * Add blank line preservation, enabled with the flag
    `MD_FLAG_PRESERVEBLANKLINES`.

    Normally any run of blank lines separating two blocks is collapsed into a
    single block boundary. With the flag, each run is instead reported as a
    block `MD_BLOCK_BLANK` whose detail (`MD_BLOCK_BLANK_DETAIL`) holds the
    number of blank lines. This is a deviation from CommonMark, intended for
    WYSIWYG-like applications which need to reproduce the vertical spacing of
    the source.

  * Add highlight span extension, enabled with the flag `MD_FLAG_HIGHLIGHT`.

    Syntax example:
    ```
    ==highlighted text==
    ```

    The HTML renderer outputs `<mark>`.

    Contributed by [Gregory Moskaliuk](https://github.com/hryhoriiK97).

  * Add GFM-like footnote extension, enabled with the flag `MD_FLAG_FOOTNOTES`.

    Syntax example:
    ```
    Here is a simple footnote[^1]. With some additional text after it.

    [^1]: My reference.
    ```

    Contributed by [Gregory Moskaliuk](https://github.com/hryhoriiK97).

  * Add extension recognizing GFM-like admonitions, as e.g.

    ```
    > [!IMPORTANT]
    > Text of an important note.
    ```

    The flag `MD_FLAG_ADMONITIONS` enables this extension.

  * Add extension recognizing superscripts and subscripts with `^superscript^`
    and `~subscript~` syntax respectively. Flags `MD_FLAG_SUPERSCRIPTS` and
    `MD_FLAG_SUBSCRIPTS` enable these extensions.

    Note that if both `MD_FLAG_SUBSCRIPTS` and `MD_FLAG_STRIKETHROUGH` are
    enabled, strike-through requires double tildes: `~~strike-through~~`.

    Contributed by [Gregory Moskaliuk](https://github.com/hryhoriiK97).

  * Add extension recognizing spoilers with `||hidden text||` syntax. This
    follows syntax used by Telegram and Discord Markdown implementations.
    It needs to be explicitly enabled with `MD_FLAG_SPOILERS`. (Note though that
    the HTML renderer outputs them verbatim in a custom `<x-spoiler>` tag.)

    Contributed by [Gregory Moskaliuk](https://github.com/hryhoriiK97).

Changes:

  * Permissive autolinks (`MD_FLAG_PERMISSIVExxxAUTOLINKS` flags) have been
    improved in several ways:
     - some more links with non-alphanumeric characters are now recognized,
     - e-mails with explicit `mailto:` prefix are now recognized,
     - `xmpp:` links are now recognized.

    However please note this will always be a subject of painful search for
    reasonable trade-off between recognizing more obscure URLs versus opening
    gates to potential false positives.

    We generally recommend to use standard autolinks as defined by
    [CommonMark specification](https://spec.commonmark.org/0.31.2/#autolinks)
    (i.e. enclosed between `<` and `>`), especially for links containing more
    obscure combinations of non-alphanumeric characters.

  * Better detection (and suppression) of too sparse tables, which would
    otherwise generate too disproportionately large output.
    (See [#345](https://github.com/mity/md4c/pull/345) for more information.)

  * For the sake completeness, we've made our Unicode-specific code compliant
    to Unicode 18.0, again.

    Explanation: By mistake we added into MD4C version 0.5.3 support for the
    Unicode standard based on pre-release version of it (probably 18.0.0 alfa).
    This has now been amended and our Unicode data tables re-generated with the
    correct final official version 18.0.0 of the standard. (Resulting in
    exactly the same tables, so strictly speaking this is a non-change.)

Fixes:

  * [#325](https://github.com/mity/md4c/pull/325):
    The HTML renderer now replaces UTF-16 surrogate codepoints, which should
    not appear in the valid UTF-8 input, with the replacement character U+FFFD
    (as it was already doing for invalid codepoints above U+10FFFF).

  * [#328](https://github.com/mity/md4c/issues/328),
    [#338](https://github.com/mity/md4c/issues/338):
    Fix several bugs of Windows Unicode builds of the parser (i.e. when built
    with `-DMD4C_USE_UTF16`).

  * [#330](https://github.com/mity/md4c/pull/330):
    The `md2html` utility collects the generated HTML into an in-memory growing
    buffer and the respective code was not checking whether the size of the
    buffer doesn't overflow.

    This may have security implications for applications which use `md2html`
    utility rather than call directly the md4c-html library and which pass the
    utility a multi-gigabyte input to process.

  * [#368](https://github.com/mity/md4c/pull/368):
    Fix check that the standard CommonMark URI autolink's scheme must begin with
    an alpha-numerical character, not any ASCII character.

  * [#398](https://github.com/mity/md4c/pull/398),
    [#399](https://github.com/mity/md4c/pull/399),
    [#400](https://github.com/mity/md4c/pull/400):
    Fix multiple memory management errors in error paths.

    Big thanks especially to [Ilia Alshanetsky](https://github.com/iliaal) for
    detecting and fixing all of these.

  * [#413](https://github.com/mity/md4c/issues/413):
    Fix a case of incorrect ending of a list.

  * [#414](https://github.com/mity/md4c/issues/414):
    Fix thematic break detection just after link reference definition.


## Version 0.5.3

Changes:

  * Avoid repeated prefix `language-` in code block language specification if
    the input already explicitly includes the prefix.

    Contributed by [Zach Newton](https://github.com/znewt99).

  * Permissive autolink extensions (`MD_FLAG_PERMISSIVExxxAUTOLINKS`) are now
    tiny bit more permissive, allowing `+` and `-` characters to be anywhere
    in the path portion of the URL. This also improves compatibility with GFM.

  * Make Unicode-specific code compliant to Unicode 18.0.0.

Fixes:

 * [#236](https://github.com/mity/md4c/issues/236):
   Fix quadratic time behavior caused by one-by-one walking over block lines
   instead of calling `md_lookup_line()`.

 * [#238](https://github.com/mity/md4c/issues/238):
   Fix quadratic time and output size behavior caused by malicious misuse of
   link reference definitions.

 * [#242](https://github.com/mity/md4c/issues/242):
   The strike-through extension (with flag `MD_FLAG_STRIKETHROUGH`) now follows
   same logic as other emphasis spans in respect to punctuation character and
   word boundaries.

 * [#248](https://github.com/mity/md4c/issues/248):
   Fix handling tab when removing trailing whitespace, especially in connection
   with ATX headers.

 * [#266](https://github.com/mity/md4c/issues/266):
   We now correctly abort the parser when a callback returns non-zero.
   (Previously it worked correctly only for negative values, values greater
   than zero were causing strange and inconsistent behavior.)

 * [#271](https://github.com/mity/md4c/issues/271):
   Fix handling a code span whose closer is on the next line and yet another
   text follows. In the case we erroneously outputted the closer code span mark
   as part of the text.

 * [#275](https://github.com/mity/md4c/issues/275):
   Fix `md_decode_utf16le_before__()`. (Only affected MD4C builds built with
   `-MD4C_USE_UTF16` on Windows.)

 * [#278](https://github.com/mity/md4c/issues/278),
   [#294](https://github.com/mity/md4c/issues/294):
   Do not try to interpret characters in a link URL as Markdown syntax
   characters.

 * [#292](https://github.com/mity/md4c/issues/292):
   Fix detection of closing code block fence if it has a trailing tabulator.

 * [#299](https://github.com/mity/md4c/issues/299):
   Fix invalid `free()` in an error path.


## Version 0.5.2

Changes:

 * Changes mandated by CommonMark specification 0.31:

   - The specification expands set of Unicode characters seen by Markdown
     parser as a punctuation. Namely all Unicode general categories P
     (punctuation) and S (symbols) are now seen as such.

   - The definition of HTML comment has been changed so that `<!-->` and
     `<!--->` are also recognized as HTML comments.

   - HTML tags recognized as HTML block starting condition of type 4 has been
     updated, namely a tag `<source>` has been removed, whereas `<search>`
     added.

   Refer to [CommonMark 0.31.2](https://spec.commonmark.org/0.31.2/) for full
   specification.

Fixes:

 * [#230](https://github.com/mity/md4c/issues/230):
   The fix [#223](https://github.com/mity/md4c/issues/223) in 0.5.1 release
   was incomplete and one corner case remained unfixed. This is now addressed.

 * [#231](https://github.com/mity/md4c/issues/231):
   `md2html --full-html` now emits `<meta charset="UTF-8">` in the HTML header.


## Version 0.5.1

Changes:

 * LaTeX math extension (`MD_FLAG_LATEXMATHSPANS`) now requires that opener
   mark is not immediately preceded with alpha-numeric character and similarly
   that closer mark is not immediately followed with alpha-numeric character.

   So for example `foo$ x + y = z $` is not recognized as LaTeX equation
   anymore because there is no space between `foo` and the opening `$`.

 * Table extension (`MD_FLAG_TABLES`) now recognizes only tables with no more
   than 128 columns. This limit has been imposed to prevent a pathological
   case of quadratic output size explosion which could be used as DoS attack
   vector.

 * We are now more strict with `MD_FLAG_PERMISSIVExxxAUTOLINKS` family of
   extensions with respect to non-alphanumeric characters, with the aim to
   mitigate false positive detections.

   Only relatively few selected non-alphanumeric are now allowed in permissive
   e-mail auto-links (`MD_FLAG_PERMISSIVEEMAILAUTOLINKS`):
     - `.`, `-`, `_`, `+` in user name part of e-mail address; and
     - `.`, `-`, `_` in host part of the e-mail address.

   Similarly for URL and e-mail auto-links (`MD_FLAG_PERMISSIVEURLAUTOLINKS` and
   `MD_FLAG_PERMISSIVEWWWAUTOLINKS`):
     - `.`, `-`, `_` in host part of the URL;
     - `/`, `.`, `-`, `_` in path part of the URL;
     - `&`, `.`, `-`, `+`, `_`, `=`, `(`, `)` in the query part of the URL
       (additionally, if present, `(` and `)` must form balanced pairs); and
     - `.`, `-`, `+`, `_` in the fragment part of the URL.

   Furthermore these characters (with some exceptions like where they serve as
   delimiter characters, e.g. `/` for paths) are generally accepted only when
   an alphanumeric character both precedes and follows them (i.e. these cannot
   be "stacked" together).

Fixes:

 * Fix several bugs where we haven't properly respected already resolved spans
   of higher precedence level in handling of permissive auto-links extensions
   (family of `MD_FLAG_PERMISSIVExxxAUTOLINKS` flags), LaTeX math extension
   (`MD_FLAG_LATEXMATHSPANS`) and wiki-links extension (`MD_FLAG_WIKILINKS`)
   of the form `[[label|text]]` (with pipe `|`). In some complex cases this
   could lead to invalid internal parser state and memory corruption.

   Identified with [OSS-Fuzz](https://github.com/google/oss-fuzz).

 * [#222](https://github.com/mity/md4c/issues/222):
   Fix strike-through extension (`MD_FLAG_STRIKETHROUGH`) which did not respect
   same rules for pairing opener and closer marks as other emphasis spans.

 * [#223](https://github.com/mity/md4c/issues/223):
   Fix incorrect handling of new-line character just at the beginning and/or
   end of a code span where we were not following CommonMark specification
   requirements correctly.


## Version 0.5.0

Changes:

 * Changes mandated by CommonMark specification 0.30.

   Actually there are only very minor changes to recognition of HTML blocks:

   - The tag `<textarea>` now triggers HTML block (of type 1 as per the
     specification).

   - HTML declaration (HTML block type 4) is not required to begin with an
     upper-case ASCII character after the `<!`. Any ASCII character is now
     allowed. Also it now doesn't require a whitespace before the closing `>`.

   Other than that, the newest specification mainly improves test coverage and
   clarifies its wording in some cases, without affecting the implementation.

   Refer to [CommonMark 0.30](https://spec.commonmark.org/0.30/) for full
   specification.

 * Make Unicode-specific code compliant to Unicode 15.1.

 * Update list of entities known to the HTML renderer from
   https://html.spec.whatwg.org/entities.json.

New Features:

 * Add extension allowing to treat all soft break as hard ones. It has to be
   explicitly enabled with `MD_FLAG_HARD_SOFT_BREAKS`.

   Contributed by [l-m](https://github.com/l1mey112).

 * Structure `MD_SPAN_A_DETAIL` now has a new member `is_autolink`.

   Contributed by [Jens Alfke](https://github.com/snej).

 * `md2html` utility now supports command line options `--html-title` and
   `--html-css`.

   Contributed by [Andreas Baumann](https://github.com/andreasbaumann).

Fixes:

 * [#163](https://github.com/mity/md4c/issues/163):
   Make HTML renderer to emit `'\n'` after the root tag when in the XHTML mode.

 * [#165](https://github.com/mity/md4c/issues/165):
   Make HTML renderer not to percent-encode `'~'` in URLs. Although it does
   work, it's not needed, and it can actually be confusing with URLs such as
   `http://www.example.com/~johndoe/`.

 * [#167](https://github.com/mity/md4c/issues/167),
   [#168](https://github.com/mity/md4c/issues/168):
   Fix multiple instances of various buffer overflow bugs, found mostly using
   a fuzz testing. Contributed by [dtldarek](https://github.com/dtldarek) and
   [Thierry Coppey](https://github.com/TCKnet).

 * [#169](https://github.com/mity/md4c/issues/169):
   Table underline now does not require 3 characters per table column anymore.
   One dash (optionally with a leading or tailing `:` appended or prepended)
   is now sufficient. This improves compatibility with the GFM.

 * [#172](https://github.com/mity/md4c/issues/172):
   Fix quadratic time behavior caused by unnecessary lookup for link reference
   definition even if the potential label contains nested brackets.

 * [#173](https://github.com/mity/md4c/issues/173),
   [#174](https://github.com/mity/md4c/issues/174),
   [#212](https://github.com/mity/md4c/issues/212),
   [#213](https://github.com/mity/md4c/issues/213):
   Multiple bugs identified with [OSS-Fuzz](https://github.com/google/oss-fuzz)
   were fixed.

 * [#190](https://github.com/mity/md4c/issues/190),
   [#200](https://github.com/mity/md4c/issues/200),
   [#201](https://github.com/mity/md4c/issues/201):
   Multiple fixes of incorrect interactions of indented code block with a
   preceding block.

 * [#202](https://github.com/mity/md4c/issues/202):
   We were not correctly calling `enter_block()` and `leave_block()` callbacks
   if multiple HTML blocks followed one after another; instead previously
   such blocks were merged into one.

   (This may likely impact only applications interested in Markdown's AST,
   and not just converting Markdown to other formats like HTML.)

 * [#210](https://github.com/mity/md4c/issues/210):
   The `md2html` utility now handles nested images with optional titles
   correctly.

 * [#214](https://github.com/mity/md4c/issues/214):
   Tags `<h2>` ... `<h6>` incorrectly did not trigger HTML block.

 * [#215](https://github.com/mity/md4c/issues/215):
   The parser incorrectly did not accept optional tabs after setext header
   underline.

 * [#217](https://github.com/mity/md4c/issues/217):
   The parser incorrectly resolved emphasis in some situations, if the emphasis
   marks were enclosed by punctuation characters.


## Version 0.4.8

Fixes:

 * [#149](https://github.com/mity/md4c/issues/149):
   A HTML block started in a container block (and not explicitly finished in
   the block) could eat 1 line of actual contents.

 * [#150](https://github.com/mity/md4c/issues/150):
   Fix `md2html` to output proper DOCTYPE and HTML tags when `--full-html`
   command line options is used, accordingly to the expected output format
   (HTML or XHTML).

 * [#152](https://github.com/mity/md4c/issues/152):
   Suppress recognition of a permissive autolink if it would otherwise form a
   complete body of an outer inline link.

 * [#153](https://github.com/mity/md4c/issues/153),
   [#154](https://github.com/mity/md4c/issues/154):
   Set `MD_BLOCK_UL_DETAIL::mark` and `MD_BLOCK_OL_DETAIL::mark_delimiter`
   correctly, even when the blocks are nested at the same line in a complicated
   ways.

 * [#155](https://github.com/mity/md4c/issues/155):
   Avoid reading 1 character beyond the input size in some complex cases.


## Version 0.4.7

Changes:

 * Add `MD_TABLE_DETAIL` structure into the API. The structure describes column
   count and row count of the table, and pointer to it is passed into the
   application-provided block callback with the `MD_BLOCK_TABLE` block type.

Fixes:

 * [#131](https://github.com/mity/md4c/issues/131):
   Fix handling of a reference image nested in a reference link.

 * [#135](https://github.com/mity/md4c/issues/135):
   Handle unmatched parenthesis pairs inside a permissive URL and WWW auto-links
   in a way more compatible with the GFM.

 * [#138](https://github.com/mity/md4c/issues/138):
   The tag `<tbody></tbody>` is now suppressed whenever the table has zero body
   rows.

 * [#139](https://github.com/mity/md4c/issues/139):
   Recognize a list item mark even when EOF follows it.

 * [#142](https://github.com/mity/md4c/issues/142):
   Fix reference link definition label matching in a case when the label ends
   with a Unicode character with non-trivial case folding mapping.


## Version 0.4.6

Fixes:

 * [#130](https://github.com/mity/md4c/issues/130):
   Fix `ISANYOF` macro, which could provide unexpected results when encountering
   zero byte in the input text; in some cases leading to broken internal state
   of the parser.

   The bug could result in denial of service and possibly also to other security
   implications. Applications are advised to update to 0.4.6.


## Version 0.4.5

Fixes:

 * [#118](https://github.com/mity/md4c/issues/118):
   Fix HTML renderer's `MD_HTML_FLAG_VERBATIM_ENTITIES` flag, exposed in the
   `md2html` utility via `--fverbatim-entities`.

 * [#124](https://github.com/mity/md4c/issues/124):
   Fix handling of indentation of 16 or more spaces in the fenced code blocks.


## Version 0.4.4

Changes:

 * Make Unicode-specific code compliant to Unicode 13.0.

New features:

 * The HTML renderer, developed originally as the heart of the `md2html`
   utility, is now built as a standalone library, in order to simplify its
   reuse in applications.

 * With `MD_HTML_FLAG_SKIP_UTF8_BOM`, the HTML renderer now skips UTF-8 byte
   order mark (BOM) if the input begins with it, before passing to the Markdown
   parser.

   `md2html` utility automatically enables the flag (unless it is custom-built
   with `-DMD4C_USE_ASCII`).

 * With `MD_HTML_FLAG_XHTML`, The HTML renderer generates XHTML instead of
   HTML.

   This effectively means `<br />` instead of `<br>`, `<hr />` instead of
   `<hr>`, and `<img ... />` instead of `<img ...>`.

   `md2html` utility now understands the command line option `-x` or `--xhtml`
   enabling the XHTML mode.

Fixes:

 * [#113](https://github.com/mity/md4c/issues/113):
   Add missing folding info data for the following Unicode characters:
   `U+0184`, `U+018a`, `U+01b2`, `U+01b5`, `U+01f4`, `U+0372`, `U+038f`,
   `U+1c84`, `U+1fb9`, `U+1fbb`, `U+1fd9`, `U+1fdb`, `U+1fe9`, `U+1feb`,
   `U+1ff9`, `U+1ffb`, `U+2c7f`, `U+2ced`, `U+a77b`, `U+a792`, `U+a7c9`.

   Due the bug, the link definition label matching did not work in the case
   insensitive way for these characters.


## Version 0.4.3

New features:

 * With `MD_FLAG_UNDERLINE`, spans enclosed in underscore (`_foo_`) are seen
   as underline (`MD_SPAN_UNDERLINE`) rather than an ordinary emphasis or
   strong emphasis.

Changes:

 * The implementation of wiki-links extension (with `MD_FLAG_WIKILINKS`) has
   been simplified.

    - A noticeable increase of MD4C's memory footprint introduced by the
      extension implementation in 0.4.0 has been removed.
    - The priority handling towards other inline elements have been unified.
      (This affects an obscure case where syntax of an image was in place of
      wiki-link destination made the wiki-link invalid. Now *all* inline spans
      in the wiki-link destination, including the images, is suppressed.)
    - The length limitation of 100 characters now always applies to wiki-link
      destination.

 * Recognition of strike-through spans (with the flag `MD_FLAG_STRIKETHROUGH`)
   has become much stricter and, arguably, reasonable.

    - Only single tildes (`~`) and double tildes (`~~`) are recognized as
      strike-through marks. Longer ones are not anymore.
    - The length of the opener and closer marks have to be the same.
    - The tildes cannot open a strike-through span if a whitespace follows.
    - The tildes cannot close a strike-through span if a whitespace precedes.

   This change follows the changes of behavior in cmark-gfm some time ago, so
   it is also beneficial from compatibility point of view.

 * When building MD4C by hand instead of using its CMake-based build, the UTF-8
   support was by default disabled, unless explicitly asked for by defining
   a preprocessor macro `MD4C_USE_UTF8`.

   This has been changed and the UTF-8 mode now becomes the default, no matter
   how `md4c.c` is compiled. If you need to disable it and use the ASCII-only
   mode, you have explicitly define macro `MD4C_USE_ASCII` when compiling it.

   (The CMake-based build as provided in our repository explicitly asked for
   the UTF-8 support with `-DMD4C_USE_UTF8`. I.e. if you are using MD4C library
   built with our vanilla `CMakeLists.txt` files, this change should not affect
   you.)

Fixes:

 * Fixed some string length handling in the special `MD4C_USE_UTF16` build.

   (This does not affect you unless you are on Windows and explicitly define
   the macro when building MD4C.)

 * [#100](https://github.com/mity/md4c/issues/100):
   Fixed an off-by-one error in the maximal length limit of some segments
   of e-mail addresses used in autolinks.

 * [#107](https://github.com/mity/md4c/issues/107):
   Fix mis-detection of asterisk-encoded emphasis in some corner cases when
   length of the opener and closer differs, as in `***foo *bar baz***`.


## Version 0.4.2

Fixes:

 * [#98](https://github.com/mity/md4c/issues/98):
   Fix mis-detection of asterisk-encoded emphasis in some corner cases when
   length of the opener and closer differs, as in `**a *b c** d*`.


## Version 0.4.1

Unfortunately, 0.4.0 has been released with badly updated ChangeLog. Fixing
this is the only change on 0.4.1.


## Version 0.4.0

New features:

 * With `MD_FLAG_LATEXMATHSPANS`, LaTeX math spans (`$...$`) and LaTeX display
   math spans (`$$...$$`) are now recognized. (Note though that the HTML
   renderer outputs them verbatim in a custom `<x-equation>` tag.)

   Contributed by [Tilman Roeder](https://github.com/dyedgreen).

 * With `MD_FLAG_WIKILINKS`, Wiki-style links (`[[...]]`) are now recognized.
   (Note though that the HTML renderer renders them as a custom `<x-wikilink>`
   tag.)

   Contributed by [Nils Blomqvist](https://github.com/niblo).

Changes:

 * Parsing of tables (with `MD_FLAG_TABLES`) is now closer to the way how
   cmark-gfm parses tables as we do not require every row of the table to
   contain a pipe `|` anymore.

   As a consequence, paragraphs now cannot interrupt tables. A paragraph which
   follows the table has to be delimited with a blank line.

Fixes:

 * [#94](https://github.com/mity/md4c/issues/94):
   `md_build_ref_def_hashtable()`: Do not allocate more memory than strictly
   needed.

 * [#95](https://github.com/mity/md4c/issues/95):
   `md_is_container_mark()`: Ordered list mark requires at least one digit.

 * [#96](https://github.com/mity/md4c/issues/96):
   Some fixes for link label comparison.


## Version 0.3.4

Changes:

 * Make Unicode-specific code compliant to Unicode 12.1.

 * Structure `MD_BLOCK_CODE_DETAIL` got new member `fenced_char`. Application
   can use it to detect character used to form the block fences (`` ` `` or
   `~`). In the case of indented code block, it is set to zero.

Fixes:

 * [#77](https://github.com/mity/md4c/issues/77):
   Fix maximal count of digits for numerical character references, as requested
   by CommonMark specification 0.29.

 * [#78](https://github.com/mity/md4c/issues/78):
   Fix link reference definition label matching for Unicode characters where
   the folding mapping leads to multiple codepoints, as e.g. in `ẞ` -> `SS`.

 * [#83](https://github.com/mity/md4c/issues/83):
   Fix recognition of an empty blockquote which interrupts a paragraph.


## Version 0.3.3

Changes:

 * Make permissive URL autolink and permissive WWW autolink extensions stricter.

   This brings the behavior closer to GFM and mitigates risk of false positives.
   In particular, the domain has to contain at least one dot and parenthesis
   can be part of the link destination only if `(` and `)` are balanced.

Fixes:

 * [#73](https://github.com/mity/md4c/issues/73):
   Some raw HTML inputs could lead to quadratic parsing times.

 * [#74](https://github.com/mity/md4c/issues/74):
   Fix input leading to a crash. Found by fuzzing.

 * [#76](https://github.com/mity/md4c/issues/76):
   Fix handling of parenthesis in some corner cases of permissive URL autolink
   and permissive WWW autolink extensions.


## Version 0.3.2

Changes:

 * Changes mandated by CommonMark specification 0.29.

   Most importantly, the white-space trimming rules for code spans have changed.
   At most one space/newline is trimmed from beginning/end of the code span
   (if the code span contains some non-space contents, and if it begins and
   ends with space at the same time). In all other cases the spaces in the code
   span are now left intact.

   Other changes in behavior are in corner cases only. Refer to [CommonMark
   0.29 notes](https://github.com/commonmark/commonmark-spec/releases/tag/0.29)
   for more info.

Fixes:

 * [#68](https://github.com/mity/md4c/issues/68):
   Some specific HTML blocks were not recognized when EOF follows without any
   end-of-line character.

 * [#69](https://github.com/mity/md4c/issues/69):
   Strike-through span not working correctly when its opener mark is directly
   followed by other opener mark; or when other closer mark directly precedes
   its closer mark.


## Version 0.3.1

Fixes:

 * [#58](https://github.com/mity/md4c/issues/58),
   [#59](https://github.com/mity/md4c/issues/59),
   [#60](https://github.com/mity/md4c/issues/60),
   [#63](https://github.com/mity/md4c/issues/63),
   [#66](https://github.com/mity/md4c/issues/66):
   Some inputs could lead to quadratic parsing times. Thanks to Anders Kaseorg
   for finding all those issues.

 * [#61](https://github.com/mity/md4c/issues/59):
   Flag `MD_FLAG_NOHTMLSPANS` erroneously affected also recognition of
   CommonMark autolinks.


## Version 0.3.0

New features:

 * Add extension for GitHub-style task lists:

   ```
    * [x] foo
    * [x] bar
    * [ ] baz
   ```

   (It has to be explicitly enabled with `MD_FLAG_TASKLISTS`.)

 * Added support for building as a shared library. On non-Windows platforms,
   this is now default behavior; on Windows static library is still the default.
   The CMake option `BUILD_SHARED_LIBS` can be used to request one or the other
   explicitly.

   Contributed by Lisandro Damián Nicanor Pérez Meyer.

 * Renamed structure `MD_RENDERER` to `MD_PARSER` and refactorize its contents
   a little bit. Note this is source-level incompatible and initialization code
   in apps may need to be updated.

   The aim of the change is to be more friendly for long-term ABI compatibility
   we shall maintain, starting with this release.

 * Added `CHANGELOG.md` (this file).

 * Make sure `md_process_table_row()` reports the same count of table cells for
   all table rows, no matter how broken the input is. The cell count is derived
   from table underline line. Bogus cells in other rows are silently ignored.
   Missing cells in other rows are reported as empty ones.

Fixes:

 * CID 1475544:
   Calling `md_free_attribute()` on uninitialized data.

 * [#47](https://github.com/mity/md4c/issues/47):
   Using bad offsets in `md_is_entity_str()`, in some cases leading to buffer
   overflow.

 * [#51](https://github.com/mity/md4c/issues/51):
   Segfault in `md_process_table_cell()`.

 * [#53](https://github.com/mity/md4c/issues/53):
   With `MD_FLAG_PERMISSIVEURLAUTOLINKS` or `MD_FLAG_PERMISSIVEWWWAUTOLINKS`
   we could generate bad output for ordinary Markdown links, if a non-space
   character immediately follows like e.g. in `[link](http://github.com)X`.


## Version 0.2.7

This was the last version before the changelog has been added.
