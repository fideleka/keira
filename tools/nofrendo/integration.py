"""SCons routing/guards. PlatformIO registration is deliberately kept outside."""
import hashlib
import os
from pathlib import Path, PureWindowsPath


def relative_source(path, root):
    # PureWindowsPath handles drive/UNC case and either slash on Windows. Native
    # realpath additionally resolves symlink aliases before this lexical check.
    windows = isinstance(path, PureWindowsPath) or isinstance(root, PureWindowsPath)
    cls = PureWindowsPath if windows else Path
    try:
        return cls(path).relative_to(cls(root))
    except ValueError:
        return None


def source_path(node):
    # VariantDir nodes identify their inputs via srcnode(), not get_abspath().
    return Path(os.path.realpath(node.srcnode().get_abspath()))


def install(env, source, overlay, build_dir, key):
    source, overlay = Path(source), Path(overlay)
    signature = 'nofrendo-integration-v2:' + key + ':' + hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    objects = Path(build_dir) / ('nofrendo-objects-' + key)

    def headers(build_env):
        build_env.Replace(CPPPATH=[str(overlay)] + [
            p for p in build_env.get('CPPPATH', []) if str(p) != str(overlay)
        ])

    def route(build_env, node):
        headers(build_env)
        relative = relative_source(source_path(node), source)
        if relative is None:
            return node
        # Neither SCons variant mapping nor PlatformIO's ordinary library object
        # names may collapse different reviewed overlay versions into one object.
        build_env.Replace(LIBPREFIX='libnofrendo-' + key + '-')
        target = objects / relative.with_suffix(relative.suffix + '.o')
        return build_env.Object(target=str(target), source=str(overlay / relative))

    def guard(target, sources, build_env):
        headers(build_env)
        for node in sources:
            path = source_path(node)
            if relative_source(path, source) is not None:
                raise ValueError('nofrendo original core reached Object builder; middleware missing or bypassed: ' + str(path))
            relative = relative_source(path, overlay)
            if relative is not None:
                expected = objects / relative.with_suffix(relative.suffix + '.o')
                if len(target) != 1 or Path(target[0].get_abspath()) != expected:
                    raise ValueError('nofrendo overlay object is not explicitly namespaced')
            elif relative_source(path, Path(build_dir)) is not None and any(
                    part.startswith('nofrendo-irq-src-') for part in path.parts):
                raise ValueError('obsolete nofrendo overlay reached Object builder')
        # Value dependencies version project/header consumers as well as the CPU.
        build_env.Depends(target, build_env.Value(signature))
        return target, sources

    # Builders are shared by SCons clones; enforce this even on a dependent-lib
    # clone that does not inherit PlatformIO's middleware registration.
    builder = env['BUILDERS']['StaticObject']
    for suffix in ('.c', '.cpp', '.cc', '.cxx', '.C', '.c++'):
        previous = builder.emitter.get(suffix)
        def checked(target, source, env, previous=previous):
            target, source = guard(target, source, env)
            return previous(target, source, env) if previous else (target, source)
        builder.add_emitter(suffix, checked)
    library = env['BUILDERS']['StaticLibrary']
    previous_library = library.emitter

    original_source = source

    def archive_guard(target, source, env):
        sources, build_env = source, env
        core = False
        for obj in sources:
            for node in obj.sources:
                path = source_path(node)
                if relative_source(path, original_source) is not None:
                    raise ValueError('nofrendo original core reached archive')
                relative = relative_source(path, overlay)
                if relative is not None:
                    core = True
                    expected = objects / relative.with_suffix(relative.suffix + '.o')
                    if Path(obj.get_abspath()) != expected:
                        raise ValueError('nofrendo archive contains an unversioned object')
                elif relative_source(path, Path(build_dir)) is not None and any(
                        part.startswith('nofrendo-irq-src-') for part in path.parts):
                    raise ValueError('nofrendo archive contains an obsolete overlay')
        if core and any(not Path(t.get_abspath()).name.startswith('libnofrendo-' + key + '-') for t in target):
            raise ValueError('nofrendo archive is not explicitly namespaced')
        build_env.Depends(target, build_env.Value(signature))
        return previous_library(target, sources, build_env) if previous_library else (target, sources)

    library.emitter = archive_guard
    headers(env)
    env.AddBuildMiddleware(route)
