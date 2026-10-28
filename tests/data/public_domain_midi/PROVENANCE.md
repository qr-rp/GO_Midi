# Piano MIDI provenance

`catalog.json` is the canonical inventory. The published package exposes a
compact player catalog and keeps this complete inventory as `provenance.json`. Each entry has an array of categories, so a
song can belong to more than one. Categories describe the repertoire, not the
synthesizer: orchestral MIDI tracks are merged and played with piano sound.

## Acceptance policy

Use only a file-specific public-domain dedication, CC0, or an equally explicit
unrestricted grant covering the actual MIDI/edition. A composition being old,
a free download, an archive's name, or a site's general CC footer is not enough.
Do not infer a contributor's license from their other uploads. This batch uses
only Public Domain and CC0-1.0, with no attribution-only or share-alike licenses.

For Mutopia, the individual edition page both links the MIDI and marks its
copyright Public Domain. Its [Public Domain policy](https://www.mutopiaproject.org/legal.html#publicdomain)
states: “The contributor of this music has dedicated their contribution into the
public domain.” That covers the contributor's edition, not just the original
composition. Each entry records the historical source edition and Mutopia
version. The license labels and policy were checked when the files were retrieved.

For Wikimedia Commons, each accepted **MIDI file's Licensing section** applies
CC0 or a contributor's public-domain dedication to their own transcription,
setting, and sound file. The catalog records permanent page revisions. The
Commons metadata CC0 footer and page-prose CC BY-SA footer do not license audio.

For PDMX, the accepted score contributor explicitly chose CC0 or marked their
score edition Public Domain on MuseScore; each entry preserves that distinction.
The archived score metadata and PDMX row must agree. A license-conflict flag
requires an explicit review of the internal declaration, bound to the same
source filename; the original flag is retained. PDMX flags any nonempty
copyright field, including `OpenScore (CC0)`, `Public Domain (PianoXML typeset)`,
bare dates, and transcriber credits.
Those cases are resolved with the actual field text and archive evidence,
not by ignoring the flag. The live page is checked when available; a broken
or unavailable page, or a later page with different terms, does not revoke a documented archived
[CC0 dedication](https://creativecommons.org/publicdomain/zero/1.0/legalcode#s2).
Such states are recorded separately from successful live license checks. Acceptance
then applies only to the identified archived revision, never a newer export.
For PianoXML’s Public Domain Mark entries, a matching internal statement
expressly dedicates the PianoXML typesetting; the page label alone is not the basis.
Historical copyright notices require a separate review of the named publication,
creator, and source rights; a date by itself does not resolve an actual notice.
PDMX mechanically exports those
scores to MIDI with MuseScore 3.6.2. The collection itself has a separate CC BY
4.0 license, recorded with dataset attribution in [evidence/pdmx-v9.json](evidence/pdmx-v9.json).
That collection license is not represented as the license of every score.
Original archive checksums, exact member paths, and original metadata hashes
bind each accepted MIDI to its source edition. No login was needed for these
archive downloads.
The supplemental older PDMX mirror supplies internal-declaration evidence
only. Its source filenames match the v9 MIDI members, and its associated raw
metadata is verified byte-for-byte against v9. The bundled MIDI bytes remain
from the identified v9 archive.

For IMSLP, the exact synthesized/MIDI block must name the downloaded file and
explicitly mark that edition CC0 or Public Domain. Other PDFs on the same work
page can have different licenses. Each entry links the reviewed work-page
revision and matches the original file's published SHA-1 and size. Some files
come from IMSLP's public mirror; others are unchanged binary fields in a pinned
Hugging Face Parquet archive, with the archive hash and row recorded. The
archive's blanket CC0 label was found to be unreliable and is not licensing
evidence. Companion editions and credits were also checked for conflicts.

CCEH explicitly extends its unrestricted-use policy to MIDI files, including
the transcription and performance layers. Its accepted files also embed a
public-domain declaration. HymnSite's CARadke explicitly releases the personally
created `umh` MIDI sequences for unrestricted use and resale; the separately
copyrighted HymnDisk arrangements are excluded. Their individual tune pages,
policy-page hashes, MIDI declarations, and companion-file checks are recorded
in [evidence/cceh-editions.json](evidence/cceh-editions.json) and
[evidence/hymnsite-editions.json](evidence/hymnsite-editions.json).

For bbloomf's LilyPond songbook, the repository's explicit release covers its
contents and generated MIDI editions. Pinned source and publishing commits,
the license file, companion scores, and download hashes are recorded in
[evidence/bloom-editions.json](evidence/bloom-editions.json). These original
MIDI exports retain their embedded historical lyrics; the player ignores lyrics.

For Daisyfield’s Japanese collection, Tom Potter explicitly donates his own
transcriptions to the public domain and identifies their Finale-to-MIDI exports.
Each accepted companion score repeats that donation. The historical editions,
individual score and MIDI hashes, and visual checks are recorded in
[evidence/daisyfield-editions.json](evidence/daisyfield-editions.json). Other
Daisyfield sections are not covered by this collection-specific grant.

The selected OpenGameArt upload explicitly releases its MIDI-containing bundle
as CC0. Its adaptation, companion score, archive member, and hashes are recorded
in [evidence/opengameart-editions.json](evidence/opengameart-editions.json).

The catalog preserves the download URL, UTC retrieval time, contributor,
license declaration and scope, revision/edition, SHA-256, and byte count. ZIP
and tar downloads additionally record the member path and archive evidence;
Parquet downloads record the row and binary field.
All bundled MIDI files are unchanged downloads or unchanged archive members.
Source declarations are recorded as declarations, not independent certifications.

## Coverage and playback

The live inventory and category counts come from [catalog.json](catalog.json).
There are entries in all five requested categories, including **Boomer Sooner**
(University of Oklahoma), **Jazzy On Wisconsin!** (University of Wisconsin),
**Anchors Aweigh** (United States Naval Academy), and the traditional tune of
**Glory, Glory** (University of Georgia), plus **Hot Time in the Old Town**
(University of Wisconsin), **Tramp! Tramp! Tramp!** (Georgetown / Universidad
Católica tune), **Waltzing Matilda** (1st Marine Division), and **Dixie**
(historical Citadel tune). Australian club connections also include **La
Marseillaise** (Brisbane Lions / Fitzroy), **When the Saints Go Marching In**
(St Kilda), **The Marines' Hymn** (Adelaide), and **Toreador March** (Geelong).
Historical uses and shared melodies are identified
separately from current institutional arrangements. These are the specific
contributors' arrangements described in each entry, sometimes for small
ensembles or a single instrument.

Nursery rhyme includes traditional children's songs, singing games, and
lullabies in several languages. Classical piano includes historical keyboard
repertoire, ragtime, and piano reductions. Classical orchestra includes full
ensemble scores of symphonic, concerto, ballet, operatic, and choral-orchestral
movements. A movement is labeled as a movement; an incomplete set is not
represented as a complete symphony or concerto. Individual practice parts are
omitted from orchestral archives. National anthems identify their country or
historical state. Multiple arrangements can represent the same composition.
Wind-band repertoire and large string ensembles are identified in their descriptions.
Contemporary classical works qualify when the composer explicitly dedicates
both the composition's score and MIDI; these are identified as contemporary.

The player searches titles, composers, and descriptions without case or accent
sensitivity and filters by category. MIDI instruments are rendered with the
player's piano synthesizer; orchestral tracks are merged into typing chords.
The original `fur-elise.mid` matched the verified Mutopia download byte for
byte. The original Mary sample lacked provenance and was replaced with Mary
Ybarra's explicitly CC0 edition; the old bytes remain in the original Type The Rhythm repository’s git history. Frère
Jacques is the introductory selection.

## Review evidence

- [evidence/mutopia-editions.json](evidence/mutopia-editions.json) records the
  individual Mutopia edition pages, source downloads, versions, historical
  editions, and checksums. Embedded source license declarations were checked
  as well as the page label.
- [evidence/pdmx-v9.json](evidence/pdmx-v9.json) records PDMX v9's fixed export
  implementation, collection attribution/license, and published archive hashes.
- [evidence/imslp-editions.json](evidence/imslp-editions.json) records the exact
  MIDI license blocks, work-page revisions, source-file checksums, historical
  source review, and archive evidence for the accepted IMSLP editions.
- Each Commons entry links the exact reviewed file-page revision; each PDMX
  entry records the archived score revision, original metadata member/hash,
  and the live-license check time or the observed unavailable/changed-license state.
- [research.md](research.md) records exclusions, unresolved candidates, and
  the login-dependent lead. A failed candidate is not evidence that another
  arrangement of the same composition has the same license.

## Add or replace a file

1. Read the file/edition page and its applicable license. Check that the grant
   covers the MIDI creator's work, and distinguish historical composition and
   modern arrangement rights. Record any territory or other limitations; do not
   silently accept incompatible restrictions.
2. Download the original `.mid` bytes into `midi/`. Record its exact
   URL, retrieval time, source revision/edition, contributor, explicit license
   declaration and scope, bytes, SHA-256, and any modifications in the catalog.
3. Assign all applicable categories and describe the actual arrangement or
   excerpt. Include the country or institution for an anthem or fight song.
4. Run `npm test` and `npm run build`, then test playback in a consuming player.
   Integrity checks bind metadata to bytes; they cannot establish that a source's
   rights claim is true.
