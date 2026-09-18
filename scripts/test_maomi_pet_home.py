"""Render the real LVGL pet home and exercise theme replacement on a host compiler.

Requires the repository's managed LVGL component, CMake, Ninja, a host GCC toolchain,
and a Chinese TTF/TTC font. All build output stays in .cache/maomi-pet-home-test.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

# Avoid an unrelated installed "tests" package shadowing our local test helpers.
sys.path.insert(0, str(Path(__file__).resolve().parent / 'tests'))
from test_maomi_host_cpp import find_host_compiler, ROOT, BOARD


def tool(name, fallback):
    found = shutil.which(name) or fallback
    if not Path(found).is_file():
        raise RuntimeError(f'Missing {name}: add it to PATH')
    return str(Path(found).resolve())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, default=Path('C:/Windows/Fonts/msyh.ttc'))
    args = parser.parse_args()
    if not args.font.is_file():
        parser.error('Supply a Chinese TTF/TTC font using --font')
    compiler = find_host_compiler()
    if compiler is None:
        parser.error('A host C++ compiler is required')
    cmake = tool('cmake', 'C:/Espressif/tools/cmake/4.0.3/bin/cmake.exe')
    ninja = tool('ninja', 'C:/Espressif/tools/ninja/1.12.1/ninja.exe')
    work = ROOT / '.cache/maomi-pet-home-test'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'lv_conf.h').write_text('''#pragma once
#define LV_CONF_H
#define LV_COLOR_DEPTH 32
#define LV_MEM_SIZE (8 * 1024 * 1024)
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_USE_OS LV_OS_NONE
''', encoding='utf-8')
    (work / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(maomi_home_test LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
set(LV_BUILD_CONF_PATH "{(work / 'lv_conf.h').as_posix()}" CACHE PATH "" FORCE)
add_subdirectory("{(ROOT / 'managed_components/lvgl__lvgl').as_posix()}" lvgl)
add_executable(home_test "{(ROOT / 'scripts/tests/maomi_pet_home_render.cc').as_posix()}")
target_compile_definitions(home_test PRIVATE CONFIG_MAOMI_LEARNING=1)
target_include_directories(home_test PRIVATE "{BOARD.as_posix()}" "{(ROOT / 'main').as_posix()}")
target_link_libraries(home_test PRIVATE lvgl)
''', encoding='utf-8')
    env = os.environ.copy()
    env['PATH'] = str(compiler.parent) + os.pathsep + env.get('PATH', '')
    c_compiler = compiler.parent / ('gcc.exe' if os.name == 'nt' else 'gcc')
    subprocess.run([cmake, '-S', str(work), '-B', str(work / 'build'), '-G', 'Ninja',
                    f'-DCMAKE_MAKE_PROGRAM={ninja}', f'-DCMAKE_C_COMPILER={c_compiler}',
                    f'-DCMAKE_CXX_COMPILER={compiler}', '-DCMAKE_BUILD_TYPE=Debug'], env=env, check=True)
    subprocess.run([cmake, '--build', str(work / 'build'), '--parallel', '8'], env=env, check=True)
    binary = work / 'build' / ('home_test.exe' if os.name == 'nt' else 'home_test')
    subprocess.run([str(binary), str(args.font.resolve())], cwd=work, env=env, check=True)
    frames = {name: (work / f'{name}.ppm').read_bytes()
              for name in ('kitten', 'adult', 'sleeping', 'hidden', 'resumed')}
    if len({frames[name] for name in ('kitten', 'adult', 'sleeping', 'hidden')}) != 4:
        raise AssertionError('Growth, sleep and hidden states must render distinct frames')
    if frames['resumed'] != frames['sleeping']:
        raise AssertionError('Restoring the home must preserve its previous appearance')
    # PPM files are always produced; PNG conversion is optional for convenient viewing.
    try:
        from PIL import Image
    except ImportError:
        pass
    else:
        for path in work.glob('*.ppm'):
            with Image.open(path) as picture:
                picture.save(path.with_suffix('.png'))
    print(f'Preview and regression evidence: {work}')


if __name__ == '__main__':
    main()
