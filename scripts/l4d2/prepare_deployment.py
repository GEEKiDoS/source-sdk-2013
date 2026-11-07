"""Deploy the repository mod in place; check files, startup bytes and gameinfo syntax.

Retail startup inputs are copied/derived locally, never supplied by the mod.
No game or DLL is loaded, and neither Steam installation is modified.
"""
import sys

# Even --check-only must not create import-cache files.
sys.dont_write_bytecode = True

from pathlib import Path
import argparse
import importlib.util
import json
import os
import re
import shutil
import stat

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[1]
MOD_ROOT = REPO_ROOT / 'game/mod_l4d2'
DLL_NAMES = ('client.dll', 'server.dll', 'game_shader_generic_l4d2.dll')
TEMPLATE_TOKEN = re.compile(r'@[A-Za-z_][A-Za-z0-9_]*@')
DEFAULT_STEAM_ROOT = Path(os.environ.get('L4D2_STEAM_ROOT', 'C:/Program Files (x86)/Steam'))
DEFAULT_ORIGINAL_INSTALL = Path(os.environ.get('L4D2_ORIGINAL_INSTALL', str(DEFAULT_STEAM_ROOT / 'steamapps/common/Left 4 Dead 2')))
DEFAULT_SDK_INSTALL = Path(os.environ.get('L4D2_SDK_INSTALL', str(DEFAULT_STEAM_ROOT / 'steamapps/common/Source SDK Base 2013 Multiplayer')))

