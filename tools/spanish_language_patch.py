#!/usr/bin/env python3
"""Adds Spanish (text, voices, lip sync and videos) to the Fable II game of the Game of the Year disc for USA and Europe.

The disc this port supports (Game of the Year Edition, USA and Europe, media ID 716F0A0D) has English, Chinese, Korean,
Polish, Czech, Hungarian and Slovak, but no Spanish. The Game of the Year disc for Spain (media ID 772E8EE8) has the same
files except the language folder: data/language/es-es instead of data/language/en-uk and the others. The game already
knows "es-es" (data/language/cultures.txt is the same on both discs), so copying that folder next to the USA/Europe
disc's and asking the game for Spanish with these two settings in fable2.toml (next to fable2-nx.nro) is enough:

    fable2_user_language = 5     (Spanish)
    fable2_user_country = 31     (Spain; without it the console says United States)

default.xex is not touched: the port keeps the USA/Europe disc's (version 0.0.0.26), the one it is made for.

Usage (<spain.iso> and <usa-europe.iso> stand for your own .iso files, whatever their names and folders):
    python3 tools/spanish_language_patch.py <spain.iso>                         add es-es to assets/game_root
    python3 tools/spanish_language_patch.py <spain.iso> -o <game folder>        add es-es to another extracted game
    python3 tools/spanish_language_patch.py <spain.iso> --usa <usa-europe.iso>  extract the USA/Europe disc first

The file names do not matter: each disc is recognised by the media ID inside its default.xex, and a disc that is not
the expected one gets a warning. Quote paths that have spaces.

Use your own discs only. What it writes is the game's data: never share it.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_iso as iso  # noqa: E402

SPAIN_MEDIA_ID = 0x772E8EE8
USA_EUROPE_MEDIA_ID = 0x716F0A0D
SPANISH_FOLDER = "data/language/es-es"


def xex_media_id(header):
    """The media ID in the first bytes of a default.xex. The optional headers start at 0x18 (key/value pairs);
    0x00040006 (execution info) starts with the media ID, as extract_iso.print_xex_info reads it."""
    if len(header) < 0x18 or header[:4] != b"XEX2":
        return None
    for i in range(iso._u32(header, 0x14)):
        if 0x20 + i * 8 > len(header):
            break
        key, value = iso._u32(header, 0x18 + i * 8), iso._u32(header, 0x1C + i * 8)
        if key == 0x00040006 and value + 4 <= len(header):
            return iso._u32(header, value)
    return None


def media_id(fh, base, entries):
    """The media ID of the image's default.xex, read straight from the disc."""
    for path, entry in entries:
        if path.lower() == "default.xex":
            fh.seek(base + entry["sector"] * iso.SECTOR)
            return xex_media_id(fh.read(0x4000))
    return None


def check_disc(found, expected, what):
    """Warns when a disc is not the expected one (the patch still runs: the user may know better)."""
    if found is None:
        print("warning: no media ID found in the %s" % what, file=sys.stderr)
    elif found != expected:
        print("warning: the media ID of the %s is %08X; the expected disc's is %08X. Check that it is the right one."
              % (what, found, expected), file=sys.stderr)


def open_image(image):
    fh = open(image, "rb")
    base = iso.find_partition(fh)[0]
    root_sector, root_size = iso.read_volume_descriptor(fh, base)
    return fh, base, list(iso.walk(fh, base, root_sector, root_size))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Adds the Spain disc's Spanish to the USA/Europe disc's game.")
    parser.add_argument("spain", help="your Fable II Game of the Year (Spain) .iso, any file name")
    parser.add_argument("-o", "--output", default=iso.default_output(),
                        help="the game folder (default: assets/game_root of this repository)")
    parser.add_argument("--usa", help="your Fable II Game of the Year (USA, Europe) .iso, any file name: extracted in "
                                      "full first")
    args = parser.parse_args(argv)

    try:
        for path in [args.spain] + ([args.usa] if args.usa else []):
            if not os.path.isfile(path):
                raise iso.ExtractError("File not found: %s" % path)
        if args.usa:
            fh, base, entries = open_image(args.usa)
            with fh:
                check_disc(media_id(fh, base, entries), USA_EUROPE_MEDIA_ID, "USA/Europe .iso")
            print("Extracting the USA/Europe disc into %s ..." % args.output)
            iso.extract_image(args.usa, args.output)
        xex = os.path.join(args.output, "default.xex")
        if not os.path.exists(xex):
            raise iso.ExtractError("No default.xex in %s. Extract the USA/Europe disc first (or pass --usa)."
                                   % args.output)
        if not args.usa:
            with open(xex, "rb") as fh:
                check_disc(xex_media_id(fh.read(0x4000)), USA_EUROPE_MEDIA_ID, "extracted game's default.xex")

        fh, base, entries = open_image(args.spain)
        with fh:
            check_disc(media_id(fh, base, entries), SPAIN_MEDIA_ID, "Spain .iso")
            files = [(path, entry) for path, entry in entries
                     if not entry["is_dir"] and (path + "/").lower().startswith(SPANISH_FOLDER + "/")]
            if not files:
                raise iso.ExtractError("This disc has no %s folder." % SPANISH_FOLDER)
            total = sum(entry["size"] for _, entry in files)
            print("Copying %d files of %s (%.2f GB) ..." % (len(files), SPANISH_FOLDER, total / 1e9))
            for path, entry in files:
                iso.extract_file(fh, base, entry, iso._safe_destination(args.output, path))
                print("  " + path)
    except (iso.ExtractError, OSError) as error:
        print("error: %s" % error, file=sys.stderr)
        return 1

    print("\nDone. Put these in fable2.toml (on the Switch, next to fable2-nx.nro):")
    print("    fable2_user_language = 5")
    print("    fable2_user_country = 31")
    return 0


if __name__ == "__main__":
    sys.exit(main())