spec = importlib.util.spec_from_file_location('l4d2_input_verifier', HERE / 'verify_inputs.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


def parse_keyvalues(text, source):
    """Keep repeated keys and distinguish quoted values from structural braces."""
    tokens = []
    quoted = re.compile(r'"(?:\\.|[^"\\])*"')
    bare = re.compile(r'[^\s{}"]+')
    offset = 0
    if text.startswith('\ufeff'):
        offset = 1
    while offset < len(text):
        if text[offset].isspace():
            offset += 1
            continue
        if text.startswith('//', offset):
            end = text.find('\n', offset)
            offset = len(text) if end < 0 else end + 1
            continue
        if text.startswith('/*', offset):
            end = text.find('*/', offset + 2)
            check.require(end >= 0, source + ': unclosed block comment')
            offset = end + 2
            continue
        character = text[offset]
        if character in '{}':
            tokens.append((character, character))
            offset += 1
            continue
        match = (quoted if character == '"' else bare).match(text, offset)
        check.require(match is not None, source + ': unclosed quote at line ' + str(text.count('\n', 0, offset) + 1))
        value = match.group()
        if character == '"':
            value = re.sub(r'\\(["\\])', r'\1', value[1:-1])
        tokens.append(('value', value))
        offset = match.end()

    position = 0

    def block(nested):
        nonlocal position
        rows = []
        while position < len(tokens):
            kind, key = tokens[position]
            position += 1
            if kind == '}':
                check.require(nested, source + ': unexpected closing brace')
                return rows
            check.require(kind == 'value', source + ': expected a key, not an opening brace')
            check.require(position < len(tokens), source + ': missing value for ' + repr(key))
            kind, value = tokens[position]
            position += 1
            check.require(kind != '}', source + ': missing value for ' + repr(key))
            rows.append((key, block(True) if kind == '{' else value))
        check.require(not nested, source + ': unclosed block')
        return rows

    return block(False)


def member(rows, name, source, object_value=False):
    values = [value for key, value in rows if key.casefold() == name.casefold()]
    check.require(len(values) == 1, source + ': expected exactly one ' + name)
    check.require(isinstance(values[0], list) == object_value, source + ': wrong value type for ' + name)
    return values[0]


def search_paths(text, source):
    tree = parse_keyvalues(text, source)
    check.require(len(tree) == 1, source + ': expected one GameInfo root')
    game = member(tree, 'GameInfo', source, object_value=True)
    filesystem = member(game, 'FileSystem', source, object_value=True)
    check.require(member(filesystem, 'SteamAppId', source) == '243750', source + ': SteamAppId must be 243750')
    rows = member(filesystem, 'SearchPaths', source, object_value=True)
    check.require(all(key and isinstance(value, str) and value for key, value in rows), source + ': SearchPaths must contain nonempty path rows, not blocks')
    return rows


def normalized_path(value):
    if value.casefold().startswith('|gameinfo_path|'):
        value = str(MOD_ROOT / value[len('|gameinfo_path|'):].replace('\\', '/').lstrip('/'))
    check.require('|' not in value and Path(value).is_absolute(), 'SearchPath must be absolute or local |gameinfo_path|: ' + value)
    return os.path.normpath(value).replace('\\', '/').casefold()


def normalized_rows(rows):
    return [(tuple(sorted(key.casefold().split('+'))), normalized_path(value)) for key, value in rows]


def below(path, root):
    return path == root or path.startswith(root.rstrip('/') + '/')


def original_rows(original):
    return [('game', (original / layer / suffix).as_posix())
            for layer in check.LAYERS for suffix in ('pak01.vpk', '')]


def render_gameinfo(template, original, sdk):
    token = '@ORIGINAL_SEARCH_PATHS@'
    check.require(template.count(token) == 1, 'gameinfo_template.txt must contain exactly one ' + token)
    line = re.search(r'(?m)^[ \t]*@ORIGINAL_SEARCH_PATHS@[ \t]*(?:\r?\n|$)', template)
    check.require(line is not None, token + ' must be on its own line')
    check.require('@SDK_ROOT@' in template, 'gameinfo_template.txt is missing @SDK_ROOT@')
    platform_token = '@ORIGINAL_PLATFORM@'
    check.require(template.count(platform_token) == 1, 'gameinfo_template.txt must contain exactly one ' + platform_token)
    unknown = set(TEMPLATE_TOKEN.findall(template)) - {token, '@SDK_ROOT@', platform_token}
    check.require(not unknown, 'Unknown gameinfo template tokens: ' + ', '.join(sorted(unknown)))
    ending = '\r\n' if line.group().endswith('\r\n') else '\n'
    rows = []
    for key, value in original_rows(original):
        check.require(not any(character in value for character in ('"', '\r', '\n')), 'Unquotable original path: ' + value)
        rows.append('\t\t\t' + key + '\t"' + value + '"')
    replacement = ending.join(rows) + (ending if line.group().endswith('\n') else '')
    text = template[:line.start()] + replacement + template[line.end():]
    text = text.replace('@SDK_ROOT@', sdk.as_posix()).replace(platform_token, (original / 'platform').as_posix())
    check.require(not TEMPLATE_TOKEN.search(text), 'Unresolved gameinfo template token')
    return text


def archive_index(logical):
    if logical.is_file():
        return logical
    physical = logical.with_name(logical.stem + '_dir.vpk')
    check.require(physical.is_file(), 'Missing VPK index: ' + str(logical) + ' (or ' + str(physical) + ')')
    return physical


def validate_search_paths(rows, original, sdk):
    normalized = normalized_rows(rows)
    original_root = normalized_path(str(original))
    sdk_root = normalized_path(str(sdk))
    mod_root = normalized_path(str(MOD_ROOT))
    native = normalized_rows(original_rows(original))
    native_platform = normalized_rows([('platform', str(original / 'platform'))])[0]
    positions = [index for index, row in enumerate(normalized)
                 if below(row[1], original_root) and row != native_platform]
    check.require(len(positions) == len(native) and [normalized[index] for index in positions] == native,
                  'Original SearchPaths must be game-only VPK/loose pairs in native layer order: ' + ', '.join(check.LAYERS))
    check.require(positions == list(range(positions[0], positions[0] + len(native))), 'Original SearchPaths must be consecutive')
    check.require(normalized.count(native_platform) == 1, 'Missing or duplicated read-only original platform SearchPath')
    download = normalized_rows([('game+download', '|gameinfo_path|download')])[0]
    check.require(normalized[-2:] == [native_platform, download],
                  'Original platform must follow SDK fallbacks and precede the final mod download SearchPath')
    check.require((original / 'platform').is_dir(), 'Missing original platform directory: ' + str(original / 'platform'))
    local = normalized_rows([('mod+mod_write', '|gameinfo_path|.'), ('game+game_write', '|gameinfo_path|.'),
                             ('default_write_path', '|gameinfo_path|.'), ('gamebin', '|gameinfo_path|bin')])
    for row in local:
        check.require(normalized.count(row) == 1 and normalized.index(row) < positions[0], 'Missing or misplaced local write/gamebin SearchPath: ' + repr(row))
    indexes = []
    for layer in check.LAYERS:
        directory = original / layer
        check.require(directory.is_dir(), 'Missing original loose layer: ' + str(directory))
        indexes.append(archive_index(directory / 'pak01.vpk'))
    sdk_archives = set()
    for index, ((ids, path), (_, value)) in enumerate(zip(normalized, rows)):
        if any(name in ids for name in ('game_write', 'mod_write', 'default_write_path')):
            check.require(below(path, mod_root), 'Writable SearchPath escapes the repository mod: ' + value)
        if below(path, original_root):
            continue
        if not below(path, sdk_root):
            check.require(below(path, mod_root), 'SearchPath is outside the mod and real installations: ' + value)
            continue
        check.require(index > positions[-1], 'SDK fallback must be below every original layer: ' + value)
        target = Path(value)
        if target.suffix.casefold() == '.vpk':
            parent = normalized_path(str(target.parent))
            expected_id = 'game' if parent == normalized_path(str(sdk / 'hl2')) else 'platform'
            check.require(parent in {normalized_path(str(sdk / 'hl2')), normalized_path(str(sdk / 'platform'))} and ids == (expected_id,),
                          'SDK archive must use its read-only game/platform SearchPathID: ' + value)
            indexes.append(archive_index(target))
            sdk_archives.add(expected_id)
        else:
            check.require((ids, path) in normalized_rows([('game', str(sdk / 'hl2')), ('platform', str(sdk / 'platform'))]),
                          'Unexpected SDK loose fallback: ' + value)
            check.require(target.is_dir(), 'Missing SDK loose fallback directory: ' + value)
    check.require(sdk_archives == {'game', 'platform'}, 'SDK fallback must include hl2 and platform VPK archives')
    for row in normalized_rows([('game', str(sdk / 'hl2')), ('platform', str(sdk / 'platform'))]):
        check.require(normalized.count(row) == 1, 'Missing or duplicated SDK loose fallback: ' + repr(row))
    return indexes


def local_directory(path, create=False):
    check.require(path == MOD_ROOT or path.is_relative_to(MOD_ROOT), 'Output must stay inside the repository mod: ' + str(path))
    current = REPO_ROOT
    for part in path.relative_to(REPO_ROOT).parts:
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError:
            check.require(create, 'Missing local output directory: ' + str(current))
            current.mkdir()
            info = current.lstat()
        check.require(stat.S_ISDIR(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                      'Local output directory must not be a symlink/junction: ' + str(current))


def write_ordinary(destination, source=None, data=None):
    local_directory(destination.parent, create=True)
    try:
        info = destination.lstat()
    except FileNotFoundError:
        info = None
    if info is not None:
        check.require(stat.S_ISREG(info.st_mode) or stat.S_ISLNK(info.st_mode), 'Refuse non-file output target: ' + str(destination))
        # Unlink only this expected filename; never write through a hardlink/symlink.
        destination.unlink()
    with destination.open('xb') as output:
        if source is None:
            output.write(data)
        else:
            with source.open('rb') as input_file:
                shutil.copyfileobj(input_file, output)


def ordinary_file(path):
    check.require(path.is_file(), 'Missing local file: ' + str(path))
    info = path.lstat()
    check.require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1 and not getattr(info, 'st_file_attributes', 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT,
                  'Expected an ordinary, unshared local file: ' + str(path))


def startup_payloads(original):
    payloads = []
    for relative in ('cfg/valve.rc', 'cfg/config_default.cfg'):
        source = original / 'left4dead2' / relative
        check.require(source.is_file(), 'Missing original startup input: ' + str(source))
        payloads.append((relative, source.read_bytes()))
    # L4D2 has no PC kb_def.lst. The SDK requires this list-format projection
    # of the original first-run bind commands, in their original order.
    bindings = []
    for line in payloads[1][1].splitlines():
        if not re.match(rb'[ \t]*bind(?:[ \t]|$)', line, re.IGNORECASE):
            continue
        match = re.fullmatch(rb'[ \t]*bind[ \t]+("(?:\\.|[^"\\])*")[ \t]+("(?:\\.|[^"\\])*")[ \t]*(?://.*)?', line, re.IGNORECASE)
        check.require(match is not None, 'Unsupported original bind syntax in cfg/config_default.cfg: ' + repr(line))
        bindings.append(match[1] + b'\t' + match[2] + b'\n')
    check.require(bindings, 'Missing original bind commands in cfg/config_default.cfg')
    payloads.append(('scripts/kb_def.lst', b'// Original L4D2 cfg/config_default.cfg bindings in the SDK default-list format.\n' + b''.join(bindings)))
    return payloads


def expected_shader_names():
    directory = REPO_ROOT / 'src/materialsystem/stdshaders'
    names = set()
    for catalog, profile in (('l4d2_shaders_dx9_20.txt', '_ps20'), ('l4d2_shaders_dx9_20b.txt', '_ps20b')):
        path = directory / catalog
        check.require(path.is_file(), 'Missing tracked shader catalog: ' + str(path))
        for line in path.read_text(encoding='utf-8-sig').splitlines():
            source = line.strip()
            if not source or source.startswith('//'):
                continue
            stem = Path(source).stem
            if stem.endswith('_ps2x'):
                stem = stem[:-len('_ps2x')] + profile
            names.add((stem + '.vcs').casefold())
    return names


def artifact_files(root, expected):
    modules = [root / 'dlls' / name for name in DLL_NAMES]
    for path in modules:
        check.require(path.is_file(), 'Missing artifact DLL: ' + str(path))
    pdbs = []
    for path in modules:
        pdb = path.with_suffix('.pdb')
        if pdb.exists() or pdb.is_symlink():
            check.require(pdb.is_file(), 'Artifact PDB is not a file: ' + str(pdb))
            pdbs.append(pdb)
    directory = root / 'vcs'
    check.require(directory.is_dir(), 'Missing artifact VCS directory: ' + str(directory))
    shaders = sorted((path for path in directory.iterdir() if path.suffix.casefold() == '.vcs'), key=lambda path: path.name.casefold())
    names = {path.name.casefold() for path in shaders}
    check.require(len(shaders) == len(names), 'Duplicate case-insensitive artifact VCS filenames in ' + str(directory))
    check.require(names == expected, 'Artifact VCS filenames differ from tracked shader catalogs; missing=' + repr(sorted(expected - names)) + ', unexpected=' + repr(sorted(names - expected)))
    for path in shaders:
        check.require(path.is_file(), 'Artifact VCS is not a file: ' + str(path))
    return modules, pdbs, shaders


def check_local_artifacts(pdbs, expected):
    modules = MOD_ROOT / 'bin/x64'
    directory = MOD_ROOT / 'shaders/fxc'
    local_directory(modules)
    local_directory(directory)
    for name in DLL_NAMES:
        ordinary_file(modules / name)
    for path in pdbs:
        ordinary_file(modules / path.name)
    actual = sorted((path for path in directory.iterdir() if path.suffix.casefold() == '.vcs'), key=lambda path: path.name.casefold())
    names = {path.name.casefold() for path in actual}
    check.require(len(actual) == len(names), 'Duplicate case-insensitive local VCS filenames in ' + str(directory))
    check.require(names == expected, 'Local VCS filenames differ from tracked shader catalogs; missing=' + repr(sorted(expected - names)) + ', unexpected=' + repr(sorted(names - expected)))
    for path in actual:
        ordinary_file(path)
    return len(actual)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-install', type=Path, default=DEFAULT_ORIGINAL_INSTALL, help='Real Left 4 Dead 2 Steam installation')
    parser.add_argument('--sdk-install', type=Path, default=DEFAULT_SDK_INSTALL, help='Real Source SDK Base 2013 Multiplayer installation')
    parser.add_argument('--artifact-root', type=Path, help='Preserved build outputs containing dlls/ and vcs/')
    parser.add_argument('--check-only', action='store_true', help='Check existing local deployment without writing or copying')
    parser.add_argument('--gameinfo-only', action='store_true', help='Render gameinfo.txt and local startup inputs without requiring or copying build artifacts')
    options = parser.parse_args()
    if options.check_only and options.gameinfo_only:
        parser.error('--check-only and --gameinfo-only are mutually exclusive')
    if options.gameinfo_only and options.artifact_root:
        parser.error('--gameinfo-only cannot be combined with --artifact-root')
    original = options.original_install.resolve(strict=True)
    sdk = options.sdk_install.resolve(strict=True)
    for root in (original, sdk):
        check.require(root.is_dir(), 'Missing installation directory: ' + str(root))
        check.require(not MOD_ROOT.is_relative_to(root) and not root.is_relative_to(MOD_ROOT), 'Repository mod and Steam installations must not overlap: ' + str(root))
    check.require(not original.is_relative_to(sdk) and not sdk.is_relative_to(original), 'Original and SDK installations must be separate')
    local_directory(MOD_ROOT)
    executable = sdk / 'hl2_win64.exe'
    check.require(executable.is_file(), 'Missing SDK executable: ' + str(executable))
    template_path = MOD_ROOT / 'gameinfo_template.txt'
    check.require(template_path.is_file(), 'Missing tracked gameinfo template: ' + str(template_path))
    rendered = render_gameinfo(template_path.read_bytes().decode('utf-8'), original, sdk)
    expected = search_paths(rendered, str(template_path))
    indexes = validate_search_paths(expected, original, sdk)
    startup = startup_payloads(original)
    artifact_root = None if options.gameinfo_only else (options.artifact_root.resolve(strict=True) if options.artifact_root else None)
    shader_names = set() if options.gameinfo_only else expected_shader_names()
    modules, pdbs, shaders = artifact_files(artifact_root, shader_names) if artifact_root is not None else ([], [], None)
    if artifact_root is None and not options.gameinfo_only:
        local_pdbs = [(MOD_ROOT / 'bin/x64' / name).with_suffix('.pdb') for name in DLL_NAMES]
        pdbs = [path for path in local_pdbs if path.exists() or path.is_symlink()]
    gameinfo = MOD_ROOT / 'gameinfo.txt'
    if not options.check_only:
        write_ordinary(gameinfo, data=rendered.encode('utf-8'))
        for relative, data in startup:
            write_ordinary(MOD_ROOT / relative, data=data)
        if not options.gameinfo_only:
            for source in modules + (pdbs if artifact_root is not None else []):
                write_ordinary(MOD_ROOT / 'bin/x64' / source.name, source=source)
            for source in shaders or []:
                write_ordinary(MOD_ROOT / 'shaders/fxc' / source.name, source=source)
    ordinary_file(gameinfo)
    text = gameinfo.read_bytes().decode('utf-8')
    check.require(not TEMPLATE_TOKEN.search(text), 'Unresolved template token in ' + str(gameinfo))
    actual = search_paths(text, str(gameinfo))
    check.require(normalized_rows(actual) == normalized_rows(expected), 'Local gameinfo SearchPaths differ from the template/native absolute precedence; regenerate with this tool without --check-only')
    for relative, data in startup:
        destination = MOD_ROOT / relative
        ordinary_file(destination)
        check.require(destination.read_bytes() == data, 'Local startup input differs from the original installation: ' + str(destination) + '; regenerate with this tool without --check-only')
    vcs_count = None if options.gameinfo_only else check_local_artifacts(pdbs, shader_names)
    print(json.dumps({'status': 'GAMEINFO_GENERATED' if options.gameinfo_only else 'FILE_PRESENCE_PARSE_ONLY', 'check_only': options.check_only,
                      'gameinfo_only': options.gameinfo_only,
                      'mod_root': MOD_ROOT.as_posix(), 'gameinfo': gameinfo.as_posix(),
                      'original_install': original.as_posix(), 'sdk_install': sdk.as_posix(),
                      'sdk_executable': executable.as_posix(),
                      'artifact_root': artifact_root.as_posix() if artifact_root else None,
                      'counts': {'original_layers': len(check.LAYERS), 'vpk_indexes': len(indexes),
                                 'search_paths': len(actual), 'startup_files': len(startup),
                                 'dlls': 0 if options.gameinfo_only else len(DLL_NAMES),
                                 'pdbs': len(pdbs), 'vcs': vcs_count},
                      'game_launches': 0}))


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(json.dumps({'status': 'FILE_PRESENCE_PARSE_FAILED', 'error': str(error), 'game_launches': 0}), file=sys.stderr)
        raise SystemExit(1)
